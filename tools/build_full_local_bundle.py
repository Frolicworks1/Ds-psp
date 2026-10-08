#!/usr/bin/env python3
"""Build a complete local PSP test bundle from a user-owned Dead Space APK.

The bundle preserves every APK entry under android_apk/ and also creates
psp_data/ from the game assets used by the PSP runtime. Nothing is downloaded.
Do not commit the resulting commercial game data or ZIP to source control.
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
    ap.add_argument("-o", "--output", type=Path, default=Path("DeadSpace-PSP-Bundle"))
    args = ap.parse_args()

    if not args.apk.is_file():
        raise SystemExit(f"APK not found: {args.apk}")
    if not args.eboot.is_file():
        raise SystemExit(f"EBOOT.PBP not found: {args.eboot}")

    root = args.output.resolve()
    if root.exists():
        shutil.rmtree(root)
    (root / "android_apk").mkdir(parents=True)
    (root / "game").mkdir(parents=True)

    # Preserve the complete APK contents for local debugging/reference.
    with zipfile.ZipFile(args.apk) as z:
        for info in z.infolist():
            if info.is_dir():
                (root / "android_apk" / info.filename).mkdir(parents=True, exist_ok=True)
                continue
            dst = root / "android_apk" / info.filename
            dst.parent.mkdir(parents=True, exist_ok=True)
            with z.open(info) as src, dst.open("wb") as out:
                shutil.copyfileobj(src, out)

        # PSP runtime data: keep all original game assets, not a hand-picked subset.
        for info in z.infolist():
            if info.is_dir() or not info.filename.startswith("assets/"):
                continue
            rel = info.filename[len("assets/"):]
            dst = root / "game" / rel
            dst.parent.mkdir(parents=True, exist_ok=True)
            with z.open(info) as src, dst.open("wb") as out:
                shutil.copyfileobj(src, out)

    shutil.copy2(args.eboot, root / "EBOOT.PBP")

    manifest = {
        "source": args.apk.name,
        "note": "Local test bundle made from a user-owned APK. Commercial files are not for repository distribution.",
        "apk_sha256": sha256_file(args.apk),
        "eboot_sha256": sha256_file(args.eboot),
        "layout": {
            "EBOOT.PBP": "PSP executable",
            "game/": "complete APK assets/ tree",
            "android_apk/": "complete original APK contents for reference/debugging",
        },
    }
    (root / "BUNDLE_MANIFEST.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8"
    )

    zip_path = root.parent / f"{root.name}.zip"
    shutil.make_archive(str(root), "zip", root)
    print(f"Bundle: {root}")
    print(f"ZIP:    {zip_path}")

if __name__ == "__main__":
    main()
