#!/usr/bin/env python3
"""Build a complete local PPSSPP test bundle from a user-owned Dead Space APK.

The resulting ZIP contains the PSP EBOOT at the normal PSP path plus the
complete APK assets tree under data/deadspace/ and the complete APK under
android_apk/ for local debugging. Nothing is downloaded or committed.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import zipfile
from pathlib import Path


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("apk", type=Path)
    ap.add_argument("--eboot", type=Path, required=True)
    ap.add_argument("-o", "--output", type=Path, default=Path("DeadSpace-PSP-Ready"))
    args = ap.parse_args()

    if not args.apk.is_file():
        raise SystemExit(f"APK not found: {args.apk}")
    if not args.eboot.is_file():
        raise SystemExit(f"EBOOT.PBP not found: {args.eboot}")

    root = args.output.resolve()
    if root.exists():
        shutil.rmtree(root)

    (root / "PSP" / "GAME" / "DEADSPACE").mkdir(parents=True)
    (root / "data" / "deadspace").mkdir(parents=True)
    (root / "android_apk").mkdir(parents=True)

    with zipfile.ZipFile(args.apk) as z:
        # Preserve every APK entry for local reference/debugging.
        for info in z.infolist():
            if info.is_dir():
                (root / "android_apk" / info.filename).mkdir(
                    parents=True, exist_ok=True
                )
                continue
            dst = root / "android_apk" / info.filename
            dst.parent.mkdir(parents=True, exist_ok=True)
            with z.open(info) as src, dst.open("wb") as out:
                shutil.copyfileobj(src, out)

        # Preserve the complete original assets/ tree at the PSP runtime root.
        for info in z.infolist():
            if info.is_dir() or not info.filename.startswith("assets/"):
                continue
            rel = info.filename[len("assets/"):]
            dst = root / "data" / "deadspace" / rel
            dst.parent.mkdir(parents=True, exist_ok=True)
            with z.open(info) as src, dst.open("wb") as out:
                shutil.copyfileobj(src, out)

    shutil.copy2(
        args.eboot,
        root / "PSP" / "GAME" / "DEADSPACE" / "EBOOT.PBP",
    )

    install = """Dead Space PSP local test bundle

PPSSPP:
1. Copy the PSP/ and data/ directories to the emulator's memory-stick root.
2. Start PSP/GAME/DEADSPACE/EBOOT.PBP.
3. Keep data/deadspace/ intact.

android_apk/ is a local reference/debug copy of the complete APK contents.
The bundle is generated from your own legally obtained APK and must not be
uploaded to a public repository or redistributed.
"""
    (root / "INSTALL.txt").write_text(install, encoding="utf-8")

    manifest = {
        "source": args.apk.name,
        "note": (
            "Local test bundle made from a user-owned APK. "
            "Commercial files are not for repository distribution."
        ),
        "apk_sha256": sha256_file(args.apk),
        "eboot_sha256": sha256_file(args.eboot),
        "layout": {
            "PSP/GAME/DEADSPACE/EBOOT.PBP": "PSP executable",
            "data/deadspace/": "complete APK assets/ tree",
            "android_apk/": "complete original APK contents for local debugging",
        },
    }
    (root / "BUNDLE_MANIFEST.json").write_text(
        json.dumps(manifest, indent=2) + "\n",
        encoding="utf-8",
    )

    zip_path = root.parent / f"{root.name}.zip"
    if zip_path.exists():
        zip_path.unlink()
    shutil.make_archive(str(root), "zip", root)

    print(f"Bundle: {root}")
    print(f"ZIP:    {zip_path}")


if __name__ == "__main__":
    main()
