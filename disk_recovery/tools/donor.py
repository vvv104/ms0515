#!/usr/bin/env python3
"""
donor.py — donor recovery for the LOST blocks (consensus tier "corrupt": garbage
on every copy we hold).  These can only come from somewhere ELSE: an orphaned
copy in free space, or the same content living inside another file.

Method — anchor-bracket (METHODOLOGY Step 6), the safe version:

  For each maximal run of bad blocks in a LOST file, the GOOD blocks immediately
  before and after the run are exact 512-byte anchors.  Search the whole corpus
  — every logical file stream in the content store AND every raw image's
  physical blocks (so free / unallocated space is included) — for a place where
  BOTH anchors appear with exactly the gap between them.  The block(s) in
  between are the donor candidate.

Two exact 512-byte anchors bracketing the gap is a strong uniqueness gate (a
single partial-anchor match is what produced past contamination — PITFALLS #2),
and donor blocks must be content-plausible (readable text) and non-trivial.
This is REPORT-ONLY: it proposes candidates with their source for review; it
does not edit any recovered file.

Usage: python donor.py
"""

import json, sys
from pathlib import Path
from collections import defaultdict

sys.path.insert(0, str(Path(__file__).resolve().parent))
from build_corpus import lbn_phys, SS, DS
from read_spanning import read_spanning

ROOT = Path(__file__).resolve().parents[2]
WORK = ROOT / "disk_recovery" / "work"
OUT = WORK / "corpus"
STORE = OUT / "files"
RECOV = OUT / "recovered"
BLOCK = 512

def readable(b):
    return (0x20 <= b <= 0x7E) or b in (9, 10, 13) or (0xC0 <= b <= 0xFF)

def is_garbage(seg):
    return seg and sum(1 for b in seg if not (readable(b) or b == 0)) / len(seg) > 0.25

def trivial(seg):
    """A block that is mostly one byte (spaces, NULs) is a useless anchor — it
    would bracket everything."""
    if not seg:
        return True
    top = max(seg.count(x) for x in set(seg))
    return top / len(seg) > 0.90

def blocks_of(data):
    return [data[i*BLOCK:(i+1)*BLOCK] for i in range(len(data)//BLOCK)]

def runs(bad):
    bad = sorted(bad); out = []
    for b in bad:
        if out and b == out[-1][-1] + 1:
            out[-1].append(b)
        else:
            out.append([b])
    return out

def logical_streams(rel, geom, raw):
    """De-skewed logical block streams (LBN order) for a raw image, so an
    orphaned copy left in free space — contiguous in LBN, scattered physically —
    becomes a contiguous run the anchor-bracket can find.  Maps each LBN to its
    physical sector via the FDC/osa-skew model (the same map build_corpus uses)."""
    pb = blocks_of(raw)
    def deskew(side, nblk, ds):
        out = []
        for L in range(nblk):
            idx = lbn_phys(L, side, ds)
            out.append(pb[idx] if idx < len(pb) else b"\x00"*BLOCK)
        return out
    if geom == "ss":
        return [(f"{rel}", deskew(0, 800, False))]
    if geom == "ds-twosided":
        return [(f"{rel}#s0", deskew(0, 800, True)), (f"{rel}#s1", deskew(1, 800, True))]
    if geom == "ds-spanning":
        res = read_spanning(raw)
        if res:
            fn = res[3]
            return [(f"{rel}#span",
                     [raw[fn(L):fn(L)+BLOCK] if fn(L)+BLOCK <= len(raw) else b"\x00"*BLOCK
                      for L in range(1600)])]
    return []

def build_haystack():
    """seqs[i] = (label, [blocks]); index[block_bytes] = [(i, pos), ...].
    Sources: the reconciled content store (extracted files, incl. cross-name
    copies) + every raw image's DE-SKEWED logical stream (free space included)."""
    seqs, index = [], defaultdict(list)
    def add(label, blks):
        i = len(seqs); seqs.append((label, blks))
        for pos, b in enumerate(blks):
            index[b].append((i, pos))
    for f in sorted(STORE.glob("*.bin")):
        add(f"content:{f.stem[:8]}", blocks_of(f.read_bytes()))
    geoms = {e["path"]: e["geometry"]
             for e in json.load(open(OUT/"formats.json", encoding="utf-8"))["images"]}
    for rel, geom in geoms.items():
        img = WORK / rel
        if not img.exists():
            continue
        raw = img.read_bytes()
        if raw[:16] == b"EXTENDED CPC DSK" or raw[:2] in (b"TD", b"td"):
            continue                           # container, not raw sectors
        for label, blks in logical_streams(rel, geom, raw):
            add(f"free:{label}", blks)
    return seqs, index

def find_donor(seqs, index, anchor_before, anchor_after, gap):
    """Donor block-lists found bracketed by BOTH anchors at the right gap
    (strong: two exact 512-byte matches make a coincidence very unlikely)."""
    out = []
    for i, pos in index.get(anchor_before, ()):
        label, blks = seqs[i]
        ap = pos + 1 + gap
        if ap < len(blks) and blks[ap] == anchor_after:
            cand = blks[pos+1:pos+1+gap]
            if all(not is_garbage(c) for c in cand):
                out.append((label, cand))
    return out

def find_oneside(seqs, index, anchor, gap, before):
    """One-sided search for an edge run (only one good neighbour exists).  Weak:
    a single anchor can coincide, so results are low-confidence — verify by eye."""
    out = []
    for i, pos in index.get(anchor, ()):
        label, blks = seqs[i]
        cand = blks[pos+1:pos+1+gap] if before else blks[pos-gap:pos]
        if len(cand) == gap and all(not is_garbage(c) for c in cand):
            out.append((label, cand))
    return out

def dedup(cands):
    return {bytes(b"".join(c)): lbl for lbl, c in cands}

def main():
    files = json.load(open(OUT / "consensus.json", encoding="utf-8"))["files"]
    lost = [r for r in files if r["tier"] == "corrupt" and "bad_blocks" in r]
    if not lost:
        print("no LOST files"); return
    print("building haystack (content store + raw images)...")
    seqs, index = build_haystack()
    print(f"  {len(seqs)} sequences, {len(index)} distinct blocks\n")

    proposed = OUT / "donor_proposed"; proposed.mkdir(exist_ok=True)
    summary = []
    for r in lost:
        name, blocks = r["name"], r["blocks"]
        data = (RECOV / f"{r['recovered_sha']}.bin").read_bytes()
        blks = list(blocks_of(data))
        bad = set(r["bad_blocks"])
        print(f"== {name} ({blocks} blk, {len(bad)} bad) ==")
        nruns = runs(bad)
        confident = 0                                  # runs resolved by a 2-sided donor
        for run in nruns:
            lo, hi, gap = run[0], run[-1], len(run)
            edge = (lo == 0, hi == len(blks) - 1)
            if not any(edge):                          # two good neighbours -> strong
                ab, aa = blks[lo-1], blks[hi+1]
                if trivial(ab) or trivial(aa):
                    print(f"   blk {lo}-{hi}: anchors trivial (blank/padding) — skip")
                    continue
                uniq = dedup(find_donor(seqs, index, ab, aa, gap))
                if uniq:
                    confident += 1
                    payload, lbl = next(iter(uniq.items()))
                    for i, b in enumerate(payload[k*BLOCK:(k+1)*BLOCK] for k in range(gap)):
                        blks[lo+i] = b                 # splice the donor in
                    print(f"   blk {lo}-{hi}: DONOR ({len(uniq)} src) from {lbl}: "
                          f"{payload[:48].decode('koi8-r','replace')}")
                else:
                    print(f"   blk {lo}-{hi}: no donor (2-sided)")
            else:                                      # file edge -> one-sided, weak
                if all(edge):
                    print(f"   blk {lo}-{hi}: whole file lost, no anchor"); continue
                if edge[0]:
                    uniq = dedup(find_oneside(seqs, index, blks[hi+1], gap, before=False))
                else:
                    uniq = dedup(find_oneside(seqs, index, blks[lo-1], gap, before=True))
                if uniq:
                    payload, lbl = next(iter(uniq.items()))
                    print(f"   blk {lo}-{hi}: one-sided lead ({len(uniq)} src, LOW confidence) "
                          f"from {lbl}: {payload[:40].decode('koi8-r','replace')}")
                else:
                    print(f"   blk {lo}-{hi}: no donor (one-sided)")
        if confident == len(nruns):                    # every run resolved -> write proposal
            (proposed / name).write_bytes(b"".join(blks))
            print(f"   -> FULLY RECOVERED, wrote {proposed/name}")
        summary.append((name, len(nruns), confident))
        print()

    print("=== summary ===")
    for name, nr, conf in summary:
        tag = "RECOVERED" if conf == nr else f"{conf}/{nr} runs"
        print(f"  {name:14s} {tag}")
    rec = sum(1 for _, nr, c in summary if c == nr)
    print(f"\n{rec}/{len(lost)} LOST files fully recovered from in-corpus donors "
          f"(proposals in {proposed}); the rest need an EXTERNAL disk.")

if __name__ == "__main__":
    main()
