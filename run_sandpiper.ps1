# Script to run Sandpiper emulator
$QEMU = "$PSScriptRoot\build\qemu-system-arm.exe"
$MACHINE = "sandpiper"
$BOOT_DIR = "petalinux\BOOT"
$IMAGE = "$BOOT_DIR\image.ub"

# Check if image exists
if (-not (Test-Path $IMAGE)) {
    Write-Error "Image file $IMAGE not found!"
    exit 1
}

Write-Host "Starting Sandpiper Emulator with machine '$MACHINE'..."
Write-Host "Press Ctrl+A, X to exit QEMU."

# Attempt to boot the image directly. 
# Note: image.ub is typically a FIT image. QEMU might support it directly or require u-boot.
# If this fails, try extracting the kernel (zImage) and DTB, or use u-boot.elf as -kernel.

# We also mount the BOOT directory as an SD card, so if U-Boot runs, it can see the files.
& $QEMU -M $MACHINE -m 1024 -serial mon:stdio -display none `
    -kernel $IMAGE `
    -drive file=fat:rw:$BOOT_DIR,if=sd,format=raw
