#!/bin/bash
# Export all instrument WAV files from factory_sounds collection

OUTDIR="/tmp/wav_export"
mkdir -p "$OUTDIR"

count=0
skipped=0

find factory_sounds -name "*.efe" -type f | while read f; do
    # Get the subdirectory name for organizing output
    subdir=$(dirname "$f" | sed 's|factory_sounds/||')
    outpath="$OUTDIR/$subdir"
    mkdir -p "$outpath"

    # Try to export - epstool will skip non-instrument files
    result=$(./epstool export-wav "$f" "$outpath/" 2>&1)

    if echo "$result" | grep -q "Extracted"; then
        echo "OK: $f"
        ((count++))
    else
        echo "SKIP: $f ($(echo "$result" | head -1))"
        ((skipped++))
    fi
done

echo ""
echo "Done. Exported instruments, skipped non-instrument files."
