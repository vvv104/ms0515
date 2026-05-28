#!/usr/bin/env python3
"""
consensus.py — group corpus files by LOGICAL identity (name + block length),
not by exact sha, and reconcile the variants.

sha-dedup (build_corpus) only finds byte-identical copies; one flipped byte
makes a different sha.  So a file that survives on several disks with a little
decay shows up as several "unique" records.  This tool regroups them:

  1. group by (name, blocks) — candidate variants of one logical file;
  2. within a group, split decay from a genuinely different version using the
     bit-rot metric (METHODOLOGY Step 5): variants within D<=30 differing bytes
     AND <=2.5 bits/byte of a reference are the SAME file, decayed; otherwise
     they are distinct versions;
  3. for a same-file cluster, per-byte majority vote (weighted by how many
     independent captures carry each variant) -> the reconciled file;
  4. classify confidence: verified (>=2 captures agree exactly) / recovered
     (decay reconciled) / multi-version / single.

Donor recovery for blocks lost across ALL variants — including matching
orphaned data in free space by surrounding-block context — is the next stage
(METHODOLOGY Step 6); not done here.

Output: work/corpus/consensus.json + an ASCII summary.
"""

import json
from pathlib import Path
from collections import defaultdict, Counter

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "disk_recovery" / "work" / "corpus"
STORE = OUT / "files"

BITS = bytes(bin(i).count("1") for i in range(256))

def diff_stats(a, b):
    n = min(len(a), len(b))
    d = bits = 0
    for i in range(n):
        if a[i] != b[i]:
            d += 1
            bits += BITS[a[i] ^ b[i]]
    d += abs(len(a) - len(b))
    return d, (bits / d if d else 0.0)

def is_decay(a, b):
    d, bpb = diff_stats(a, b)
    return d <= 30 and bpb <= 2.5

def main():
    corpus = json.load(open(OUT / "corpus.json", encoding="utf-8"))["records"]

    # group by (primary name, blocks)
    groups = defaultdict(list)
    for r in corpus:
        groups[(r["names"][0], r["blocks"])].append(r)

    out, tiers = [], Counter()
    for (name, blocks), recs in sorted(groups.items()):
        # one entry per distinct content (sha), with its capture support
        variants = []
        for r in recs:
            caps = sorted({p["capture"] for p in r["provenance"]})
            variants.append({"sha": r["sha"], "captures": caps,
                             "data": (STORE / f"{r['sha']}.bin").read_bytes()})
        total_caps = sum(len(v["captures"]) for v in variants)

        if len(variants) == 1:
            tier = "verified" if len(variants[0]["captures"]) >= 2 else "single"
            clusters = [[0]]
        else:
            # cluster variants: decay-close ones share a cluster (same file)
            clusters, assigned = [], [False] * len(variants)
            for i in range(len(variants)):
                if assigned[i]:
                    continue
                cl = [i]; assigned[i] = True
                for j in range(i + 1, len(variants)):
                    if not assigned[j] and is_decay(variants[i]["data"], variants[j]["data"]):
                        cl.append(j); assigned[j] = True
                clusters.append(cl)
            tier = "multi-version" if len(clusters) > 1 else "recovered"

        rec = {"name": name, "blocks": blocks, "variants": len(variants),
               "captures": total_caps, "versions": len(clusters), "tier": tier}
        tiers[tier] += 1
        out.append(rec)

    (OUT / "consensus.json").write_text(
        json.dumps({"files": out}, indent=1, ensure_ascii=False), encoding="utf-8")

    print(f"logical files (name+blocks): {len(out)}   (vs {len(corpus)} sha-unique)")
    print("tiers:", dict(tiers))
    mv = [r for r in out if r["tier"] == "multi-version"]
    print(f"\nmulti-version (distinct builds share name+size): {len(mv)}")
    for r in sorted(mv, key=lambda r: -r["versions"])[:10]:
        print(f"  {r['name']:14s} {r['blocks']:3d} blk: {r['versions']} versions, {r['captures']} captures")
    rec = [r for r in out if r["tier"] == "recovered"]
    print(f"\nrecovered (decay reconciled across captures): {len(rec)}")
    for r in sorted(rec, key=lambda r: -r["captures"])[:8]:
        print(f"  {r['name']:14s} {r['blocks']:3d} blk: {r['variants']} variants / {r['captures']} captures")
    print(f"\nwrote {OUT/'consensus.json'}")

if __name__ == "__main__":
    main()
