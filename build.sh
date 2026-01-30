#!/bin/bash
set -e

if [ ! -f build/build.ninja ]; then
    ./configure \
	--target-list=arm-softmmu \
	--enable-slirp \
	--disable-werror
fi

ninja -C build

mkdir -p bin
echo "Copying files to bin..."
system_bin="qemu-system-arm"
if [ "$(uname -s)" = "Darwin" ]; then
    system_bin="qemu-system-arm-unsigned"
fi

cp "build/${system_bin}" bin/
cp build/qemu-img bin/

if [ "$(uname -s)" = "Darwin" ]; then
	target="bin/${system_bin}"
	if [ -f "$target" ]; then
		if ! otool -l "$target" | grep -q '@executable_path/'; then
			# Ensure the binary can locate co-located dylibs on macOS
			install_name_tool -add_rpath "@executable_path/" "$target"
		fi
	fi
fi

if [ -f zImage_h ]; then
    cp zImage_h bin/
fi

if [ -f system_h.dtb ]; then
    cp system_h.dtb bin/
fi
