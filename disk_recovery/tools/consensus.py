#!/usr/bin/env python3
"""
consensus.py — group corpus files by LOGICAL identity (name + block length),
not by exact sha, and reconcile the variants using the bad-maps.

sha-dedup (build_corpus) only finds byte-identical copies; one flipped byte
makes a different sha.  So a file that survives on several disks with a little
decay shows up as several "unique" records.  This tool regroups them and
folds in the per-block read-status that build_corpus linked from the capture
bad-maps (provenance "bad" = file-block indices on a flagged sector):

  1. group by (name, blocks) — candidate variants of one logical file;
  2. per variant, the SUSPECT blocks = blocks flagged on EVERY capture that
     produced that exact content (a block read clean even once is trusted);
  3. two variants are the SAME file (decayed) when every block they DIFFER on
     is suspect on at least one of them — i.e. the disagreement sits in
     flagged sectors, not clean data.  (A small clean-block difference still
     counts as decay via the bit-rot metric, METHODOLOGY Step 5; a large
     difference in CLEAN blocks is a genuinely different version.)  This is
     what rescues a heavily-damaged copy from the "multi-version" bucket;
  4. reconcile a same-file cluster block-by-block: for each block take the
     capture-weighted majority among the variants whose copy of that block is
     NOT flagged; a block flagged on ALL variants is LOST (no clean copy) and
     becomes the donor worklist (METHODOLOGY Step 6);
  5. classify: verified / recovered / partial (lost blocks remain) /
     multi-version / single.

Donor recovery for the lost blocks — matching orphaned data in free space by
surrounding-block context — is the next stage; this tool emits the worklist.

Output: work/corpus/consensus.json + an ASCII summary.
"""

import json, hashlib
from pathlib import Path
from collections import defaultdict, Counter

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "disk_recovery" / "work" / "corpus"
STORE = OUT / "files"
RECOV = OUT / "recovered"
BLOCK = 512

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

def variant_flagged(rec):
    """Suspect blocks of one content: flagged on EVERY occurrence that produced
    it.  A provenance entry without "bad" (raw dump, or a converted capture that
    flagged none of this file's blocks) is a clean read and clears all flags."""
    fl = None
    for p in rec["provenance"]:
        b = set(p.get("bad", []))
        fl = b if fl is None else (fl & b)
        if not fl:
            break
    return fl or set()

def differing_blocks(a, b, blocks):
    return {i for i in range(blocks)
            if a[i*BLOCK:(i+1)*BLOCK] != b[i*BLOCK:(i+1)*BLOCK]}

def same_file(va, vb, blocks):
    diff = differing_blocks(va["data"], vb["data"], blocks)
    if not diff:
        return True
    if diff <= (va["flagged"] | vb["flagged"]):   # all disagreement in flagged sectors
        return True
    return is_decay(va["data"], vb["data"])        # small clean-block decay

def reconcile(variants, blocks):
    """Block-by-block capture-weighted majority among variants whose copy of the
    block is not flagged.  Returns (bytes, [lost block indices])."""
    out, lost = bytearray(), []
    for b in range(blocks):
        clean = [v for v in variants if b not in v["flagged"]]
        pool = clean or variants
        cnt = Counter()
        for v in pool:
            cnt[v["data"][b*BLOCK:(b+1)*BLOCK]] += v["weight"]
        out += cnt.most_common(1)[0][0]
        if not clean:
            lost.append(b)
    return bytes(out), lost

def cluster(variants, blocks):
    """Greedy same-file clustering; returns list of clusters (index lists)."""
    clusters, assigned = [], [False] * len(variants)
    for i in range(len(variants)):
        if assigned[i]:
            continue
        cl = [i]; assigned[i] = True
        for j in range(i + 1, len(variants)):
            if not assigned[j] and any(same_file(variants[k], variants[j], blocks)
                                       for k in cl):
                cl.append(j); assigned[j] = True
        clusters.append(cl)
    return clusters

def main():
    corpus = json.load(open(OUT / "corpus.json", encoding="utf-8"))["records"]
    RECOV.mkdir(exist_ok=True)

    groups = defaultdict(list)
    for r in corpus:
        groups[(r["names"][0], r["blocks"])].append(r)

    out, tiers = [], Counter()
    for (name, blocks), recs in sorted(groups.items()):
        variants = []
        for r in recs:
            caps = sorted({p["capture"] for p in r["provenance"]})
            variants.append({"sha": r["sha"], "captures": caps,
                             "weight": len(caps), "flagged": variant_flagged(r),
                             "data": (STORE / f"{r['sha']}.bin").read_bytes()})

        clusters = cluster(variants, blocks) if len(variants) > 1 else [[0]]
        # primary = best-supported cluster
        clusters.sort(key=lambda cl: -sum(variants[i]["weight"] for i in cl))
        main_cl = [variants[i] for i in clusters[0]]
        data, lost = reconcile(main_cl, blocks)
        total_caps = sum(v["weight"] for v in variants)

        if len(clusters) > 1:
            tier = "multi-version"
        elif lost:
            tier = "partial"
        elif len(variants) > 1 or any(v["flagged"] for v in main_cl):
            tier = "recovered"
        else:
            tier = "verified" if total_caps >= 2 else "single"

        rec = {"name": name, "blocks": blocks, "variants": len(variants),
               "captures": total_caps, "versions": len(clusters),
               "lost": len(lost), "tier": tier}
        if lost:
            rec["lost_blocks"] = lost
        if tier in ("recovered", "partial", "verified"):
            h = hashlib.sha256(data).hexdigest()
            (RECOV / f"{h}.bin").write_bytes(data)
            rec["recovered_sha"] = h
        tiers[tier] += 1
        out.append(rec)

    (OUT / "consensus.json").write_text(
        json.dumps({"files": out}, indent=1, ensure_ascii=False), encoding="utf-8")

    print(f"logical files (name+blocks): {len(out)}   (vs {len(corpus)} sha-unique)")
    print("tiers:", dict(tiers))

    rec = [r for r in out if r["tier"] == "recovered"]
    print(f"\nrecovered (decay reconciled, fully clean): {len(rec)}")
    for r in sorted(rec, key=lambda r: -r["captures"])[:8]:
        print(f"  {r['name']:14s} {r['blocks']:3d} blk: "
              f"{r['variants']} variants / {r['captures']} captures")

    part = [r for r in out if r["tier"] == "partial"]
    print(f"\npartial (blocks lost on every copy -> donor worklist): {len(part)}")
    for r in sorted(part, key=lambda r: -r["lost"])[:12]:
        print(f"  {r['name']:14s} {r['blocks']:3d} blk: "
              f"{r['lost']} lost, {r['variants']} variants / {r['captures']} captures")

    mv = [r for r in out if r["tier"] == "multi-version"]
    print(f"\nmulti-version (distinct builds share name+size): {len(mv)}")
    for r in sorted(mv, key=lambda r: -r["versions"])[:8]:
        print(f"  {r['name']:14s} {r['blocks']:3d} blk: "
              f"{r['versions']} versions, {r['captures']} captures")
    print(f"\nwrote {OUT/'consensus.json'}")

if __name__ == "__main__":
    main()
