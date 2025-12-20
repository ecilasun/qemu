#!/bin/bash
set -e

if [ -z "$1" ]; then
    echo "Usage: $0 <device_path>"
    echo "Example: $0 /dev/sdb"
    exit 1
fi

INPUT_DEVICE="$1"
OUT_FILE="extracted_sdcard.img"

if [ ! -b "$INPUT_DEVICE" ]; then
    echo "Error: $INPUT_DEVICE is not a block device."
    exit 1
fi

# Try to find the parent disk if a partition is provided
DEVICE="$INPUT_DEVICE"
if command -v lsblk >/dev/null 2>&1; then
    TYPE=$(lsblk -no TYPE "$DEVICE" | head -n 1 | tr -d '[:space:]')
    if [ "$TYPE" = "part" ]; then
        PARENT_NAME=$(lsblk -no pkname "$DEVICE" | head -n 1 | tr -d '[:space:]')
        if [ -n "$PARENT_NAME" ]; then
            DEVICE="/dev/$PARENT_NAME"
            echo "Note: $INPUT_DEVICE is a partition. Switching to full disk: $DEVICE"
        fi
    fi
fi

echo "WARNING: This will read the ENTIRE content of $DEVICE and save it to $OUT_FILE."
echo "Press Enter to continue or Ctrl+C to cancel."
read

echo "Starting extraction..."
# Check if we have read permission, otherwise try sudo
if [ -r "$DEVICE" ]; then
    dd if="$DEVICE" of="$OUT_FILE" bs=1M status=progress
else
    echo "Requesting sudo privileges to read from $DEVICE..."
    sudo dd if="$DEVICE" of="$OUT_FILE" bs=1M status=progress
fi

echo "Extraction complete: $OUT_FILE"
