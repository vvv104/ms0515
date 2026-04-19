# Memory — Address Space and Bank Switching

## Overview

The MS0515 has a 16-bit address bus (64 KB addressable), with 128 KB of
physical RAM accessed through bank switching.  The memory subsystem also
manages ROM overlay, VRAM virtual window access, and I/O register space.

## Physical Storage

| Component | Size   | Chips                          |
|-----------|--------|--------------------------------|
| RAM       | 128 KB | K565RU5G dynamic RAM           |
| ROM       | 16 KB  | 2 × K573RF4B UV-EPROM          |
| VRAM      | 16 KB  | Shared with RAM bank 7 area    |

## Address Map

```
  Address (octal)    Description
  ────────────────   ─────────────────────────────────
  000000 – 017777    Bank 0  (8 KB)
  020000 – 037777    Bank 1  (8 KB)
  040000 – 057777    Bank 2  (8 KB)
  060000 – 077777    Bank 3  (8 KB)
  100000 – 117777    Bank 4  (8 KB)
  120000 – 137777    Bank 5  (8 KB)
  140000 – 157777    Bank 6  (8 KB) — shadowed by extended ROM
  160000 – 177377    Bank 7  (8 KB) — overlaid with ROM
  177400 – 177776    I/O register space
```

Each bank has a primary and extended (secondary) counterpart, for a total
of 16 physical banks (128 KB).  Selection is controlled by the Memory
Dispatcher register.

## Memory Dispatcher Register (177400)

```
  15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
 ┌───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┐
 │ — │ — │STB│ — │VW1│VW0│TAI│MON│VEN│ B6│ B5│ B4│ B3│ B2│ B1│ B0│
 └───┴───┴───┴───┴───┴───┴───┴───┴───┴───┴───┴───┴───┴───┴───┴───┘
```

| Bits  | Name | Description                                          |
|-------|------|------------------------------------------------------|
| 0–6   | Bn   | Bank select: 1=primary, 0=extended                   |
| 7     | VEN  | VRAM access enable through virtual window             |
| 8     | MON  | Monitor interrupt request (1=assert VBlank IRQ)       |
| 9     | TAI  | Timer interrupt enable (1=enable timer IRQ)           |
| 10–11 | VWn  | VRAM virtual window position selector                 |
| 12–13 | STB  | Parallel interface (IRPR) control signals             |
| 14–15 | —    | Unused                                                |

## VRAM Virtual Window

VRAM occupies physical addresses behind ROM (160000–177777) and cannot be
accessed directly by the CPU.  Instead, when bit 7 of the dispatcher is set,
a 16 KB window at one of three positions maps to VRAM:

| Bit 11 | Bit 10 | Window address range     |
|--------|--------|--------------------------|
|   0    |   0    | 000000 – 037777          |
|   0    |   1    | 040000 – 077777          |
|   1    |   X    | 100000 – 137777          |

After boot self-test, the BIOS sets the window to 040000–077777.

## ROM Mapping

In default mode, only the upper 8 KB of ROM is visible at 160000–177377.
When System Register A bit 7 ("extended ROM") is set, the full 16 KB is
mapped at 140000–177377, but bank 6 of RAM becomes inaccessible.

## Address Translation Algorithm

```
1. If address >= 177400 → I/O register space
2. If extended ROM enabled and address >= 140000 → ROM
   Else if address >= 160000 → ROM (upper 8 KB)
3. If VRAM enabled (bit 7) and address falls in virtual window → VRAM
4. Otherwise → RAM bank, selected by dispatcher bits 0–6
```

## Sources

- NS4 technical description (3.858.420 TO), sections 4.3–4.4, figures 5–6
- NS4 technical description, Appendix 1 (register address table)
