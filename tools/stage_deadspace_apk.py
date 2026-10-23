#!/usr/bin/env python3
"""Stage Dead Space Mobile data from a user-owned APK for the PSP port.

This tool intentionally does NOT download or redistribute the commercial game.
Run it against your own APK. It copies game data into a PSP-friendly tree and
writes a manifest; Android framework files are excluded.
"""
from __future__ import annotations
import argparse, hashlib, json, os, zipfile
from pathlib import Path

KEEP_PREFIXES = ("assets/published/",)
KEEP_EXACT = ("assets/EAMCore.ini", "assets/EAMToolManager.ini",
              "assets/guest_libs/libDeadSpace.so")

def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("apk", type=Path)
    ap.add_argument("-o","--out", type=Path, default=Path("psp_data"))
    args=ap.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    manifest={"source":"user-supplied APK","files":[]}
    with zipfile.ZipFile(args.apk) as z:
        for info in z.infolist():
            name=info.filename
            if name.endswith("/") or not (name.startswith(KEEP_PREFIXES) or name in KEEP_EXACT):
                continue
            rel=name.removeprefix("assets/")
            dst=args.out/rel
            dst.parent.mkdir(parents=True, exist_ok=True)
            data=z.read(info)
            dst.write_bytes(data)
            manifest["files"].append({
                "path":str(Path("assets")/rel),
                "size":len(data),
                "sha256":sha256(data)
            })
    manifest["count"]=len(manifest["files"])
    (args.out/"MANIFEST.json").write_text(json.dumps(manifest,indent=2)+"\n",encoding="utf-8")
    print(f"Staged {len(manifest['files'])} game-data files in {args.out}")

if __name__=="__main__":
    main()
