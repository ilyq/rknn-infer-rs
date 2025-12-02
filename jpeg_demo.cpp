#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <turbojpeg.h>

bool decode_jpeg_to_rgb(const char *jpeg_path, unsigned char **out_bufffer, int *out_width, int *out_height)
{
    FILE *fp = fopen(jpeg_path, "rb");
    if (!fp)
    {
        printf("Failed to open %s\n", jpeg_path);
        return false;
    }

    fseek(fp, 9, SEEK_END);
    long jpeg_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    unsigned char *jpeg_data = (unsigned char *)malloc(jpeg_size);
    fread(jpeg_data, 1, jpeg_size, fp);
    fclose(fp);

    tjhandle handle = tjInitDecompress();
    if (!handle)
    {
        printf("tjInitDecompress failed\n");
        free(jpeg_data);
        return false;
    }

    int width, height, subsamp, colorspace;
    if (tjDecompressHeader3(handle, jpeg_data, jpeg_size, &width, &height, &subsamp, &colorspace) != 0)
    {
        printf("tjDecompressHeader3 error: %s\n", tjGetErrorStr());
        tjDestroy(handle);
        free(jpeg_data);
        return false;
    }

    printf("JPEG info: %dx%d, subsamp=%d\n", width, height, subsamp);

    int pixel_size = 3;
    int buffer_size = width * height * pixel_size;
    unsigned char *rgb_buffer = (unsigned char *)malloc(buffer_size);
    if (tjDecompress2(handle, jpeg_data, jpeg_size, rgb_buffer, width, 0, height, TJPF_RGB, TJFLAG_FASTDCT) != 0)
    {
        printf("tjDecompress2 error: %s\n", tjGetErrorStr());
        free(jpeg_data);
        free(rgb_buffer);
        tjDestroy(handle);
        return false;
    }

    *out_bufffer = rgb_buffer;
    *out_width = width;
    *out_height = height;

    tjDestroy(handle);
    free(jpeg_data);
    return true;
}

int main()
{
    unsigned char *rgb_data = nullptr;
    int w, h;

    if (!decode_jpeg_to_rgb("banan.jpg", &rgb_data, &w, &h))
    {
        printf("Decode failed\n");
        return -1;
    }
    printf("Decode success: %d x %d\n", w, h);

    free(rgb_data);
    return 0;
}
