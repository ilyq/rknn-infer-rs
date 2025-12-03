cmake_minimum_required(VERSION 3.10)
project(jpeg_demo)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(jpeg_demo jpeg_demo.cpp)

target_include_directories(jpeg_demo PRIVATE
    /opt/libjpeg-turbo/include
)

target_link_directories(jpeg_demo PRIVATE
    /opt/libjpeg-turbo/lib64
)

target_link_libraries(jpeg_demo PRIVATE
    turbojpeg
)

if (UNIX AND NOT APPLE)
    set_target_properties(jpeg_demo PROPERTIES
        INSTALL_RPATH "/opt/libjpeg-turbo/lib64"
    )
endif()