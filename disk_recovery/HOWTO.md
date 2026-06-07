# How to use the recovery pipeline

Step-by-step walkthrough from an empty `work/` to a reviewed set of files
in the GUI. The numbers and outputs below come from a live end-to-end run
on a single small image (`Buhgal.dsk`, 409600 B, 7 files) — use them as a
sanity check while learning. For the *why*, see `METHODOLOGY.md`; for the
trust model, `README.md`; for what each script does in detail,
`tools/README.md`.

All scripts are pure Python (stdlib only) and derive their paths from the
script location. Run them from anywhere; outputs land under
`disk_recovery/work/` (gitignored). Re-runs are idempotent.

## 0. One-time setup

Build the C++ format tools used by the Python layer:

```
conan build src --build=missing
```

That produces `src/build/Release/tools/disk/ms0515-disk.exe` — the
Python `build_corpus.py` shells out to it to extract files from RT-11
volumes. No further build is needed for the GUI itself (stdlib tkinter).

## 1. Put material into work/data/

The pipeline walks `work/` recursively. Anywhere under `work/data/` is
fine; the relative path becomes part of the disk label later (e.g. a
file at `work/data/src/test/Buhgal.dsk` shows up under the disk label
`data/src/test/Buhgal` in the export tree and the GUI's disk filter).

Two ways:

a. **Manual** — just copy / drop files under `work/data/`. Works (used
   in the verification run for this guide). No magic needed.

b. **Dedupe importer** (recommended for archives — recursively unpacks
   `.rar`/`.7z`/`.zip` and skips by md5):

   ```
   python disk_recovery/tools/import_images.py <external-folder>
   ```

   Loose files land under `work/data/src/<relative>/`; archive contents
   under `work/data/arc/<archive-name>/`. Re-running adds only what's
   genuinely new. The source tree is left untouched.

### What counts as a usable capture

The pipeline accepts these capture formats:

| Format | File | Carries read-status? |
|--------|------|----------------------|
| Plain raw single-sided | 409600 B `.img` / `.dsk` / `.raw` | No (unless `.dat` siblings, below) |
| Plain raw double-sided | 819200 B `.img` / `.dsk` / `.raw` | No (unless `.dat` siblings) |
| SAMdisk Extended-CPC | `.dsk` (variable size, "EXTENDED CPC DSK" header) | Yes — ST1/ST2 status bytes |
| Sydex TeleDisk | `.TD0` / `.td0` | Yes — per-sector flags |
| Split per-side raws | `<base>_Head0.<ext>` + `<base>_Head1.<ext>` | No (unless `.dat` siblings) |

**Don't** put recovered or merged outputs back in — only original
physical captures. A merge already baked in decisions that may be
wrong; re-importing it contaminates the consensus. See `PITFALLS.md`
§1, §6.

### Read-status siblings (drop these next to the raw image)

`build_corpus.py` picks up several sibling files that travel with a raw
disk image and turn an otherwise unverifiable raw into per-sector
verified material. All three live in the same directory as the
`<stem>.dsk` / `.raw` file:

| Sibling | Origin | What it adds |
|---------|--------|--------------|
| `<stem>.map` | Koshka (anasana) | One ASCII digit per physical sector. `'3'` (good), `'4'` (OK with warnings), `'9'` (changed to OK after re-read) count as read OK; `'0'` (unprocessed), `'1'` (in progress), `'2'` (formatted/no data), `'5'` (bad fatal), `'6'` (CRC error), `'7'` (not found), `'8'` (unknown/user) all flag the sector. Same index space as a `.badmap`. Full per-sector coverage. |
| `<stem>.log` | Koshka | Per-attempt error log in cp866, lines like `Head 0, Track 28, sector 10, retry 4, error 27 - <description>`. Codes are Win32 system errors from `fdrawcmd.sys` (0 = success, 27 = `ERROR_SECTOR_NOT_FOUND`, 23 = `ERROR_CRC`, ...). A sector that never reports `error 0` is flagged. |
| `<stem>_crc_error_Head<N>_Track<N>_Sector<N>_<timestamp>.dat` | KryoFlux per-sector re-reads | Each file is a single 512-byte attempted re-read of a CRC-flagged sector. Multiple attempts on the same sector are **majority-voted** byte-by-byte to recover the bytes; their presence also flags the sector. |

So for an image read with Koshka (anasana's `RX50_DZ_koshka` /
[forum thread](https://zx-pk.ru/threads/28146-koshka.html)) and topped
up with KryoFlux re-reads, you'd have:

```
work/data/.../amk_1.dsk          # the image
work/data/.../amk_1.map          # Koshka per-sector status
work/data/.../amk_1.log          # Koshka per-attempt error log
work/data/.../amk_1_crc_error_Head0_Track32_Sector1_124097D1.dat
work/data/.../amk_1_crc_error_Head0_Track40_Sector1_36F4A0B5.dat
...
```

The pipeline UNIONs all flagged-sector sets from `.map`, `.log` and any
`.dat` siblings into one per-image read-status. Wrong path or wrong
name = the sibling is silently ignored, and the image falls back to
"no read-status" (every band lands in UNVERIFIED).

Note on `.map` digit semantics: only `'3'` is confirmed as OK by the
program's author. Other digits (1, 4, 5, 8 seen in the wild) are
treated as flagged — provisional until anasana documents the codes.

Same scheme works for the converter sources: a `<stem>.map` /
`<stem>.log` next to a `.TD0` or an Extended-CPC `.dsk` layers on top
of the converter's own `.badmap` from ST1/ST2 / TD0 flags.

## 2. Identify what you imported

```
python disk_recovery/tools/identify.py
```

Classifies every image in `work/` by **content** (signature/size/geometry),
never by name. Writes `work/corpus/formats.json` — a manifest of
format / geometry / RT-11-directory-readable / read-status-source per
file. Cheap to re-run after adding more material.

Real output from a one-image run:

```
images: 1
format:     {'raw': 1}
geometry:   {'ss': 1}
read_status: {'none': 1}

with read-status (recoverable error info): 0
unreadable here (no directory): 0
wrote ...work/corpus/formats.json
```

## 3. Convert foreign formats to raw (only if needed)

Two scripts turn captures `ms0515-disk` can't read directly into raw
images + a sidecar `.badmap` of CRC-flagged blocks. Skip if step 2 says
every image is already `'raw'`.

```
python disk_recovery/tools/convert_samdisk.py work/path/to/*.dsk
python disk_recovery/tools/convert_teledisk.py work/path/to/*.TD0
```

Each writes a per-side `<base>_s0.img` / `<base>_s1.img` + a per-side
`.badmap`. PowerShell users: brace-expand the path explicitly rather
than relying on `**/` globs.

If you have two raw per-side files and want a single double-sided
image:

```
src/build/Release/tools/disk/ms0515-disk.exe merge sideA sideB out_ds.img
```

(Not exercised in the verification run — we had a single SS raw — but
`tools/README.md` documents both.)

## 4. Read DS-spanning volumes (only if needed)

A DS-spanning floppy stores one ~1600-block RT-11 filesystem across
both sides as a single logical device — `ms0515-disk` can't read it
because the OS that wrote it treated the whole floppy as one drive,
which the MS-0515 FDC doesn't. The Python reader handles it:

```
python disk_recovery/tools/read_spanning.py work/path/to/<image>
```

`build_corpus` calls this internally for spanning volumes it detects;
running it standalone is rarely needed. (Not exercised in the
verification run.)

## 5. Build the corpus

```
python disk_recovery/tools/build_corpus.py
```

This is the heavy step. For every image in `work/`:

- Reads the RT-11 directory; for each file, extracts the bytes via the
  C++ `ms0515-disk` tool (or `read_spanning` for spanning volumes).
- SHA-256s the content; one entry per unique sha goes into the content
  store at `work/corpus/files/<sha>.bin`.
- Unifies read-status from any source — Extended-CPC ST1/ST2,
  TeleDisk flags, and majority-voted `.dat` re-reads — into one
  flagged physical-block set, then maps it to each file's LBN
  positions via the FDC geometry (per `src/core/src/floppy.c`).
- Writes `work/corpus/corpus.json` (unique-content file records with
  provenance) and `work/corpus/captures.json` (per-capture
  **directory fingerprint** — files + start blocks + sizes; used
  later to tell whether two captures are the same physical disk).

Real output:

```
captures: 1   unique files: 7
by category: {'exec': 2, 'other': 1, 'system': 4}
shared across >1 capture: 0
wrote ...work/corpus/corpus.json
```

After step 5 `work/corpus/` contains:

```
formats.json     captures.json     corpus.json     files/
```

Re-run after every new import / conversion. Idempotent.

## 6. Reconcile into builds (consensus)

```
python disk_recovery/tools/consensus.py
```

Regroups the corpus by **logical identity** (filename + block length,
not sha) and reconciles bit-rot copies within each cluster using the
three-state read-status model: CLEAN > UNKNOWN > FLAGGED (a raw
read can't cancel a flag from a status capture). For each logical
file it emits:

- A tier: `verified` / `recovered` / `corrupt` / `multi-version` /
  `single`.
- Per-file **builds**: one per cluster, with bit-rot already
  reconciled inside the cluster. Multi-version files get N builds —
  these are the genuine versions you'll compare in the GUI.

Writes `work/corpus/consensus.json`.

Real output (Buhgal — every file unique, no multi-version):

```
logical files (name+blocks): 7   (vs 7 sha-unique)
tiers: {'single': 7}
corrupt: 0  (text 0, binary 0)
coverage: 0 files have some read-status; 7 are raw-only
wrote ...work/corpus/consensus.json
```

## 7. Donor recovery (optional, for the lost blocks)

```
python disk_recovery/tools/donor.py
```

For each run of bad blocks in a file marked `corrupt`, uses the
neighbour good blocks as exact 512-byte anchors and searches the
whole corpus — content store + every disk's DE-SKEWED logical stream
(so an orphaned copy left in free space is contiguous) — for both
anchors bracketing the gap. Two exact anchors + plausibility gate
the match (`PITFALLS.md` §2). Confident two-sided hits go to
`work/corpus/donor_proposed/`; one-sided edge runs become flagged
leads.

Also runs a **second-copy hunt** for `UNVERIFIED` single-source
files: a full identical orphaned copy found elsewhere on another
disk's free space corroborates them.

Writes `work/corpus/donor.json`.

Real output (Buhgal — single disk, nothing to corroborate against):

```
=== LOST summary ===
0/0 LOST files fully recovered from in-corpus donors
=== second-copy hunt for 7 UNVERIFIED single-source files ===
  binary: 0 corroborated by a 2nd copy
  text: 0 corroborated by a 2nd copy
binary blind spot: 6 unverifiable -> 6 still need an external disk
wrote ...work/corpus/donor.json
```

## 8. Build the confidence matrix + per-disk export

```
python disk_recovery/tools/report.py
python disk_recovery/tools/export.py
```

- `report.py` reads `consensus.json` (+ optional `donor.json`) and
  bands every file via `verdict.py`:

  - **GUARANTEED** — byte-identical on >=2 DIFFERENT physical disks
    (strongest static evidence; "different" = different
    directory-fingerprint).
  - **HIGH** — identical across >=2 reads of one disk.
  - **GOOD** — reconciled clean from differing copies, or
    donor-recovered.
  - **MEDIUM** — single disk, every sector CRC-clean.
  - **UNVERIFIED** — single disk, no read-status, no 2nd copy
    (blind spot).
  - **AMBIGUOUS** — several distinct builds share the name;
    you must pick the canonical one.
  - **CHOSEN** — you picked a canonical version (see step 9).
  - **LOST** — bad on every copy; needs an external donor disk.

  Writes `REPORT.md`, `report.csv`, `healthy.txt`.

- `export.py` lays every file under `work/corpus/export/<disk>/`
  (one folder per physical disk; same disk's captures are merged via
  directory fingerprint). Each disk folder gets a `VERDICT.txt` with
  per-file band + plain-language verdict + action. The tree has a
  top `INDEX.txt`.

Real `INDEX.txt` from the Buhgal run:

```
DISK                               FILES  HEALTH (band counts)
------------------------------------------------------------------------------------------
data/src/test/Buhgal                   7  UNVERIFIED=7
```

Real `VERDICT.txt` excerpt:

```
disk: data/src/test/Buhgal
files: 7
  UNVERIFIED=7
VRFD = blocks byte-identical on >=2 different physical disks
FILE              BLK     VRFD  CAT     VERDICT
----------------------------------------------------------------------------------
AAUSER.MSH          1      0/1  other   UNVERIFIED — single disk, no read-status, no 2nd copy
DZ.SYS              3      0/3  system  UNVERIFIED — single disk, no read-status, no 2nd copy
KZARM.SAV           4      0/4  exec    UNVERIFIED — single disk, no read-status, no 2nd copy
...
```

Everything UNVERIFIED here is **the correct verdict for a single raw
SS dump with no .dat re-reads** — there's literally no way to check
it without another source. That is the baseline; bringing in a
second capture of the same disk (or another disk that happens to
carry the same files) is what lifts files out of UNVERIFIED.

## 9. Review and pick canonical versions

```
python disk_recovery/tools/review.py
```

Desktop GUI for the human-decision part of the workflow. Six tabs by
display band; selecting a file shows its distinct builds + the disks
each lives on, with a preview pane and the diff/merge tools:

- **disk filter** (top combobox) — restrict every tab to one
  physical disk's worth of files for per-floppy review.
- **type-ahead search** — start typing inside a tree to jump to
  the first file whose name matches; Esc clears, 1.5 s clears.
- **view as** (combobox above the preview pane) — `auto (KOI-7 /
  KOI-8R / KOI-8R+gfx / ASCII / binary→hex)` / `original` (Latin-1
  1:1) / `hex` (full hexdump).
- **Diff 2** — side-by-side compare of two selected versions, in
  text mode (line-aligned via difflib so a bit-rot region doesn't
  shift the rest, inline char-level highlight) or hex mode (with
  Disasm cmp and Zero analysis aids).
- **Byte-vote sel** — per-byte majority of versions you judge to
  be the same build (recovers scattered bit-rot).
- **Text-merge sel** — per-byte text-preferring merge for text
  files (takes the readable byte where another version has binary
  garbage).
- **Toggle canonical** — marks the selected version(s) as
  canonical for this file. Multiple canonicals are allowed (e.g.
  `RT11SJ.SYS` where several monitor builds are all legit).
  Writes `disk_recovery/decisions.tsv` so the pick survives across
  runs.

Headless model check (no window):

```
python disk_recovery/tools/review.py --selftest
```

Prints the band counts. Used by the verification run to confirm the
GUI reads the freshly-built corpus:

```
GUARANTEED     0
CHOSEN         0
HIGH           0
GOOD           0
MEDIUM         0
UNVERIFIED     7
AMBIGUOUS      0
LOST           0
```

A text-only alternative to the GUI:

```
python disk_recovery/tools/decide.py
```

Regenerates `decisions.tsv` with every ambiguous file + its
versions; you fill the `CHOOSE` column with a sha8 (or a disk
name) and re-run `report.py` / `export.py`.

Your picks live in `disk_recovery/decisions.tsv` (outside `work/`,
local to whoever runs the GUI — gitignored).  Deleting it resets all
canonical picks.  The GUI rewrites it on every toggle.

## 10. Iterate

The pipeline is incremental:

- Found a new capture? Drop it under `work/data/`, re-run
  `identify.py` (cheap), the relevant `convert_*.py` if foreign,
  then `build_corpus.py` → `consensus.py` → `donor.py` →
  `report.py` → `export.py`. The GUI reads `consensus.json` and
  `decisions.tsv` on launch.
- Found a fingerprint mismatch (two captures incorrectly grouped
  as one disk, or one disk split across two fingerprints)?
  Investigate the directory dump; the pipeline trusts content,
  never names (`PITFALLS.md` §4).

## Verified cheat sheet

The complete pipeline from empty `work/` (assuming `ms0515-disk`
is already built). Steps 3 and 4 only fire if there's actually
something to convert / span:

```
python disk_recovery/tools/identify.py                       # 2
python disk_recovery/tools/convert_samdisk.py  ...           # 3a (optional)
python disk_recovery/tools/convert_teledisk.py ...           # 3b (optional)
python disk_recovery/tools/read_spanning.py    ...           # 4  (optional)
python disk_recovery/tools/build_corpus.py                   # 5
python disk_recovery/tools/consensus.py                      # 6
python disk_recovery/tools/donor.py                          # 7
python disk_recovery/tools/report.py                         # 8
python disk_recovery/tools/export.py                         # 8
python disk_recovery/tools/review.py                         # 9 (GUI)
```

Files produced under `work/corpus/` (all verified by running the
above on `Buhgal.dsk`):

```
formats.json        step 2  — format/geometry per capture
corpus.json         step 5  — unique-file inventory + provenance
captures.json       step 5  — per-capture directory fingerprint
files/<sha>.bin     step 5  — content store
consensus.json      step 6  — logical files with reconciled builds
donor.json          step 7  — donor proposals + corroboration hits
donor_proposed/     step 7  — donor bytes for review
REPORT.md           step 8  — confidence matrix
report.csv          step 8  — full per-file table
healthy.txt         step 8  — files passing the trust bar
export/<disk>/      step 8  — per-disk browsable tree
export/INDEX.txt    step 8  — disk-level summary
```

Everything in `work/` is gitignored; so is
`disk_recovery/decisions.tsv` (per-operator canonical picks, local
state).
