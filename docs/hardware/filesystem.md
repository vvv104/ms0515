# Filesystem — RT-11 on the MS0515

## Overview

The MS0515 runs several RT-11-compatible operating systems: **OSA**,
**Omega**, **Mihin (MihinSoft OS-16SJ)** and **rodionov (RT-15SJ /
ФОДОС)**.  The on-disk filesystem is standard RT-11 with a
hardware-specific physical layout: tracks 1..79 hold blocks 0..789 and
**track 0 wraps to the end** (blocks 790..799 for SS, 1580..1599 for
DS).  Sectors within a track are reordered depending on the OS driver:

- **canonical 2:1 sector interleave** — used for boot/home/dir
  metadata on every system.
- **+2 per-track skew on top of 2:1 interleave** — used for file
  bytes by OSA / Omega / Mihin drivers.
- **no sector interleave** — used by some rare drivers, including
  the DS-spanning filesystems on the ARCSAV/disk4/disk5/superBAK7
  family of disks.

All offsets below refer to a single-sided 409,600-byte image
(80 tracks × 10 sectors × 512 bytes) unless explicitly tagged as DS.

## Layout families observed in the wild

Four distinct LBN → physical-byte mappings have been observed across
~60 surviving MS0515 disks:

| Tag                  | Used on                                              |
|----------------------|------------------------------------------------------|
| `ss-canonical`       | rodionov (RT-15SJ); metadata of every other system   |
| `ss-osa-skew`        | OSA / Omega / Mihin file data                        |
| `ss-cyl0last-noil`   | superBAK7-style and rare RT-11 driver builds         |
| `ds-cyl0last-noil`   | ARCSAV / disk4 / disk5 / superBAK7 DS-spanning FS    |

Detection is by **boot-block hash** (matched against a small reference
table of disks of known provenance) plus structural heuristics: a
directory whose entries span beyond 800 blocks is necessarily
DS-spanning.

## Block-to-Byte formulas

Common notation: `N` = logical block number; `INT = [0, 2, 4, 6, 8, 1, 3, 5, 7, 9]`
is the 2:1 interleave table; `TRACK = 5120` (10 sectors × 512).

### `ss-canonical` — plain 2:1 + cyl-0-last

```
track   = (N // 10 + 1) % 80              # cyl 0 placed last
sector  = INT[N % 10]                     # 2:1 interleave
byte    = track * 5120 + sector * 512
```

This is the layout for **all metadata** (boot at LBN 0, home at
LBN 1, directory segments starting at LBN 6) on every MS0515 system,
which is why those parse correctly even if the file-area mapping
is something else.

It is also the layout for **all file data** on rodionov-family
(RT-15SJ) disks.

### `ss-osa-skew` — 2:1 + +2 sectors per track

OSA, Omega and Mihin drivers add a rotational skew on top of the
canonical 2:1 interleave: every additional track shifts the
sector-of-LBN-N by +2 sectors modulo 10.

```
track   = (N // 10 + 1) % 80
sector  = (INT[N % 10] + 2 * track - 2) % 10
byte    = track * 5120 + sector * 512
```

Track 1 has skew 0 so the formula reduces to plain canonical there —
this is the reason metadata at LBNs 0..6 still lands at canonical
byte positions.  At track 2+ the rotation grows.

Empirical derivation: a fresh INIT'd disk under each of OSA, Omega
and Mihin was loaded with PIP-written content from inside the
running OS (boot, INIT DZ1:, run PIP, type tagged content lines
from the keyboard, dump the resulting DZ1: image).  All three
monitors produced byte-identical placements matching the formula
above.

### `ss-cyl0last-noil` — cyl-0-last without interleave

Some disks were written by a driver that omits sector interleave:

```
track   = (N // 10 + 1) % 80
sector  = N % 10                          # no interleave
byte    = track * 5120 + sector * 512
```

Example: superBAK7 disks.

### `ds-cyl0last-noil` — double-sided spanning filesystem

A handful of disks store a single ~1600-block filesystem spanning
both sides.  After track-interleaved DS recombine (side-0 track,
side-1 track, side-0 track+1, side-1 track+1, …) the formula is:

```
cyl     = (N // 20 + 1) % 80              # cyl-0-last
head    = (N // 10) % 2                   # alternates sides per track
sector  = N % 10                          # no interleave
byte    = (cyl * 2 + head) * 5120 + sector * 512
```

Examples: ARCSAV, disk4, disk5, disk5-final, vvv104_disk5-final,
superBAK7.  Some of these are byte-identical duplicates of each
other under different recovery names (`_s0/_s1` vs `_Head0/_Head1`).

## Example mappings

For `ss-canonical` (block 0 = first track 1, sector 0):

| Block | Track | Logical | Physical | Byte offset | Purpose       |
|------:|------:|--------:|---------:|------------:|---------------|
|     0 |     1 |       0 |        0 |        5120 | Boot block    |
|     1 |     1 |       1 |        2 |        6144 | Home block    |
|     6 |     1 |       6 |        3 |        6656 | Directory     |
|    10 |     2 |       0 |        0 |       10240 | First data    |
|   790 |     0 |       0 |        0 |           0 | Track-0 wrap  |
|   799 |     0 |       9 |        9 |        4608 | Last block    |

For `ss-osa-skew` the differences start at track 2:

| Block | Track | INT[N%10] | + 2·track − 2 | Physical | Byte |
|------:|------:|----------:|--------------:|---------:|-----:|
|    10 |     2 |         0 |             2 |        2 | 11264 |
|    20 |     3 |         0 |             4 |        4 | 17408 |
|    30 |     4 |         0 |             6 |        6 | 23552 |

## Disk Layout

A formatted single-sided RT-11 disk has 800 usable blocks (tracks
1..79, 10 blocks per track).  Track 0 (the first 5120 bytes of the
image file) holds the last 10 blocks of the volume.

| Block(s)      | Contents                              |
|--------------:|---------------------------------------|
|             0 | Boot block (bootstrap loader code)    |
|             1 | Home block (volume parameters)        |
|           2-5 | Reserved (typically zero)             |
|             6 | Directory segment 1 (start)           |
|            7+ | Additional directory segments (if any)|
| `data_start`+ | File data area                        |

`data_start` is stored in the directory header; typical values
are 8 (tiny directory) or 14 (4 segments).

## Boot Block (LBN 0)

512 bytes of PDP-11 bootstrap code, loaded and executed by the
hardware ROM.  Non-bootable volumes contain a stub that prints
`?BOOT-U-No boot on volume`.

The first bytes are usually `a0 00 …` — a PDP-11 `NOP`-style
opcode used as a signature that the trampoline checks before
jumping in.

## Home Block (LBN 1)

Most of the 512-byte home block is zero.  Key fields are near the
end at standard RT-11 octal offsets (`0o722` and up):

| Hex offset | Size | Field          | Description                       |
|-----------:|-----:|:---------------|:----------------------------------|
| `0x1D2`    |    2 | cluster_size   | Allocation unit in blocks (usually 1) |
| `0x1D4`    |    2 | dir_start      | First directory block number      |
| `0x1D6`    |    2 | system_version | RAD50-encoded (e.g. "V05")        |
| `0x1D8`    |   12 | volume_id      | Volume label (KOI-8 / cp866)      |
| `0x1E4`    |   12 | owner_name     | Owner name (cp866, space-padded)  |
| `0x1F0`    |   12 | system_id      | System identification             |
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

Each directory **segment** occupies **2 blocks** (1024 bytes).
Segments are stored at LBNs `dir_start`, `dir_start + 2`,
`dir_start + 4`, …  Each segment can hold up to 72 entries (1024
header bytes ÷ 14 minus the 10-byte header).

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

The date word packs `(age << 14) | (month << 10) | (day << 5) | year`,
where `year` is a 5-bit offset from a base year encoded by the `age`
bits.  Year `0` is treated as invalid by `DATE` and refuses the
prompt.

### Status word values

```
E_TENT = 0o000400 = 0x0100   tentative (during creation)
E_MPTY = 0o001000 = 0x0200   empty (free space)
E_PERM = 0o002000 = 0x0400   permanent (normal file)
E_EOS  = 0o004000 = 0x0800   end of segment marker
E_READ = 0o000040 = 0x0040   read-only
E_PROT = 0o100000 = 0x8000   protected
```

The flags combine; e.g. `0x8400` = protected + permanent.

### File data location

Files are stored contiguously.  Block N of the volume directly
follows block N-1 (modulo the cyl-0-last wrap).  The starting block
of file `i` is:

```
file_start[i] = data_start + sum(length[0 .. i-1])
```

There is no free-block bitmap — the directory entries themselves
define which blocks are used.  `E_MPTY` entries describe runs of
free space.

## RAD50 Encoding

Filenames and extensions use Radix-50: 3 characters per 16-bit word.

Character set, indices 0..39:

```
 ABCDEFGHIJKLMNOPQRSTUVWXYZ$.?0123456789
```

(0 = space, 27 = `$`, 28 = `.`, 29 = `?`, 30..39 = `0`..`9`.)

Encoding: `word = c1*1600 + c2*40 + c3`
Decoding: `c3 = word % 40; c2 = (word // 40) % 40; c1 = word // 1600`

Filename limit is **6 chars + 3-char extension** — strictly enforced
by RT-11's CSI parser.  Longer names are rejected with
`?CSI-F-unknown command`.

## Image formats consumed by the emulator

- **409,600 bytes** — single-sided, raw byte-for-byte sector dump
  in physical order (track 0, sectors 1..10; track 1, sectors 1..10; …).
- **819,200 bytes, track-interleaved DS** — both sides of a physical
  disk, each track stored as `[side0-track-N][side1-track-N]`.  This
  is what 5.25" hardware readers (Catweasel, KryoFlux) and Soviet
  forum dumps produce.

The emulator's `fdc_attach` looks at file size; for the DS variant it
sets `track_stride = 2 * 5120` and offsets the side-1 logical units
(FD2/FD3) by 5120 into each track slot.

Layouts other than these two (LBN-linear flat 819,200, Extended CPC
DSK, TeleDisk TD0) are handled by an offline ingest step that
normalises them to physical byte-for-byte before the emulator sees
them.

## Sources

- RT-11 V5.6 Software Support Manual (AA-PD6LA-TC), Chapter 1.
- Empirical analysis across ~60 MS0515 disk dumps from collectors
  and a derive-by-emulation probe of OSA / Omega / Mihin write
  paths (PIP-from-keyboard inside each monitor).
