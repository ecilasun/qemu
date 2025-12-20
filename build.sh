#!/bin/bash
set -e

if [ ! -f build/build.ninja ]; then
    mkdir -p build
    ./configure --target-list=arm-softmmu
fi

ninja -C build

mkdir -p bin
echo "Copying files to bin..."
cp build/qemu-system-arm bin/
cp build/qemu-img bin/

if [ -f zImage_h ]; then
    cp zImage_h bin/
fi

if [ -f system_h.dtb ]; then
    cp system_h.dtb bin/
fi
