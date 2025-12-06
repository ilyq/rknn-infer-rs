#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#include <turbojpeg.h>

#include "im2d.h"
#include "RgaUtils.h"
#include "RgaApi.h"

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/dma-heap.h>

// -----------------------------
// RV1106 DMA allocator
// -----------------------------
static int dma_alloc(int size, int *fd, void **ptr)
{
    const char *heap_path = "/dev/rk_dma_heap/rk-dma-heap-cma";
    int heap_fd = open(heap_path, O_RDWR);
    if (heap_fd < 0)
    {
        perror("open rk-dma-heap-cma failed");
        return -1;
    }

    struct dma_heap_allocation_data alloc_data;
    memset(&alloc_data, 0, sizeof(alloc_data));
    alloc_data.len = size;
    alloc_data.fd_flags = O_RDWR | O_CLOEXEC;
    alloc_data.heap_flags = 0;

    if (ioctl(heap_fd, DMA_HEAP_IOCTL_ALLOC, &alloc_data) < 0)
    {
        perror("DMA_HEAP_IOCTL_ALLOC failed");
        close(heap_fd);
        return -1;
    }

    *fd = alloc_data.fd;

    *ptr = mmap(NULL,
                size,
                PROT_READ | PROT_WRITE,
                MAP_SHARED,
                *fd,
                0);
    if (*ptr == MAP_FAILED)
    {
        perror("mmap failed");
        close(*fd);
        close(heap_fd);
        return -1;
    }

    close(heap_fd);
    return 0;
}

static void dma_free(int fd, void *ptr, int size)
{
    if (ptr)
        munmap(ptr, size);
    if (fd >= 0)
        close(fd);
}

// -----------------------------
// 主函数
// -----------------------------
int main(int argc, char **argv)
{
    if (argc != 5)
    {
        printf("Usage: %s in.jpg out.jpg out_w out_h\n", argv[0]);
        return -1;
    }

    const char *in_path = argv[1];
    const char *out_path = argv[2];
    int out_w = atoi(argv[3]);
    int out_h = atoi(argv[4]);

    // -----------------------------
    // 1. 读取 JPEG 文件到内存
    // -----------------------------
    FILE *f = fopen(in_path, "rb");
    if (!f)
    {
        perror("fopen input");
        return -1;
    }
    fseek(f, 0, SEEK_END);
    long jpg_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    unsigned char *jpg_buf = new unsigned char[jpg_size];
    fread(jpg_buf, 1, jpg_size, f);
    fclose(f);

    // -----------------------------
    // 2. 解码 JPEG 到 DMA 内存（RGBA）
    // -----------------------------
    tjhandle tjd = tjInitDecompress();
    int in_w, in_h, subsamp, cs;
    if (tjDecompressHeader3(tjd, jpg_buf, jpg_size, &in_w, &in_h, &subsamp, &cs) != 0)
    {
        printf("tjDecompressHeader3 failed: %s\n", tjGetErrorStr());
        return -1;
    }

    size_t src_size = in_w * in_h * 4;
    int src_fd;
    void *src_dma;
    if (dma_alloc(src_size, &src_fd, &src_dma) < 0)
    {
        printf("dma_alloc src failed\n");
        return -1;
    }

    if (tjDecompress2(tjd, jpg_buf, jpg_size, (unsigned char *)src_dma,
                      in_w, in_w * 4, in_h, TJPF_RGBA, TJFLAG_FASTDCT) != 0)
    {
        printf("tjDecompress2 failed: %s\n", tjGetErrorStr());
        return -1;
    }

    tjDestroy(tjd);
    delete[] jpg_buf;

    // -----------------------------
    // 3. 分配 RGA 输出 DMA 内存
    // -----------------------------
    size_t dst_size = out_w * out_h * 4;
    int dst_fd;
    void *dst_dma;
    if (dma_alloc(dst_size, &dst_fd, &dst_dma) < 0)
    {
        printf("dma_alloc dst failed\n");
        return -1;
    }

    // -----------------------------
    // 4. RGA resize
    // -----------------------------
    rga_buffer_t src = wrapbuffer_fd(src_fd, in_w, in_h, RK_FORMAT_RGBA_8888);
    rga_buffer_t dst = wrapbuffer_fd(dst_fd, out_w, out_h, RK_FORMAT_RGBA_8888);

    im_rect src_rect = {0, 0, in_w, in_h};
    im_rect dst_rect = {0, 0, out_w, out_h};

    int ret = imresize(src, dst);
    if (ret != IM_STATUS_SUCCESS)
    {
        printf("imresize failed: %s\n", imStrError((IM_STATUS)ret));
        return -1;
    }

    // -----------------------------
    // 5. JPEG 编码直接从 DMA 内存
    // -----------------------------
    tjhandle tjc = tjInitCompress();
    unsigned char *out_jpg_buf = nullptr;
    unsigned long out_jpg_size = 0;

    if (tjCompress2(tjc, (unsigned char *)dst_dma,
                    out_w, out_w * 4, out_h,
                    TJPF_RGBA,
                    &out_jpg_buf, &out_jpg_size,
                    TJSAMP_420, 90, TJFLAG_FASTDCT) != 0)
    {
        printf("tjCompress2 failed: %s\n", tjGetErrorStr());
        return -1;
    }

    FILE *fo = fopen(out_path, "wb");
    fwrite(out_jpg_buf, 1, out_jpg_size, fo);
    fclose(fo);

    tjFree(out_jpg_buf);
    tjDestroy(tjc);

    dma_free(src_fd, src_dma, src_size);
    dma_free(dst_fd, dst_dma, dst_size);

    printf("✅ Zero-copy RGA resize success: %dx%d -> %dx%d\n", in_w, in_h, out_w, out_h);
    return 0;
}
