#!/usr/bin/env bash
# Verify the runtime foundation. Successful checks do not establish gameplay.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
cmake -S tests -B build-host -DDSPSP_SANITIZE=ON
cmake --build build-host -j2
ctest --test-dir build-host --output-on-failure
if [[ -z "${PSPDEV:-}" || ! -x "$PSPDEV/bin/psp-gcc" ]]; then
    echo 'PSPDEV must point to a prepared PSP SDK to run the PSP build.' >&2
    exit 2
fi
export PATH="$PSPDEV/bin:$PATH"
cmake -S . -B build
cmake --build build -j2
test -s build/EBOOT.PBP
echo 'Host checks and PSP package build passed; gameplay/device verification remains separate.'
