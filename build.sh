#!/bin/bash

# g++ jpeg_demo.cpp -o jpeg_demo \
# -I/opt/libjpeg-turbo/include \
# -L/opt/libjpeg-turbo/lib64 \
# -lturbojpeg \
# -Wl,-rpath,/opt/libjpeg-turbo/lib64 \
# -O2 -Wall


# ~/work/turbo-pipeline/toolchains/arm-rockchip830-linux-uclibcgnueabihf/bin/arm-rockchip830-linux-uclibcgnueabihf-g++ \
#     jpeg_demo.cpp \
#     -o jpeg_demo_arm \
#     -I/home/sd1/work/turbo-pipeline/3rdparty/jpeg_turbo/include \
#     -L/home/sd1/work/turbo-pipeline/3rdparty/jpeg_turbo/Linux/armhf_uclibc \
#     -lturbojpeg \
#     -O2 -Wall -s

# echo "完成！输出文件: jpeg_demo_arm"
# file jpeg_demo_arm


rm -rf rga_demo_arm

# 获取当前脚本所在目录
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$SCRIPT_DIR"

echo "当前工作目录: $PROJECT_ROOT"

# 交叉编译工具链相对路径
TOOLCHAIN_DIR="$PROJECT_ROOT/toolchains/arm-rockchip830-linux-uclibcgnueabihf"
CXX="$TOOLCHAIN_DIR/bin/arm-rockchip830-linux-uclibcgnueabihf-g++"

# 检查工具链是否存在
if [ ! -f "$CXX" ]; then
    echo "错误: 找不到交叉编译工具链: $CXX"
    echo "请确保toolchains目录下包含正确的工具链"
    exit 1
fi


$CXX \
    rga_demo.cpp \
    -o rga_demo_arm \
    -I./3rdparty/jpeg_turbo/include \
    -I./3rdparty/librga/include \
    -I./3rdparty/rknpu2/include \
    -L./3rdparty/jpeg_turbo/Linux/armhf_uclibc \
    -L./3rdparty/librga/Linux/armhf_uclibc \
    -L./3rdparty/rknpu2/Linux/armhf_uclibc \
    -lturbojpeg \
    -lrga \
    -lrknnmrt \
    -O2 -Wall -s

echo "完成！输出文件: rga_demo_arm"
file rga_demo_arm