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
if [ -f build/qemu-system-arm.exe ]; then
	cp build/qemu-system-arm.exe bin/
else
	cp build/qemu-system-arm bin/
fi
if [ -f build/qemu-img.exe ]; then
	cp build/qemu-img.exe bin/
else
	cp build/qemu-img bin/
fi

# Copy required DLLs (e.g., slirp, SDL2, GTK stack) next to the executables
copy_deps() {
	local exe="$1"
	local prefix="${MINGW_PREFIX:-/mingw64}"
	if ! command -v ntldd >/dev/null 2>&1; then
		echo "ntldd not found; skipping DLL copy for $exe"
		return
	fi
	mapfile -t libs < <(
		ntldd -R "$exe" 2>/dev/null \
		| sed 's/=>/ /' \
		| awk '{for(i=1;i<=NF;i++) if ($i ~ /mingw64[\\\\\/]bin[\\\\\/].*\.dll$/) print $i}' \
		| sed 's#\\\\#/#g' \
		| sort -u
	)
	echo "Dependencies for $exe: ${#libs[@]}"
	IFS=':' read -ra path_entries <<< "$PATH"
	for lib in "${libs[@]}"; do
		if [ -f "$lib" ]; then
			base="$(basename "$lib")"
			dir="$(dirname "$lib")"
			on_path=false
			for p in "${path_entries[@]}"; do
				[ -z "$p" ] && continue
				p="${p//\\/\/}"
				if [ "$p" = "$dir" ]; then
					on_path=true
					break
				fi
			done
			if [ "$on_path" = true ]; then
				echo "  skip $base (dir on PATH)"
				continue
			fi
			if [ -f "bin/$base" ]; then
				echo "  skip $base (already in bin)"
				continue
			fi
			echo "  copy $base"
			cp -u "$lib" bin/
		fi
	 done
}

copy_deps bin/qemu-system-arm.exe 2>/dev/null || copy_deps bin/qemu-system-arm
copy_deps bin/qemu-img.exe 2>/dev/null || copy_deps bin/qemu-img

# Strip executables to reduce size when strip is available
if command -v strip >/dev/null 2>&1; then
	[ -f bin/qemu-system-arm.exe ] && strip bin/qemu-system-arm.exe
	[ -f bin/qemu-system-arm ] && strip bin/qemu-system-arm
	[ -f bin/qemu-img.exe ] && strip bin/qemu-img.exe
	[ -f bin/qemu-img ] && strip bin/qemu-img
else
	echo "strip not found; skipping binary stripping"
fi

if [ -f zImage_h ]; then
    cp zImage_h bin/
fi

if [ -f system_h.dtb ]; then
    cp system_h.dtb bin/
fi
