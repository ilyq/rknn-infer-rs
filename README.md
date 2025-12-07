# libjpeg-turbo 编译

```
git clone --depth 1 https://github.com/libjpeg-turbo/libjpeg-turbo.git
cmake -G"Unix Makefiles" -DCMAKE_BUILD_TYPE=Release
make
sudo make install

/opt/libjpeg-turbo/include/
```

# 编译命令说明

```
g++ jpeg_demo.cpp -o jpeg_demo \
-I/opt/libjpeg-turbo/include \
-L/opt/libjpeg-turbo/lib64 \
-lturbojpeg \
-Wl,-rpath,/opt/libjpeg-turbo/lib64 \
-O2 -Wall


头文件包含路径
-I: 添加头文件搜索路径
-I/opt/libjpeg-turbo/include

库文件链接路径
-L: 添加库文件搜索路径
-L/opt/libjpeg-turbo/lib64

链接特定库
-l: 链接指定的库
-lturbojpeg

运行时库路径设置
-Wl: 将后续参数传递给链接器
-rpath: 设置运行时库搜索路径
-Wl,-rpath,/opt/libjpeg-turbo/lib64

编译优化和警告选项
-O2: 启用二级优化，提高代码执行效率
-Wall: 显示所有警告信息，帮助发现潜在问题
-O2 -Wall
```

# rknn yolo 模型输入和输出

```
input
name: norm_tensor:0
tensor: int8[1,3,640,640]

output
name: norm_tensor:1
tensor: int8[1,64,80,80]
output1
name: norm_tensor:2
tensor: int8[1,80,80,80]
output2
name: norm_tensor:3
tensor: int8[1,1,80,80]
output3
name: norm_tensor:4
tensor: int8[1,64,40,40]
output4
name: norm_tensor:5
tensor: int8[1,80,40,40]
output5
name: norm_tensor:6
tensor: int8[1,1,40,40]
output6
name: norm_tensor:7
tensor: int8[1,64,20,20]
output7
name: norm_tensor:8
tensor: int8[1,80,20,20]
output8
name: norm_tensor:9
tensor: int8[1,1,20,20]
```
