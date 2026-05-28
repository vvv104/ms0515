#!/usr/bin/env python3
"""
analyze_corpus.py — second-pass analysis over work/corpus/ (corpus.json +
the sha content store).

1. Generation grouping: cluster captures by their MONITOR (.SYS) — MON8SJ
   (OSA), RT11SJ (Omega/Mihin), RT15SJ (rodionov/FODOS).  Files that travel
   with one monitor build (its sha) form a compatibility generation; since
   even standard .SAV utilities version-check the monitor, this is the real
   "what works with what" grouping (see ../METHODOLOGY.md).  Also flags files
   that exist in several versions (same name, different sha) across generations.

2. Content analysis: per unique file, the readable-byte fraction (printable
   ASCII + KOI-8R Cyrillic + CR/LF/TAB, over the content before NUL padding) —
   distinguishes clean text from files with binary garbage — and, for
   executables, the printable strings (what the program is/does).  Re-tags
   "other" files as textlike/binary by content.

Writes work/corpus/analysis.json (UTF-8, with decoded strings) and prints an
ASCII-only summary.

Usage: python analyze_corpus.py
"""

import json
from pathlib import Path
from collections import defaultdict

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "disk_recovery" / "work" / "corpus"
STORE = OUT / "files"

MONITORS = {"MON8SJ.SYS": "OSA", "RT11SJ.SYS": "Omega/Mihin",
            "RT15SJ.SYS": "rodionov/FODOS"}

def readable(b):
    return (0x20 <= b <= 0x7E) or b in (9, 10, 13) or (0xC0 <= b <= 0xFF)

def readable_fraction(data):
    end = len(data)
    while end and data[end-1] == 0:   # ignore trailing NUL padding
        end -= 1
    if end == 0:
        return 1.0
    return sum(1 for b in data[:end] if readable(b)) / end

def strings(data, minlen=6):
    out, cur = [], bytearray()
    for x in data:
        if x != 0 and readable(x):
            cur.append(x)
        else:
            if len(cur) >= minlen:
                out.append(bytes(cur))
            cur = bytearray()
    if len(cur) >= minlen:
        out.append(bytes(cur))
    return out

def main():
    corpus = json.load(open(OUT / "corpus.json", encoding="utf-8"))["records"]

    # capture -> [(name, sha, category)]
    cap_files = defaultdict(list)
    for r in corpus:
        for p in r["provenance"]:
            cap_files[(p["capture"], p["side"])].append((p["name"], r["sha"], r["category"]))

    # ── generations: group captures by their monitor (name, sha) ──
    gens = defaultdict(list)
    no_monitor = []
    for cap, files in cap_files.items():
        mon = next(((n, sha) for n, sha, _ in files if n.upper() in MONITORS), None)
        (gens[mon] if mon else no_monitor).append(cap)

    # version-split files: same name -> multiple shas
    name_shas = defaultdict(set)
    for r in corpus:
        for n in r["names"]:
            name_shas[n].add(r["sha"])
    versioned = {n: len(s) for n, s in name_shas.items() if len(s) > 1}

    # ── content analysis ──
    content = []
    retag = defaultdict(int)
    for r in corpus:
        data = (STORE / f"{r['sha']}.bin").read_bytes()
        rf = readable_fraction(data)
        item = {"sha": r["sha"], "name": r["names"][0], "category": r["category"],
                "blocks": r["blocks"], "readable": round(rf, 3)}
        if r["category"] in ("exec", "other"):
            ss = sorted(set(s.decode("koi8-r", "replace") for s in strings(data)),
                        key=len, reverse=True)[:8]
            item["strings"] = ss
        if r["category"] == "other":
            item["content_kind"] = "textlike" if rf > 0.85 else "binary"
            retag[item["content_kind"]] += 1
        content.append(item)

    (OUT / "analysis.json").write_text(json.dumps({
        "generations": [{"monitor": f"{n} {sha[:8]}", "os": MONITORS[n.upper()],
                         "captures": sorted(c[0] for c in caps)}
                        for (n, sha), caps in sorted(gens.items())],
        "no_monitor_captures": sorted(set(c[0] for c in no_monitor)),
        "versioned_files": dict(sorted(versioned.items())),
        "content": content,
    }, indent=1, ensure_ascii=False), encoding="utf-8")

    # ── ASCII summary ──
    print(f"=== generations ({len(gens)} monitor builds) ===")
    for (n, sha), caps in sorted(gens.items()):
        uniq = sorted(set(c[0] for c in caps))
        print(f"  {n} {sha[:8]} [{MONITORS[n.upper()]}]: {len(uniq)} captures")
        for c in uniq[:4]:
            print(f"      {c}")
        if len(uniq) > 4:
            print(f"      ... +{len(uniq)-4}")
    if no_monitor:
        print(f"  (no monitor): {len(set(c[0] for c in no_monitor))} captures (data/program disks)")
    print(f"\nversion-split files (same name, >1 content): {len(versioned)}")
    top = sorted(versioned.items(), key=lambda kv: -kv[1])[:10]
    print("  " + ", ".join(f"{n}x{c}" for n, c in top))
    print(f"\n=== content: 'other' re-tagged by content: {dict(retag)} ===")
    low = [c for c in content if c["readable"] < 0.5 and c["category"] == "text"]
    print(f"text files with low readability (<0.5, suspect garbage): {len(low)}")
    for c in low[:8]:
        print(f"      {c['name']} readable={c['readable']}")
    print(f"\nwrote {OUT/'analysis.json'}")

if __name__ == "__main__":
    main()
