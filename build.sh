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

~/work/turbo-pipeline/toolchains/arm-rockchip830-linux-uclibcgnueabihf/bin/arm-rockchip830-linux-uclibcgnueabihf-g++ \
    rga_demo.cpp \
    -o rga_demo_arm \
    -I/home/sd1/work/turbo-pipeline/3rdparty/jpeg_turbo/include \
    -I/home/sd1/work/turbo-pipeline/3rdparty/librga/include \
    -L/home/sd1/work/turbo-pipeline/3rdparty/jpeg_turbo/Linux/armhf_uclibc \
    -L/home/sd1/work/turbo-pipeline/3rdparty/librga/Linux/armhf_uclibc \
    -lturbojpeg \
    -lrga \
    -O2 -Wall -s

echo "完成！输出文件: rga_demo_arm"
file rga_demo_arm