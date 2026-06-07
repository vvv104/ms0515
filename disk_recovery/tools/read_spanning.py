#!/usr/bin/env python3
"""
read_spanning.py — read a DS-spanning RT-11 volume (one ~1600-block filesystem
across BOTH sides of a double-sided disk), which ms0515-disk does NOT handle
(it reads single-sided / two-independent-side double-sided only).

The 819200-byte image is physically track-interleaved
(byte = (cyl*2 + head)*5120 + sector*512).  The spanning filesystem maps
LBN 0..1599 across the 80 cylinders * 2 heads; the exact ordering is not
emulator-grounded, so we try candidate mappings and pick the one whose files
match the corpus (independent raw sources) — i.e. the mapping is validated by
content, not assumed.

Usage:
    python read_spanning.py <image.dsk>            # try mappings, list files
    python read_spanning.py <image.dsk> --out DIR  # extract with the best mapping
"""

import sys, json, hashlib, argparse
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CORPUS = ROOT / "disk_recovery/work/corpus/corpus.json"
BLOCK = 512
IL = [0, 2, 4, 6, 8, 1, 3, 5, 7, 9]
RAD = " ABCDEFGHIJKLMNOPQRSTUVWXYZ$.?0123456789"

def rad(x):
    return RAD[x // 1600] + RAD[(x // 40) % 40] + RAD[x % 40] if x < 64000 else "???"

def name_of(n1, n2, ext):
    nm = (rad(n1) + rad(n2)).rstrip()
    e = rad(ext).rstrip()
    return f"{nm}.{e}" if e else nm

# Candidate LBN -> byte mappings for a 1600-block spanning volume on a
# track-interleaved 819200 image.  cyl in 0..79, head in 0..1, 10 sec/track.
def make_mappings():
    def phys(cyl, head, sec):
        return (cyl * 2 + head) * 5120 + sec * BLOCK
    M = {}
    M["cyl0last-noil"]  = lambda N: phys((N // 20 + 1) % 80, (N // 10) % 2, N % 10)
    M["cyl0first-noil"] = lambda N: phys(N // 20,            (N // 10) % 2, N % 10)
    # head alternates per *block-pair*? other orderings:
    M["cyl0last-headlo"]= lambda N: phys((N // 20 + 1) % 80, (N // 10) % 2,
                                         IL[N % 10])  # +2:1 interleave
    return M

def w(b, o):
    return b[o] | (b[o + 1] << 8)

def read_block(img, to_byte, lbn):
    off = to_byte(lbn)
    return img[off:off + BLOCK] if off + BLOCK <= len(img) else b""

def parse_dir(img, to_byte):
    """Return list of (name, start_block, length) permanent files, or None."""
    home = read_block(img, to_byte, 1)
    if len(home) < BLOCK:
        return None
    dir_start = w(home, 0x1D4)
    if not (1 <= dir_start <= 1600):
        return None
    files, seg_lbn, guard = [], dir_start, 0
    cur = None
    while seg_lbn and guard < 32:
        guard += 1
        seg = read_block(img, to_byte, seg_lbn) + read_block(img, to_byte, seg_lbn + 1)
        if len(seg) < 1024:
            return None
        seg_total = w(seg, 0)
        nxt = w(seg, 2)
        extra = w(seg, 6)
        data_blk = w(seg, 8)
        if not (1 <= seg_total <= 31) or (extra & 1) or not (1 <= data_blk <= 1600):
            return None
        if cur is None:
            cur = data_blk
        p, esz, saw_eos = 10, 14 + extra, False
        while p + esz <= 1024:
            st = w(seg, p)
            if st == 0:
                return None
            length = w(seg, p + 8)
            if st & 0o2000:                       # permanent
                date_w = w(seg, p + 12)
                date = None
                if date_w:
                    age = (date_w >> 14) & 0x3
                    month = (date_w >> 10) & 0xF
                    day = (date_w >> 5) & 0x1F
                    yr = date_w & 0x1F
                    date = f"{1972 + (age << 5) + yr:04d}-{month:02d}-{day:02d}"
                protected_ = bool(st & 0o100000)
                files.append((name_of(w(seg, p+2), w(seg, p+4), w(seg, p+6)),
                              cur, length, date, protected_))
            cur += length
            p += esz
            if st & 0o4000:
                saw_eos = True
                break
        if not saw_eos:
            return None
        seg_lbn = dir_start + (nxt - 1) * 2 if nxt else 0
    return files

def extract(img, to_byte, start, length):
    return b"".join(read_block(img, to_byte, start + i) for i in range(length))

def read_spanning(img):
    """Read a spanning volume with the best structurally-valid mapping.
    Returns (mapping_tag, {name: bytes},
             [(name, start, length, date_or_None, protected_bool)], to_byte)
    or None.  The entries carry the RT-11 directory metadata for each
    permanent file, and the to_byte mapping lets the caller link a physical
    bad-map to each file's blocks (phys_block = to_byte(lbn)//512).  Used by
    build_corpus as a fallback for 819200 images that ms0515-disk cannot read."""
    if len(img) != 819200:
        return None
    best = None
    for tag, fn in make_mappings().items():
        files = parse_dir(img, fn)
        if files and (best is None or len(files) > len(best[2])):
            best = (tag, fn, files)
    if not best:
        return None
    tag, fn, files = best
    return (tag, {f[0]: extract(img, fn, f[1], f[2]) for f in files},
            files, fn)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    ap.add_argument("--out")
    args = ap.parse_args()
    img = Path(args.image).read_bytes()
    if len(img) != 819200:
        sys.exit(f"{args.image}: not an 819200-byte image ({len(img)} B)")

    corpus = set()
    if CORPUS.exists():
        corpus = {r["sha"] for r in json.load(open(CORPUS, encoding="utf-8"))["records"]}

    best = None
    for tag, fn in make_mappings().items():
        files = parse_dir(img, fn)
        if not files:
            print(f"  {tag:18s}: no valid directory")
            continue
        matched = 0
        for nm, st, ln in files:
            h = hashlib.sha256(extract(img, fn, st, ln)).hexdigest()
            if h in corpus:
                matched += 1
        print(f"  {tag:18s}: {len(files)} files, {matched} match corpus")
        if best is None or matched > best[2]:
            best = (tag, fn, matched, files)

    if best and best[2] > 0:
        tag, fn, matched, files = best
        print(f"\nbest mapping: {tag} ({matched}/{len(files)} match corpus)")
        if args.out:
            outd = Path(args.out); outd.mkdir(parents=True, exist_ok=True)
            for nm, st, ln in files:
                (outd / nm).write_bytes(extract(img, fn, st, ln))
            print(f"extracted {len(files)} files -> {outd}")
    else:
        print("\nno mapping produced corpus-matching files — unknown spanning layout")

if __name__ == "__main__":
    main()
