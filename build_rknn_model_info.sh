#!/bin/bash


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


rm -rf rknn_model_info_arm

$CXX \
    rknn_model_info.cpp \
    -o rknn_model_info_arm \
    -I./3rdparty/rknpu2/include \
    -L./3rdparty/rknpu2/Linux/armhf-uclibc \
    -lrknnmrt \
    -O2 -Wall -s

echo "完成！输出文件: rknn_model_info_arm"
file rknn_model_info_arm
