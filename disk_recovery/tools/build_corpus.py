#!/usr/bin/env python3
"""
build_corpus.py — unique-file corpus over the disk images in
disk_recovery/work/, using the verified ms0515-disk tool for extraction.

For every standard diskette image (409600 SS / 819200 DS) it extracts each
side's files via `ms0515-disk get`, hashes them (sha-256), and consolidates:
one record per unique content, with provenance (which image+side it came
from), a type-based category, and flags for images we cannot read yet
(DS-spanning, odd sizes — convert them first with the convert tools).

Output: work/corpus/corpus.json + a printed summary.  The category is a
type-based hint only; the real system/generation grouping is derived later
from co-occurrence (provenance), since even standard .SAV utilities are
version-bound to a monitor generation (see disk_recovery/METHODOLOGY.md).

Usage: python build_corpus.py
"""

import subprocess, hashlib, json, tempfile, sys
from pathlib import Path
from collections import defaultdict

ROOT = Path(__file__).resolve().parents[2]
WORK = ROOT / "disk_recovery" / "work"
_tool = ROOT / "src/build/Release/tools/disk/ms0515-disk.exe"
TOOL = _tool if _tool.exists() else _tool.with_suffix("")
OUT  = WORK / "corpus"

SS, DS = 409600, 819200

CATEGORY = {}
for e in ["SYS"]:                                            CATEGORY[e] = "system"
for e in ["SAV", "EXE", "OBJ"]:                              CATEGORY[e] = "exec"
for e in ["DAT", "HLP", "BAK", "STB", "MLB", "SML"]:         CATEGORY[e] = "aux"
for e in ["TXT","MAC","FOR","BAS","PAS","C","COM","DOC",
          "LST","MAP","DIR","DIF","SLP","TFP"]:              CATEGORY[e] = "text"

def categorize(name):
    ext = name.rsplit(".", 1)[1].upper() if "." in name else ""
    return CATEGORY.get(ext, "other")

def get_side(img, side):
    """Extract one side's files; return {name: bytes} or None if unreadable."""
    with tempfile.TemporaryDirectory() as td:
        r = subprocess.run([str(TOOL), "get", str(img), "--side", str(side),
                            "--out", td], capture_output=True, text=True)
        files = {p.name: p.read_bytes() for p in Path(td).iterdir() if p.is_file()}
        return files if (files or r.returncode == 0) else None

def main():
    if not TOOL.exists():
        sys.exit(f"ms0515-disk not built at {TOOL} — build src/ first")
    images = sorted(p for p in WORK.rglob("*")
                    if p.is_file() and p.suffix.lower() in (".dsk", ".raw")
                    and p.stat().st_size in (SS, DS) and "corpus" not in p.parts)

    corpus, flagged = {}, []
    for img in images:
        rel = str(img.relative_to(WORK)).replace("\\", "/")
        sides = [0] if img.stat().st_size == SS else [0, 1]
        any_ok = False
        for s in sides:
            files = get_side(img, s)
            if not files:
                continue
            any_ok = True
            for name, data in files.items():
                h = hashlib.sha256(data).hexdigest()
                rec = corpus.setdefault(h, {
                    "sha": h, "names": [], "size": len(data),
                    "blocks": len(data) // 512, "category": categorize(name),
                    "provenance": []})
                if name not in rec["names"]:
                    rec["names"].append(name)
                rec["provenance"].append({"image": rel, "side": s, "name": name})
        if not any_ok:
            flagged.append({"image": rel,
                            "reason": "no readable directory — DS-spanning or "
                                      "non-emulator format; convert first"})

    OUT.mkdir(exist_ok=True)
    records = sorted(corpus.values(), key=lambda r: (r["category"], r["names"][0]))
    (OUT / "corpus.json").write_text(
        json.dumps({"records": records, "flagged": flagged}, indent=1,
                   ensure_ascii=False), encoding="utf-8")

    by_cat = defaultdict(int)
    for r in records:
        by_cat[r["category"]] += 1
    shared = sum(1 for r in records if len({p["image"] for p in r["provenance"]}) > 1)
    print(f"images scanned: {len(images)}   unique files: {len(records)}")
    print("by category:", dict(sorted(by_cat.items())))
    print(f"shared across >1 image: {shared}")
    if flagged:
        print("flagged (need conversion):")
        for f in flagged:
            print(f"  {f['image']}")
    print(f"wrote {OUT/'corpus.json'}")

if __name__ == "__main__":
    main()
