#!/usr/bin/env python3
"""
export.py — lay every recovered file out into a directory tree by PHYSICAL DISK,
with a per-file verdict, so the result is browsable (which disk, the bytes, and
how trustworthy each file is).

  work/corpus/export/
    <disk>/                     one folder per physical disk (captures merged)
      <FILE>                    best available bytes for that file
      VERDICT.txt               per-file band + plain-language verdict + action
    INDEX.txt                   all disks, file counts, health summary

Best bytes per file: a donor proposal if recovered that way, else the consensus
reconstruction, else (for multi-version) the exact version THIS disk carried.
Confidence bands come from the shared model (verdict.py); GUARANTEED = identical
on >=2 different physical disks.
"""

import json, re, sys
from pathlib import Path
from collections import defaultdict

sys.path.insert(0, str(Path(__file__).resolve().parent))
import verdict as V

OUT = Path(__file__).resolve().parents[2] / "disk_recovery" / "work" / "corpus"
STORE, RECOV, PROP, EXPORT = OUT/"files", OUT/"recovered", OUT/"donor_proposed", OUT/"export"
BAD = re.compile(r'[<>:"/\\|?*]')

def safe(name):
    return BAD.sub("_", name)

def verdict_text(r, band, recs_by_key, recovered, corro, sha, own):
    key = (r["name"], r["blocks"])
    note = "" if own else "  [this disk's copy was damaged; exported the recovered version]"
    if band == "GUARANTEED":
        return f"GUARANTEED — byte-identical on {V.phys_disks(key, recs_by_key, corro)} different physical disks" + note
    if band == "HIGH":
        return f"HIGH — identical across {r['captures']} reads, but only this physical disk" + note
    if band == "GOOD":
        return ("GOOD — recovered via donor from free space" if key in recovered
                else "GOOD — reconciled clean from differing copies") + note
    if band == "MEDIUM":
        return "MEDIUM — single disk, every sector CRC-clean (no 2nd copy)"
    if band == "UNVERIFIED":
        return "UNVERIFIED — single disk, no read-status, no 2nd copy: cannot be checked"
    if band == "AMBIGUOUS":
        return f"AMBIGUOUS — this disk has version {sha[:8]} of {r['versions']} distinct builds; pick canonical"
    if band == "LOST":
        return f"LOST — {r['corrupt']+r['flagged']} bad blocks on every copy; needs an external donor disk"
    return band

def main():
    files = json.load(open(OUT/"consensus.json", encoding="utf-8"))["files"]
    corpus = json.load(open(OUT/"corpus.json", encoding="utf-8"))["records"]
    cons = {(r["name"], r["blocks"]): r for r in files}
    sha2canon = {r["sha"]: (r["names"][0], r["blocks"]) for r in corpus}
    recs_by_key = defaultdict(list)
    for r in corpus:
        recs_by_key[(r["names"][0], r["blocks"])].append(r)
    donor = json.load(open(OUT/"donor.json", encoding="utf-8")) if (OUT/"donor.json").exists() \
        else {"recovered": [], "corroborated": []}
    recovered = {(d["name"], d["blocks"]) for d in donor["recovered"]}
    corro = {(d["name"], d["blocks"]): d["source"] for d in donor["corroborated"]}

    # physical disk -> {(name, blocks): sha this disk carried}
    disk_files = defaultdict(dict)
    for r in corpus:
        for p in r["provenance"]:
            base = V.base_disk(p["capture"])
            if base:
                disk_files[base][(p["name"], r["blocks"])] = r["sha"]

    if EXPORT.exists():
        import shutil; shutil.rmtree(EXPORT)
    EXPORT.mkdir(parents=True)

    index = []
    for disk in sorted(disk_files):
        ddir = EXPORT / disk
        ddir.mkdir(parents=True, exist_ok=True)
        rows, bandc = [], defaultdict(int)
        for (name, blocks), sha in sorted(disk_files[disk].items()):
            r = cons.get(sha2canon.get(sha, (name, blocks)))
            band = V.classify(r, recs_by_key, recovered, corro) if r else "UNVERIFIED"
            # choose best content
            prop = PROP / name
            if (name, blocks) in recovered and prop.exists():
                data, csha = prop.read_bytes(), None
            elif r and r.get("recovered_sha") and (RECOV/f"{r['recovered_sha']}.bin").exists():
                csha = r["recovered_sha"]; data = (RECOV/f"{csha}.bin").read_bytes()
            else:
                csha = sha; data = (STORE/f"{sha}.bin").read_bytes()
            own = (csha == sha)
            (ddir / safe(name)).write_bytes(data)
            rows.append((name, blocks, r["category"] if r else "?", band,
                         verdict_text(r, band, recs_by_key, recovered, corro, sha, own)))
            bandc[band] += 1
        # VERDICT.txt
        lines = [f"disk: {disk}", f"files: {len(rows)}",
                 "  " + "  ".join(f"{b}={bandc[b]}" for b in V.BANDS if bandc[b]), "",
                 f"{'FILE':<16}{'BLK':>5}  {'CAT':<7} VERDICT", "-"*78]
        for name, blocks, cat, band, txt in rows:
            lines.append(f"{name:<16}{blocks:>5}  {cat:<7} {txt}")
        (ddir / "VERDICT.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")
        index.append((disk, len(rows), bandc))

    # INDEX.txt
    idx = [f"{'DISK':<34}{'FILES':>6}  HEALTH (band counts)", "-"*90]
    for disk, n, bc in index:
        h = "  ".join(f"{b}={bc[b]}" for b in V.BANDS if bc[b])
        idx.append(f"{disk:<34}{n:>6}  {h}")
    (EXPORT/"INDEX.txt").write_text("\n".join(idx) + "\n", encoding="utf-8")

    nfiles = sum(n for _, n, _ in index)
    print(f"exported {nfiles} files across {len(index)} physical disks -> {EXPORT}")
    print("see INDEX.txt and each disk's VERDICT.txt")

if __name__ == "__main__":
    main()
