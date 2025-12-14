#!/bin/bash

export TOOLCHAIN=$PWD/toolchains/arm-rockchip830-linux-uclibcgnueabihf
export PATH=$TOOLCHAIN/bin:$PATH

which arm-rockchip830-linux-uclibcgnueabihf-gcc
export RUSTFLAGS="-Zunstable-options -C panic=immediate-abort"


cargo +nightly build \
  -Z build-std=core,alloc,std \
  -Z unstable-options \
  --release

file target/armv7-uclibc/release/turbo-pipeline


# cargo +nightly build \
#   -Z build-std=core,alloc,std \
#   -Z unstable-options

# file target/armv7-uclibc/debug/turbo-pipeline
