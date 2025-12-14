# Script to run Sandpiper emulator with extracted SD card image

# Add MSYS2 bin to PATH so QEMU can find DLLs
$env:PATH = "C:\msys64\mingw64\bin;C:\msys64\usr\bin;" + $env:PATH

$QEMU = "$PSScriptRoot\build\qemu-system-arm.exe"
$MACHINE = "sandpiper"
$SD_IMG = "extracted_sdcard.img"
$KERNEL = "zImage_h"
$DTB = "system_h.dtb"

# Check if SD image exists
if (-not (Test-Path $SD_IMG)) {
    Write-Error "SD card image $SD_IMG not found! Please extract it from the physical drive."
    exit 1
}

# Check if Kernel exists
if (-not (Test-Path $KERNEL)) {
    Write-Error "Kernel $KERNEL not found! (Should have been extracted from H:\image.ub)"
    exit 1
}

# Check if DTB exists
if (-not (Test-Path $DTB)) {
    Write-Error "DTB $DTB not found! (Should have been extracted from H:\image.ub)"
    exit 1
}

Write-Host "Starting Sandpiper Emulator with machine '$MACHINE'..."
Write-Host "Press Ctrl+A, X to exit QEMU."

# Booting with direct kernel loading (-kernel, -dtb)
# Using the extracted SD card image which should contain the rootfs.

& $QEMU -M $MACHINE -m 1024 -serial stdio `
    -drive file=$SD_IMG,if=sd,format=raw `
    -kernel $KERNEL `
    -dtb $DTB `
    -append "console=ttyPS0,115200 root=/dev/mmcblk0p2 rw rootwait earlyprintk"
