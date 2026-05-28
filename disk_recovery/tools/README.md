# Recovery pipeline (Python)

The Python recovery/ingest layer, on top of the C++ format primitives
(`ms0515-disk`).  Format-level reading/writing lives in C++ (verified against
the OS); these scripts do the host-side orchestration: importing material,
converting foreign capture formats to the emulator's plain raw layout, and
building the unique-file corpus.  All working data goes under
`disk_recovery/work/` (gitignored); these scripts are the committed pipeline.

Paths are derived from the script location — no absolute paths.

## Tools

| Script | Job |
|--------|-----|
| `import_images.py <src-dir>...` | Pull images/archives from an external source into `work/data/`, deduplicated by content (recursively extracts .rar/.7z/.zip, skips anything already under `work/`). |
| `convert_extended_dsk.py <file.dsk>...` | Convert a SAMdisk **Extended-CPC DSK** container to per-side raw `_s0/_s1.img` + a per-side `.badmap` (read status from the FDC ST1/ST2 bytes). |
| `convert_td0.py <file.TD0>...` | Decode a Sydex **TeleDisk** image to a raw physical-sector image + a `.badmap` (read status from the TD0 sector flags). |
| `build_corpus.py` | Extract every readable diskette in `work/` via `ms0515-disk get`, hash (sha-256), and write `work/corpus/corpus.json`: one record per unique file with provenance (image+side) and a type-based category. |

## Typical flow

1. **import** new material: `python import_images.py <external-disks-dir>`.
2. **convert** anything not already a plain raw SS (409600) / track-interleaved
   DS (819200) image — Extended-CPC `.dsk` and `.TD0` — so `ms0515-disk` (and
   the emulator) can read it.  Two raw sides can be combined with
   `ms0515-disk merge` if a single double-sided image is wanted.
3. **build the corpus**: `python build_corpus.py`.

## Read-status (bad-maps)

Both capture formats record which sectors the controller flagged on read, and
both converters now emit a `.badmap` (one byte per sector, 0 = good, 1 =
flagged): TeleDisk from its per-sector flags, Extended-CPC from the FDC ST1/ST2
status bytes (CRC/data error, missing address mark, no data).  These maps are
the input to the consensus layer's natural-vs-lost-zero verdict (`METHODOLOGY.md`
Step 7) — they tell which sectors are trustworthy versus disputed.  Raw `.raw`
dumps carry no such metadata, so a disk's read-status comes from its TD0 /
Extended-CPC capture or from majority vote across per-sector re-reads (`.dat`).

## Notes / gaps

- The corpus categories (`system` / `exec` / `aux` / `text` / `other`) are a
  type hint only.  The real compatibility grouping is by **monitor
  generation**: even standard `.SAV` utilities version-check the monitor and
  refuse a mismatched one (see `../METHODOLOGY.md` and the OSA error messages),
  so files that travel together on a disk form a generation.  That grouping is
  derived from the corpus provenance (co-occurrence) — a later step.
- **DS-spanning** volumes (one ~1600-block filesystem across both sides:
  ARCSAV / superBAK7 / disk4 / disk5 family) are NOT readable by `ms0515-disk`
  (it reads SS / two-independent-side DS, not spanning).  `build_corpus.py`
  flags them; extracting their files needs a spanning-aware reader — still TODO.
- The heuristic recovery itself (per-byte consensus, donor gating, readability
  scoring, bit-rot classifier, TD0 natural-zero verdict) is specified in
  `../METHODOLOGY.md` and not yet implemented here.
