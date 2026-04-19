# Video Controller

## Overview

The MS0515 video controller is integrated on the system module.  It reads
VRAM data continuously (also providing DRAM refresh) and generates a
composite video signal with horizontal and vertical sync for the monitor.

## Display Modes

| Mode            | Resolution | Colors      | VRAM usage          |
|-----------------|------------|-------------|---------------------|
| Medium (default)| 320 x 200  | 8 (+ bright)| 8 KB data + 8 KB attr|
| High            | 640 x 200  | 2 (mono)    | 16 KB data           |

Mode is selected by bit 3 of System Register C (177604):
- 0 = medium resolution (320x200, color attributes)
- 1 = high resolution (640x200, monochrome)

## Refresh Rates

Selected by hardware jumpers E3/E4, readable via System Register B bits 4-3:

| E3   | E4   | H-sync (us) | Line (us) | Frame (ms) | Rate (Hz) |
|------|------|-------------|-----------|------------|-----------|
| Set  | Set  | 4.7         | 63.9      | 20.0       | 50        |
| Open | Set  | 4.7         | 63.9      | 16.7       | 60        |
| Set  | Open | 3.9         | 52.6      | 13.9       | 72        |

## Medium Resolution Mode (320 x 200)

Each 16-bit word in VRAM encodes 8 pixels:

```
  15   14   13   12   11   10    9    8    7    6    5    4    3    2    1    0
 ┌────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┐
 │ F  │ I  │ G' │ R' │ B' │ G  │ R  │ B  │ D7 │ D6 │ D5 │ D4 │ D3 │ D2 │ D1 │ D0 │
 └────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┘
  ← Attributes (high byte) →        ← Pixel data (low byte) →
```

### Attribute bits

| Bit | Name | Description                                        |
|-----|------|----------------------------------------------------|
| 15  | F    | Flash: 0=steady, 1=blink at 3 Hz                  |
| 14  | I    | Intensity: 0=dim (half brightness), 1=bright       |
| 13  | G'   | Background green                                   |
| 12  | R'   | Background red                                     |
| 11  | B'   | Background blue                                    |
| 10  | G    | Foreground green                                   |
| 9   | R    | Foreground red                                     |
| 8   | B    | Foreground blue                                    |

Pixel data bits: 1 = foreground color, 0 = background color.
Scan direction: D7 is leftmost, D0 is rightmost.

This is similar to the ZX Spectrum attribute system, but applied per
8-pixel group (word) rather than per 8x8 character cell.

### Color Palette (8 colors)

| G | R | B | Color    |
|---|---|---|----------|
| 0 | 0 | 0 | Black    |
| 0 | 0 | 1 | Blue     |
| 0 | 1 | 0 | Red      |
| 0 | 1 | 1 | Magenta  |
| 1 | 0 | 0 | Green    |
| 1 | 0 | 1 | Cyan     |
| 1 | 1 | 0 | Yellow   |
| 1 | 1 | 1 | White    |

The intensity bit affects both foreground and background colors within the
same attribute group.

## High Resolution Mode (640 x 200)

All 16 bits of each word are pixel data (no attributes).  The word is
displayed with bytes swapped in the shift register:

```
  Display order: D7 D6 D5 D4 D3 D2 D1 D0 D15 D14 D13 D12 D11 D10 D9 D8
                 ← Low byte first →       ← High byte second →
```

Foreground color = complement of border color.
Background color = border color (from Reg C bits 2-0).

## Border Color

Set by System Register C bits 2-0 (GRB format), from the same 8-color
palette.  Visible in the non-active area of the screen.  In high-resolution
mode, the border color also determines the background/foreground colors.

## VRAM Organization

VRAM is 16 KB, mapped to physical addresses 160000–177777 (behind ROM).
CPU access is through the virtual window mechanism (see memory.md).

Screen layout: 320x200 mode requires 320/8 = 40 words per line, 200 lines
= 8000 words = 16000 bytes.  640x200 mode: 640/16 = 40 words per line,
200 lines = 8000 words = 16000 bytes.

The remaining ~384 bytes of VRAM are not displayed.

## Sources

- NS4 technical description (3.858.420 TO), sections 4.6, 4.7
- NS4 technical description, figures 8–13, tables 7–8
