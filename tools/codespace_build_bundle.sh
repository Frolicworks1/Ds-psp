#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

APK="${1:-}"
if [ -z "$APK" ]; then
  APK="$(find /workspaces -maxdepth 4 -type f -iname '*.apk' -print -quit 2>/dev/null || true)"
fi
if [ -z "$APK" ] || [ ! -f "$APK" ]; then
  echo "ERROR: APK not found. Pass the path to your legally owned APK."
  exit 2
fi
if [ -z "${PSPDEV:-}" ] || [ ! -x "$PSPDEV/bin/psp-gcc" ]; then
  echo "ERROR: PSPDEV toolchain unavailable. Rebuild the Codespace."
  exit 3
fi

echo "[1/3] Building PSP EBOOT..."
rm -rf build
cmake -S . -B build
cmake --build build -j"$(nproc)"
test -f build/EBOOT.PBP

echo "[2/3] Creating PPSSPP-ready bundle..."
python3 tools/build_full_local_bundle.py "$APK" --eboot build/EBOOT.PBP --output DeadSpace-PSP-Ready

echo "[3/3] Verifying..."
test -f DeadSpace-PSP-Ready/PSP/GAME/DEADSPACE/EBOOT.PBP
test -d DeadSpace-PSP-Ready/data/deadspace
test -f DeadSpace-PSP-Ready/BUNDLE_MANIFEST.json
test -f DeadSpace-PSP-Ready.zip

echo "SUCCESS"
echo "ZIP: $ROOT/DeadSpace-PSP-Ready.zip"
