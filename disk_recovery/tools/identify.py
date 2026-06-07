#!/usr/bin/env python3
"""
identify.py — classify every disk image in work/ by its CONTENT, not its name.

File names carry no meaning here: "-final", ".raw", ".dsk" are just labels a
human typed.  What an image *is* is decided by its bytes — the container
signature, its size/geometry, and whether an RT-11 directory actually reads
out of it.  This tool records that verdict to a persistent manifest
(work/corpus/formats.json) so the identification is never lost (e.g. across a
context compaction) and so the rest of the pipeline can be driven by fact.

Per image it reports:
  format        extended-cpc | teledisk | raw | unknown   (by signature/size)
  geometry      ss | ds-twosided | ds-spanning | ld-container | unknown
  read_status   what carries per-sector read-status for this physical disk:
                  st1st2 (Extended-CPC), td0-flags (TeleDisk),
                  dat-rereads (sibling *_crc_error_*.dat), or none (plain raw)
  files         permanent files the directory yields (0 = unreadable here)
  dat_rereads   count of sibling per-sector re-read .dat files

Usage: python identify.py
"""

import subprocess, json, tempfile, shutil, sys, re
from pathlib import Path
from collections import Counter

sys.path.insert(0, str(Path(__file__).resolve().parent))
from read_spanning import read_spanning

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
WORK = ROOT / "disk_recovery" / "work"
OUT = WORK / "corpus"
_tool = ROOT / "package/ms0515-disk.exe"
TOOL = _tool if _tool.exists() else _tool.with_suffix("")
PY = sys.executable
SS, DS = 409600, 819200

DAT_RE = re.compile(r"_crc_error_Head\d+_Track\d+_Sector\d+_", re.I)

def sniff(path):
    """Container format from the leading bytes / size — never the name."""
    with open(path, "rb") as f:
        head = f.read(32)
    if head[:16] == b"EXTENDED CPC DSK":
        return "extended-cpc"
    if head[:2] == b"TD":
        return "teledisk"
    if head[:2] == b"td":
        return "teledisk-lzss"          # compressed variant (decoder unsupported)
    return "raw"

def dir_files(img, side):
    """Permanent-file count from `ms0515-disk dir`, or -1 if no directory."""
    r = subprocess.run([str(TOOL), "dir", str(img), "--side", str(side)],
                       capture_output=True, text=True)
    if "no RT-11 directory" in r.stdout or r.returncode != 0:
        return -1
    m = re.search(r"(\d+) permanent file", r.stdout)
    return int(m.group(1)) if m else -1

def raw_geometry(img, size):
    """Classify a flat image by reading its directory the candidate ways."""
    if size == SS:
        n = dir_files(img, 0)
        return ("ss", n) if n >= 0 else ("unknown", 0)
    if size == DS:
        n0, n1 = dir_files(img, 0), dir_files(img, 1)
        if n0 >= 0 and n1 >= 0:
            return "ds-twosided", n0 + n1
        sp = read_spanning(img.read_bytes())
        if sp:
            return "ds-spanning", len(sp[1])
        if n0 >= 0:
            return "ds-twosided", n0       # one side readable only
        return "unknown", 0
    if size and size % 512 == 0:
        n = dir_files(img, 0)              # logical-disk container (linear)
        return ("ld-container", n) if n >= 0 else ("unknown", 0)
    return "unknown", 0

def classify(path, tmp):
    size = path.stat().st_size
    fmt = sniff(path)
    rel = str(path.relative_to(WORK)).replace("\\", "/")
    entry = {"path": rel, "size": size, "format": fmt}

    # sibling per-sector re-reads (read-status for an otherwise statusless raw)
    dats = [p for p in path.parent.glob(path.stem + "_crc_error_*.dat")]
    entry["dat_rereads"] = len(dats)

    if fmt == "raw":
        geom, files = raw_geometry(path, size)
        entry["geometry"], entry["files"] = geom, files
        entry["read_status"] = "dat-rereads" if dats else "none"
    elif fmt == "extended-cpc":
        work = tmp / path.name; shutil.copy2(path, work)
        subprocess.run([PY, str(HERE/"convert_samdisk.py"), str(work)],
                       capture_output=True)
        sides = sorted(work.parent.glob(work.stem + "_s*.img"))
        files = sum(max(dir_files(s, 0), 0) for s in sides)
        # a single ~1600-block volume split across two 800-block sides reads as
        # spanning, not as two independent sides
        merged = work.with_name(work.stem + "_merged.dsk")
        spanning = 0
        if len(sides) == 2:
            data = sides[0].read_bytes() + sides[1].read_bytes()
            sp = read_spanning(data) if len(data) == DS else None
            spanning = len(sp[1]) if sp else 0
        entry["geometry"] = ("ds-spanning" if spanning > files else
                             "ds-twosided" if len(sides) == 2 else "ss")
        entry["files"] = max(files, spanning)
        entry["read_status"] = "st1st2"
    elif fmt == "teledisk":
        work = tmp / path.name; shutil.copy2(path, work)
        subprocess.run([PY, str(HERE/"convert_teledisk.py"), str(work)],
                       capture_output=True)
        out = work.with_name(work.stem + "_td0.dsk")
        if out.exists():
            geom, files = raw_geometry(out, out.stat().st_size)
        else:
            geom, files = "unknown", 0
        entry["geometry"], entry["files"] = geom, files
        entry["read_status"] = "td0-flags"
    else:
        entry["geometry"], entry["files"], entry["read_status"] = "unknown", 0, "none"
    return entry

def main():
    if not TOOL.exists():
        sys.exit(f"ms0515-disk not built at {TOOL} — build src/ first")
    sources = sorted(p for p in WORK.rglob("*")
                     if p.is_file() and p.suffix.lower() in (".dsk", ".raw", ".td0")
                     and "corpus" not in p.parts)
    OUT.mkdir(exist_ok=True)
    manifest = []
    with tempfile.TemporaryDirectory() as td:
        tmp = Path(td)
        for p in sources:
            manifest.append(classify(p, tmp))
    (OUT / "formats.json").write_text(
        json.dumps({"images": manifest}, indent=1, ensure_ascii=False),
        encoding="utf-8")

    by_fmt = Counter(e["format"] for e in manifest)
    by_geom = Counter(e["geometry"] for e in manifest)
    by_stat = Counter(e["read_status"] for e in manifest)
    print(f"images: {len(manifest)}")
    print("format:    ", dict(by_fmt))
    print("geometry:  ", dict(by_geom))
    print("read_status:", dict(by_stat))
    print(f"\nwith read-status (recoverable error info): "
          f"{sum(1 for e in manifest if e['read_status'] != 'none')}")
    unread = [e for e in manifest if e["files"] <= 0]
    print(f"unreadable here (no directory): {len(unread)}")
    for e in unread:
        print(f"  {e['path']:40s} {e['format']}/{e['geometry']} {e['size']}B")
    print(f"\nwrote {OUT/'formats.json'}")

if __name__ == "__main__":
    main()
