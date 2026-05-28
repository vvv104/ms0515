#!/usr/bin/env python3
"""
report.py — turn consensus.json into a per-file recovery-confidence matrix:
for every logical file, how sure are we it is correct, and what (if anything)
is still needed.

Confidence bands (derived from the consensus tier + corroboration + read-status):

  HIGH        verified — >=2 captures byte-identical, no corruption indicator
  GOOD        recovered — damage reconciled away from clean copies
  MEDIUM      single capture, but a read-status capture confirms no flagged
              sectors (content read clean, just no second copy)
  UNVERIFIED  single/raw-only — one source, no read-status, no second copy:
              a genuine blind spot (could be silently corrupt, can't tell)
  AMBIGUOUS   multi-version — several distinct builds share the name; a human
              must pick the canonical one (by monitor generation)
  LOST        corrupt — blocks bad on every copy; needs an EXTERNAL donor

Writes work/corpus/REPORT.md (summary + actionable lists) and
work/corpus/report.csv (the full matrix), and prints the summary.
"""

import json, csv
from pathlib import Path
from collections import Counter

OUT = Path(__file__).resolve().parents[2] / "disk_recovery" / "work" / "corpus"

ACTION = {
    "HIGH":       "-",
    "GOOD":       "-",
    "MEDIUM":     "find a 2nd independent copy to confirm",
    "UNVERIFIED": "find a 2nd copy or a read-status capture",
    "AMBIGUOUS":  "pick the canonical build (by monitor generation)",
    "LOST":       "external donor disk / free-space anchor search",
}
ORDER = ["LOST", "AMBIGUOUS", "UNVERIFIED", "MEDIUM", "GOOD", "HIGH"]

def confidence(r):
    t = r["tier"]
    if t == "corrupt":       return "LOST"
    if t == "multi-version": return "AMBIGUOUS"
    if t == "verified":      return "HIGH"
    if t == "recovered":     return "GOOD"
    has_status = (r["clean"] or r["flagged"] or r["corrupt"])
    if has_status and not r["flagged"] and not r["corrupt"]:
        return "MEDIUM"
    return "UNVERIFIED"

def main():
    files = json.load(open(OUT / "consensus.json", encoding="utf-8"))["files"]
    for r in files:
        r["confidence"] = confidence(r)

    # full matrix -> CSV
    cols = ["name", "blocks", "category", "is_binary", "tier", "confidence",
            "captures", "versions", "clean", "unknown", "flagged", "corrupt"]
    with open(OUT / "report.csv", "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(cols + ["action"])
        for r in sorted(files, key=lambda r: (ORDER.index(r["confidence"]), r["name"])):
            w.writerow([r.get(c, "") for c in cols] + [ACTION[r["confidence"]]])

    band = Counter(r["confidence"] for r in files)
    band_bin = Counter(r["confidence"] for r in files if r["is_binary"])
    band_txt = Counter(r["confidence"] for r in files if not r["is_binary"])

    def section(title, rows, extra=lambda r: ""):
        out = [f"### {title} ({len(rows)})", ""]
        if not rows:
            out += ["_none_", ""]; return out
        out += ["| file | blk | cat | captures | detail |", "|---|--:|---|--:|---|"]
        for r in rows:
            out.append(f"| {r['name']} | {r['blocks']} | {r['category']} | "
                       f"{r['captures']} | {extra(r)} |")
        return out + [""]

    md = ["# Recovery confidence matrix", "",
          f"{len(files)} logical files.  Full per-file table: `report.csv`.", "",
          "## Summary", "",
          "| band | all | text | binary | meaning / action |",
          "|---|--:|--:|--:|---|"]
    MEANING = {
        "HIGH": "verified, >=2 captures identical — no action",
        "GOOD": "recovered from clean copies — no action",
        "MEDIUM": "single source, read-status clean — confirm with a 2nd copy",
        "UNVERIFIED": "single raw read, no status — blind spot",
        "AMBIGUOUS": "several builds share the name — pick canonical",
        "LOST": "bad on every copy — needs external donor",
    }
    for b in ORDER:
        md.append(f"| {b} | {band.get(b,0)} | {band_txt.get(b,0)} | "
                  f"{band_bin.get(b,0)} | {MEANING[b]} |")
    md.append("")

    lost = sorted([r for r in files if r["confidence"] == "LOST"],
                  key=lambda r: -(r["corrupt"]+r["flagged"]))
    md += ["## Action needed", ""]
    md += section("LOST — needs an external donor disk", lost,
                  lambda r: f"{r['corrupt']+r['flagged']} bad blocks")

    amb = sorted([r for r in files if r["confidence"] == "AMBIGUOUS"],
                 key=lambda r: -r["versions"])
    md += section("AMBIGUOUS — multiple builds, pick canonical", amb[:25],
                  lambda r: f"{r['versions']} versions")

    unv = sorted([r for r in files if r["confidence"] == "UNVERIFIED"],
                 key=lambda r: (not r["is_binary"], r["name"]))
    md += section("UNVERIFIED — single source, no way to check (blind spot)",
                  unv, lambda r: "binary" if r["is_binary"] else "text")

    (OUT / "REPORT.md").write_text("\n".join(md), encoding="utf-8")

    print("confidence bands (all / text / binary):")
    for b in ORDER:
        print(f"  {b:11s} {band.get(b,0):4d}   {band_txt.get(b,0):4d}   {band_bin.get(b,0):4d}   {MEANING[b]}")
    print(f"\nwrote {OUT/'REPORT.md'} and {OUT/'report.csv'}")

if __name__ == "__main__":
    main()
