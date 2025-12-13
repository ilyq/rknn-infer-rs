#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/dma-heap.h>

#include <turbojpeg.h>

#include "rknn_api.h"
#include "im2d.h"
#include "RgaUtils.h"
#include "RgaApi.h"
#include <algorithm>

// ============================================================
// DMA allocator (RV1106)
// ============================================================
static int dma_alloc(size_t size, int *fd, void **ptr)
{
    const char *heap_path = "/dev/rk_dma_heap/rk-dma-heap-cma";
    int heap_fd = open(heap_path, O_RDWR);
    if (heap_fd < 0)
        return -1;

    struct dma_heap_allocation_data alloc_data;
    memset(&alloc_data, 0, sizeof(alloc_data));
    alloc_data.len = size;
    alloc_data.fd_flags = O_RDWR | O_CLOEXEC;

    if (ioctl(heap_fd, DMA_HEAP_IOCTL_ALLOC, &alloc_data) < 0)
    {
        close(heap_fd);
        return -1;
    }

    *fd = alloc_data.fd;
    *ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, *fd, 0);
    close(heap_fd);

    return (*ptr == MAP_FAILED) ? -1 : 0;
}

static void dma_free(int fd, void *ptr, size_t size)
{
    if (ptr)
        munmap(ptr, size);
    if (fd >= 0)
        close(fd);
}

// ============================================================
// MAIN
// ============================================================
int main(int argc, char **argv)
{
    if (argc != 3)
    {
        printf("Usage: %s yolov8.rknn input.jpg\n", argv[0]);
        return -1;
    }

    const char *model_path = argv[1];
    const char *image_path = argv[2];
    const int TARGET = 640;

    // ========================================================
    // 1. Load RKNN model
    // ========================================================
    FILE *mf = fopen(model_path, "rb");
    fseek(mf, 0, SEEK_END);
    size_t model_size = ftell(mf);
    fseek(mf, 0, SEEK_SET);

    void *model_data = malloc(model_size);
    fread(model_data, 1, model_size, mf);
    fclose(mf);

    rknn_context ctx;
    int ret = rknn_init(&ctx, model_data, model_size, 0, NULL);
    if (ret != RKNN_SUCC)
    {
        printf("rknn_init failed: %d\n", ret);
        return -1;
    }

    // ========================================================
    // 2. JPEG → RGBA (DMA)
    // ========================================================
    FILE *jf = fopen(image_path, "rb");
    fseek(jf, 0, SEEK_END);
    long jpg_size = ftell(jf);
    fseek(jf, 0, SEEK_SET);

    unsigned char *jpg_buf = (unsigned char *)malloc(jpg_size);
    fread(jpg_buf, 1, jpg_size, jf);
    fclose(jf);

    tjhandle tjd = tjInitDecompress();
    int in_w, in_h, subsamp, cs;
    tjDecompressHeader3(tjd, jpg_buf, jpg_size, &in_w, &in_h, &subsamp, &cs);

    int src_fd;
    void *src_dma;
    dma_alloc(in_w * in_h * 4, &src_fd, &src_dma);

    tjDecompress2(tjd, jpg_buf, jpg_size,
                  (unsigned char *)src_dma,
                  in_w, in_w * 4, in_h,
                  TJPF_RGBA, TJFLAG_FASTDCT);

    tjDestroy(tjd);
    free(jpg_buf);

    // ========================================================
    // 3. RGA resize + letterbox (RGBA 640x640)
    // ========================================================
    float scale = std::min((float)TARGET / in_w, (float)TARGET / in_h);
    int rw = in_w * scale;
    int rh = in_h * scale;
    int pw = TARGET - rw;
    int ph = TARGET - rh;

    int resize_fd;
    void *resize_dma;
    dma_alloc(rw * rh * 4, &resize_fd, &resize_dma);

    rga_buffer_t src = wrapbuffer_fd(src_fd, in_w, in_h, RK_FORMAT_RGBA_8888);
    rga_buffer_t dst = wrapbuffer_fd(resize_fd, rw, rh, RK_FORMAT_RGBA_8888);
    imresize(src, dst, (double)rw / in_w, (double)rh / in_h, 0, 1, NULL);

    int pad_fd;
    void *pad_dma;
    dma_alloc(TARGET * TARGET * 4, &pad_fd, &pad_dma);
    memset(pad_dma, 0, TARGET * TARGET * 4);

    for (int y = 0; y < rh; y++)
    {
        memcpy((uint8_t *)pad_dma + ((y + ph / 2) * TARGET + pw / 2) * 4,
               (uint8_t *)resize_dma + y * rw * 4,
               rw * 4);
    }

    dma_free(resize_fd, resize_dma, rw * rh * 4);
    dma_free(src_fd, src_dma, in_w * in_h * 4);

    // ========================================================
    // 4. RGBA → RGB (DMA)
    // ========================================================
    int rgb_fd;
    void *rgb_dma;
    dma_alloc(TARGET * TARGET * 3, &rgb_fd, &rgb_dma);

    rga_buffer_t rgba = wrapbuffer_fd(pad_fd, TARGET, TARGET, RK_FORMAT_RGBA_8888);
    rga_buffer_t rgb = wrapbuffer_fd(rgb_fd, TARGET, TARGET, RK_FORMAT_RGB_888);
    imcvtcolor(rgba, rgb, RK_FORMAT_RGBA_8888, RK_FORMAT_RGB_888);

    dma_free(pad_fd, pad_dma, TARGET * TARGET * 4);

    // ========================================================
    // 5. Create zero-copy input
    // ========================================================
    rknn_tensor_attr in_attr;
    memset(&in_attr, 0, sizeof(in_attr));
    in_attr.index = 0;
    rknn_query(ctx, RKNN_QUERY_INPUT_ATTR, &in_attr, sizeof(in_attr));

    rknn_tensor_mem *input_mem =
        rknn_create_mem(ctx, TARGET * TARGET * 3);
    memcpy(input_mem->virt_addr, rgb_dma, TARGET * TARGET * 3);
    rknn_set_io_mem(ctx, input_mem, &in_attr);

    dma_free(rgb_fd, rgb_dma, TARGET * TARGET * 3);

    // ========================================================
    // 6. Create 9 output tensors (YOLOv8)
    // ========================================================
    rknn_tensor_mem *out_mem[9];
    rknn_tensor_attr out_attr[9];

    for (int i = 0; i < 9; i++)
    {
        memset(&out_attr[i], 0, sizeof(out_attr[i]));
        out_attr[i].index = i;
        rknn_query(ctx, RKNN_QUERY_OUTPUT_ATTR, &out_attr[i], sizeof(out_attr[i]));

        out_mem[i] = rknn_create_mem(ctx, out_attr[i].size);
        rknn_set_io_mem(ctx, out_mem[i], &out_attr[i]);

        printf("output[%d]: size=%d type=%d qnt=%d\n",
               i, out_attr[i].size, out_attr[i].type, out_attr[i].qnt_type);
    }

    // ========================================================
    // 7. Run inference
    // ========================================================
    ret = rknn_run(ctx, NULL);
    if (ret != RKNN_SUCC)
    {
        printf("rknn_run failed: %d\n", ret);
        return -1;
    }

    printf("✅ YOLOv8 RV1106 inference done (9 outputs ready)\n");

    // ========================================================
    // 8. Cleanup
    // ========================================================
    for (int i = 0; i < 9; i++)
        rknn_destroy_mem(ctx, out_mem[i]);

    rknn_destroy_mem(ctx, input_mem);
    rknn_destroy(ctx);
    free(model_data);

    return 0;
}
