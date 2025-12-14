/*******************************************************
 * rknn_infer_demo.cpp
 * RV1106 YOLOv8 INT8 NHWC FULL DEMO
 *******************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <vector>
#include <algorithm>

#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/dma-heap.h>

#include "rknn_api.h"
#include "im2d.h"
#include "RgaApi.h"
#include <turbojpeg.h>

/* =================== 参数 =================== */
#define INPUT_W 640
#define INPUT_H 640
#define OBJ_CLASS_NUM 80
#define DFL_LEN 16
#define CONF_THRESH 0.25f
#define NMS_THRESH 0.45f

/* =================== COCO labels =================== */
static const char *coco_labels[80] = {
    "person", "bicycle", "car", "motorcycle", "airplane", "bus", "train", "truck", "boat",
    "traffic light", "fire hydrant", "stop sign", "parking meter", "bench", "bird", "cat",
    "dog", "horse", "sheep", "cow", "elephant", "bear", "zebra", "giraffe", "backpack", "umbrella",
    "handbag", "tie", "suitcase", "frisbee", "skis", "snowboard", "sports ball", "kite",
    "baseball bat", "baseball glove", "skateboard", "surfboard", "tennis racket", "bottle",
    "wine glass", "cup", "fork", "knife", "spoon", "bowl", "banana", "apple", "sandwich", "orange",
    "broccoli", "carrot", "hot dog", "pizza", "donut", "cake", "chair", "couch", "potted plant",
    "bed", "dining table", "toilet", "tv", "laptop", "mouse", "remote", "keyboard", "cell phone",
    "microwave", "oven", "toaster", "sink", "refrigerator", "book", "clock", "vase", "scissors",
    "teddy bear", "hair drier", "toothbrush"};

/* =================== DMA =================== */
static int dma_alloc(size_t size, int *fd, void **ptr)
{
    int heap = open("/dev/rk_dma_heap/rk-dma-heap-cma", O_RDWR);
    if (heap < 0)
        return -1;

    struct dma_heap_allocation_data data;
    memset(&data, 0, sizeof(data));
    data.len = size;
    data.fd_flags = O_RDWR | O_CLOEXEC;

    if (ioctl(heap, DMA_HEAP_IOCTL_ALLOC, &data) < 0)
        return -1;

    *fd = data.fd;
    *ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, *fd, 0);
    close(heap);

    return (*ptr == MAP_FAILED) ? -1 : 0;
}

/* =================== DFL =================== */
static void compute_dfl(const float *src, float *dst)
{
    for (int b = 0; b < 4; b++)
    {
        float sum = 0.f, acc = 0.f;
        for (int i = 0; i < DFL_LEN; i++)
        {
            float v = expf(src[b * DFL_LEN + i]);
            sum += v;
            acc += v * i;
        }
        dst[b] = acc / sum;
    }
}

/* =================== Box =================== */
struct Box
{
    float x1, y1, x2, y2, score;
    int cls;
};

/* =================== IoU & NMS =================== */
static float iou(const Box &a, const Box &b)
{
    float xx1 = fmax(a.x1, b.x1);
    float yy1 = fmax(a.y1, b.y1);
    float xx2 = fmin(a.x2, b.x2);
    float yy2 = fmin(a.y2, b.y2);
    float w = fmax(0.f, xx2 - xx1);
    float h = fmax(0.f, yy2 - yy1);
    float inter = w * h;
    float areaA = (a.x2 - a.x1) * (a.y2 - a.y1);
    float areaB = (b.x2 - b.x1) * (b.y2 - b.y1);
    return inter / (areaA + areaB - inter);
}

static void nms(std::vector<Box> &boxes)
{
    std::sort(boxes.begin(), boxes.end(),
              [](const Box &a, const Box &b)
              { return a.score > b.score; });
    std::vector<Box> out;
    std::vector<int> remove(boxes.size(), 0);

    for (size_t i = 0; i < boxes.size(); i++)
    {
        if (remove[i])
            continue;
        out.push_back(boxes[i]);
        for (size_t j = i + 1; j < boxes.size(); j++)
        {
            if (boxes[i].cls == boxes[j].cls &&
                iou(boxes[i], boxes[j]) > NMS_THRESH)
                remove[j] = 1;
        }
    }
    boxes.swap(out);
}

/* =================== 后处理 RV1106 =================== */
static void process_branch(
    int8_t *box, int8_t *cls, int8_t *sum,
    int gh, int gw, int stride,
    rknn_tensor_attr &box_attr,
    rknn_tensor_attr &cls_attr,
    rknn_tensor_attr &sum_attr,
    std::vector<Box> &out)
{
    int grid = gh * gw;
    int8_t sum_th = (int8_t)(CONF_THRESH / sum_attr.scale + sum_attr.zp);

    for (int i = 0; i < gh; i++)
        for (int j = 0; j < gw; j++)
        {
            int idx = i * gw + j;
            if (sum && sum[idx] < sum_th)
                continue;

            int base = idx * OBJ_CLASS_NUM;
            int best = -1;
            int8_t best_q = -cls_attr.zp;

            for (int c = 0; c < OBJ_CLASS_NUM; c++)
            {
                int8_t v = cls[base + c];
                if (v > best_q)
                {
                    best_q = v;
                    best = c;
                }
            }

            float score = (best_q - cls_attr.zp) * cls_attr.scale;
            if (score < CONF_THRESH)
                continue;

            float dfl[64];
            int off = idx * 64;
            for (int k = 0; k < 64; k++)
                dfl[k] = (box[off + k] - box_attr.zp) * box_attr.scale;

            float dist[4];
            compute_dfl(dfl, dist);

            float cx = (j + 0.5f) * stride;
            float cy = (i + 0.5f) * stride;

            Box b;
            b.x1 = cx - dist[0] * stride;
            b.y1 = cy - dist[1] * stride;
            b.x2 = cx + dist[2] * stride;
            b.y2 = cy + dist[3] * stride;
            b.score = score;
            b.cls = best;
            out.push_back(b);
        }
}

/* =================== main =================== */
int main(int argc, char **argv)
{
    if (argc != 3)
    {
        printf("Usage: %s model.rknn image.jpg\n", argv[0]);
        return -1;
    }

    const char *model_path = argv[1];
    const char *img_path = argv[2];

    /******************** 1. RKNN init ********************/
    rknn_context ctx;
    if (rknn_init(&ctx, (void *)model_path, 0, 0, NULL) != RKNN_SUCC)
    {
        printf("rknn_init failed\n");
        return -1;
    }

    rknn_input_output_num io_num;
    rknn_query(ctx, RKNN_QUERY_IN_OUT_NUM, &io_num, sizeof(io_num));

    /******************** 2. query input ********************/
    rknn_tensor_attr in_attr;
    memset(&in_attr, 0, sizeof(in_attr));
    in_attr.index = 0;
    rknn_query(ctx, RKNN_QUERY_NATIVE_INPUT_ATTR, &in_attr, sizeof(in_attr));

    in_attr.type = RKNN_TENSOR_UINT8;
    in_attr.fmt = RKNN_TENSOR_NHWC;

    rknn_tensor_mem *input_mem = rknn_create_mem(ctx, in_attr.size_with_stride);
    rknn_set_io_mem(ctx, input_mem, &in_attr);

    /******************** 3. query outputs ********************/
    rknn_tensor_attr out_attr[9];
    rknn_tensor_mem *out_mem[9];

    for (int i = 0; i < 9; i++)
    {
        memset(&out_attr[i], 0, sizeof(out_attr[i]));
        out_attr[i].index = i;
        rknn_query(ctx, RKNN_QUERY_NATIVE_NHWC_OUTPUT_ATTR,
                   &out_attr[i], sizeof(out_attr[i]));
        out_mem[i] = rknn_create_mem(ctx, out_attr[i].size_with_stride);
        rknn_set_io_mem(ctx, out_mem[i], &out_attr[i]);
    }

    /******************** 4. read JPEG ********************/
    FILE *fp = fopen(img_path, "rb");
    if (!fp)
    {
        printf("open image failed\n");
        return -1;
    }
    fseek(fp, 0, SEEK_END);
    long jpg_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    unsigned char *jpg_buf = (unsigned char *)malloc(jpg_size);
    fread(jpg_buf, 1, jpg_size, fp);
    fclose(fp);

    /******************** 5. JPEG -> RGBA (DMA) ********************/
    tjhandle tjd = tjInitDecompress();
    int img_w, img_h, subsamp, cs;
    tjDecompressHeader3(tjd, jpg_buf, jpg_size,
                        &img_w, &img_h, &subsamp, &cs);

    int src_fd;
    void *src_dma;
    size_t src_size = img_w * img_h * 4;
    dma_alloc(src_size, &src_fd, &src_dma);

    tjDecompress2(tjd, jpg_buf, jpg_size,
                  (unsigned char *)src_dma,
                  img_w, img_w * 4, img_h,
                  TJPF_RGBA, TJFLAG_FASTDCT);

    tjDestroy(tjd);
    free(jpg_buf);

    /******************** 6. RGA resize ********************/
    float scale = fmin(640.f / img_w, 640.f / img_h);
    int resize_w = (int)(img_w * scale);
    int resize_h = (int)(img_h * scale);

    int resize_fd;
    void *resize_dma;
    dma_alloc(resize_w * resize_h * 4, &resize_fd, &resize_dma);

    rga_buffer_t src = wrapbuffer_fd(src_fd, img_w, img_h, RK_FORMAT_RGBA_8888);
    rga_buffer_t dst = wrapbuffer_fd(resize_fd, resize_w, resize_h, RK_FORMAT_RGBA_8888);

    imresize(src, dst,
             (double)resize_w / img_w,
             (double)resize_h / img_h,
             0, 1, NULL);

    /******************** 7. letterbox to 640x640 RGB ********************/
    int lb_fd;
    void *lb_dma;
    dma_alloc(640 * 640 * 3, &lb_fd, &lb_dma);

    memset(lb_dma, 114, 640 * 640 * 3);

    int pad_x = (640 - resize_w) / 2;
    int pad_y = (640 - resize_h) / 2;

    unsigned char *src_rgba = (unsigned char *)resize_dma;
    unsigned char *dst_rgb = (unsigned char *)lb_dma;

    for (int y = 0; y < resize_h; y++)
    {
        for (int x = 0; x < resize_w; x++)
        {
            unsigned char *s = src_rgba + (y * resize_w + x) * 4;
            unsigned char *d = dst_rgb +
                               ((y + pad_y) * 640 + (x + pad_x)) * 3;
            d[0] = s[0];
            d[1] = s[1];
            d[2] = s[2];
        }
    }

    /******************** 8. copy to RKNN input ********************/
    memcpy(input_mem->virt_addr, lb_dma, 640 * 640 * 3);

    /******************** 9. run ********************/
    rknn_run(ctx, NULL);

    /******************** 10. post process ********************/
    std::vector<Box> boxes;

    process_branch((int8_t *)out_mem[0]->virt_addr,
                   (int8_t *)out_mem[1]->virt_addr,
                   (int8_t *)out_mem[2]->virt_addr,
                   80, 80, 8,
                   out_attr[0], out_attr[1], out_attr[2], boxes);

    process_branch((int8_t *)out_mem[3]->virt_addr,
                   (int8_t *)out_mem[4]->virt_addr,
                   (int8_t *)out_mem[5]->virt_addr,
                   40, 40, 16,
                   out_attr[3], out_attr[4], out_attr[5], boxes);

    process_branch((int8_t *)out_mem[6]->virt_addr,
                   (int8_t *)out_mem[7]->virt_addr,
                   (int8_t *)out_mem[8]->virt_addr,
                   20, 20, 32,
                   out_attr[6], out_attr[7], out_attr[8], boxes);

    nms(boxes);

    /******************** 11. output ********************/
    for (auto &b : boxes)
    {
        printf("%s %.3f [%d %d %d %d]\n",
               coco_labels[b.cls], b.score,
               (int)b.x1, (int)b.y1, (int)b.x2, (int)b.y2);
    }

    /******************** 12. cleanup ********************/
    rknn_destroy(ctx);
    return 0;
}
