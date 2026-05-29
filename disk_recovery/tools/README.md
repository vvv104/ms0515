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
| `identify.py` | Classify every image in `work/` by its **content** (signature/size/geometry), never its name: format (extended-cpc / teledisk / raw), geometry (ss / ds-twosided / ds-spanning / ld-container), whether an RT-11 directory actually reads out, and what carries read-status (ST1/ST2, TD0 flags, `.dat` re-reads).  Writes a persistent manifest `work/corpus/formats.json` so the identification is never lost. |
| `convert_samdisk.py <file.dsk>...` | Convert a SAMdisk **Extended-CPC DSK** container to per-side raw `_s0/_s1.img` + a per-side `.badmap` (read status from the FDC ST1/ST2 bytes). |
| `convert_teledisk.py <file.TD0>...` | Decode a Sydex **TeleDisk** image to a raw physical-sector image + a `.badmap` (read status from the TD0 sector flags). |
| `read_spanning.py <image>` | Read a **DS-spanning** RT-11 volume (one ~1600-block filesystem across both sides), which `ms0515-disk` cannot.  Tries candidate LBN→byte mappings and keeps the one that structurally validates (confirmed against the corpus). |
| `build_corpus.py` | Extract every readable disk in `work/` (format by content; DS-spanning via `read_spanning`), hash (sha-256), and write `work/corpus/corpus.json` + a sha content store: one record per unique file with provenance and a type-based category.  Unifies read-status into a flagged physical-block set from ANY source — Extended-CPC ST1/ST2, TeleDisk flags, and sibling `.dat` re-reads (whose multiple attempts are majority-voted to overlay recovered sector bytes) — links it to each file's blocks (LBN→physical via the FDC/osa-skew map), and marks each occurrence `status` (capture had read-status) + `bad` (which file blocks were flagged).  Also writes `work/corpus/captures.json` — a per-capture **directory fingerprint** (ordered files + start blocks + sizes, as the tool reads them) used to tell whether two captures are the same physical disk. |
| `analyze_corpus.py` | Second pass over the corpus: group captures into **monitor-generation** families (by the `.SYS` monitor build), list version-split files, and analyse content (readable-byte fraction; printable strings for executables).  Writes `work/corpus/analysis.json`. |
| `consensus.py` | Regroup the corpus by **logical** identity (name + block length, not sha) and reconcile variants with a **three-state** per-block read-status — CLEAN (a status capture read it unflagged) > UNKNOWN (raw, no status) > FLAGGED (CRC-bad).  A statusless raw can't cancel a real flag, and the best tier wins per block; for text files a garbage block is never emitted while any copy is readable.  Per-block outcome = clean / unknown / flagged (binary corruption signal) / corrupt (text garbage everywhere).  Tiers = verified / recovered / corrupt / multi-version / single.  For each file it also emits the reconciled **builds** — one per cluster, with the bit-rot inside a cluster already resolved by read-status (a CRC-clean read beats a flagged one) — so `decide.py`/`review.py` present the genuine versions to choose among, not raw shas (a 1–5-byte bit-rot copy is not a separate version).  Writes `work/corpus/consensus.json` + a reconciled content store, and reports remaining text **and** binary corruption. |
| `report.py` | Turn `consensus.json` (+ `donor.json`) into a per-file **recovery-confidence matrix** using the shared model `verdict.py`: each file banded GUARANTEED (byte-identical on >=2 DIFFERENT physical disks) / HIGH (>=2 reads of one disk) / GOOD (reconciled or donor) / MEDIUM (single disk, CRC-clean) / UNVERIFIED (single disk, no check) / AMBIGUOUS (several builds) / LOST (bad on every copy).  Emits the **healthy** set + the GUARANTEED count.  Writes `work/corpus/REPORT.md`, `report.csv` (full matrix), `healthy.txt`. |
| `export.py` | Lay every recovered file into `work/corpus/export/<disk>/` grouped by **physical disk** (merging a disk's .raw / SAMdisk -final / TeleDisk / split-side captures).  Each file gets its best bytes (donor proposal > consensus reconstruction > this disk's own version for multi-version); each disk gets a `VERDICT.txt` (per-file band + plain-language verdict + action) and the tree has a top `INDEX.txt`.  Browsable: which disk, the actual bytes, and how trustworthy each file is. |
| `decide.py` | Manage `disk_recovery/decisions.tsv` — your manual pick of the canonical version for each AMBIGUOUS file.  Regenerates the file with every ambiguous file + its versions (sha8 @ disks), **preserving** any choices you already made; you fill the CHOOSE column (sha8 or a disk), comparing the bytes in `export/<disk>/<file>`.  `report.py`/`export.py` then read it: decided files move AMBIGUOUS -> CHOSEN and the picked version becomes canonical.  Lives outside `work/` so it survives a rebuild and can be committed (names + chosen sha only, no disk content). |
| `review.py` | Desktop GUI (tkinter, stdlib) to review recoveries and pick canonical versions.  Tabs = confidence bands; each lists its files; selecting one shows its versions and the disks each is on, with **View** (KOI-8R text or hex), **Diff 2** (unified text diff, or a byte-level binary diff listing every differing block + the exact offsets/bytes — bit-rot vs a real version is visible), **Byte-vote sel** (per-byte majority of the versions YOU judge to be the same build — recovers scattered bit-rot into a clean copy), **Text-merge sel** (per-block readable-preferring merge for text files — takes the text-block where another version has binary garbage), **Set canonical** (writes the pick to `decisions.tsv` → file moves to CHOSEN) and **Clear**.  Reads `decisions.tsv` on start so already-decided files are out of AMBIGUOUS.  `python review.py` (or `--selftest` for a headless model check). |
| `verdict.py` | Shared confidence model (bands + `classify`) used by `report.py` and `export.py`.  `physical_disks()` groups captures into physical disks by their **directory fingerprint** (`captures.json`) — same file list / order / start blocks = same disk, immune to bad-block read variance — so re-reads of one floppy (its .raw, -final, TeleDisk, split sides; even differently-named captures like SAVDOC=disk5, ARCSAV=disk4) never count as the cross-disk GUARANTEED bar. |
| `donor.py` | **Donor recovery** for the LOST blocks (METHODOLOGY Step 6): for each run of bad blocks, use the good neighbour blocks as exact anchors and search the whole corpus — the content store AND every disk's DE-SKEWED logical stream (LBN order, so an orphaned copy in free space is contiguous and findable) — for both anchors bracketing the gap.  Two exact 512-byte anchors + plausibility gate the match (PITFALLS #2).  Report-only: confident two-sided hits are written to `work/corpus/donor_proposed/<name>` for review; edge runs get a flagged low-confidence one-sided lead.  Then runs a **second-copy hunt** for UNVERIFIED single-source files: binaries can't be checked by content, so a full identical orphaned copy found elsewhere (incl. free space) is the only in-corpus way to corroborate them (or expose a discrepancy). |

## Typical flow

1. **import** new material: `python import_images.py <external-disks-dir>`.
2. **convert** anything not already a plain raw SS (409600) / track-interleaved
   DS (819200) image — Extended-CPC `.dsk` and `.TD0` — so `ms0515-disk` (and
   the emulator) can read it.  Two raw sides can be combined with
   `ms0515-disk merge` if a single double-sided image is wanted.
3. **build the corpus**: `python build_corpus.py`.

## Read-status (three sources)

A sector's read-status — did the controller flag it on read — comes from three
places, all unified by `build_corpus` into one flagged physical-block set per
image:

- **Extended-CPC** ST1/ST2 status bytes (CRC/data error, missing address mark,
  no data) → `.badmap` from `convert_samdisk`.
- **TeleDisk** per-sector flags → `.badmap` from `convert_teledisk`.
- **`.dat` re-reads** — sibling `<disk>_crc_error_Head_Track_Sector_*.dat` files
  are per-sector re-read attempts of CRC-flagged sectors on an otherwise
  statusless raw dump.  Their presence flags the sector; their multiple attempts
  are **majority-voted** to overlay recovered bytes into the extraction image.

A plain raw dump with none of the above carries no read-status: its blocks are
UNKNOWN, neither trusted nor flagged.  Crucially a raw read must never *cancel*
a flag raised by a status capture (PITFALLS #3) — `consensus.py` enforces this
with its three-state model.

## Notes / gaps

- The corpus categories (`system` / `exec` / `aux` / `text` / `other`) are a
  type hint only.  The real compatibility grouping is by **monitor generation**
  (even standard `.SAV` utilities version-check the monitor and refuse a
  mismatched one — see `../METHODOLOGY.md` and the OSA error messages), and is
  produced by `analyze_corpus.py` from the corpus provenance (co-occurrence).
- **DS-spanning** volumes are read by `read_spanning.py` and folded into the
  corpus by `build_corpus.py`.  The mapping differs per capture ordering
  (KryoFlux raw = cyl-0-last, some forum images = cyl-0-first); the reader picks
  the structurally-valid one.  A lone per-side half of a spanning disk
  (`*_Head0/_Head1`, an Extended-CPC of a spanning disk) is not a complete
  volume and is still flagged — its files come from the full 819200 capture.
- The collection holds **only original reads**.  Prior-recovery outputs
  (`*_authoritative.dsk` and the like) are not guarded against in code — they're
  simply not present (`work/recovered/` was removed).  Don't re-import derived
  builds (PITFALLS #1, #6).
- **Corruption ≠ disagreement.**  A file can be byte-identical across captures
  yet still corrupt, because a dead sector reads the same garbage every time
  (PITFALLS #3 — confirmed live: `BASICO.DOC` is identical on two captures and
  still has 11 garbage blocks).  `consensus.py` therefore reports a `corrupt`
  tier from CONTENT (text garbage on every copy) and from STATUS (binary blocks
  CRC-flagged on every copy, no clean read anywhere), independent of agreement.
- **Detectability caveat:** corruption can only be *detected* where read-status
  or a readable-text check applies.  Raw-only binaries with no second copy are
  unverifiable — they may be silently corrupt and the tool cannot tell; the
  summary prints how many files are in that blind spot.
- **DS-spanning read-status is linked.**  A spanning volume (one ~1600-block
  RT-11 filesystem addressed across both sides as a single device — the writing
  machine treats the floppy as one drive, which the MS-0515 FDC does not) is
  read by `read_spanning`; build_corpus maps each spanning file's blocks through
  the spanning mapping to the capture's physical bad-map, so TeleDisk flags
  (ARCSAV.TD0) and Extended-CPC ST1/ST2 (disk5-final, rebuilt by interleaving
  its two per-side images) now carry over.  Spanning files are also donors for
  the same files on normal disks.
- Still to do: donor recovery for the `corrupt` blocks — matching orphaned data
  in **free space** (e.g. a file's blocks left behind after an `INIT`) by
  surrounding-block context (anchor-pair search, METHODOLOGY Step 6).  Note some
  lost text (BASICO.DOC) is byte-identical across every capture with the dead
  sectors NOT CRC-flagged, so it needs an EXTERNAL donor disk, not a re-read.
