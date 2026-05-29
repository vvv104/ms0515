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

def main():
    corpus = json.load(open(OUT / "corpus.json", encoding="utf-8"))["records"]
    files = json.load(open(OUT / "consensus.json", encoding="utf-8"))["files"]
    cap_fp = json.load(open(OUT / "captures.json", encoding="utf-8")) \
        if (OUT / "captures.json").exists() else {}
    recs_by_key = {}
    for r in corpus:
        recs_by_key.setdefault((r["names"][0], r["blocks"]), []).append(r)
    disk_of = V.physical_disks(corpus, cap_fp)

    chosen = V.load_decisions(V.DECISIONS, recs_by_key, disk_of)   # preserve choices
    amb = [r for r in files if r["tier"] == "multi-version"]
    V.write_decisions(V.DECISIONS, amb, recs_by_key, disk_of, chosen)

    decided = sum(1 for r in amb if (r["name"], r["blocks"]) in chosen)
    print(f"{len(amb)} AMBIGUOUS files; {decided} decided, {len(amb)-decided} to go.")
    print(f"edit {V.DECISIONS} (CHOOSE column) or use review.py, then re-run report.py / export.py")

if __name__ == "__main__":
    main()
