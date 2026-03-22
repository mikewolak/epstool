#!/bin/bash
#
# import_factory_sounds.sh - Import factory sound library into EPS disk image
#
# Usage: ./import_factory_sounds.sh <disk_image> <factory_sounds_dir>
#
# Creates a top-level directory for the collection (ENSEMBLE-FX) with
# subdirectories for each disk in the collection.

set -e

if [ $# -lt 2 ]; then
    echo "Usage: $0 <disk_image> <factory_sounds_dir>"
    echo "Example: $0 eps200mb.hda factory_sounds/"
    exit 1
fi

DISK_IMAGE="$1"
FACTORY_DIR="$2"
EPSTOOL="./epstool"
EFEFILE="./efefile"
COLLECTION_NAME="ENSEMBLE-FX"

# Check tools exist
if [ ! -x "$EPSTOOL" ]; then
    echo "Error: epstool not found or not executable"
    exit 1
fi

if [ ! -x "$EFEFILE" ]; then
    echo "Error: efefile not found or not executable"
    exit 1
fi

if [ ! -f "$DISK_IMAGE" ]; then
    echo "Error: Disk image not found: $DISK_IMAGE"
    exit 1
fi

if [ ! -d "$FACTORY_DIR" ]; then
    echo "Error: Factory sounds directory not found: $FACTORY_DIR"
    exit 1
fi

# Temporary directory for stripped files
TEMP_DIR=$(mktemp -d)
trap "rm -rf $TEMP_DIR" EXIT

echo "=== Importing Factory Sounds ==="
echo "Disk image: $DISK_IMAGE"
echo "Source: $FACTORY_DIR"
echo ""

# Create top-level collection directory
echo "Creating collection directory: $COLLECTION_NAME"
$EPSTOOL "$DISK_IMAGE" mkdir "$COLLECTION_NAME" 2>/dev/null || echo "  (already exists)"

# Process each subdirectory (disk)
for DISK_DIR in "$FACTORY_DIR"/*/; do
    if [ ! -d "$DISK_DIR" ]; then
        continue
    fi

    DISK_NAME=$(basename "$DISK_DIR")

    # Skip non-directories and hidden files
    if [[ "$DISK_NAME" == .* ]]; then
        continue
    fi

    # Truncate to 12 characters for EPS
    DISK_NAME_EPS="${DISK_NAME:0:12}"

    echo ""
    echo "=== Processing disk: $DISK_NAME_EPS ==="

    # Create disk subdirectory
    $EPSTOOL "$DISK_IMAGE" mkdir "$COLLECTION_NAME/$DISK_NAME_EPS" 2>/dev/null || echo "  (directory exists)"

    # Process each EFE file in the disk directory
    for EFE_FILE in "$DISK_DIR"*.efe "$DISK_DIR"*.EFE; do
        if [ ! -f "$EFE_FILE" ]; then
            continue
        fi

        # Get instrument info from Giebler header
        INFO=$("$EFEFILE" info "$EFE_FILE" 2>/dev/null)
        if [ $? -ne 0 ]; then
            echo "  Warning: Could not read $EFE_FILE"
            continue
        fi

        # Extract name from info output
        INST_NAME=$(echo "$INFO" | grep "^Name:" | head -1 | sed 's/Name:[[:space:]]*//')

        # Extract type from info output
        FILE_TYPE=$(echo "$INFO" | grep "^Type:" | head -1 | sed 's/Type:[[:space:]]*//' | cut -d' ' -f1)

        # Map Giebler type to epstool type
        case "$FILE_TYPE" in
            "Instrument")  TYPE="inst" ;;
            "Bank")        TYPE="bank" ;;
            "Sequence")    TYPE="seq" ;;
            "Song")        TYPE="song" ;;
            "SysEx")       TYPE="sysex" ;;
            "Macro")       TYPE="macro" ;;
            "Effect")      TYPE="effect" ;;
            *)             TYPE="inst" ;;  # Default to instrument
        esac

        # Strip Giebler header to create raw file
        RAW_FILE="$TEMP_DIR/temp_raw.efe"
        if ! "$EFEFILE" strip "$EFE_FILE" "$RAW_FILE" 2>/dev/null; then
            echo "  Warning: Could not strip header from $EFE_FILE"
            continue
        fi

        # Import into disk image
        DEST_PATH="$COLLECTION_NAME/$DISK_NAME_EPS/$INST_NAME"
        echo "  Importing: $INST_NAME ($TYPE)"
        if ! $EPSTOOL "$DISK_IMAGE" import "$RAW_FILE" "$DEST_PATH" "$TYPE" 2>/dev/null; then
            echo "    Warning: Failed to import $INST_NAME"
        fi

        rm -f "$RAW_FILE"
    done
done

echo ""
echo "=== Import Complete ==="
$EPSTOOL "$DISK_IMAGE" info
echo ""
echo "Collection structure:"
$EPSTOOL "$DISK_IMAGE" tree
