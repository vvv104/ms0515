#!/usr/bin/env python3
"""
decide.py — manage disk_recovery/decisions.tsv: the human's choice of the
canonical version for each AMBIGUOUS file (several distinct builds share a name).

Workflow:
  1. python decide.py            # (re)write decisions.tsv with every AMBIGUOUS
                                 #   file + its versions, PRESERVING your choices
  2. edit decisions.tsv          # put the sha8 (or a disk it's on) in CHOOSE;
                                 #   compare the actual bytes in export/<disk>/<file>
  3. python report.py / export.py# files you decided move from AMBIGUOUS -> CHOSEN

The file lives OUTSIDE work/ (which the pipeline regenerates) so your choices
survive a rebuild and can be committed.  It holds only metadata (names + chosen
sha), no disk content.  Re-running step 1 never discards a choice.
"""

import json, sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import verdict as V

OUT = Path(__file__).resolve().parents[2] / "disk_recovery" / "work" / "corpus"
HEADER = ("# Pick the canonical version for each AMBIGUOUS file.\n"
          "# Put the sha8 (or a disk it is on) in CHOOSE; blank = undecided.\n"
          "# Compare the bytes in export/<disk>/<file>.  Tab-separated; keep the columns.\n"
          "# NAME\tBLK\tCHOOSE\tVERSIONS (sha8 @ disks)\n")

def main():
    corpus = json.load(open(OUT / "corpus.json", encoding="utf-8"))["records"]
    files = json.load(open(OUT / "consensus.json", encoding="utf-8"))["files"]
    cap_fp = json.load(open(OUT / "captures.json", encoding="utf-8")) \
        if (OUT / "captures.json").exists() else {}
    recs_by_key = {}
    for r in corpus:
        recs_by_key.setdefault((r["names"][0], r["blocks"]), []).append(r)
    disk_of = V.physical_disks(corpus, cap_fp)

    # preserve existing choices verbatim (by name+blocks)
    prev = {}
    if V.DECISIONS.exists():
        for line in V.DECISIONS.read_text(encoding="utf-8").splitlines():
            if line.startswith("#") or not line.strip():
                continue
            c = line.split("\t")
            if len(c) >= 3 and c[2].strip():
                prev[(c[0].strip(), c[1].strip().replace("blk", ""))] = c[2].strip()

    amb = sorted([r for r in files if r["tier"] == "multi-version"],
                 key=lambda r: r["name"])
    lines = [HEADER]
    decided = 0
    for r in amb:
        name, blocks = r["name"], r["blocks"]
        vers = V.version_disks((name, blocks), recs_by_key, disk_of)
        opts = " ;; ".join(f"{sha[:8]} @ {','.join(disks)}" for sha, disks in vers)
        choose = prev.get((name, str(blocks)), "")
        if choose:
            decided += 1
        lines.append(f"{name}\t{blocks}\t{choose}\t{opts}\n")

    V.DECISIONS.write_text("".join(lines), encoding="utf-8")
    print(f"{len(amb)} AMBIGUOUS files; {decided} decided, {len(amb)-decided} to go.")
    print(f"edit {V.DECISIONS}  (CHOOSE column), then re-run report.py / export.py")

if __name__ == "__main__":
    main()
