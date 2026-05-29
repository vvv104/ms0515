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
from pathlib import Path
from collections import defaultdict

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

def classify(r, recs_by_key, recovered, corro, disk_of):
    """Confidence band for a consensus file record `r`.
    recovered = set of (name,blocks) donor-recovered; corro = {(name,blocks): src};
    disk_of = capture -> physical-disk label (from physical_disks())."""
    key = (r["name"], r["blocks"])
    if key in recovered:
        return "GOOD"                            # donor-recovered + verified
    if r["tier"] == "corrupt":
        return "LOST"
    if r["tier"] == "multi-version":
        return "AMBIGUOUS"
    if phys_disks(key, recs_by_key, corro, disk_of) >= 2:
        return "GUARANTEED"
    if r["tier"] == "verified":
        return "HIGH"
    if r["tier"] == "recovered":
        return "GOOD"
    if (r["clean"] or r["flagged"] or r["corrupt"]) and not r["flagged"] and not r["corrupt"]:
        return "MEDIUM"
    return "UNVERIFIED"

BANDS = ["GUARANTEED", "HIGH", "GOOD", "MEDIUM", "UNVERIFIED", "AMBIGUOUS", "LOST"]
HEALTHY = {"GUARANTEED", "HIGH", "GOOD", "MEDIUM"}
MEANING = {
    "GUARANTEED": "byte-identical on >=2 DIFFERENT physical disks",
    "HIGH":       "identical across >=2 reads of the SAME disk",
    "GOOD":       "reconciled clean / donor-recovered",
    "MEDIUM":     "single disk, every sector CRC-clean",
    "UNVERIFIED": "single disk, no read-status, no 2nd copy",
    "AMBIGUOUS":  "several builds share the name",
    "LOST":       "bad on every copy",
}
ACTION = {
    "GUARANTEED": "-",
    "HIGH":       "confirm with a different physical disk",
    "GOOD":       "-",
    "MEDIUM":     "find a 2nd copy on another disk",
    "UNVERIFIED": "find a 2nd copy or a read-status capture",
    "AMBIGUOUS":  "pick the canonical build (by monitor generation)",
    "LOST":       "external donor disk / free-space anchor search",
}
