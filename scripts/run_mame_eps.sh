#!/usr/bin/env bash
# Helper to launch MAME’s Ensoniq EPS driver with the v2.4 EPROM set.
# Assumptions:
#   - MAME is installed and available on PATH as `mame`
#   - The MAME driver shortname is `eps` (used by current upstream builds)
#   - This repo contains `Ensoniq EPS Version 2.4/eps_os_24_hi.bin`
#     and `Ensoniq EPS Version 2.4/eps_os_24_lo.bin`
#   - You provide a boot floppy image (IMG/IMA) via $FLOPPY or argv
#
# The script creates a minimal ROM directory layout and invokes MAME with:
#   -rompath ./mame-roms       (generated here)
#   -flop1   <floppy image>    (optional)
#   -window -nomax             (keep windowed)
#   -skip_gameinfo             (skip audit nags)
#   -log -debug                (capture debug log; open debugger)
#
# Usage:
#   ./scripts/run_mame_eps.sh path/to/boot_floppy.img
#   FLOPPY=path/to/boot_floppy.img ./scripts/run_mame_eps.sh

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ROM_SRC_HI="$ROOT_DIR/Ensoniq EPS Version 2.4/eps_os_24_hi.bin"
ROM_SRC_LO="$ROOT_DIR/Ensoniq EPS Version 2.4/eps_os_24_lo.bin"
ROM_DST_BASE="$ROOT_DIR/mame-roms"
ROM_DST_DIR="$ROM_DST_BASE/eps"

FLOPPY_IMG="${1:-${FLOPPY:-}}"

if ! command -v mame >/dev/null 2>&1; then
  echo "ERROR: mame not found on PATH." >&2
  exit 1
fi

if [[ ! -f "$ROM_SRC_HI" || ! -f "$ROM_SRC_LO" ]]; then
  echo "ERROR: EPS v2.4 hi/lo ROMs not found in expected location:" >&2
  echo "  $ROM_SRC_HI" >&2
  echo "  $ROM_SRC_LO" >&2
  exit 1
fi

mkdir -p "$ROM_DST_DIR"
# MAME’s eps driver expects names eps-h.bin / eps-l.bin.
cp "$ROM_SRC_HI" "$ROM_DST_DIR/eps-h.bin"
cp "$ROM_SRC_LO" "$ROM_DST_DIR/eps-l.bin"
# Keep the descriptive copies too (harmless, can help other tools).
cp "$ROM_SRC_HI" "$ROM_DST_DIR/eps_os_24_hi.bin"
cp "$ROM_SRC_LO" "$ROM_DST_DIR/eps_os_24_lo.bin"

MAME_ARGS=(
  eps
  -rompath "$ROM_DST_BASE"
  -window
  -nomax
  -skip_gameinfo
  -log
  -debug
)

if [[ -n "$FLOPPY_IMG" ]]; then
  if [[ ! -f "$FLOPPY_IMG" ]]; then
    echo "ERROR: floppy image '$FLOPPY_IMG' not found." >&2
    exit 1
  fi
  MAME_ARGS+=(-flop1 "$FLOPPY_IMG")
fi

echo "Launching: mame ${MAME_ARGS[*]}"
exec mame "${MAME_ARGS[@]}"
