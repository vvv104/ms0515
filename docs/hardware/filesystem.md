# Filesystem — RT-11 on the MS0515

## Overview

The MS0515 runs several RT-11-compatible operating systems: **OSA**,
**Omega**, **Mihin (MihinSoft OS-16SJ)** and **rodionov (RT-15SJ /
ФОДОС)**.  The on-disk filesystem is standard RT-11.

Where a logical block (LBN) physically lands is defined by the emulator
**FDC** ([`../../src/core/src/floppy.c`](../../src/core/src/floppy.c))
plus the standard OS disk driver — that pair is the single source of
truth.  It is two layers:

1. **FDC storage geometry**, selected by image *size*:
   `byte = side*5120 + track*track_stride + (sector-1)*512`
   - **single-sided** (409,600 B): `track_stride = 5120`, one side.
   - **double-sided** (819,200 B): `track_stride = 2*5120`,
     **track-interleaved** — each cylinder stores side 0 then side 1
     back to back; side 1 is at `+5120`.  The two sides are **two
     independent 800-block RT-11 volumes**, not one spanning volume.

2. **OS driver `LBN → (track, sector)`** — the *same* mapping on every
   MS0515 diskette:
   ```
   track  = (N // 10 + 1) % 80              # cylinder 0 placed last
   sector = (INT[N % 10] + 2*track - 2) % 10  # 2:1 interleave + per-track skew
   INT    = [0, 2, 4, 6, 8, 1, 3, 5, 7, 9]
   ```
   Track 1 has skew 0, so LBN 0..9 (boot, home, directory, first data)
   coincide with a plain 2:1-interleave placement.  That is why metadata
   parses even under a wrong assumption, and why the file-data mapping
   **cannot be told from structure alone** (see PITFALLS — the
   canonical/skew ambiguity).

So a diskette is fully described by its **size** (SS/DS) and, for DS,
the **side**.  There is no per-OS "layout" choice: OSA / Omega / Mihin
(single-sided) and rodionov (double-sided) were each verified
**byte-for-byte** to use the mapping above — INIT a blank inside the
running OS, PIP a file onto it, dump the image and compare.

All offsets below refer to a single side's 409,600-byte volume unless
explicitly tagged DS.

## Historical "layout" tags (superseded)

Earlier (pre-FDC) recovery work catalogued several `LBN→byte` "layouts"
(`ss-canonical`, `ss-osa-skew`, `ss-cyl0last-noil`, `ss-cyl0first-noil`,
`ds-cyl0last-noil`).  Most were analysis artifacts:

- `ss-canonical` is just the **track-1 shadow** of the real mapping
  (skew is 0 on track 1).  It is not a distinct file-data layout on any
  verified diskette.
- The rodionov "`ss-cyl0last-noil`, directory at LBN 13" reading was a
  **DS-slicing artifact** — treating a track-interleaved 819,200 dump as
  two contiguous halves.  Read with the correct DS geometry, rodionov
  uses the standard mapping with the directory at **LBN 6**, identical to
  the other systems.
- `ss-osa-skew` *is* the standard mapping above (the "osa" in the old
  name is misleading — the skew is universal, not OSA-specific).

Two genuinely different, **non-diskette** container types exist and are
out of scope for the diskette geometry above:

- **DS-spanning volume** — a whole double-sided disk addressed as one
  ~1600-block RT-11 volume (not two independent sides).  A real format
  (ARCSAV / disk4 / disk5 family, per historical recovery) that the
  **emulator does not support**; it needs its own recombine and has not
  been re-verified under the FDC model.  A directory whose entries point
  past 800 blocks is a sign of this kind.
- **Logical-disk (LD) container** — a plain file living inside a
  diskette's filesystem, mounted by the OS's LD driver.  Same RT-11
  filesystem, but addressed **linearly** (`byte = N * 512`), any size
  that is a multiple of 512 and strictly smaller than a diskette
  (e.g. a 106 KB PROGRAMS sub-volume).  Not yet handled by the tooling.

## Block-to-Byte formula (diskette)

Common notation: `N` = logical block number; `INT = [0,2,4,6,8,1,3,5,7,9]`
is the 2:1 interleave table; sector and track per the driver mapping above.

**Single-sided:**
```
track  = (N // 10 + 1) % 80
sector = (INT[N % 10] + 2*track - 2) % 10
byte   = track * 5120 + sector * 512
```

**Double-sided**, side `S ∈ {0,1}` (track-interleaved storage):
```
byte   = S * 5120 + track * 10240 + sector * 512
```
(same `track`/`sector`; only the stride doubles and the side adds 5120).

Track 1 has skew 0, so metadata at LBN 0..9 lands at the plain-interleave
positions; the skew grows from track 2 on.

### Worked example (single-sided)

| Block | Track | `INT[N%10]` | `+2·track−2` | Sector | Byte offset | Purpose      |
|------:|------:|------------:|-------------:|-------:|------------:|--------------|
|     0 |     1 |           0 |            0 |      0 |        5120 | Boot block   |
|     1 |     1 |           2 |            0 |      2 |        6144 | Home block   |
|     6 |     1 |           3 |            0 |      3 |        6656 | Directory    |
|    10 |     2 |           0 |            2 |      2 |       11264 | First data   |
|    20 |     3 |           0 |            4 |      4 |       17408 | (track 3)    |
|    30 |     4 |           0 |            6 |      6 |       23552 | (track 4)    |

For the same volume as side 0 of a double-sided dump, multiply the track
term by 2 (stride 10240): LBN 6 → `0 + 1*10240 + 3*512 = 11776`,
LBN 1 (home) → `0 + 10240 + 2*512 = 11264`.

## Disk Layout

A formatted single-sided RT-11 volume has 800 usable blocks (tracks
1..79, 10 blocks per track).  Track 0 holds the last 10 blocks of the
volume (the cyl-0-last wrap).

| Block(s)      | Contents                              |
|--------------:|---------------------------------------|
|             0 | Boot block (bootstrap loader code)    |
|             1 | Home block (volume parameters)        |
|           2-5 | Reserved (typically blank)            |
|             6 | Directory segment 1 (start)           |
|           6+  | Additional directory segments         |
| `data_start`+ | File data area                        |

`data_start` is stored in the directory header.  A real RT-11 `INIT`
reserves **4 directory segments** (LBNs 6..13) so `data_start = 14`; a
minimal volume (e.g. an LD container) may use 1 segment with
`data_start = 8`.

## Boot Block (LBN 0)

512 bytes of PDP-11 bootstrap code, loaded and executed by the hardware
ROM.  A non-bootable volume (what `INIT` writes) contains a stub that
prints `?BOOT-U-No boot on volume`, then zeros.  The first bytes are
`A0 00 …` — a PDP-11 opcode the trampoline checks before jumping in.

## Home Block (LBN 1)

`INIT` writes a few leading bytes (`00 00 00 F0 FF 0F`), an `FF FF`
marker at `0x1C0`, and the identity/parameter fields near the end;
bytes it does not touch keep the blank `B6 6D` pattern.  Key fields at
the standard RT-11 octal offsets (`0o722` and up):

| Hex offset | Size | Field          | Description                       |
|-----------:|-----:|:---------------|:----------------------------------|
| `0x1D2`    |    2 | cluster_size   | Allocation unit in blocks (usually 1) |
| `0x1D4`    |    2 | dir_start      | First directory block number (6)  |
| `0x1D6`    |    2 | system_version | RAD50-encoded (e.g. "V05" = 0x8E53) |
| `0x1D8`    |   12 | volume_id      | Volume label (ASCII/KOI-8, space-padded) |
| `0x1E4`    |   12 | owner_name     | Owner name (space-padded)         |
| `0x1F0`    |   12 | system_id      | System identification ("DECRT11A") |
| `0x1FC`    |    2 | checksum       | Home-block checksum               |

## Directory Format

### Segment header (10 bytes)

| Offset | Size | Field        | Description                        |
|-------:|-----:|:-------------|:-----------------------------------|
|      0 |    2 | total_segs   | Total number of directory segments |
|      2 |    2 | next_seg     | Next segment number (0 = last)     |
|      4 |    2 | highest_seg  | Highest segment in use             |
|      6 |    2 | extra_bytes  | Extra bytes per directory entry    |
|      8 |    2 | data_start   | First data block number            |

Each directory **segment** occupies **2 blocks** (1024 bytes).  Segments
are at LBNs `dir_start`, `dir_start + 2`, `dir_start + 4`, …  A segment
holds up to 72 entries ((1024 − 10) ÷ 14).

### Directory entry (14 bytes + `extra_bytes`)

| Offset | Size | Field   | Description                           |
|-------:|-----:|:--------|:--------------------------------------|
|      0 |    2 | status  | File status word (see below)          |
|      2 |    2 | name1   | Filename chars 1-3 (RAD50)            |
|      4 |    2 | name2   | Filename chars 4-6 (RAD50)            |
|      6 |    2 | ext     | Extension chars 1-3 (RAD50)           |
|      8 |    2 | length  | File length in blocks                 |
|     10 |    2 | job/ch  | Channel/job (one word, usually 0)     |
|     12 |    2 | date    | Creation date (packed, see below)     |

The date word packs `(age << 14) | (month << 10) | (day << 5) | year`.

### Status word values

```
E_TENT = 0o000400 = 0x0100   tentative (during creation)
E_MPTY = 0o001000 = 0x0200   empty (free space)
E_PERM = 0o002000 = 0x0400   permanent (normal file)
E_EOS  = 0o004000 = 0x0800   end of segment marker
E_READ = 0o000040 = 0x0040   read-only
E_PROT = 0o100000 = 0x8000   protected
```

The flags combine; e.g. `0x8400` = protected + permanent.  `INIT` leaves
the free area as one `E_MPTY` entry named " EMPTY.FIL" followed by a
bare `E_EOS` marker.

### File data location

Files are stored contiguously.  The starting block of file `i` is
`data_start + sum(length[0 .. i-1])` (modulo the cyl-0-last wrap).  There
is no free-block bitmap — `E_MPTY` entries describe runs of free space.

**File length is in whole blocks only — there is no byte-exact length.**
The tail of a file's last block, from the logical end of content to the
512-byte boundary, is **NUL-padded** by the OS (verified on real text
command files).  See `../../disk_recovery/METHODOLOGY.md` for why this
matters to recovery (distinguishing authentic padding zeros from a
lost sector).

## RAD50 Encoding

Filenames and extensions use Radix-50: 3 characters per 16-bit word.
Character set, indices 0..39:

```
 ABCDEFGHIJKLMNOPQRSTUVWXYZ$.?0123456789
```

(0 = space, 27 = `$`, 28 = `.`, 29 = `?`, 30..39 = `0`..`9`.)

Encoding: `word = c1*1600 + c2*40 + c3`
Decoding: `c3 = word % 40; c2 = (word // 40) % 40; c1 = word // 1600`

Filename limit is **6 chars + 3-char extension**, and every character
must be in the RAD50 set above (letters, digits, `$`, `.`, `?`).  Names
that are longer or contain other characters (`-`, `_`, …) are rejected.

## Image formats consumed by the emulator

- **409,600 bytes** — single-sided, raw byte-for-byte sector dump in
  physical order (track 0, sectors 1..10; track 1, sectors 1..10; …).
- **819,200 bytes, track-interleaved DS** — both sides of a physical
  disk, each track stored as `[side0-track-N][side1-track-N]`.  This is
  what 5.25" hardware readers (Catweasel, KryoFlux) and Soviet forum
  dumps produce.

The emulator's `fdc_attach` looks at the file size; for the DS variant
it sets `track_stride = 2 * 5120` and offsets the side-1 logical units
(FD2/FD3) by 5120 into each track slot.

Other containers — a DS-spanning whole-disk volume, an LD container, an
LBN-linear flat dump, Extended-CPC DSK, or TeleDisk TD0 — are **not**
read directly: an offline ingest step normalises them to one of the two
forms above before the emulator (or the `ms0515-disk` tool) sees them.

## Sources

- RT-11 V5.6 Software Support Manual (AA-PD6LA-TC), Chapter 1.
- The emulator FDC (`src/core/src/floppy.c`) — authoritative for the
  physical geometry.
- Derive-by-emulation probe: INIT a blank inside OSA / Omega / Mihin /
  rodionov, PIP a file in, dump and compare — confirms the single
  `LBN → (track, sector)` mapping byte-for-byte for SS and DS.
