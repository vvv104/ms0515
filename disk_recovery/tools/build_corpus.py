#!/usr/bin/env python3
"""
build_corpus.py — unique-file corpus over every readable disk in
disk_recovery/work/, using the verified ms0515-disk tool for extraction.

Ingests all three capture kinds: raw images directly, and TeleDisk / SAMdisk
Extended-CPC captures by converting them to raw first (via convert_teledisk /
convert_samdisk, in a temp dir).  Each side's files are extracted via
`ms0515-disk get`, hashed (sha-256), and consolidated: one record per unique
content, with provenance (which capture + side) and a type-based category.
DS-spanning and other non-readable images are flagged.

Output: work/corpus/corpus.json + a printed summary.  The category is a type
hint only; the real system/generation grouping is derived later from
co-occurrence (provenance), since even standard .SAV utilities are version-
bound to a monitor generation (see ../METHODOLOGY.md).

Usage: python build_corpus.py
"""

import subprocess, hashlib, json, tempfile, shutil, sys, re
from pathlib import Path
from collections import defaultdict, Counter

sys.path.insert(0, str(Path(__file__).resolve().parent))
from read_spanning import read_spanning

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
WORK = ROOT / "disk_recovery" / "work"
_tool = ROOT / "src/build/Release/tools/disk/ms0515-disk.exe"
TOOL = _tool if _tool.exists() else _tool.with_suffix("")
OUT  = WORK / "corpus"
PY   = sys.executable

SS, DS = 409600, 819200

CATEGORY = {}
for e in ["SYS"]:                                            CATEGORY[e] = "system"
for e in ["SAV", "EXE", "OBJ"]:                              CATEGORY[e] = "exec"
for e in ["DAT", "HLP", "BAK", "STB", "MLB", "SML"]:         CATEGORY[e] = "aux"
for e in ["TXT","MAC","FOR","BAS","PAS","C","COM","DOC",
          "LST","MAP","DIR","DIF","SLP","TFP"]:              CATEGORY[e] = "text"

def categorize(name):
    ext = name.rsplit(".", 1)[1].upper() if "." in name else ""
    return CATEGORY.get(ext, "other")

IL = [0, 2, 4, 6, 8, 1, 3, 5, 7, 9]

def lbn_phys(lbn, side, ds):
    """Physical block index of an LBN under the FDC + osa-skew driver.

    Equals lbnToByte(lbn, side, ds) // 512 — which is exactly how both
    converters index their bad-maps (one byte per physical sector: TeleDisk
    track_index*10+sec, Extended-CPC per-side track*10+sec).  So this is the
    bridge from a file's directory LBN to the bad-map slot for that block."""
    n = lbn % 800
    track = (n // 10 + 1) % 80
    sec = (IL[n % 10] + 2 * track - 2) % 10
    return track * 20 + side * 10 + sec if ds else track * 10 + sec

DIR_RE = re.compile(r"^\s+(\S+)\s+blk=\s*(\d+)\s+len=\s*(\d+)")

def dir_entries(img, side):
    """name -> (start_block, length) parsed from `ms0515-disk dir`."""
    r = subprocess.run([str(TOOL), "dir", str(img), "--side", str(side)],
                       capture_output=True, text=True)
    ents = {}
    for line in r.stdout.splitlines():
        m = DIR_RE.match(line)
        if m:
            ents[m.group(1)] = (int(m.group(2)), int(m.group(3)))
    return ents

def flagged_blocks(start, length, side, ds, flagged):
    """Indices (0-based, within the file) of blocks whose physical sector is in
    the flagged set."""
    return [i for i in range(length) if lbn_phys(start + i, side, ds) in flagged]

def sniff(path):
    """Container format from the leading bytes / size — never the file name."""
    with open(path, "rb") as f:
        head = f.read(16)
    if head == b"EXTENDED CPC DSK":
        return "extended-cpc"
    if head[:2] in (b"TD", b"td"):
        return "teledisk"
    return "raw"

def badmap_set(path):
    """Load a converter .badmap (1 byte per physical sector) into a set of
    flagged physical-block indices, or None if absent."""
    if not path.exists():
        return None
    return {i for i, x in enumerate(path.read_bytes()) if x}

# ── Filename normalisation ──────────────────────────────────────────────────
# Three explicit name aliases.  An earlier rule globally rewrote .EXE -> .SAV
# at ingest, on the theory that .EXE is just RT-11's alias extension for .SAV.
# But on the ARCSAV/disk4 cluster .EXE is the convention for an ENTIRE
# parallel toolchain — its LINK.SAV is configured to emit .EXE outputs, its
# MACRO/PIP/DUP are different binaries from the .SAV-default ones on
# h0/PAPER/etc.  Of 43 ARCSAV files only 22 were sha-identical to the .SAV
# namesakes elsewhere; the other 21 were genuinely different programs.
# Conflating them all into one .SAV namespace bundled 21 distinct programs
# into AMBIGUOUS groups misleadingly, while only adding 2 files to GUARANTEED
# (UDAW/ZASTM).  The 22 truly-identical pairs merge cleanly through the
# normal sha-dedup path: one corpus record gets both names in its `names`
# list and consensus.canonical_name() picks the .SAV form.
#
# DIRRT/PIPRT/DUPRT.EXE on the other hand are documented basename renames —
# same binaries as DIR/PIP/DUP.SAV under a different filename on some
# Rodionov-era disks — kept here.
NAME_ALIASES = {
    "DIRRT.EXE": "DIR.SAV",
    "PIPRT.EXE": "PIP.SAV",
    "DUPRT.EXE": "DUP.SAV",
}

def alias_name(name):
    return NAME_ALIASES.get(name.upper(), name)

DAT_RE = re.compile(r"_crc_error_Head(\d+)_Track(\d+)_Sector(\d+)_", re.I)

# Koshka .log line: "Head N, Track N, sector N, probe|retry K, error CODE - desc"
# CODE = Win32 system error from Simon Owen's fdrawcmd.sys (0 = success,
# 27 = ERROR_SECTOR_NOT_FOUND, 23 = ERROR_CRC, ...).  cp866-encoded.
LOG_RE = re.compile(
    r"Head\s*(\d+)\s*,\s*Track\s*(\d+)\s*,\s*sector\s*(\d+)\s*,\s*"
    r"(?:probe|retry)\s*\d+\s*,\s*error\s*(\d+)",
    re.I)

def dat_readstatus(src, ds):
    """Read-status + recovered content from sibling per-sector re-reads
    (<stem>_crc_error_Head_Track_Sector_*.dat).  Returns (flagged_set, overlay),
    where overlay maps a physical-block index to the majority-voted bytes across
    the re-read attempts (max info: METHODOLOGY Step 5).  (None, None) if the
    disk has no re-reads."""
    dats = list(src.parent.glob(src.stem + "_crc_error_*.dat"))
    if not dats:
        return None, None
    bysec = defaultdict(list)
    for d in dats:
        m = DAT_RE.search(d.name)
        if not m:
            continue
        h, t, s = int(m.group(1)), int(m.group(2)), int(m.group(3))
        idx = t*20 + h*10 + (s-1) if ds else t*10 + (s-1)
        blk = d.read_bytes()[:512]
        if len(blk) == 512:
            bysec[idx].append(blk)
    flagged = set(bysec)
    overlay = {idx: Counter(reads).most_common(1)[0][0] for idx, reads in bysec.items()}
    return flagged, overlay

def koshka_readstatus(src, ds):
    """Read-status from Koshka (anasana) sibling files: <stem>.map gives one
    ASCII byte per physical sector ('3' = OK, anything else = flagged; same
    index space as a converter .badmap), <stem>.log lists per-attempt
    fdrawcmd errors per (Head, Track, sector) in cp866 — a sector that never
    reports error 0 is flagged.  Returns the union as a physical-index set,
    or None if neither file is present.

    Code semantics for .map are provisional: only '3' is confirmed OK by
    anasana; other digits (seen: 1, 4, 5, 8) are treated as flagged.
    Refinement waits on the program's documentation.
    """
    flagged = set()
    found = False
    map_path = src.with_suffix(".map")
    if map_path.exists():
        flagged |= {i for i, b in enumerate(map_path.read_bytes()) if b != ord('3')}
        found = True
    log_path = src.with_suffix(".log")
    if log_path.exists():
        text = log_path.read_bytes().decode("cp866", errors="replace")
        per_sec = defaultdict(set)
        for m in LOG_RE.finditer(text):
            h, t, s, err = int(m[1]), int(m[2]), int(m[3]), int(m[4])
            idx = (t*20 + h*10 + (s-1)) if ds else (t*10 + (s-1))
            per_sec[idx].add(err)
        for idx, errs in per_sec.items():
            if 0 not in errs:
                flagged.add(idx)
        if per_sec:
            found = True
    return flagged if found else None

def raw_images_for(src, tmp):
    """Return [(image_path, [sides], flagged_set_or_None)] for a capture.

    Format is decided by CONTENT, not the file name.  `flagged_set` is the set
    of physical-block indices known bad for that image (Extended-CPC ST1/ST2,
    TeleDisk flags, or sibling .dat re-reads); None means no read-status.  For a
    raw disk with .dat re-reads the returned image is a temp copy with the
    majority-voted sectors overlaid (recovered content)."""
    def _union(*sets):
        live = [s for s in sets if s is not None]
        return set().union(*live) if live else None

    fmt = sniff(src); sz = src.stat().st_size
    if fmt == "raw" and sz in (SS, DS):
        ds = sz == DS
        dat_flagged, overlay = dat_readstatus(src, ds)
        ksh_flagged = koshka_readstatus(src, ds)
        flagged = _union(dat_flagged, ksh_flagged)
        img = src
        if overlay:
            img = tmp / src.name
            data = bytearray(src.read_bytes())
            for idx, b in overlay.items():
                data[idx*512:(idx+1)*512] = b
            img.write_bytes(data)
        return [(img, [0, 1] if ds else [0], flagged)]
    if fmt == "teledisk":
        work = tmp / src.name; shutil.copy2(src, work)
        subprocess.run([PY, str(HERE/"convert_teledisk.py"), str(work)], capture_output=True)
        out = work.with_name(work.stem + "_td0.dsk")
        if not out.exists():
            return []
        ds = out.stat().st_size == DS
        flagged = _union(badmap_set(work.with_name(work.stem + "_td0.badmap")),
                         koshka_readstatus(src, ds))
        return [(out, [0, 1] if ds else [0], flagged)]
    if fmt == "extended-cpc":
        work = tmp / src.name; shutil.copy2(src, work)
        subprocess.run([PY, str(HERE/"convert_samdisk.py"), str(work)], capture_output=True)
        # Koshka files (if any) sit alongside the ORIGINAL .dsk; their flag
        # indices share the per-side track*10+sec space, so they layer cleanly
        # on top of the converter's per-side .badmap.
        ksh = koshka_readstatus(src, ds=False)
        return [(p, [0], _union(badmap_set(p.with_suffix(".badmap")), ksh))
                for p in sorted(work.parent.glob(work.stem + "_s*.img"))]
    return []

def spanning_candidate(imgs):
    """Build one track-interleaved 819200 image (+ its physical flagged set) for
    a DS-spanning volume that ms0515-disk can't read.  A converted DS image
    (raw/TeleDisk) is used directly; an Extended-CPC arrives as two per-side
    409600 images which are interleaved by cylinder (and their per-side bad-maps
    merged into one physical set).  Returns (image_bytes, flagged_set_or_None)
    or None."""
    ds = [(i, f) for i, _, f in imgs if i.stat().st_size == DS]
    if ds:
        img, flagged = ds[0]
        return img.read_bytes(), flagged
    ss = sorted((p, f) for p, s, f in imgs if p.stat().st_size == SS)
    if len(ss) == 2:                              # Extended-CPC: _s0 + _s1
        (p0, f0), (p1, f1) = ss
        s0, s1 = p0.read_bytes(), p1.read_bytes()
        merged = bytearray(DS)
        for cyl in range(80):
            merged[(cyl*2)*5120:(cyl*2)*5120+5120]   = s0[cyl*5120:(cyl+1)*5120]
            merged[(cyl*2+1)*5120:(cyl*2+1)*5120+5120] = s1[cyl*5120:(cyl+1)*5120]
        flagged = None
        if f0 is not None or f1 is not None:
            flagged = set()
            for head, sset in ((0, f0), (1, f1)):
                for idx in (sset or ()):
                    t, sec = divmod(idx, 10)       # per-side track*10+sec
                    flagged.add(t*20 + head*10 + sec)   # -> physical block
        return bytes(merged), flagged
    return None

def get_side(img, side):
    with tempfile.TemporaryDirectory() as td:
        r = subprocess.run([str(TOOL), "get", str(img), "--side", str(side),
                            "--out", td], capture_output=True, text=True)
        files = {p.name: p.read_bytes() for p in Path(td).iterdir() if p.is_file()}
        return files if (files or r.returncode == 0) else None

PAIR_RE = re.compile(r"^(.*)_(?:Head|S|s)([01])$")

def head_pairs(sources):
    """Group raw sources stored as separate physical sides (`*_Head0/_Head1`)
    by their base name, so a spanning volume split across two files can be
    rejoined."""
    g = defaultdict(dict)
    for p in sources:
        if p.suffix.lower() in (".dsk", ".raw"):
            m = PAIR_RE.match(p.stem)
            if m:
                g[(p.parent, m.group(1))][int(m.group(2))] = p
    return g

def main():
    if not TOOL.exists():
        sys.exit(f"ms0515-disk not built at {TOOL} — build src/ first")
    sources = sorted(p for p in WORK.rglob("*")
                     if p.is_file() and p.suffix.lower() in (".dsk", ".raw", ".td0")
                     and "corpus" not in p.parts)

    OUT.mkdir(exist_ok=True)
    store = OUT / "files"; store.mkdir(exist_ok=True)   # content store: sha -> bytes
    corpus, flagged, cap_fp = {}, [], {}   # cap_fp: capture -> directory fingerprint

    def dir_sig(entries):
        """Fingerprint a volume's DIRECTORY (ordered files + start blocks + sizes),
        as the layout-correct tool reads it.  Two captures of one physical disk
        share this even if bad blocks read differently; different disks differ."""
        items = sorted((side, name, start, length)
                       for (side, name), (start, length) in entries.items())
        return hashlib.sha256(repr(items).encode()).hexdigest()[:16]

    def ingest(name, data, rel, side, bad=None, status=False):
        h = hashlib.sha256(data).hexdigest()
        if h not in corpus:
            corpus[h] = {"sha": h, "names": [], "size": len(data),
                         "blocks": len(data)//512, "category": categorize(name),
                         "provenance": []}
            (store / f"{h}.bin").write_bytes(data)
        rec = corpus[h]
        if name not in rec["names"]:
            rec["names"].append(name)
        prov = {"capture": rel, "side": side, "name": name}
        if status:                    # this capture carries per-sector read-status
            prov["status"] = True     # -> blocks NOT in "bad" are confirmed clean
        if bad:                       # file-block indices on a flagged sector
            prov["bad"] = bad
        rec["provenance"].append(prov)

    # Rejoin spanning volumes split into separate per-side files (raw
    # _Head0/_Head1 that don't read individually) — interleave and read spanning.
    consumed = set()
    for (parent, base), sd in head_pairs(sources).items():
        if set(sd) != {0, 1}:
            continue
        p0, p1 = sd[0], sd[1]
        if not (sniff(p0) == "raw" == sniff(p1)
                and p0.stat().st_size == SS == p1.stat().st_size):
            continue
        if get_side(p0, 0) or get_side(p1, 0):     # genuine independent sides — leave alone
            continue
        cand = spanning_candidate([(p0, [0], None), (p1, [0], None)])
        if not cand:
            continue
        res = read_spanning(cand[0])
        if not res:
            continue
        _, files, entries, _ = res
        rel = str((parent / f"{base}_Head0+1").relative_to(WORK)).replace("\\", "/")
        for name, start, length in entries:
            ingest(alias_name(name), files[name], rel, "span")
        cap_fp[rel] = dir_sig({("span", n): (s, l) for n, s, l in entries})
        consumed |= {p0, p1}

    with tempfile.TemporaryDirectory() as tmpd:
        tmp = Path(tmpd)
        for src in sources:
            if src in consumed:
                continue
            rel = str(src.relative_to(WORK)).replace("\\", "/")
            imgs = raw_images_for(src, tmp)
            if not imgs:
                continue
            any_ok = False
            cap_entries = {}
            for img, sides, flagset in imgs:
                status = flagset is not None
                ds_img = img.stat().st_size == DS
                for s in sides:
                    files = get_side(img, s)
                    if not files:
                        continue
                    any_ok = True
                    dents = dir_entries(img, s)
                    for n, (start, length) in dents.items():
                        cap_entries[(s, n)] = (start, length)
                    for name, data in files.items():
                        bad = None
                        if status and name in dents:
                            start, length = dents[name]
                            bad = flagged_blocks(start, length, s, ds_img, flagset)
                        ingest(alias_name(name), data, rel, s, bad, status)
            if any_ok and cap_entries:
                cap_fp[rel] = dir_sig(cap_entries)
            if not any_ok:
                # Fall back to the DS-spanning reader (one ~1600-block volume
                # across both sides) ms0515-disk can't read; link the spanning
                # capture's physical bad-map to each file's blocks.
                cand = spanning_candidate(imgs)
                if cand:
                    img_bytes, flagset = cand
                    res = read_spanning(img_bytes)
                    if res:
                        any_ok = True
                        _, files, entries, to_byte = res
                        status = flagset is not None
                        for name, start, length in entries:
                            bad = None
                            if status:
                                bad = [i for i in range(length)
                                       if to_byte(start + i)//512 in flagset]
                            ingest(alias_name(name), files[name], rel, "span", bad, status)
                        cap_fp[rel] = dir_sig({("span", n): (s, l) for n, s, l in entries})
            if not any_ok:
                flagged.append({"capture": rel,
                                "reason": "no readable directory (unknown layout)"})

    OUT.mkdir(exist_ok=True)
    records = sorted(corpus.values(), key=lambda r: (r["category"], r["names"][0]))
    (OUT / "corpus.json").write_text(
        json.dumps({"records": records, "flagged": flagged}, indent=1,
                   ensure_ascii=False), encoding="utf-8")
    # capture -> directory fingerprint; captures sharing one are the same physical
    # disk (used by verdict.py to count distinct disks for the GUARANTEED bar).
    (OUT / "captures.json").write_text(
        json.dumps(cap_fp, indent=1, ensure_ascii=False), encoding="utf-8")

    by_cat = defaultdict(int)
    for r in records:
        by_cat[r["category"]] += 1
    shared = sum(1 for r in records if len({p["capture"] for p in r["provenance"]}) > 1)
    print(f"captures: {len(sources)}   unique files: {len(records)}")
    print("by category:", dict(sorted(by_cat.items())))
    print(f"shared across >1 capture: {shared}")
    if flagged:
        print("flagged (need a spanning reader):")
        for f in flagged:
            print(f"  {f['capture']}")
    print(f"wrote {OUT/'corpus.json'}")

if __name__ == "__main__":
    main()
