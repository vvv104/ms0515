# HD: — Paravirtual Hard Disk

## Overview

The Elektronika MS 0515 never shipped with a hard-disk controller.  `HD:`
is a **paravirtual** block device — a convention shared across PDP-11
emulators (it originates from the DVK emulator at pdp-11.org.ru) — that a
stock RT-11 driver (`HD.SYS`) drives over two I/O registers.  It lets
RT-11 mount a backing image of arbitrary size as a fast random-access
volume, without modelling any real silicon.

It is implemented in `core/src/hd.c` (state + protocol), wired into the
I/O bus in `core/src/board.c`, exposed to hosts through
`ms0515::Emulator::mountHd`/`unmountHd`/`hdActive`, and surfaced in both
binaries via the shared `--hd <path>` flag / `hd:` YAML key.

## Controller variants

The `HD.SYS` distribution kit defines five controller types.  `HD.SYS`
detects the type from the at-rest CSR value and refuses a mismatch
(`Wrong HD type`):

| Variant | Driver  | Interrupts        | Notes                              |
|---------|---------|-------------------|------------------------------------|
| t1      | v1.6    | no (stops CPU)    | direct DMA, halts the processor    |
| **t2**  | v2.0    | **no**            | 32-bit image size, 22-bit addrs    |
| t3      | v2.0/3.0| yes (vector 0164) | t2 + interrupts                    |
| t4      | v4.0    | no                | sets the CS.DMA marker bit         |
| t5      | v5.0    | yes               | t4 + interrupts                    |

We emulate **t2**: synchronous programmed DMA, no interrupts.  It is the
natural fit for our RT-11 SJ V5.04 target — no interrupt vector needed,
16-bit buffer addresses under SJ (`MMG$T = 0`), and it maps cleanly onto
our synchronous bus model.  `HD.SYS` v2.0 also serves t3, leaving an easy
path to interrupts later (vector 0164 is free on the MS 0515).

## Register map

`HD.SYS` defaults `HDCSR` to `0177720`.  These addresses overlap the
(stubbed, unused) MS 0515 serial-port TX side, so `board.c` routes them to
HD **only while an image is mounted** — the HD device and the serial port
are mutually exclusive on the bus.

| Address  | Name      | Write          | Read              |
|----------|-----------|----------------|-------------------|
| 0177720  | HDCSR     | command code   | status            |
| 0177721  | HDCSR+1   | DMA addr high byte (XM builds) | status high |
| 0177722  | HDDAT     | command argument | result (e.g. size) |

### Status (HDCSR read)

- low-byte bit 7 (`HD_CS_READY`) — set for any non-t1 controller; HD.SYS
  uses a byte read (`TstB`) of the CSR to reject t1.  Because our DMA is
  synchronous, "ready" is always true.
- bit 6 (`HD_CS_INT`, `CS.INT`) — interrupt-capable (t3/t5); clear for us.
- bit 14 (`HD_CS_DMA`, `CS.DMA`) — t4/t5 marker; clear for us.
- bit 15 (`HD_CS_ERROR`) — the last command failed (out-of-range transfer
  or unknown command code).

So our at-rest CSR reads `0x0080`, identifying a healthy t2 device.

## Command protocol

To issue a command, the driver writes the argument to `HDDAT` (0177722)
then the command code to `HDCSR` (0177720); the CSR write executes it
immediately.  After a transfer it spins on the CSR until ready, then tests
the sign bit for an error.

| Code | Name      | Effect                                             |
|------|-----------|----------------------------------------------------|
| 1    | SetUnit   | select unit (`arg & 7`; only unit 0 is backed)     |
| 2    | SetBlock  | set the starting block number                      |
| 3    | SetBuf    | set the DMA buffer address (low 16 via HDDAT, high via HDCSR+1) |
| 4    | SetWCount | set the transfer length in words                   |
| 5    | Read      | image[block] → memory[buffer], WordCount words     |
| 6    | Write     | memory[buffer] → image[block], WordCount words     |
| 7    | GetSize   | report the unit's size in blocks (read from HDDAT) |

DMA goes through `mem_translate`, so a buffer address is resolved exactly
like a CPU access (respecting the current bank mapping).

## Host model

Two concepts are kept separate, mirroring real hardware:

- **Controller presence** — whether the card decodes the bus (and so the
  serial port does not).  Toggled by `Emulator::setHdEnabled` /
  `hdEnabled`, the YAML `hd_enabled` key, and the "HD: / Serial port" radio
  under the Components menu.  An enabled controller with no media is a
  valid offline drive (`GetSize == 0`).
- **Media** — the mounted image.  `Emulator::mountHd` reads the whole file
  into a RAM buffer (the C core stays free of file I/O — reads are served
  from the buffer) and also enables the controller; `unmountHd` ejects it
  while leaving the controller present.  The image is mounted from the File
  menu, beside the floppies, or via the `--hd <path>` flag / `hd` YAML key.

Writes are **write-through**: the core fires a callback after each Write
command with the changed byte range, and the lib persists it to the
backing file immediately (open-write-close per call).  So the file always
reflects the volume mid-session and survives a crash or a killed process —
nothing waits for a clean shutdown.

An image must be a positive multiple of 512 bytes; in practice RT-11 caps
a logical volume at 65535 blocks (~32 MB), so a larger backing file is
reported truncated to that.

## Sources

- HD driver distribution kit (HD t1..t5), pdp-11.org.ru
- `HD.SYS` v2.0 source (`HD Sources/v2.0/HD.MAC`)
