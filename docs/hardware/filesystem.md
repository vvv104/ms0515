# Filesystem — RT-11 on the MS0515 (OMEGA OS)

## Overview

The MS0515 runs OMEGA OS, which is a localized build of RT-11 (DEC).
The on-disk filesystem is standard RT-11 with one critical
hardware-specific detail: **2:1 sector interleave** within each track
and **track 0 placed last** (blocks 790-799 map to track 0).

This document describes the physical layout as observed on real
MS0515 floppy disks.  All offsets refer to a single-sided 409,600-byte
disk image (80 tracks x 10 sectors x 512 bytes).

## Block-to-Sector Mapping

The OS operates in terms of logical **blocks** (512 bytes each).
Blocks do not map sequentially to image sectors.  Two transformations
apply:

1. **Track 0 is placed last** — blocks 0-789 map to tracks 1-79,
   and blocks 790-799 wrap to track 0.
2. **2:1 sector interleave** reorders sectors within each track.

### Interleave table

Logical sector (0-based) to physical sector (0-based) within a track:

| Logical | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 |
|---------|---|---|---|---|---|---|---|---|---|---|
| Physical| 0 | 2 | 4 | 6 | 8 | 1 | 3 | 5 | 7 | 9 |

### Conversion formula

```
track          = (block / 10 + 1) % 80   (integer division; wraps to track 0)
logical_sector = block % 10              (0-based position within track)
physical_sector = INTERLEAVE[logical_sector]
image_sector   = track * 10 + physical_sector
byte_offset    = image_sector * 512
```

This gives 800 usable blocks across 80 tracks. Track 0 stores the last
10 blocks (790-799), which are typically free space or the tail of the
last file on a nearly-full disk.

Where `INTERLEAVE = [0, 2, 4, 6, 8, 1, 3, 5, 7, 9]`.

### Example mappings

| Block | Track | Logical | Physical | Image sector | Byte offset | Purpose        |
|------:|------:|--------:|---------:|-------------:|------------:|----------------|
|     0 |     1 |       0 |        0 |           10 |        5120 | Boot block     |
|     1 |     1 |       1 |        2 |           12 |        6144 | Home block     |
|     6 |     1 |       6 |        3 |           13 |        6656 | Directory      |
|     8 |     1 |       8 |        7 |           17 |        8704 | First data     |
|     9 |     1 |       9 |        9 |           19 |        9728 | (data cont.)   |
|    10 |     2 |       0 |        0 |           20 |       10240 | (data cont.)   |
|   790 |     0 |       0 |        0 |            0 |           0 | Track 0 wrap   |
|   799 |     0 |       9 |        9 |            9 |        4608 | Last block     |

Note: sectors within each track follow the pattern odd-even-odd — the
head reads physical sector 0, skips 1, reads 2, skips 3, ... then
wraps around to read 1, 3, 5, 7, 9.  This gives the CPU time to
process each sector before the next one arrives under the head.

## Disk Layout

A formatted single-sided disk has 800 blocks (tracks 1-79,
10 blocks per track).  Track 0 (sectors 0-9 in the image file) is
unused and zero-filled.

| Block(s) | Contents                              |
|---------:|---------------------------------------|
|        0 | Boot block (bootstrap loader code)    |
|        1 | Home block (volume parameters)        |
|      2-5 | Reserved (typically zero)              |
|        6 | Directory segment 1 (start)           |
|      7+  | Additional directory segments (if any)|
| data_start.. | File data area                   |

The `data_start` value (stored in the directory header) indicates
where file data begins.  Typical values: 8 (small directory) or 14.

## Boot Block (Block 0)

512 bytes of PDP-11 bootstrap code.  Loaded and executed by the
hardware bootstrap ROM.  On non-bootable volumes, contains a stub
that prints "No boot on volume".

## Home Block (Block 1)

Most of the 512-byte home block is zero.  Key fields are near the
end, at the standard RT-11 offsets (octal word addresses 0o722+):

| Offset | Size | Field           | Description                      |
|-------:|-----:|:----------------|:---------------------------------|
| 0x1D2  |    2 | cluster_size    | Allocation unit in blocks (usually 1) |
| 0x1D4  |    2 | dir_start       | First directory block number     |
| 0x1D6  |    2 | system_version  | RAD50-encoded (e.g. "V05")       |
| 0x1D8  |   12 | volume_id       | Volume label (ASCII / KOI-8)     |
| 0x1E4  |   12 | owner_name      | Owner name (ASCII, space-padded) |
| 0x1F0  |   12 | system_id       | System identification (ASCII)    |
| 0x1FC  |    4 | (reserved)      | Zeros                            |

## Directory Format

### Segment header (10 bytes)

| Offset | Size | Field        | Description                        |
|-------:|-----:|:-------------|:-----------------------------------|
|      0 |    2 | total_segs   | Total number of directory segments |
|      2 |    2 | next_seg     | Next segment number (0 = last)     |
|      4 |    2 | highest_seg  | Highest segment in use             |
|      6 |    2 | extra_bytes  | Extra bytes per directory entry     |
|      8 |    2 | data_start   | First data block number            |

Each directory segment occupies **2 blocks** (1024 bytes).  Segments
are stored at blocks `dir_start`, `dir_start + 2`, `dir_start + 4`,
etc.

### Directory entry (14 + extra_bytes bytes)

| Offset | Size | Field   | Description                           |
|-------:|-----:|:--------|:--------------------------------------|
|      0 |    2 | status  | File status word (see below)          |
|      2 |    2 | name1   | Filename chars 1-3 (RAD50)            |
|      4 |    2 | name2   | Filename chars 4-6 (RAD50)            |
|      6 |    2 | ext     | Extension chars 1-3 (RAD50)           |
|      8 |    2 | length  | File length in blocks                 |
|     10 |    1 | job     | Job/channel (usually 0)               |
|     11 |    1 | date_hi | Creation date (high bits)             |
|     12 |    2 | date_lo | Creation date (low bits)              |

### Status word values

| Value  | Meaning                                |
|-------:|:---------------------------------------|
| 0x0200 | Empty (free space)                     |
| 0x0400 | Permanent (normal file)                |
| 0x0800 | End of segment marker                  |
| 0x8400 | Permanent + protected (read-only file) |

Bit 15 (0x8000) = protected flag.  Can be combined with other status
values (e.g., 0x8400 = protected permanent file).

### File data location

Files are stored contiguously.  The first file starts at block
`data_start`.  Each subsequent file follows immediately after the
previous one.  The starting block of file N is:

```
file_start = data_start + sum(length[0] .. length[N-1])
```

There is no block allocation bitmap — the directory entries themselves
define which blocks are used and which are free.

## RAD50 Encoding

Filenames and extensions are encoded in Radix-50, a compact character
set that packs 3 characters into one 16-bit word.

Character set (index 0-39):

```
 ABCDEFGHIJKLMNOPQRSTUVWXYZ$. 0123456789
```

(Index 0 = space, 27 = `$`, 28 = `.`, 29 = `%`, 30-39 = `0`-`9`)

Encoding: `word = char1 * 1600 + char2 * 40 + char3`

Decoding: `char3 = word % 40; word /= 40; char2 = word % 40; char1 = word / 40`

A filename like `VC    .SAV` is stored as:
- name1 = RAD50("VC ") = 22*1600 + 3*40 + 0 = 35320 = 0x89F8
- name2 = RAD50("   ") = 0*1600 + 0*40 + 0 = 0x0000
- ext   = RAD50("SAV") = 19*1600 + 1*40 + 22 = 30462 = 0x76FE

## Sources

- RT-11 V5.6 Software Support Manual (AA-PD6LA-TC), Chapter 1
- Empirical analysis of formatted MS0515 floppy disks (OMEGA OS)
