#!/usr/bin/env python3
"""
verdict.py — shared confidence model for report.py and export.py.

Confidence bands, strongest first:

  GUARANTEED  byte-identical on >=2 DIFFERENT physical disks (not just repeat
              reads of one disk).  Two independent media carrying the exact same
              bytes is a real guarantee — the user's bar for "100% healthy".
  HIGH        identical across >=2 captures, but all of ONE physical disk
              (e.g. a disk's .raw + its SAMdisk -final): strong, not cross-disk.
  GOOD        reconciled clean from differing copies, or donor-recovered.
  MEDIUM      single physical disk, but every sector read CRC-clean.
  UNVERIFIED  single physical disk, no read-status, no 2nd copy — blind spot.
  AMBIGUOUS   several distinct builds share the name — pick the canonical one.
  LOST        bad on every copy — needs an external donor disk.

A "physical disk" merges all captures of one floppy (its .raw, SAMdisk -final,
TeleDisk, split _Head0/_Head1, ...), so re-reads of one disk never count as the
cross-disk guarantee.
"""

import re
import hashlib
from pathlib import Path
from collections import defaultdict, Counter

_STORE = Path(__file__).resolve().parents[2] / "disk_recovery" / "work" / "corpus" / "files"

def byte_majority(datas):
    """Per-byte majority across copies of one file (for reconciling bit-rot).
    Apply only to copies of the SAME build — across different builds it blends."""
    n = min(len(d) for d in datas)
    out = bytearray(n)
    for i in range(n):
        out[i] = Counter(d[i] for d in datas).most_common(1)[0][0]
    return bytes(out)

def store_voted(datas):
    """Compute the byte-majority of `datas`, save it into the content store, and
    return its sha (so a voted result is a first-class, selectable version)."""
    voted = byte_majority(datas)
    sha = hashlib.sha256(voted).hexdigest()
    _STORE.mkdir(parents=True, exist_ok=True)
    (_STORE / f"{sha}.bin").write_bytes(voted)
    return sha

# capture/source suffixes that denote a different CAPTURE of the same disk
_SUFFIX = re.compile(r"(-final|_Head0\+1|_Head[01]|_s[01])$", re.I)

def base_disk(source):
    """Readable physical-disk label from a capture / donor source string.
    Returns None for a content-store reference (not a physical disk)."""
    s = source.rsplit("#", 1)[0]                 # drop side marker
    if s.startswith("content:"):
        return None
    if ":" in s:                                 # drop free:/image: prefix
        s = s.split(":", 1)[1]
    p = Path(s)
    stem = p.stem if p.suffix else p.name
    return str(p.parent / _SUFFIX.sub("", stem)).replace("\\", "/")

def physical_disks(corpus, cap_fp):
    """Map every capture -> a physical-disk label.  Two captures are the SAME
    physical disk when their DIRECTORY fingerprints match (same file list, order,
    start blocks, sizes — written once at create time, immune to bad-block read
    variance).  Captures with no fingerprint stand alone.  This is what the
    GUARANTEED bar counts: identical content on >=2 DIFFERENT physical disks."""
    caps = {p["capture"] for r in corpus for p in r["provenance"]}
    groups = defaultdict(list)
    for c in caps:
        groups[cap_fp.get(c) or f"__{c}"].append(c)
    disk_of = {}
    for members in groups.values():
        label = base_disk(min(members, key=lambda m: (len(m), m))) or members[0]
        for c in members:
            disk_of[c] = label
    return disk_of

def _disk(disk_of, source):
    s = source.rsplit("#", 1)[0]
    if s.startswith("content:"):
        return None
    if ":" in s:
        s = s.split(":", 1)[1]
    return disk_of.get(s, base_disk(source))

def phys_disks(key, recs_by_key, corro, disk_of):
    """Max number of DISTINCT physical disks carrying one identical content of
    this file (any single sha), plus a free-space corroboration disk if any."""
    best = 0
    for cr in recs_by_key.get(key, []):
        disks = {disk_of.get(p["capture"]) for p in cr["provenance"]}
        disks.discard(None)
        if key in corro:
            d = _disk(disk_of, corro[key])
            if d:
                disks.add(d)
        best = max(best, len(disks))
    return best

def version_disks(cons_rec):
    """[(sha, [physical disks]), ...] — the consensus BUILDS of a file: one entry
    per cluster (bit-rot already reconciled), with the disks each lives on, for
    the human to compare and pick.  (Not raw shas — those include decay copies.)"""
    return [(b["sha"], b.get("disks", [])) for b in (cons_rec or {}).get("builds", [])]

def resolve_choice(choose, versions):
    """Map a user's pick (sha8 prefix or a disk label) to a full sha, or None."""
    choose = choose.strip()
    if not choose:
        return None
    for sha, disks in versions:
        if sha.startswith(choose) or any(choose == d for d in disks):
            return sha
    return None

def load_decisions(path, cons_by_key):
    """Parse the human decisions file -> {(name,blocks): chosen_sha}.
    cons_by_key maps (name,blocks) -> consensus record (for its builds)."""
    chosen = {}
    p = Path(path)
    if not p.exists():
        return chosen
    for line in p.read_text(encoding="utf-8").splitlines():
        if line.startswith("#") or not line.strip():
            continue
        cells = line.split("\t")
        if len(cells) < 3 or not cells[2].strip():
            continue
        name = cells[0].strip()
        try:
            blocks = int(cells[1].strip().replace("blk", ""))
        except ValueError:
            continue
        choose = cells[2].strip()
        sha = resolve_choice(choose, version_disks(cons_by_key.get((name, blocks))))
        if not sha:                              # a voted/synthetic content in the store
            hits = list(_STORE.glob(choose + "*.bin")) if len(choose) >= 6 else []
            if len(hits) == 1:
                sha = hits[0].stem
        if sha:
            chosen[(name, blocks)] = sha
    return chosen

def classify(r, recs_by_key, recovered, corro, disk_of, chosen=frozenset()):
    """Confidence band for a consensus file record `r`.
    recovered = donor-recovered (name,blocks); corro = {(name,blocks): src};
    disk_of = capture -> physical-disk label; chosen = (name,blocks) the human
    picked a canonical version for."""
    key = (r["name"], r["blocks"])
    if key in recovered:
        return "GOOD"                            # donor-recovered + verified
    if r["tier"] == "corrupt":
        return "LOST"
    if r["tier"] == "multi-version":
        return "CHOSEN" if key in chosen else "AMBIGUOUS"
    if phys_disks(key, recs_by_key, corro, disk_of) >= 2:
        return "GUARANTEED"
    if r["tier"] == "verified":
        return "HIGH"
    if r["tier"] == "recovered":
        return "GOOD"
    if (r["clean"] or r["flagged"] or r["corrupt"]) and not r["flagged"] and not r["corrupt"]:
        return "MEDIUM"
    return "UNVERIFIED"

BANDS = ["GUARANTEED", "CHOSEN", "HIGH", "GOOD", "MEDIUM", "UNVERIFIED", "AMBIGUOUS", "LOST"]
HEALTHY = {"GUARANTEED", "CHOSEN", "HIGH", "GOOD", "MEDIUM"}
MEANING = {
    "GUARANTEED": "byte-identical on >=2 DIFFERENT physical disks",
    "CHOSEN":     "you picked the canonical version (decisions.tsv)",
    "HIGH":       "identical across >=2 reads of the SAME disk",
    "GOOD":       "reconciled clean / donor-recovered",
    "MEDIUM":     "single disk, every sector CRC-clean",
    "UNVERIFIED": "single disk, no read-status, no 2nd copy",
    "AMBIGUOUS":  "several builds share the name",
    "LOST":       "bad on every copy",
}
ACTION = {
    "GUARANTEED": "-",
    "CHOSEN":     "-",
    "HIGH":       "confirm with a different physical disk",
    "GOOD":       "-",
    "MEDIUM":     "find a 2nd copy on another disk",
    "UNVERIFIED": "find a 2nd copy or a read-status capture",
    "AMBIGUOUS":  "pick the canonical build in decisions.tsv",
    "LOST":       "external donor disk / free-space anchor search",
}

DECISIONS = Path(__file__).resolve().parents[2] / "disk_recovery" / "decisions.tsv"

DECISION_HEADER = (
    "# Pick the canonical version for each AMBIGUOUS file.\n"
    "# Put the sha8 (or a disk it is on) in CHOOSE; blank = undecided.\n"
    "# Compare the bytes in export/<disk>/<file>.  Tab-separated; keep the columns.\n"
    "# NAME\tBLK\tCHOOSE\tVERSIONS (sha8 @ disks)\n")

def write_decisions(path, amb_records, chosen):
    """Write the decisions file for the AMBIGUOUS consensus records, filling
    CHOOSE from `chosen` ({(name,blocks): sha}).  Shared by decide.py and the GUI."""
    lines = [DECISION_HEADER]
    for r in sorted(amb_records, key=lambda r: r["name"]):
        key = (r["name"], r["blocks"])
        opts = " ;; ".join(f"{s[:8]} @ {','.join(d)}" for s, d in version_disks(r))
        ch = chosen.get(key, "")
        lines.append(f"{r['name']}\t{r['blocks']}\t{ch[:8]}\t{opts}\n")
    Path(path).write_text("".join(lines), encoding="utf-8")
