#!/usr/bin/env python3
"""
import_images.py — pull disk images from an external source into
disk_recovery/work/data/, deduplicated by content.

Recursively extracts archives (.rar/.7z/.zip, including nested ones), then
copies every file whose content (md5) is not already present anywhere under
work/ — so re-runs add only genuinely new material and internal/already-seen
copies are skipped.  The source tree is read-only; provenance is preserved by
mirroring the relative path under work/data/ (src/ for loose files, arc/ for
archive contents).

Usage:  python import_images.py <source-dir> [more-source-dirs...]
        python import_images.py --7z "C:/Program Files/7-Zip/7z.exe" <src>
"""

import sys, shutil, hashlib, subprocess, tempfile, argparse
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
WORK = ROOT / "disk_recovery" / "work"
DST  = WORK / "data"
ARCHIVE_EXT = {".rar", ".7z", ".zip"}

def find_7z(override):
    if override:
        return override
    for c in ("7z", "7za", "7zr"):
        p = shutil.which(c)
        if p:
            return p
    for p in (r"C:/Program Files/7-Zip/7z.exe", r"C:/Program Files (x86)/7-Zip/7z.exe"):
        if Path(p).exists():
            return p
    return None

def md5(path):
    h = hashlib.md5()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()

def extract_recursive(stage, sz):
    """Extract every archive under `stage` in place, repeatedly, deleting the
    archive after extraction, until no archives remain."""
    if not sz:
        return
    while True:
        arcs = [p for p in stage.rglob("*") if p.suffix.lower() in ARCHIVE_EXT]
        if not arcs:
            break
        for a in arcs:
            subprocess.run([sz, "x", "-y", "-bso0", "-bsp0",
                            f"-o{a}__x", str(a)], capture_output=True)
            a.unlink(missing_ok=True)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("sources", nargs="+")
    ap.add_argument("--7z", dest="sz")
    args = ap.parse_args()
    sz = find_7z(args.sz)
    if not sz:
        print("warning: no 7-Zip found — archives will be skipped", file=sys.stderr)

    DST.mkdir(parents=True, exist_ok=True)
    seen = {}
    for p in WORK.rglob("*"):
        if p.is_file():
            seen[md5(p)] = 1

    copied = skipped = 0
    def take(src, destbase, rel):
        nonlocal copied, skipped
        h = md5(src)
        if h in seen:
            skipped += 1
            return
        seen[h] = 1
        d = destbase / rel
        d.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, d)
        copied += 1

    with tempfile.TemporaryDirectory() as tmp:
        stage = Path(tmp)
        for srcdir in args.sources:
            srcdir = Path(srcdir)
            if not srcdir.is_dir():
                print(f"skip (not a dir): {srcdir}", file=sys.stderr)
                continue
            # stage archives for recursive extraction
            arcdir = stage / "arc"
            arcdir.mkdir(exist_ok=True)
            for a in srcdir.rglob("*"):
                if a.is_file() and a.suffix.lower() in ARCHIVE_EXT:
                    shutil.copy2(a, arcdir / a.name)
            # loose (non-archive) files
            for f in srcdir.rglob("*"):
                if f.is_file() and f.suffix.lower() not in ARCHIVE_EXT:
                    take(f, DST / "src", f.relative_to(srcdir))
        extract_recursive(stage, sz)
        for f in stage.rglob("*"):
            if f.is_file() and f.suffix.lower() not in ARCHIVE_EXT:
                rel = Path(str(f.relative_to(stage / "arc")).replace("__x", ""))
                take(f, DST / "arc", rel)

    total = sum(1 for p in DST.rglob("*") if p.is_file())
    print(f"copied (new unique): {copied}   skipped (duplicates): {skipped}")
    print(f"work/data now holds {total} files")

if __name__ == "__main__":
    main()
