#!/bin/bash

g++ jpeg_demo.cpp -o jpeg_demo \
-I/opt/libjpeg-turbo/include \
-L/opt/libjpeg-turbo/lib64 \
-lturbojpeg \
-Wl,-rpath,/opt/libjpeg-turbo/lib64 \
-O2 -Wall
