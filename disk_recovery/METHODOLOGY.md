# Recovery Methodology

The general playbook for turning a pile of fallible floppy captures
into a set of files with a stated confidence level. Disk-agnostic.
Read [`README.md`](README.md) (trust model) and
[`PITFALLS.md`](PITFALLS.md) first.

The work is **file-centric, not image-centric**: a logical file (say
`K.PAS`) may exist on several physical disks and in several captures of
each. Each occurrence is an independent reading of the same bytes.
Recovery means consolidating all occurrences of each file into one
best version and saying how sure we are.

---

## Step 0 — Inventory every capture, establish independence

List every physical-level capture you have. Typical kinds:

| Kind | Notes |
|------|-------|
| `*.raw` | Raw byte-for-byte physical sector dump. **Canonical** — closest to the medium. |
| `*-final.dsk` | SAMdisk Extended-CPC-DSK output. Carries the same reads as `.raw` but in a container that can shift/duplicate sectors and inject "Track-Info" leakage. Sometimes holds rescue data the raw missed; never trust its framing blindly. |
| `*.TD0` | TeleDisk capture (often older, different reader/era). Stores per-sector flags (CRC-error, missing-data) — invaluable for the natural-zero vs lost verdict (Step 7). |
| per-sector reread `.dat` | Targeted re-reads of CRC-error sectors. A pile of these is a majority-vote goldmine for one bad sector. |
| sibling disks | Other disks that carry copies of the same files. |

For each capture record its **family**: which physical floppy it came
from, and whether it derives from another capture. Two captures of the
*same* floppy are **not independent** (see PITFALLS — circular donor).
Independence is what makes agreement meaningful.

Build a `sources.json`-style manifest: `{capture, family, tier, kind}`
where `tier ∈ {own, foreign}` per the trust model.

## Step 1 — Detect the layout of each capture

Identify, per side, which LBN→byte mapping the disk uses, so the
directory and file bytes can be read. Use both signals:

1. **Boot-block hash** against a reference table of known-provenance
   disks (the emulator ships OSA / Omega / Mihin / rodionov references).
   A match fixes both the directory layout and the (possibly different)
   file-data layout.
2. **Structural fallback** when the boot block is unknown or damaged:
   try each candidate mapping and keep the one whose directory segment
   chain validates and exposes the most `PERM` entries. Metadata (boot,
   home, directory) is at `ss-canonical` positions on *every* known
   MS-0515 variant, so the directory usually parses even when file data
   needs a different mapping.

A directory whose entries point past 800 blocks is **DS-spanning** —
the filesystem covers both sides as one ~1600-block volume and must be
read with `ds-cyl0last-noil`, not as two back-to-back SS volumes. (This
is the layout the legacy Python `rt11_dir.py` does *not* implement and
why it fails on those disks; the formula is in
[`../docs/hardware/filesystem.md`](../docs/hardware/filesystem.md).)

## Step 2 — Adjudicate raw vs final per capture

Where a disk has both `.raw` and `-final.dsk`, default to **raw** at
every disagreement: `-final` framing is often SAMdisk shift-corruption
rather than rescue data, even where it is non-zero. Only prefer `-final`
for a specific block when you can show it carries real content the raw
lost (e.g. raw sector is a known missing-data hole and final has a
CRC-valid payload there).

## Step 3 — Per-byte consensus across *independent* sources

For each logical file, gather every occurrence across all captures and
sibling disks, aligned by the file's logical block range. Then per byte:

- If all independent sources agree → accept, high confidence.
- If they disagree → **majority vote among independent sources only**
  (collapse same-family captures to one vote first, or a misread shared
  by a family wins by sheer count — see PITFALLS).
- On ties, fall through to the scoring metrics (Steps 4–5).

Never let the count be inflated by non-independent copies.

## Step 4 — Score candidates by *readability*, not by non-zero count

When choosing between byte/block candidates for text or source files,
score by **readable-byte count**: printable 7-bit ASCII **plus** valid
KOI-8R Cyrillic (0xC0–0xFF) and the common control bytes (CR, LF, TAB).
Do **not** rank by raw non-zero count — a block full of high-entropy
garbage has more non-zero bytes than a correct block that contains
runs of spaces/zeros, and the garbage would win. Readability is the
metric that distinguishes real text from noise.

For binary files (`.SAV`, `.EXE`, `.OBJ`) readability does not apply;
rely on cross-source agreement and donor matching instead.

## Step 5 — Classify each disagreeing block: bit-rot vs real patch

When a block differs between a candidate and a clean reference, decide
whether the difference is **decay** (fixable) or a **deliberate
difference** (must be preserved — e.g. an OEM customization, a different
file version):

- Compute, over the block: number of differing bytes `D`, and average
  number of *bits* flipped per differing byte `B`.
- **Bit-rot** if `D` is small **and** `B` is low — heuristic
  `D ≤ 30 AND B ≤ 2.5`. Magnetic decay flips individual bits, so a few
  bytes each differing by 1–2 bits is the rot signature. Replace with
  the clean-reference value.
- **Real difference** otherwise (many bytes, or high bits/byte = the
  blocks are simply different data). Preserve as-is; do not "fix".

Smoking-gun example of a 1-bit rot: `?KMON-` read back as `?KION-`
(`M`=0x4D → `I`=0x49, one bit). A single readable→broken transition on
an otherwise identical block is decay, and the readable side wins.

## Step 6 — Donor search for blocks no own-source can supply

For blocks lost across all own captures (all-zero / all missing-data),
look for the same content on another disk. A candidate donor must clear
**both** gates or it is contamination:

1. **High full-file match** — the donor's copy of the file matches the
   target's surviving blocks at **≥ 80%**. This proves it is the *same
   binary/version*, not a different file that merely shares common byte
   runs. (Aligning on a few common bytes without this check is how
   `MSDOS←FORTRA` and `183107←065` fake recoveries happened.)
2. **Independence** — the donor is not in the target's own family (not
   an earlier capture of the same floppy, not a derivative build). A
   foreign-tier donor additionally gets a lower-confidence flag on the
   recovered blocks.

Anchor-pair byte-pattern search (match a unique LEFT+RIGHT context,
~64 bytes, around the gap) locates the donor blocks; the uniqueness of
the anchor on the *target* disk is what prevents false placement.

## Step 7 — Zero blocks: natural vs lost (TD0 flag inspection)

A zero block in an extracted file is ambiguous: it could be genuine
file padding/BSS, or a lost sector zero-filled by the reader. Resolve
it with TeleDisk sector flags when a `.TD0` capture exists:

- flag `OK` + all-zero data + **valid CRC** → **natural zero**. The CRC
  of all-zeros is a specific non-zero value; a matching stored CRC
  cannot be faked by decay, so the medium genuinely held zeros.
- flag `CRC-error` + non-zero in TD0 + zero in the `.dsk` → **donor
  from TD0** (data survived in the TeleDisk, lost in conversion).
- flag `missing-data` + zero everywhere → **truly lost**.

Without a TD0, a zero block agreed by independent sources is probably
natural but cannot be *proven* so — record it as such.

## Output — confidence classification

Every recovered file lands in exactly one tier:

| Tier | Criterion |
|------|-----------|
| **100% (verified)** | Independent own-tier sources positively agree on every byte, **or** a single own-tier copy with no damage and TD0-confirmed natural zeros. |
| **recovered** | Damage in some copies, resolved by consensus / bit-rot fix / own-tier donor. High confidence, but reconstructed. |
| **foreign-donor** | Some blocks could only be supplied by a foreign-tier disk that cleared the donor bar. Plausible, flagged. |
| **residual loss** | Damaged in the same place across *all* available sources. Cannot reach 100% — record exactly which blocks are lost. Do not paper over with a guess. |

Produce a per-file matrix: `file, #independent copies, agreement %,
tier, lost blocks`. That matrix *is* the recovery result.

## Rebuild images last

Only after the file set is classified, rebuild bootable images by
writing the consensus files back through the correct layout for the
target OS. The images are derived artifacts; the classified files are
the authority. Images that pass re-verification (boot in the emulator +
files re-extract byte-identical) may be promoted into
[`authoritative/`](authoritative/).
