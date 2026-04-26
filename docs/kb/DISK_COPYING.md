# Copying MS0515 floppy disk images

## TL;DR

- `.dsk` images are flat byte-for-byte sector dumps with no metadata.
- Image-level copy = `cp src.dsk dst.dsk` (or any equivalent file copy).
  That is byte-perfect and preserves every protection we have seen.
- Never run the emulator with a fixture image mounted writable; some
  Soviet OSes flush dirty buffer pages on close and corrupt the image
  (see `KNOWN_ISSUES.md`).  Always work on a copy.

## Why image-level copy works on copy-protected disks

Every copy-protection scheme observed on MS0515 software so far is
**content-only**: it reads specific bytes from a specific sector via
the standard ROM sector-read trampoline (`ROM 0o160010 → 0o162652`)
and uses those bytes to patch RAM at boot time.  There is no
MFM-level, timing-based, weak-bit, or bad-track protection in our
corpus.  The KR1818VG93 / WD1793 driver in ROM exposes only flat
512-byte sectors to software, so a byte-for-byte sector dump is
indistinguishable from the original to any ROM-level reader.

Worked example: the Rodionov disk (`065_full.dsk`) reads four bytes
from track 0 / side 1 / sector 3 and uses them as a 4-byte instruction
slot (`ADD #740, R2`).  Any tool that captures all 80 × 2 × 10 × 512
sectors verbatim — `cp` on the host, `dd` on a raw block device, or a
Greaseweazle / KryoFlux / Catweasel dump in IBM-MFM mode — yields a
working clone.  See `KNOWN_ISSUES.md` for the protection details.

## Host-level copy (recommended)

Single command, works for both image sizes:

```
cp src.dsk dst.dsk          # 409600 bytes (single-sided)
                            # 819200 bytes (track-interleaved DS)
```

Mount the copy and confirm:

```
ms0515.exe --disk0 dst.dsk            # for an 819200-byte DS image
ms0515.exe --disk0-side0 dst.dsk      # for a 409600-byte SS image
```

## Test fixtures: TempDisk

The unit tests under `emu/tests/` never mount fixture `.dsk` files
directly, for the corruption reason above.  They use the RAII helper
`TempDisk` defined in `emu/tests/test_disk.hpp`:

- The constructor copies the source fixture to a unique file under
  `TESTS_BUILD_DIR/temp/`.
- Tests mount the writable copy via `.path()`.
- The destructor unlinks the copy.

Field-order rule: declare `TempDisk` *before* any `Emulator` in the
same scope so it outlives the FDC handle.  Otherwise the temp file is
still open inside the emulator when `fs::remove()` runs, the unlink
silently fails on Windows, and stale copies pile up under
`build/tests/temp/`.

## Within the emulator (RT-11 / OSA disk-to-disk)

Mostly historical; the host-level copy above is simpler and safer.
For completeness:

1. Pre-create a blank double-sided target image (819200 zero bytes):
   ```
   dd if=/dev/zero of=blank.dsk bs=1024 count=800
   ```
2. Mount the source on drive 0 and the blank on drive 1:
   ```
   ms0515.exe --disk0 source.dsk --disk1 blank.dsk
   ```
3. Boot from drive 0, then run the OS's device-level copy command.
   For RT-11-derived systems that is typically `COPY/DEVICE` from the
   monitor or `/COP` from inside `DUP`:
   ```
   .COPY/DEVICE DZ0: DZ1:
   .COPY/DEVICE DZ2: DZ3:
   ```
   Driver letter pairs differ between OS builds (`DZ`, `MZ`, `MY`,
   `MD`); see `docs/hardware/floppy.md` for the mapping between
   driver names and our FD0..FD3 logical units.

A device-level copy reads every block, including sectors not assigned
to any file, so the protection sector is preserved.  A file-level
copy (e.g. `COPY *.* DZ0: DZ1:`) is **not** sufficient.

## Physical media (real floppies)

Out of scope for the emulator itself, but for the record:

- **Read**: Greaseweazle / KryoFlux / Catweasel in IBM-MFM mode at the
  5.25" 80 × 2 × 10 × 512 geometry produces a track-interleaved
  819200-byte image directly mountable via `--disk0`.
- **Write**: the same tools write the `.dsk` back to physical media.
- The 2:1 sector skew used by Soviet RT-11 derivatives lives at the
  filesystem layer; on-disk physical sectors are 1..10 in order.

## See also

- `docs/hardware/floppy.md` — disk image layouts (SS and DS)
- `docs/hardware/filesystem.md` — RT-11 block layout, sector skew
- `docs/kb/KNOWN_ISSUES.md` — Rodionov protection reverse-engineering
