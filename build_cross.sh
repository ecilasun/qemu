#!/bin/bash
set -euo pipefail

# Cross-compile QEMU for Windows x86_64 using MinGW-w64
# Requires: x86_64-w64-mingw32-gcc, pkg-config, ninja

BUILD_DIR="build-win64"
MXE_ROOT="${MXE_ROOT:-$HOME/mxe}"
MXE_TARGET="${MXE_TARGET:-x86_64-w64-mingw32.static}"
CROSS_PREFIX="$MXE_ROOT/usr/bin/${MXE_TARGET}-"
PKG_CONFIG_BIN="${CROSS_PREFIX}pkg-config"
STRIP_BIN="${CROSS_PREFIX}strip"

if ! command -v "$PKG_CONFIG_BIN" >/dev/null 2>&1; then
    echo "Error: $PKG_CONFIG_BIN not found. Build MXE target $MXE_TARGET first." >&2
    exit 1
fi

export PATH="$MXE_ROOT/usr/bin:$PATH"
export PKG_CONFIG="$PKG_CONFIG_BIN"
export PKG_CONFIG_SYSROOT_DIR="$MXE_ROOT/usr/$MXE_TARGET"
export PKG_CONFIG_LIBDIR="$MXE_ROOT/usr/$MXE_TARGET/lib/pkgconfig:$MXE_ROOT/usr/$MXE_TARGET/share/pkgconfig"

if [ ! -f "$BUILD_DIR/build.ninja" ]; then
    mkdir -p "$BUILD_DIR"
    ( \
        cd "$BUILD_DIR" && \
        ../configure \
            --cross-prefix="$CROSS_PREFIX" \
            --target-list=arm-softmmu \
            --enable-slirp \
            --disable-werror \
            --prefix="$(pwd)/install" \
    )
fi

ninja -C "$BUILD_DIR"

mkdir -p bin
echo "Copying files to bin..."
cp "$BUILD_DIR/qemu-system-arm.exe" bin/
cp "$BUILD_DIR/qemu-img.exe" bin/

if [ -f "$BUILD_DIR/subprojects/slirp/libslirp-0.dll" ]; then
    cp "$BUILD_DIR/subprojects/slirp/libslirp-0.dll" bin/
elif [ -f "$BUILD_DIR/install/libslirp-0.dll" ]; then
    cp "$BUILD_DIR/install/libslirp-0.dll" bin/
fi

if command -v "$STRIP_BIN" >/dev/null 2>&1; then
    "$STRIP_BIN" --strip-unneeded bin/qemu-system-arm.exe bin/qemu-img.exe
fi
