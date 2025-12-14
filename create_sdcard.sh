#!/bin/bash
set -e

IMAGE="sdcard.img"
BOOT_DIR="petalinux/BOOT"
ROOTFS_DIR="petalinux/rootfs"

# Size in MB
SIZE=1024

echo "Creating $IMAGE of size ${SIZE}MB..."
dd if=/dev/zero of=$IMAGE bs=1M count=$SIZE status=progress

echo "Partitioning $IMAGE..."
# Create partition table (MBR)
# Partition 1: 100MB, FAT32, Bootable
# Partition 2: Rest, Linux
sfdisk $IMAGE <<EOF
,100M,0xC,*
,,0x83
EOF

echo "Setting up loop device..."
LOOPDEV=$(losetup -fP --show $IMAGE)
echo "Loop device: $LOOPDEV"

p1="${LOOPDEV}p1"
p2="${LOOPDEV}p2"

echo "Formatting partitions..."
mkfs.vfat -n BOOT $p1
mkfs.ext4 -L rootfs $p2

echo "Mounting and copying files..."
mkdir -p mnt_boot mnt_root

# Handle BOOT
mount $p1 mnt_boot
if [ -d "$BOOT_DIR" ]; then
    echo "Copying BOOT files..."
    cp -r $BOOT_DIR/* mnt_boot/ || true
else
    echo "Warning: $BOOT_DIR does not exist."
fi
umount mnt_boot

# Handle rootfs
mount $p2 mnt_root
if [ -d "$ROOTFS_DIR" ]; then
    echo "Copying rootfs files..."
    # Check if directory is not empty before copying to avoid error
    if [ "$(ls -A $ROOTFS_DIR)" ]; then
        cp -a $ROOTFS_DIR/* mnt_root/
    else
        echo "$ROOTFS_DIR is empty, skipping copy."
    fi
else
    echo "Warning: $ROOTFS_DIR does not exist."
fi
umount mnt_root

echo "Cleaning up..."
losetup -d $LOOPDEV
rmdir mnt_boot mnt_root

echo "Done! $IMAGE created successfully."
