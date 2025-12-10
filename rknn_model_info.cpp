#include <rknn_api.h>

#include <vector>
#include <string>
#include <fstream>
#include <iostream>
#include <cstring>

class Infer
{
public:
    Infer(const void *model_buffer, size_t buffer_size)
    {
        int ret = rknn_init(&this->ctx, const_cast<void *>(model_buffer),
                            (uint32_t)buffer_size, 0, NULL);
        if (ret != RKNN_SUCC)
        {
            throw std::runtime_error("rknn_init failed: " + std::to_string(ret));
        }

        // 获取输入数量 + 输入属性信息
        rknn_input_output_num io_num2;
        ret = rknn_query(this->ctx, RKNN_QUERY_IN_OUT_NUM, &io_num2, sizeof(io_num2));
        if (ret != RKNN_SUCC)
            throw std::runtime_error("RKNN_QUERY_IN_OUT_NUM failed: " + std::to_string(ret));

        input_num = io_num2.n_input;

        input_attrs.resize(input_num);
        for (int i = 0; i < input_num; ++i)
        {
            rknn_tensor_attr &attr = input_attrs[i];
            memset(&attr, 0, sizeof(attr));
            attr.index = i;

            ret = rknn_query(this->ctx, RKNN_QUERY_INPUT_ATTR, &attr, sizeof(attr));
            if (ret != RKNN_SUCC)
                throw std::runtime_error("RKNN_QUERY_INPUT_ATTR failed: " + std::to_string(ret));
        }

        // 输出输入属性信息
        std::cout << "RKNN input num: " << input_num << std::endl;
        for (int i = 0; i < input_num; ++i)
        {
            auto &a = input_attrs[i];
            std::cout << "Input " << i << " shape: ";
            for (uint32_t j = 0; j < a.n_dims; ++j)
                std::cout << a.dims[j] << " ";

            std::cout << "\n"
                      << std::endl;
            std::cout << "data_type=" << a.type << "\n"
                      << "data_format=" << a.fmt << "\n"
                      << std::endl;
        }

        // 获取输出数量
        rknn_input_output_num io_num;
        ret = rknn_query(this->ctx, RKNN_QUERY_IN_OUT_NUM, &io_num, sizeof(io_num));
        if (ret != RKNN_SUCC)
            throw std::runtime_error("RKNN_QUERY_IN_OUT_NUM failed: " + std::to_string(ret));

        output_num = io_num.n_output;

        // 获取每个输出的维度信息
        output_attrs.resize(output_num);
        for (int i = 0; i < output_num; ++i)
        {
            rknn_tensor_attr &attr = output_attrs[i];
            memset(&attr, 0, sizeof(attr));
            attr.index = i;

            rknn_query(this->ctx, RKNN_QUERY_OUTPUT_ATTR, &attr, sizeof(attr));
        }

        std::cout << "RKNN output num: " << output_num << std::endl;
        for (int i = 0; i < output_num; ++i)
        {
            auto &a = output_attrs[i];
            std::cout << "Output " << i << " shape: ";
            for (uint32_t j = 0; j < a.n_dims; ++j)
                std::cout << a.dims[j] << " ";
            std::cout << std::endl;
        }
    }

    ~Infer()
    {
        if (ctx)
        {
            rknn_destroy(ctx);
            ctx = 0;
        }
    }

private:
    rknn_context ctx = 0;
    int input_num = 0;
    int output_num = 0;

    std::vector<rknn_tensor_attr> input_attrs;
    std::vector<rknn_tensor_attr> output_attrs;
};

unsigned char *load_model(const char *filename, size_t *model_size)
{
    FILE *fp = fopen(filename, "rb");
    if (!fp)
    {
        printf("Cannot open file: %s\n", filename);
        return NULL;
    }

    fseek(fp, 0, SEEK_END);
    int model_len = ftell(fp);
    unsigned char *model = (unsigned char *)malloc(model_len);
    fseek(fp, 0, SEEK_SET);
    if ((size_t)model_len != fread(model, 1, model_len, fp))
    {

        printf("fread %s fail!\n", filename);
        free(model);
        fclose(fp);
        return NULL;
    }
    *model_size = model_len;
    fclose(fp);
    return model;
}

int main(int argc, char **argv)
{
    const char *model_path = argv[1];
    size_t model_size;

    unsigned char *model_data = load_model(model_path, &model_size);
    if (!model_data)
    {
        printf("Load model failed\n");
        return -1;
    }

    Infer infer(model_data, model_size);
}