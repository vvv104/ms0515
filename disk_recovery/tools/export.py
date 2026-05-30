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

def verdict_text(r, band, recs_by_key, recovered, corro, disk_of, chosen, sha, own):
    key = (r["name"], r["blocks"])
    note = "" if own else "  [this disk's copy was damaged; exported the recovered version]"
    if band == "CHOSEN":
        cl = chosen[key]
        if isinstance(cl, str): cl = [cl]
        builds_map = {s: d for s, d in V.version_disks(r)}
        own = sha in cl
        parts = [f"{c[:8]} @ {','.join(builds_map.get(c, ['?']))}" for c in cl]
        tag = "this disk's variant IS canonical" if own else "canonical lives on another disk"
        return f"CHOSEN — {len(cl)} canonical: " + " | ".join(parts) + f"; {tag}"
    if band == "GUARANTEED":
        return f"GUARANTEED — byte-identical on {V.phys_disks(key, recs_by_key, corro, disk_of)} different physical disks" + note
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
        builds = V.version_disks(r)
        parts = []
        for vsha, disks in builds:
            dl = ", ".join(disks[:3]) + (f" +{len(disks)-3}" if len(disks) > 3 else "")
            parts.append(f"{vsha[:8]} @ {dl}")
        return (f"AMBIGUOUS — {len(builds)} builds; compare export/<disk>/{r['name']}:  "
                + "  |  ".join(parts))
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
    cap_fp = json.load(open(OUT / "captures.json", encoding="utf-8")) \
        if (OUT / "captures.json").exists() else {}
    disk_of = V.physical_disks(corpus, cap_fp)
    chosen = V.load_decisions(V.DECISIONS, cons)

    # physical disk -> {(name, blocks): sha this disk carried}
    disk_files = defaultdict(dict)
    for r in corpus:
        for p in r["provenance"]:
            base = disk_of.get(p["capture"])
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
            band = V.classify(r, recs_by_key, recovered, corro, disk_of, chosen) if r else "UNVERIFIED"
            # choose best content
            prop = PROP / name
            ckey = (name, blocks)
            if ckey in chosen:                              # human-picked canonical version(s)
                cl = chosen[ckey]
                if isinstance(cl, str): cl = [cl]
                csha = sha if sha in cl else cl[0]          # prefer this disk's own canonical
                data = (STORE/f"{csha}.bin").read_bytes()
            elif ckey in recovered and prop.exists():
                data, csha = prop.read_bytes(), None
            elif r and r.get("recovered_sha") and (RECOV/f"{r['recovered_sha']}.bin").exists():
                csha = r["recovered_sha"]; data = (RECOV/f"{csha}.bin").read_bytes()
            else:
                csha = sha; data = (STORE/f"{sha}.bin").read_bytes()
            own = (csha == sha)
            (ddir / safe(name)).write_bytes(data)
            vrfd = f"{r.get('verified_blocks', 0)}/{blocks}" if r else "?"
            rows.append((name, blocks, r["category"] if r else "?", band,
                         verdict_text(r, band, recs_by_key, recovered, corro, disk_of, chosen, sha, own),
                         vrfd))
            bandc[band] += 1
        # VERDICT.txt
        lines = [f"disk: {disk}", f"files: {len(rows)}",
                 "  " + "  ".join(f"{b}={bandc[b]}" for b in V.BANDS if bandc[b]), "",
                 "VRFD = blocks byte-identical on >=2 different physical disks",
                 f"{'FILE':<16}{'BLK':>5} {'VRFD':>8}  {'CAT':<7} VERDICT", "-"*82]
        for name, blocks, cat, band, txt, vrfd in rows:
            lines.append(f"{name:<16}{blocks:>5} {vrfd:>8}  {cat:<7} {txt}")
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
