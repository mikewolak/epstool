#!/bin/bash
#
# copy_to_eps.sh - Safely copy disk image to EPS SD card
#
# Usage: ./copy_to_eps.sh <source_image>
#

set -e

MOUNT_POINT="/media/mwolak/eps"
DEVICE="/dev/sda"
TARGET_FILE="hd0.hda"

# Color output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Cleanup function - always remove mount point on exit
cleanup() {
    if [ -d "$MOUNT_POINT" ]; then
        sudo umount "$MOUNT_POINT" 2>/dev/null || true
        sudo rmdir "$MOUNT_POINT" 2>/dev/null || true
    fi
}
trap cleanup EXIT

error() {
    echo -e "${RED}ERROR: $1${NC}" >&2
    exit 1
}

warn() {
    echo -e "${YELLOW}WARNING: $1${NC}"
}

info() {
    echo -e "${GREEN}$1${NC}"
}

# Check arguments
if [ $# -ne 1 ]; then
    echo "Usage: $0 <source_image>"
    echo "Example: $0 eps200mb.hda"
    exit 1
fi

SOURCE="$1"

# Validate source file exists
if [ ! -f "$SOURCE" ]; then
    error "Source file not found: $SOURCE"
fi

info "Source: $SOURCE"
info "Target: $MOUNT_POINT/$TARGET_FILE"

# Step 1: Unmount if mounted anywhere (ignore errors)
echo "Unmounting $DEVICE (if mounted)..."
sudo umount "$DEVICE" 2>/dev/null || true
sudo umount "$MOUNT_POINT" 2>/dev/null || true

# Step 2: Remove stale mount point if it exists
if [ -d "$MOUNT_POINT" ]; then
    echo "Removing stale mount point..."
    sudo rmdir "$MOUNT_POINT" 2>/dev/null || sudo rm -rf "$MOUNT_POINT"
fi

# Step 3: Check device exists
echo "Checking device $DEVICE..."
if [ ! -b "$DEVICE" ]; then
    error "Device $DEVICE not found. Is the SD card inserted?"
fi

# Check device size (should be > 0)
SIZE=$(lsblk -b -n -o SIZE "$DEVICE" 2>/dev/null || echo "0")
if [ "$SIZE" = "0" ] || [ -z "$SIZE" ]; then
    error "Device $DEVICE has zero size. SD card may not be properly inserted."
fi

info "Device $DEVICE detected ($(numfmt --to=iec $SIZE))"

# Step 4: Create fresh mount point
echo "Creating mount point $MOUNT_POINT..."
sudo mkdir -p "$MOUNT_POINT"

# Step 5: Mount the device
echo "Mounting $DEVICE to $MOUNT_POINT..."
sudo mount -t exfat "$DEVICE" "$MOUNT_POINT" || error "Failed to mount $DEVICE"

# Step 6: Validate old file exists
echo "Checking for existing $TARGET_FILE..."
if [ ! -f "$MOUNT_POINT/$TARGET_FILE" ]; then
    warn "No existing $TARGET_FILE found on SD card (this may be OK for first copy)"
else
    OLD_SIZE=$(stat -c%s "$MOUNT_POINT/$TARGET_FILE")
    info "Found existing $TARGET_FILE ($(numfmt --to=iec $OLD_SIZE))"
fi

# Step 7: Copy file
echo "Copying $SOURCE to $MOUNT_POINT/$TARGET_FILE..."
sudo cp "$SOURCE" "$MOUNT_POINT/$TARGET_FILE" || error "Copy failed"

# Step 8: Sync to ensure write completes
echo "Syncing filesystem..."
sync

# Step 9: Drop caches to ensure we read from disk, not cache
echo "Dropping filesystem caches..."
sudo sh -c 'echo 3 > /proc/sys/vm/drop_caches'

# Step 10: Calculate and compare checksums
echo "Calculating checksums..."
SRC_CRC=$(md5sum "$SOURCE" | awk '{print $1}')
DST_CRC=$(sudo md5sum "$MOUNT_POINT/$TARGET_FILE" | awk '{print $1}')

echo "Source MD5:      $SRC_CRC"
echo "Destination MD5: $DST_CRC"

if [ "$SRC_CRC" != "$DST_CRC" ]; then
    error "Checksum mismatch! Copy may be corrupted."
fi

# Step 10: Verify file sizes match
SRC_SIZE=$(stat -c%s "$SOURCE")
DST_SIZE=$(sudo stat -c%s "$MOUNT_POINT/$TARGET_FILE")

if [ "$SRC_SIZE" != "$DST_SIZE" ]; then
    error "Size mismatch! Source: $SRC_SIZE, Destination: $DST_SIZE"
fi

# Step 11: Unmount and remove mount point (cleanup trap handles this)
echo "Unmounting and cleaning up..."

info "=========================================="
info "OK - Copy completed and verified!"
info "Source:      $SOURCE ($SRC_SIZE bytes)"
info "Destination: $TARGET_FILE"
info "MD5:         $SRC_CRC"
info "=========================================="
