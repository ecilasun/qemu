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
else
	# On Linux, set RPATH to look in the same directory as the executable
	target="bin/${system_bin}"
	if command -v patchelf >/dev/null 2>&1; then
		echo "Setting RPATH for ${system_bin}..."
		patchelf --set-rpath '$ORIGIN' "$target"
	else
		echo "Warning: patchelf not found. Installing it is recommended for proper library loading."
		echo "You can install it with: sudo apt-get install patchelf"
	fi
fi


if [ -f system_h.dtb ]; then
    cp system_h.dtb bin/
fi

# Copy libslirp library
if [ -f build/subprojects/slirp/libslirp.so.0.4.0 ]; then
    echo "Copying libslirp to bin..."
    cp -P build/subprojects/slirp/libslirp.so* bin/
fi
