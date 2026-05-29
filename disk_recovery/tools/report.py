#!/usr/bin/env python3
"""
report.py — per-file recovery-confidence matrix from consensus.json (+ donor.json),
using the shared verdict model (verdict.py).

Bands (strongest first): GUARANTEED (identical on >=2 different physical disks) /
HIGH (>=2 reads of one disk) / GOOD (reconciled or donor) / MEDIUM (single disk,
CRC-clean) / UNVERIFIED (single disk, no check) / AMBIGUOUS (several builds) /
LOST (bad on every copy).

Writes work/corpus/REPORT.md (summary + actionable lists), report.csv (full
matrix) and healthy.txt (the trustworthy files).
"""

import json, csv, sys
from pathlib import Path
from collections import Counter

sys.path.insert(0, str(Path(__file__).resolve().parent))
import verdict as V

OUT = Path(__file__).resolve().parents[2] / "disk_recovery" / "work" / "corpus"

def main():
    files = json.load(open(OUT / "consensus.json", encoding="utf-8"))["files"]
    corpus = json.load(open(OUT / "corpus.json", encoding="utf-8"))["records"]
    recs_by_key = {}
    for r in corpus:
        recs_by_key.setdefault((r["names"][0], r["blocks"]), []).append(r)
    donor = {"recovered": [], "corroborated": []}
    if (OUT / "donor.json").exists():
        donor = json.load(open(OUT / "donor.json", encoding="utf-8"))
    recovered = {(d["name"], d["blocks"]) for d in donor["recovered"]}
    corro = {(d["name"], d["blocks"]): d["source"] for d in donor["corroborated"]}
    cap_fp = json.load(open(OUT / "captures.json", encoding="utf-8")) \
        if (OUT / "captures.json").exists() else {}
    disk_of = V.physical_disks(corpus, cap_fp)
    cons_by_key = {(r["name"], r["blocks"]): r for r in files}
    chosen = V.load_decisions(V.DECISIONS, cons_by_key)

    for r in files:
        r["band"] = V.classify(r, recs_by_key, recovered, corro, disk_of, chosen)

    cols = ["name", "blocks", "verified_blocks", "category", "is_binary", "tier", "band",
            "captures", "versions", "clean", "unknown", "flagged", "corrupt"]
    with open(OUT / "report.csv", "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(cols + ["action"])
        for r in sorted(files, key=lambda r: (V.BANDS.index(r["band"]), r["name"])):
            w.writerow([r.get(c, "") for c in cols] + [V.ACTION[r["band"]]])

    band = Counter(r["band"] for r in files)
    band_t = Counter(r["band"] for r in files if not r["is_binary"])
    band_b = Counter(r["band"] for r in files if r["is_binary"])

    def section(title, rows, extra):
        out = [f"### {title} ({len(rows)})", ""]
        if not rows:
            return out + ["_none_", ""]
        out += ["| file | blk | cat | captures | detail |", "|---|--:|---|--:|---|"]
        for r in rows:
            out.append(f"| {r['name']} | {r['blocks']} | {r['category']} | "
                       f"{r['captures']} | {extra(r)} |")
        return out + [""]

    md = ["# Recovery confidence matrix", "",
          f"{len(files)} logical files.  Full table: `report.csv`.  Files by disk: `export/`.",
          "", "## Summary", "",
          "| band | all | text | binary | meaning |", "|---|--:|--:|--:|---|"]
    for b in V.BANDS:
        md.append(f"| {b} | {band.get(b,0)} | {band_t.get(b,0)} | {band_b.get(b,0)} | {V.MEANING[b]} |")
    md.append("")

    md += ["## Action needed", ""]
    md += section("LOST — needs an external donor disk",
                  sorted([r for r in files if r["band"] == "LOST"],
                         key=lambda r: -(r["corrupt"]+r["flagged"])),
                  lambda r: f"{r['corrupt']+r['flagged']} bad blocks")
    md += section("AMBIGUOUS — multiple builds, pick canonical",
                  sorted([r for r in files if r["band"] == "AMBIGUOUS"],
                         key=lambda r: -r["versions"])[:25],
                  lambda r: f"{r['versions']} versions")
    md += section("UNVERIFIED — single disk, no way to check (blind spot)",
                  sorted([r for r in files if r["band"] == "UNVERIFIED"],
                         key=lambda r: (not r["is_binary"], r["name"])),
                  lambda r: "binary" if r["is_binary"] else "text")

    healthy = sorted([r for r in files if r["band"] in V.HEALTHY],
                     key=lambda r: (V.BANDS.index(r["band"]), not r["is_binary"], r["name"]))
    (OUT / "healthy.txt").write_text(
        "\n".join(f"{r['name']}\t{r['blocks']}blk\t{r['category']}\t{r['band']}"
                  for r in healthy) + "\n", encoding="utf-8")
    guaranteed = [r for r in healthy if r["band"] == "GUARANTEED"]
    hb = sum(1 for r in healthy if r["is_binary"])
    md += ["## Healthy files (safe to trust)", "",
           f"**{len(healthy)} of {len(files)}** healthy ({len(healthy)-hb} text, {hb} binary) — "
           f"full list in `healthy.txt`.  Of these, **{len(guaranteed)}** are GUARANTEED "
           "(byte-identical on >=2 different physical disks — the strongest bar).", "",
           "_The only literal 100% is running each file in the emulator.  GUARANTEED is the "
           "strongest static evidence: two independent media with identical bytes._", ""]

    (OUT / "REPORT.md").write_text("\n".join(md), encoding="utf-8")

    print("bands (all / text / binary):")
    for b in V.BANDS:
        print(f"  {b:11s} {band.get(b,0):4d}  {band_t.get(b,0):4d}  {band_b.get(b,0):4d}   {V.MEANING[b]}")
    print(f"\nHEALTHY: {len(healthy)}/{len(files)}  (GUARANTEED {len(guaranteed)}: identical on >=2 different disks)")
    print(f"wrote {OUT/'REPORT.md'}, {OUT/'report.csv'}, {OUT/'healthy.txt'}")

if __name__ == "__main__":
    main()
