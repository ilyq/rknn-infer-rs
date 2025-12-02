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
