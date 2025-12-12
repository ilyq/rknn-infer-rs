#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <vector>
#include <algorithm>

#include <turbojpeg.h>

#include "rknn_api.h"
#include "im2d.h"
#include "RgaApi.h"
#include "RgaUtils.h"

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/dma-heap.h>
#include <cmath>

#define MODEL_INPUT_SIZE 224
#define MODEL_CHANNELS 3
#define MODEL_CLASSES 1000

// =======================================================
// RV1106 DMA allocator
// =======================================================
static int dma_alloc(int size, int *fd, void **ptr)
{
    const char *heap_path = "/dev/rk_dma_heap/rk-dma-heap-cma";
    int heap_fd = open(heap_path, O_RDWR);
    if (heap_fd < 0)
    {
        perror("open rk-dma-heap-cma");
        return -1;
    }

    struct dma_heap_allocation_data alloc_data = {};
    alloc_data.len = size;
    alloc_data.fd_flags = O_RDWR | O_CLOEXEC;

    if (ioctl(heap_fd, DMA_HEAP_IOCTL_ALLOC, &alloc_data) < 0)
    {
        perror("DMA_HEAP_IOCTL_ALLOC");
        close(heap_fd);
        return -1;
    }

    *fd = alloc_data.fd;
    *ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, *fd, 0);
    if (*ptr == MAP_FAILED)
    {
        perror("mmap");
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

// =======================================================
// 主函数
// =======================================================
int main(int argc, char **argv)
{
    if (argc != 3)
    {
        printf("Usage: %s mobilenet.rknn image.jpg\n", argv[0]);
        return -1;
    }

    const char *model_path = argv[1];
    const char *img_path = argv[2];

    // ---------------------------------------------------
    // 1. 加载 RKNN 模型
    // ---------------------------------------------------
    FILE *fm = fopen(model_path, "rb");
    if (!fm)
    {
        perror("open model");
        return -1;
    }
    fseek(fm, 0, SEEK_END);
    size_t model_size = ftell(fm);
    fseek(fm, 0, SEEK_SET);

    void *model_data = malloc(model_size);
    fread(model_data, 1, model_size, fm);
    fclose(fm);

    rknn_context ctx;
    int ret = rknn_init(&ctx, model_data, model_size, 0, NULL);
    if (ret != RKNN_SUCC)
    {
        printf("rknn_init failed: %d\n", ret);
        return -1;
    }

    // ---------------------------------------------------
    // 2. 查询输入信息（强烈建议）
    // ---------------------------------------------------
    rknn_tensor_attr input_attr = {};
    input_attr.index = 0;
    rknn_query(ctx, RKNN_QUERY_INPUT_ATTR, &input_attr, sizeof(input_attr));

    rknn_tensor_attr output_attr = {};
    output_attr.index = 0;
    rknn_query(ctx, RKNN_QUERY_OUTPUT_ATTR, &output_attr, sizeof(output_attr));

    printf("Input:  %d %d %d %d  type=%d fmt=%d size=%d\n",
           input_attr.n_dims > 0 ? input_attr.dims[0] : 0,
           input_attr.n_dims > 1 ? input_attr.dims[1] : 0,
           input_attr.n_dims > 2 ? input_attr.dims[2] : 0,
           input_attr.n_dims > 3 ? input_attr.dims[3] : 0,
           input_attr.type, input_attr.fmt, input_attr.size);

    printf("Output: n_elems=%d type=%d qnt=%d zp=%d scale=%f size=%d\n",
           output_attr.n_elems, output_attr.type, output_attr.qnt_type,
           output_attr.zp, output_attr.scale, output_attr.size);

    // ---------------------------------------------------
    // 3. 读取 JPEG 图像
    // ---------------------------------------------------
    FILE *fi = fopen(img_path, "rb");
    if (!fi)
    {
        perror("open image");
        return -1;
    }
    fseek(fi, 0, SEEK_END);
    long jpg_size = ftell(fi);
    fseek(fi, 0, SEEK_SET);

    unsigned char *jpg_buf = new unsigned char[jpg_size];
    fread(jpg_buf, 1, jpg_size, fi);
    fclose(fi);

    // ---------------------------------------------------
    // 4. JPEG → RGBA (DMA)
    // ---------------------------------------------------
    tjhandle tjd = tjInitDecompress();
    int img_w, img_h, subsamp, cs;
    tjDecompressHeader3(tjd, jpg_buf, jpg_size,
                        &img_w, &img_h, &subsamp, &cs);

    int rgba_fd;
    void *rgba_dma;
    size_t rgba_size = img_w * img_h * 4;

    dma_alloc(rgba_size, &rgba_fd, &rgba_dma);

    tjDecompress2(tjd, jpg_buf, jpg_size,
                  (unsigned char *)rgba_dma,
                  img_w, img_w * 4, img_h,
                  TJPF_RGBA, TJFLAG_FASTDCT);

    tjDestroy(tjd);
    delete[] jpg_buf;

    // ---------------------------------------------------
    // 5. RGA: RGBA → RGB + resize 到 224x224
    // ---------------------------------------------------
    int rgb_fd;
    void *rgb_dma;
    size_t rgb_size = MODEL_INPUT_SIZE * MODEL_INPUT_SIZE * MODEL_CHANNELS;

    dma_alloc(rgb_size, &rgb_fd, &rgb_dma);

    rga_buffer_t src =
        wrapbuffer_fd(rgba_fd, img_w, img_h, RK_FORMAT_RGBA_8888);
    rga_buffer_t dst =
        wrapbuffer_fd(rgb_fd, MODEL_INPUT_SIZE, MODEL_INPUT_SIZE, RK_FORMAT_RGB_888);

    int ret_rga = imresize(src, dst,
                           (double)MODEL_INPUT_SIZE / img_w,
                           (double)MODEL_INPUT_SIZE / img_h,
                           0, 1, NULL);
    if (ret_rga != IM_STATUS_SUCCESS)
    {
        printf("RGA resize failed: %s\n", imStrError((IM_STATUS)ret_rga));
        return -1;
    }

    // ---------------------------------------------------
    // 6. Zero-copy 输入（RV1106 必须）
    // ---------------------------------------------------
    rknn_tensor_mem *input_mem = rknn_create_mem(ctx, rgb_size);
    if (!input_mem)
    {
        printf("rknn_create_mem(input) failed\n");
        return -1;
    }

    memcpy(input_mem->virt_addr, rgb_dma, rgb_size);

    rknn_tensor_attr in_bind = input_attr;
    in_bind.index = 0;

    ret = rknn_set_io_mem(ctx, input_mem, &in_bind);
    if (ret != RKNN_SUCC)
    {
        printf("rknn_set_io_mem(input) failed: %d\n", ret);
        return -1;
    }

    // ---------------------------------------------------
    // 7. 创建输出内存并设置
    // ---------------------------------------------------
    rknn_tensor_mem *output_mem = rknn_create_mem(ctx, output_attr.size);
    if (!output_mem)
    {
        printf("rknn_create_mem(output) failed\n");
        return -1;
    }

    rknn_tensor_attr out_bind = output_attr;
    out_bind.index = 0;

    ret = rknn_set_io_mem(ctx, output_mem, &out_bind);
    if (ret != RKNN_SUCC)
    {
        printf("rknn_set_io_mem(output) failed: %d\n", ret);
        return -1;
    }

    // ---------------------------------------------------
    // 8. 执行推理
    // ---------------------------------------------------
    ret = rknn_run(ctx, NULL);
    if (ret != RKNN_SUCC)
    {
        printf("rknn_run failed: %d\n", ret);
        return -1;
    }

    // ---------------------------------------------------
    // 9. 反量化输出并处理 softmax
    // ---------------------------------------------------
    std::vector<float> logits(output_attr.n_elems);
    if (output_attr.type == RKNN_TENSOR_INT8)
    {
        int8_t *p = (int8_t *)output_mem->virt_addr;
        for (uint32_t i = 0; i < output_attr.n_elems; ++i)
        {
            logits[i] = (p[i] - output_attr.zp) * output_attr.scale;
        }
    }
    else if (output_attr.type == RKNN_TENSOR_UINT8)
    {
        uint8_t *p = (uint8_t *)output_mem->virt_addr;
        for (uint32_t i = 0; i < output_attr.n_elems; ++i)
        {
            logits[i] = (p[i] - output_attr.zp) * output_attr.scale;
        }
    }

    // softmax
    float maxv = logits[0];
    for (auto v : logits)
        if (v > maxv)
            maxv = v;

    std::vector<float> probs(logits.size());
    float sum = 0.f;
    for (size_t i = 0; i < logits.size(); ++i)
    {
        probs[i] = expf(logits[i] - maxv);
        sum += probs[i];
    }
    for (auto &x : probs)
        x /= (sum + 1e-9f);

    // top5
    std::vector<std::pair<int, float>> vec;
    vec.reserve(probs.size());
    for (int i = 0; i < (int)probs.size(); ++i)
        vec.emplace_back(i, probs[i]);

    std::partial_sort(vec.begin(), vec.begin() + 5, vec.end(),
                      [](auto &a, auto &b)
                      { return a.second > b.second; });

    printf("Top-5 results:\n");
    for (int i = 0; i < 5; ++i)
    {
        printf("  #%d  class=%d  score=%.6f\n", i + 1, vec[i].first, vec[i].second);
    }

    // ---------------------------------------------------
    // 10. 释放资源
    // ---------------------------------------------------
    rknn_destroy_mem(ctx, input_mem);
    rknn_destroy_mem(ctx, output_mem);

    rknn_destroy(ctx);
    dma_free(rgba_fd, rgba_dma, rgba_size);
    dma_free(rgb_fd, rgb_dma, rgb_size);

    free(model_data);

    return 0;
}
