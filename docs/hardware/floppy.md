# Floppy — KR1818VG93 (WD1793) Disk Controller

## Overview

The MS0515 uses a KR1818VG93 (КР1818ВГ93), a Soviet clone of the Western
Digital WD1793 floppy disk controller.  It manages MFM-encoded data transfer
with 5.25" floppy drives.

## Disk Geometry

| Parameter       | Value                                    |
|-----------------|------------------------------------------|
| Tracks          | 80 (0–79)                                |
| Sides per drive | 2 (each side is a separate logical unit) |
| Sectors/track   | 10                                       |
| Bytes/sector    | 512                                      |
| Track size      | 5120 bytes (10 × 512)                    |
| Total capacity  | 400 KB per side (80 × 10 × 512)          |

## Drive Mapping

The BIOS treats each surface of each physical drive as a separate logical
unit.  The 2-bit unit index is written by the BIOS directly into bits 1:0
of System Register A; the emulator refers to these four slots as FD0..FD3.

| Logical name | Physical drive | Surface |
|--------------|----------------|---------|
| FD0          | 0              | Lower   |
| FD1          | 1              | Lower   |
| FD2          | 0              | Upper   |
| FD3          | 1              | Upper   |

(The OS-level driver name for the FDC on every observed MS0515 system —
OSA, Omega, Mihin, rodionov — is `DZ.SYS`.  Other two-letter device
names that appear in the same driver set are unrelated to the floppy:
`VM` is the built-in RAM disk, `VS` is the sound-playback driver,
`TT` is the console, `LD` is the file-backed logical-disk driver,
`EX` is the optional 512 KB expansion RAM disk, `LP` is the line
printer, and so on.  The `DZ` name is RT-11 / OSA convention, not a
hardware designation, and the emulator deliberately avoids it.)

The user-facing emulator CLI / YAML / GUI describe each unit as
"drive N, side M" (e.g. `--disk0-side0`, `disk0_side0:` in the YAML
config, "Disk N" with Mount image / Mount upper side in the File
menu).  That maps onto the
hardware naming above as

| User-facing       | Core unit |
|-------------------|-----------|
| disk 0, side 0    | FD0       |
| disk 0, side 1    | FD2       |
| disk 1, side 0    | FD1       |
| disk 1, side 1    | FD3       |

`--disk0` (and `--disk1`) mounts by the image's size: an 819200-byte
double-sided image goes on both side units of the drive (`fdc_attach`
picks the right offset for each side), a 409600-byte one on the lower
side.

## FDC Register Addresses

| Address | Read          | Write         |
|---------|---------------|---------------|
| 177640  | Status        | Command       |
| 177642  | Track         | Track         |
| 177644  | Sector        | Sector        |
| 177646  | Data          | Data          |

## Drive Control (System Register A — 177600)

Drive selection and motor control are handled through the PPI, not the FDC:

| Bit  | Function                                         |
|------|--------------------------------------------------|
| 1-0  | Physical drive select (0 or 1)                   |
| 2    | Motor on (active low: 0 = motor on)              |
| 3    | Side select: 1 = side 0 (the lower), 0 = side 1 (the upper) - the bit goes to the drive's side-select line unchanged (`board.md`, System Register A). |

The logical unit is computed as: `unit = side × 2 + drive`, where
`side = (reg_a & 0x08) ? 0 : 1` (active-low).

## FDC Status (System Register B — 177602)

| Bit | Function                                       |
|-----|------------------------------------------------|
| 0   | INTRQ inverted (0 = command complete/ready)    |
| 1   | DRQ (1 = data byte ready for transfer)         |
| 2   | Drive ready inverted (0 = drive ready)         |

The FDC does **not** use the CPU interrupt controller.  Instead, the CPU
polls the DRQ and INTRQ bits in System Register B during disk operations.

## WD1793 Command Summary

### Type I — Seek commands

| Command   | Code (hex) | Description                        |
|-----------|------------|------------------------------------|
| Restore   | 0x00–0x0F  | Seek to track 0                   |
| Seek      | 0x10–0x1F  | Seek to track in data register    |
| Step      | 0x20–0x3F  | Step in last direction            |
| Step In   | 0x40–0x5F  | Step toward center (track++)      |
| Step Out  | 0x60–0x7F  | Step toward edge (track--)        |

### Type II — Read/Write

| Command      | Code (hex) | Description                     |
|--------------|------------|---------------------------------|
| Read Sector  | 0x80–0x9F  | Read sector into data register  |
| Write Sector | 0xA0–0xBF  | Write sector from data register |

### Type III — Format/Address

| Command       | Code (hex) | Description                                          |
|---------------|------------|-------------------------------------------------------|
| Read Address  | 0xC0–0xCF  | The next ID field's track into the sector register (the BIOS head-position self-test, the handlers' first step) |
| Write Track   | 0xF0–0xFF  | Format the track under the head                       |

### Type IV — Control

| Command         | Code (hex) | Description                   |
|-----------------|------------|-------------------------------|
| Force Interrupt | 0xD0–0xDF  | Abort current command         |

## Timing: the turning diskette

The controller's commands take the machine's time (`floppy.c`): a
step is 6/12/20/30 ms by the command's rate bits, a data byte comes
every 32 µs (250 kbit/s MFM), WRITE TRACK runs from index to index,
6250 bytes.  Where a sector *is* on the turn is modelled when the
diskette turns (`Emulator::setDisketteTurning`, on in the GUI, the
browser page and the CLI's `--realtime`; off by default, for
`ms0515-run`, scripted runs and the tests, where the time is the
host's):

- The turn is 200 ms at 300 rpm, 6250 bytes.  Each physical drive has
  an angle, the byte under its head, which runs while its motor does
  and restarts at the index when the motor starts.
- The track's layout is the IBM System 34 double-density one the
  machine's formatters write, sectors 1..10 in physical order: 146
  bytes from the index to the first ID field, then 604 a sector (a
  22-byte ID field, 22 of gap, 16 of sync and data mark, 512 of data,
  2 of CRC, 30 of gap); `fdc_id_at(s)` and `fdc_data_at(s)` say where
  a sector's ID and first data byte lie.
- A Type II command finds its sector where it lies: the data starts
  when the sector's ID has come round and its data field after it - a
  sector asked for just after its ID passed costs a turn.  A sector
  not on the track is RECORD NOT FOUND after five turns.  A
  multi-sector command goes on to the next sector a slot later.  The E
  flag's 15 ms of settling come first.
- READ ADDRESS ends when the next ID field to come has passed under
  the head; WRITE TRACK begins at the index.
- READY comes two turns after the motor starts (the МС 5305 raises it
  at the third index), and the index pulse shows in a Type I status for
  4 ms once a turn.
- Without the turn (the default) a Type II command finds any sector 5
  ms after it is given, READ ADDRESS ends at once and a drive is ready
  as its motor starts.

What it means for a program: RT-11's DZ handler begins every request
with READ ADDRESS and gives READ SECTOR after it; a request of many
blocks reads them two sectors apart, 40 ms a block, within the turn.  A
program that asks for its blocks one at a time and works between them
longer than the 21 ms to the next block's ID waits a turn for each -
Saboteur 2, copying each block through the ROM for 22 ms, loads at 240
ms a block, 85 blocks in 20 s, as it did on the machine (the FPGA
rebuild's measurement, `test_turning.cpp`).  The angle is saved in a
state (snapshot format 4); whether the diskette turns is the
application's setting, not the machine's.

## Disk Image Format

The emulator uses **single-sided** images — one file per logical unit:

```
Track 0, Sectors 1–10 (5120 bytes)
Track 1, Sectors 1–10 (5120 bytes)
...
Track 79, Sectors 1–10 (5120 bytes)
```

Total image size: **409,600 bytes** (400 KB, 800 sectors).

Sectors within a track are stored in physical order (1–10).  The
mapping from RT-11 logical blocks to physical sectors is the **same**
on every `DZ:` volume (the `DV:`/`MZ:` whole-diskette volumes are the
exception - [`filesystem.md`](filesystem.md)) — a 2:1 interleave plus a +2-sectors-per-track
skew (OSA, Omega, Mihin and rodionov were all verified byte-for-byte to
use it; the FDC itself does no interleave — it serves physical sectors,
and the OS driver applies the mapping).  See
[`filesystem.md`](filesystem.md) for the formula.

### Double-sided raw images

Floppy images captured by hardware readers (Catweasel, KryoFlux) and
the surviving forum dumps are double-sided with track-interleaved
layout:

```
Track 0 Side 0, Track 0 Side 1, Track 1 Side 0, Track 1 Side 1, ...
```

Total: 819,200 bytes.  The emulator reads this format natively —
`fdc_attach` looks at the file size, sets `track_stride` to
2*FDC_TRACK_SIZE (= 10240) for DS, and offsets the side-1 logical
units (FD2/FD3) by FDC_TRACK_SIZE (= 5120) into each track slot.

Mount via `--disk0 path` / `-d0 path` (similarly for drive 1) and
both sides become readable through one open file with writes
routed to the right halves.

## MFM Encoding

The KR1818VG93 uses Modified Frequency Modulation (MFM), a three-frequency
encoding method:

- "1" is encoded as a pulse in the middle of the bit cell
- A clock pulse is inserted between two adjacent "0" bits
- Resulting intervals: 1T, 1.5T, 2T (where T = 4 μs at 250 kbit/s)

## Sources

- WD1793 datasheet (Western Digital, 1983)
- NS4 technical description (3.858.420 TO), section 4.11
- WD1793 reference: Hans Otten's pages, now at https://hansotten.file-hunter.com/ (see [`../kb/REFERENCES.md`](../kb/REFERENCES.md))
