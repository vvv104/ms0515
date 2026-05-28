#!/usr/bin/env python3
"""
build_corpus.py — unique-file corpus over every readable disk in
disk_recovery/work/, using the verified ms0515-disk tool for extraction.

Ingests all three capture kinds: raw images directly, and TeleDisk / SAMdisk
Extended-CPC captures by converting them to raw first (via convert_teledisk /
convert_samdisk, in a temp dir).  Each side's files are extracted via
`ms0515-disk get`, hashed (sha-256), and consolidated: one record per unique
content, with provenance (which capture + side) and a type-based category.
DS-spanning and other non-readable images are flagged.

Output: work/corpus/corpus.json + a printed summary.  The category is a type
hint only; the real system/generation grouping is derived later from
co-occurrence (provenance), since even standard .SAV utilities are version-
bound to a monitor generation (see ../METHODOLOGY.md).

Usage: python build_corpus.py
"""

import subprocess, hashlib, json, tempfile, shutil, sys, re
from pathlib import Path
from collections import defaultdict

sys.path.insert(0, str(Path(__file__).resolve().parent))
from read_spanning import read_spanning

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
WORK = ROOT / "disk_recovery" / "work"
_tool = ROOT / "src/build/Release/tools/disk/ms0515-disk.exe"
TOOL = _tool if _tool.exists() else _tool.with_suffix("")
OUT  = WORK / "corpus"
PY   = sys.executable

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

IL = [0, 2, 4, 6, 8, 1, 3, 5, 7, 9]

def lbn_phys(lbn, side, ds):
    """Physical block index of an LBN under the FDC + osa-skew driver.

    Equals lbnToByte(lbn, side, ds) // 512 — which is exactly how both
    converters index their bad-maps (one byte per physical sector: TeleDisk
    track_index*10+sec, Extended-CPC per-side track*10+sec).  So this is the
    bridge from a file's directory LBN to the bad-map slot for that block."""
    n = lbn % 800
    track = (n // 10 + 1) % 80
    sec = (IL[n % 10] + 2 * track - 2) % 10
    return track * 20 + side * 10 + sec if ds else track * 10 + sec

DIR_RE = re.compile(r"^\s+(\S+)\s+blk=\s*(\d+)\s+len=\s*(\d+)")

def dir_entries(img, side):
    """name -> (start_block, length) parsed from `ms0515-disk dir`."""
    r = subprocess.run([str(TOOL), "dir", str(img), "--side", str(side)],
                       capture_output=True, text=True)
    ents = {}
    for line in r.stdout.splitlines():
        m = DIR_RE.match(line)
        if m:
            ents[m.group(1)] = (int(m.group(2)), int(m.group(3)))
    return ents

def flagged_blocks(start, length, side, ds, badmap):
    """Indices (0-based, within the file) of blocks on a flagged sector."""
    out = []
    for i in range(length):
        idx = lbn_phys(start + i, side, ds)
        if idx < len(badmap) and badmap[idx]:
            out.append(i)
    return out

def is_extended_cpc(p):
    try:
        with open(p, "rb") as f:
            return f.read(21).startswith(b"EXTENDED CPC DSK")
    except OSError:
        return False

def raw_images_for(src, tmp):
    """Return [(raw_image_path, [sides], badmap_path_or_None)] for a capture,
    converting TeleDisk / Extended-CPC to raw in `tmp` first.  Raw dumps carry
    no read-status (badmap=None); the converters emit a per-physical-sector
    .badmap alongside their output.  Empty list if not convertible."""
    ext = src.suffix.lower()
    if ext in (".dsk", ".raw") and src.stat().st_size in (SS, DS) and not is_extended_cpc(src):
        return [(src, [0] if src.stat().st_size == SS else [0, 1], None)]
    if ext == ".td0":
        work = tmp / src.name; shutil.copy2(src, work)
        subprocess.run([PY, str(HERE/"convert_teledisk.py"), str(work)], capture_output=True)
        out = work.with_name(work.stem + "_td0.dsk")
        bm = work.with_name(work.stem + "_td0.badmap")
        return [(out, [0, 1] if out.stat().st_size == DS else [0],
                 bm if bm.exists() else None)] if out.exists() else []
    if ext == ".dsk" and is_extended_cpc(src):
        work = tmp / src.name; shutil.copy2(src, work)
        subprocess.run([PY, str(HERE/"convert_samdisk.py"), str(work)], capture_output=True)
        out = []
        for p in sorted(work.parent.glob(work.stem + "_s*.img")):
            bm = p.with_suffix(".badmap")
            out.append((p, [0], bm if bm.exists() else None))
        return out
    return []

def get_side(img, side):
    with tempfile.TemporaryDirectory() as td:
        r = subprocess.run([str(TOOL), "get", str(img), "--side", str(side),
                            "--out", td], capture_output=True, text=True)
        files = {p.name: p.read_bytes() for p in Path(td).iterdir() if p.is_file()}
        return files if (files or r.returncode == 0) else None

def main():
    if not TOOL.exists():
        sys.exit(f"ms0515-disk not built at {TOOL} — build src/ first")
    sources = sorted(p for p in WORK.rglob("*")
                     if p.is_file() and p.suffix.lower() in (".dsk", ".raw", ".td0")
                     and "corpus" not in p.parts)

    OUT.mkdir(exist_ok=True)
    store = OUT / "files"; store.mkdir(exist_ok=True)   # content store: sha -> bytes
    corpus, flagged = {}, []

    def ingest(name, data, rel, side, bad=None):
        h = hashlib.sha256(data).hexdigest()
        if h not in corpus:
            corpus[h] = {"sha": h, "names": [], "size": len(data),
                         "blocks": len(data)//512, "category": categorize(name),
                         "provenance": []}
            (store / f"{h}.bin").write_bytes(data)
        rec = corpus[h]
        if name not in rec["names"]:
            rec["names"].append(name)
        prov = {"capture": rel, "side": side, "name": name}
        if bad:                       # block indices on a flagged sector (decay)
            prov["bad"] = bad
        rec["provenance"].append(prov)
    with tempfile.TemporaryDirectory() as tmpd:
        tmp = Path(tmpd)
        for src in sources:
            rel = str(src.relative_to(WORK)).replace("\\", "/")
            imgs = raw_images_for(src, tmp)
            if not imgs:
                continue
            any_ok = False
            for img, sides, bmpath in imgs:
                badmap = bmpath.read_bytes() if bmpath else None
                ds_img = img.stat().st_size == DS
                for s in sides:
                    files = get_side(img, s)
                    if not files:
                        continue
                    any_ok = True
                    dents = dir_entries(img, s) if badmap else {}
                    for name, data in files.items():
                        bad = None
                        if badmap and name in dents:
                            start, length = dents[name]
                            bad = flagged_blocks(start, length, s, ds_img, badmap)
                        ingest(name, data, rel, s, bad)
            if not any_ok:
                # Fall back to the DS-spanning reader (one ~1600-block volume
                # across both sides) for 819200 images ms0515-disk can't read.
                for img, _, _ in imgs:
                    if img.stat().st_size != DS:
                        continue
                    res = read_spanning(img.read_bytes())
                    if res:
                        any_ok = True
                        for name, data in res[1].items():
                            ingest(name, data, rel, "span")
                        break
            if not any_ok:
                flagged.append({"capture": rel,
                                "reason": "no readable directory — unknown layout"})

    OUT.mkdir(exist_ok=True)
    records = sorted(corpus.values(), key=lambda r: (r["category"], r["names"][0]))
    (OUT / "corpus.json").write_text(
        json.dumps({"records": records, "flagged": flagged}, indent=1,
                   ensure_ascii=False), encoding="utf-8")

    by_cat = defaultdict(int)
    for r in records:
        by_cat[r["category"]] += 1
    shared = sum(1 for r in records if len({p["capture"] for p in r["provenance"]}) > 1)
    print(f"captures: {len(sources)}   unique files: {len(records)}")
    print("by category:", dict(sorted(by_cat.items())))
    print(f"shared across >1 capture: {shared}")
    if flagged:
        print("flagged (need a spanning reader):")
        for f in flagged:
            print(f"  {f['capture']}")
    print(f"wrote {OUT/'corpus.json'}")

if __name__ == "__main__":
    main()
