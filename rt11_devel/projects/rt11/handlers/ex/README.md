# EX.SYS — the electronic disk of the expansion board

512 KB of dynamic memory behind a parallel interface, served as a volume.
A system copied onto it frees the floppy drive for the work diskette, and
the machine can be booted from it — which is what the board was bought
for.

The kits carry the handler as a binary only.  `EX.MAC` is its source,
written back from that binary with `tools/rt11_handler.py`; the board
itself is described from its schematic in
`src/core/include/ms0515/core/ramdisk.h`.

    python ../../kit/build_handler.py --source . EX OUTDIR            # a sound board
    python ../../kit/build_handler.py --bitmap --source . EX=EXKIT,EX OUTDIR   # the kit's

The second is the kit's `EX.SYS` **to the byte** — the request code, the
installation check, the primary driver, the table, and the block bitmap
its author left in by linking it plainly.

## What the binary showed

* Of all the kits' handlers this is the one written most by DEC's book:
  the controller's address is in the header where `.DRDEF` puts it, there
  is an installation check (`.DRINS` — a value written to the interface's
  port A has to read back, or `INSTALL` refuses the device; the routine
  after it is the `FINDRV` of DEC's `DX.MAC`), and a primary driver.
* A page is 256 bytes and a block two pages.  The page number goes to the
  interface — its low byte to port A, the bits above with START to port B,
  after a START+RESET that zeroes the board's byte counter — and then every
  access to the data port is the next byte.  The first byte of a page
  written goes to `177570`, the data port's other address.
* The memory is dynamic and must not be kept waiting, so a page is moved
  at priority 7 and interrupts are let in between pages.
* **A table of redirected pages.**  The kit's handler ends in four pairs —
  pages `2103`, `2460`, `2663` and `3151` are served by the spare pages
  `3774`..`3777` at the top — which is why its volume is 1022 blocks and
  not 1024.  Someone ran a memory test on one particular board, found four
  bad places, and wrote them into the handler.  `EX$KIT` (the prefix file
  `EXKIT.MAC`) keeps that table; without it the board is taken to be sound:
  no table, all 1024 blocks.

`validate.py` is the oracle for the sound-board build: `INSTALL` finds the
board, a 49-block file goes onto `EX:` and back to a diskette identical,
then the system is copied over, a bootstrap written, and `BOOT EX:` brings
the monitor up from the electronic disk.
