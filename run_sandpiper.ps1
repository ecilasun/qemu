# Script to run Sandpiper emulator

# Add MSYS2 bin to PATH so QEMU can find DLLs
$env:PATH = "C:\msys64\mingw64\bin;C:\msys64\usr\bin;" + $env:PATH

$QEMU = "$PSScriptRoot\build\qemu-system-arm.exe"
$MACHINE = "sandpiper"
$SD_IMG = "sdcard.img"
$KERNEL = "zImage"
$DTB = "system.dtb"

# Check if SD image exists
if (-not (Test-Path $SD_IMG)) {
    Write-Error "SD card image $SD_IMG not found! Run create_sdcard.sh (in WSL) first."
    exit 1
}

# Check if Kernel exists
if (-not (Test-Path $KERNEL)) {
    Write-Error "Kernel $KERNEL not found! Run dumpimage to extract it from image.ub."
    exit 1
}

# Check if DTB exists
if (-not (Test-Path $DTB)) {
    Write-Error "DTB $DTB not found! Run dumpimage to extract it from image.ub."
    exit 1
}

Write-Host "Starting Sandpiper Emulator with machine '$MACHINE'..."
Write-Host "Press Ctrl+A, X to exit QEMU."

# Booting with direct kernel loading (-kernel, -dtb) because we don't have a BootROM.
# We still attach the SD card so the kernel can mount rootfs from it.
# We append 'root=/dev/mmcblk0p2' to tell Linux to use the second partition of the SD card as root.

& $QEMU -M $MACHINE -m 1024 -serial stdio `
    -drive file=$SD_IMG,if=sd,format=raw `
    -kernel $KERNEL `
    -dtb $DTB `
    -append "console=ttyPS0,115200 root=/dev/mmcblk0p2 rw rootwait earlyprintk" `
    -d guest_errors,unimp
