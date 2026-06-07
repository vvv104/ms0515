#!/usr/bin/env python3
"""
consensus.py — group corpus files by LOGICAL identity (name + block length) and
reconcile the variants using a THREE-STATE per-block read-status.

build_corpus records, per file occurrence, whether the capture carries
per-sector read-status (`status`) and which file blocks sat on a flagged sector
(`bad`).  From that, every block of every variant is one of:

  CLEAN    — a read-status capture (Extended-CPC ST1/ST2, TeleDisk flags, or a
             .dat re-read set) read it WITHOUT a flag → trusted.
  FLAGGED  — flagged by a read-status capture and never read clean → suspect.
  UNKNOWN  — only ever read by a plain raw dump (no read-status) → can't say.

A plain raw read has NO read-status, so it can neither flag a block nor vouch
for one — it must not cancel a real flag (PITFALLS #3).  Reconciliation picks,
per block, the best available tier (CLEAN > UNKNOWN > FLAGGED), capture-weighted
majority within it.  For text files it additionally refuses to emit a garbage
block while any variant holds readable content (PITFALLS #4) — agreement does
NOT prove correctness when every copy shares a dead sector (e.g. BASICO.DOC).

Per-block outcome:
  clean    chosen from a confirmed-clean copy
  unknown  no clean copy; bytes from a statusless raw (unverified)
  flagged  no clean/unknown copy; every copy is CRC-flagged (suspect) — binary
           corruption signal, since content can't be judged
  corrupt  text only: garbage on every copy (lost data, donor needed)

Tiers: verified / recovered / corrupt / multi-version / single.

Output: work/corpus/consensus.json + a reconciled content store + a summary
that answers how much text AND binary data is still corrupt.
"""

import json, hashlib, sys
from pathlib import Path
from collections import defaultdict, Counter

sys.path.insert(0, str(Path(__file__).resolve().parent))
import verdict as V

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "disk_recovery" / "work" / "corpus"
STORE = OUT / "files"
RECOV = OUT / "recovered"
BLOCK = 512

BITS = bytes(bin(i).count("1") for i in range(256))
# Content-plausibility applies only to genuinely-textual files; tokenized BASIC
# (.BAS) and binary command scripts are not byte-text, so they are excluded.
TEXT_EXT = {"DOC", "TXT", "PAS", "FOR", "MAC", "C", "LST", "MAP"}
BINARY_CAT = {"system", "exec", "aux"}

def is_text(name):
    return (name.rsplit(".", 1)[1].upper() if "." in name else "") in TEXT_EXT

def readable(b):
    # printable ASCII; CR/LF/TAB; BS/VT/FF; SO/SI (KOI-7 РУС/ЛАТ shifts); ESC
    # (RUNOFF/terminal escapes); full KOI-8R high half — 0x80..0xBF is
    # box-drawing / pseudographics (used heavily on Rodionov disks), 0xC0..0xFF
    # is Cyrillic.  Omitting 0x80..0xBF made is_garbage() flag legitimate
    # KOI-8R screens as binary corruption -> file ended up in LOST.
    return ((0x20 <= b <= 0x7E) or b in (8, 9, 10, 11, 12, 13, 14, 15, 27)
            or 0x80 <= b <= 0xFF)

def is_garbage(seg):
    """Two-pronged garbage check for a text block:

    1. Inline low-control bytes — anything in 0x00..0x1F except NUL and the
       handful of control codes that occur naturally in text (TAB/LF/CR,
       BS/VT/FF, SO/SI, ESC).  Real text never has more than a stray one or
       two; binary content has many.  >5% is a strong binary signal even when
       the overall "readable" ratio looks fine because random high bytes
       happened to fall inside the KOI-8R range.
    2. Original > 50% non-readable (and not NUL padding) fallback.

    Without (1), BASICO.DOC's bit-rot blocks (high-byte garbage + scattered
    low-control bytes) slipped past the threshold and the file ended up
    VERIFIED instead of LOST."""
    if not seg:
        return False
    n = len(seg)
    low_ctrl = sum(1 for b in seg
                   if b and b < 0x20 and b not in (8, 9, 10, 11, 12, 13, 14, 15, 27))
    if low_ctrl / n > 0.05:
        return True
    non_readable = sum(1 for b in seg if not (readable(b) or b == 0))
    return non_readable / n > 0.5

def diff_stats(a, b):
    n = min(len(a), len(b)); d = bits = 0
    for i in range(n):
        if a[i] != b[i]:
            d += 1; bits += BITS[a[i] ^ b[i]]
    d += abs(len(a) - len(b))
    return d, (bits / d if d else 0.0)

def is_decay(a, b):
    d, bpb = diff_stats(a, b)
    return d <= 30 and bpb <= 2.5

def variant_quality(rec, blocks):
    """CLEAN and FLAGGED block sets for one content (UNKNOWN = the rest)."""
    clean, flagged = set(), set()
    allb = set(range(blocks))
    for p in rec["provenance"]:
        if p.get("status"):
            bad = set(p.get("bad", []))
            clean |= allb - bad
            flagged |= bad
    flagged -= clean
    return clean, flagged

def differing_blocks(a, b, blocks):
    return {i for i in range(blocks)
            if a[i*BLOCK:(i+1)*BLOCK] != b[i*BLOCK:(i+1)*BLOCK]}

def same_file(a, b, blocks):
    diff = differing_blocks(a["data"], b["data"], blocks)
    if not diff:
        return True
    if diff & (a["clean"] & b["clean"]):      # both read it CLEAN yet differ = real version
        return False
    if diff <= (a["flagged"] | b["flagged"]):  # all disagreement sits in flagged sectors = decay
        return True
    return is_decay(a["data"], b["data"])      # no read-status to judge: bit-rot metric decides

def cluster(variants, blocks):
    out, used = [], [False]*len(variants)
    for i in range(len(variants)):
        if used[i]:
            continue
        cl = [i]; used[i] = True
        for j in range(i+1, len(variants)):
            if not used[j] and any(same_file(variants[k], variants[j], blocks) for k in cl):
                cl.append(j); used[j] = True
        out.append(cl)
    return out

def pick_block(variants, b, text):
    """Choose block b: best tier (clean>unknown>flagged), capture-weighted
    majority; text refuses garbage while any variant is readable."""
    seg = lambda v: v["data"][b*BLOCK:(b+1)*BLOCK]
    def rank(v):
        if b in v["clean"]:   return 0
        if b not in v["flagged"]: return 1
        return 2
    cands, corrupt = variants, False
    if text:
        good = [v for v in variants if not is_garbage(seg(v))]
        if good:
            cands = good
        else:
            corrupt = True            # garbage on every copy
    best = min(rank(v) for v in cands)
    pool = [v for v in cands if rank(v) == best]
    cnt = Counter()
    for v in pool:
        cnt[seg(v)] += v["weight"]
    chosen = cnt.most_common(1)[0][0]
    tag = "corrupt" if corrupt else ("clean", "unknown", "flagged")[best]
    return chosen, tag

def reconcile(variants, blocks, text):
    out, tags = bytearray(), []
    for b in range(blocks):
        seg, tag = pick_block(variants, b, text)
        out += seg; tags.append(tag)
    return bytes(out), tags

_BASE_ALIAS = {"DIRRT": "DIR", "PIPRT": "PIP", "DUPRT": "DUP"}

def canonical_alias(name):
    """Normalise a filename to the alias-canonical form used to group related
    files into one consensus entry:

      * extension `.EXE` -> `.SAV`  (RT-11 treats them as the same kind of
        thing — a .SAV next to a .EXE on different disks is the same logical
        program even when bytes differ);
      * `DIRRT` / `PIPRT` / `DUPRT` basenames -> `DIR` / `PIP` / `DUP`
        (Rodionov-era renames of the standard utilities).

    So `DIRRT.EXE`, `DIR.EXE`, `DIR.SAV` all share the same alias-canonical
    `DIR.SAV` and end up in ONE consensus group; different shas surface as
    candidate variants in the choose-canonical UI, so a healthy DIRRT.EXE on
    disk4 can substitute for a damaged DIR.SAV elsewhere even without an
    exact sha match."""
    if "." not in name:
        return name
    base, _, ext = name.rpartition(".")
    base_up, ext_up = base.upper(), ext.upper()
    base_up = _BASE_ALIAS.get(base_up, base_up)
    if ext_up == "EXE":
        ext_up = "SAV"
    return f"{base_up}.{ext_up}"

def canonical_name(names):
    """Consensus row's display name.  Each corpus record carries the names
    actually seen for that sha — sha-dedup at ingest accumulates them across
    captures.  Pick a SAV-form when present (cross-disk users mostly think
    of the SAV-named form); otherwise the first name as recorded."""
    for n in names:
        if n.upper().endswith(".SAV"):
            return n
    return names[0]

def group_key(record):
    """The (name, blocks) group this record belongs to.  We DO NOT merge
    different basenames into one consensus row (disk4 carries DIRRT.EXE and
    DIR.EXE as DISTINCT physical files — bundling them would hide one).
    Cross-name recovery — letting `DUP.EXE` on disk4 stand in as a candidate
    for `DUP.SAV` on a damaged disk — is exposed at the UI layer via
    canonical_alias()-based version aggregation, not by collapsing rows
    here."""
    return (canonical_name(record["names"]), record["blocks"])

def main():
    corpus = json.load(open(OUT / "corpus.json", encoding="utf-8"))["records"]
    cap_fp = json.load(open(OUT / "captures.json", encoding="utf-8")) \
        if (OUT / "captures.json").exists() else {}
    disk_of = V.physical_disks(corpus, cap_fp)
    RECOV.mkdir(exist_ok=True)
    # Group by alias-canonical (name, blocks).  DIRRT.EXE, DIR.EXE and
    # DIR.SAV all funnel into the DIR.SAV group; different shas appear as
    # candidate variants so a healthy disk's bytes can substitute for a
    # damaged one's even when the names differ.  Per-disk display in
    # review.py then surfaces each entry under the name actually used on
    # the disk being filtered to.
    groups = defaultdict(list)
    for r in corpus:
        groups[group_key(r)].append(r)

    out, tiers = [], Counter()
    for (name, blocks), recs in sorted(groups.items()):
        text = is_text(name)
        variants = []
        for r in recs:
            caps = sorted({p["capture"] for p in r["provenance"]})
            clean, flagged = variant_quality(r, blocks)
            variants.append({"sha": r["sha"], "weight": len(caps), "captures": caps,
                             "clean": clean, "flagged": flagged,
                             "data": (STORE / f"{r['sha']}.bin").read_bytes()})

        idx = cluster(variants, blocks) if len(variants) > 1 else [[0]]
        clusters = [[variants[i] for i in cl] for cl in idx]
        clusters.sort(key=lambda cl: sum(v["weight"] for v in cl), reverse=True)

        # reconcile EACH cluster -> one "build" (bit-rot already resolved by
        # read-status), stored so the GUI offers genuine versions, not raw shas.
        builds = []
        primary = None
        recon = []                                   # (cdata, disks) per build
        for cl in clusters:
            cdata, ctags = reconcile(cl, blocks, text)
            csha = hashlib.sha256(cdata).hexdigest()
            (STORE / f"{csha}.bin").write_bytes(cdata)
            disks = sorted({disk_of.get(c) for v in cl for c in v["captures"]} - {None})
            builds.append({"sha": csha, "disks": disks, "copies": len(cl)})
            recon.append((cdata, set(disks)))
            if primary is None:
                primary, data, tags = cl, cdata, ctags

        # block-level GUARANTEED = ALL builds agree on this block AND it sits on
        # >=2 different physical disks.  A block where the builds DIFFER is a
        # real version-split, not bit-rot.
        verified_blocks = 0
        for b in range(blocks):
            segs = [d[b*BLOCK:(b+1)*BLOCK] for d, _ in recon]
            if len(set(segs)) == 1 and len(set().union(*(ds for _, ds in recon))) >= 2:
                verified_blocks += 1

        tagc = Counter(tags)
        total_caps = sum(v["weight"] for v in variants)
        corrupt = tagc["corrupt"] + tagc["flagged"]   # lost-text + suspect (flagged-everywhere)

        if len(clusters) > 1:
            tier = "multi-version"
        elif corrupt:
            tier = "corrupt"
        elif len(variants) > 1:
            tier = "recovered"
        else:
            tier = "verified" if total_caps >= 2 else "single"

        rec = {"name": name, "blocks": blocks, "category": recs[0]["category"],
               "is_binary": recs[0]["category"] in BINARY_CAT,
               "variants": len(variants), "captures": total_caps,
               "versions": len(clusters), "builds": builds,
               "verified_blocks": verified_blocks,
               "clean": tagc["clean"], "unknown": tagc["unknown"],
               "flagged": tagc["flagged"], "corrupt": tagc["corrupt"],
               "tier": tier}
        if tagc["corrupt"] or tagc["flagged"]:
            rec["bad_blocks"] = [i for i, t in enumerate(tags) if t in ("corrupt", "flagged")]
        if tier != "multi-version":
            h = builds[0]["sha"]
            (RECOV / f"{h}.bin").write_bytes(data)
            rec["recovered_sha"] = h
        tiers[tier] += 1
        out.append(rec)

    (OUT / "consensus.json").write_text(
        json.dumps({"files": out}, indent=1, ensure_ascii=False), encoding="utf-8")

    print(f"logical files (name+blocks): {len(out)}   (vs {len(corpus)} sha-unique)")
    print("tiers:", dict(tiers))

    corr = [r for r in out if r["tier"] == "corrupt"]
    ct = [r for r in corr if not r["is_binary"]]
    cb = [r for r in corr if r["is_binary"]]
    print(f"\ncorrupt: {len(corr)}  (text {len(ct)}, binary {len(cb)}) — still need recovery")
    print("  TEXT (garbage content on every copy):")
    for r in sorted(ct, key=lambda r: -(r["corrupt"]+r["flagged"]))[:15]:
        print(f"    {r['name']:14s} {r['blocks']:4d}blk  corrupt={r['corrupt']} flagged={r['flagged']}  ({r['captures']} captures)")
    print("  BINARY (CRC-flagged on every copy, no clean read anywhere):")
    for r in sorted(cb, key=lambda r: -r["flagged"])[:20]:
        print(f"    {r['name']:14s} {r['blocks']:4d}blk  flagged={r['flagged']}  cat={r['category']}  ({r['captures']} captures)")

    # coverage: how much can we even judge?
    no_status = sum(1 for r in out if r["clean"] == 0 and r["flagged"] == 0 and r["corrupt"] == 0)
    print(f"\ncoverage: {len(out)-no_status} files have some read-status; "
          f"{no_status} are raw-only (corruption undetectable without a status capture)")
    print(f"wrote {OUT/'consensus.json'}")

if __name__ == "__main__":
    main()
