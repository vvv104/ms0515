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

(The RT-11 / OSA operating-system drivers for this hardware use the names
`DZ`, `MZ`, `MY`, `MD` depending on the OS build — those are driver-level
names, not hardware designations, and the emulator deliberately avoids
them.)

The user-facing emulator CLI / YAML / GUI describe each unit as
"drive N, side M" (e.g. `--disk0-side0`, `disk0_side0:` in the YAML
config, "Disk 0 side 0" in the File menu).  That maps onto the
hardware naming above as

| User-facing       | Core unit |
|-------------------|-----------|
| disk 0, side 0    | FD0       |
| disk 0, side 1    | FD2       |
| disk 1, side 0    | FD1       |
| disk 1, side 1    | FD3       |

`--disk0` (and `--disk1`) is a convenience for one 819200-byte
double-sided image: it mounts the same file on both side units of
the drive, and `fdc_attach` picks the right offset for each side
automatically.

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
| 3    | Side select (active low: 0 = upper, 1 = lower)  |

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

### Type IV — Control

| Command         | Code (hex) | Description                   |
|-----------------|------------|-------------------------------|
| Force Interrupt | 0xD0–0xDF  | Abort current command         |

## Disk Image Format

The emulator uses **single-sided** images — one file per logical unit:

```
Track 0, Sectors 1–10 (5120 bytes)
Track 1, Sectors 1–10 (5120 bytes)
...
Track 79, Sectors 1–10 (5120 bytes)
```

Total image size: **409,600 bytes** (400 KB, 800 sectors).

Sectors within a track are stored in physical order (1–10).  The OS
uses 2:1 sector interleave when mapping logical blocks to physical
sectors — see `docs/filesystem.md` for the block-to-sector mapping.

### Double-sided raw images

Floppy images captured by hardware readers (Catweasel, KryoFlux) are
typically double-sided with track-interleaved layout:

```
Track 0 Side 0, Track 0 Side 1, Track 1 Side 0, Track 1 Side 1, ...
```

Total: 819,200 bytes.  Use `tools/split_double_sided.py` to split
these into two single-sided images for the emulator.

## MFM Encoding

The KR1818VG93 uses Modified Frequency Modulation (MFM), a three-frequency
encoding method:

- "1" is encoded as a pulse in the middle of the bit cell
- A clock pulse is inserted between two adjacent "0" bits
- Resulting intervals: 1T, 1.5T, 2T (where T = 4 μs at 250 kbit/s)

## Sources

- WD1793 datasheet (Western Digital, 1983)
- NS4 technical description (3.858.420 TO), section 4.11
- WD1793 reference: http://msx.hansotten.com/technical-info/wd1793/
