# Known Issues

## Mihin (OS-16SJ) — РУС/ЛАТ key prints `^N`/`^O`, locks input on exit

- **ROMs**: ROM-A and ROM-B both affected (different flavours)
- **Disks**: any Mihinsoft / OS-16SJ image (`mihin.dsk`, `tests/disks/test_mihin.dsk`)
- **Reproduction**:
  Boot Mihin to its dot prompt, then tap the РУС/ЛАТ key.  The first
  tap prints two characters at the cursor (visible garbage); the
  second tap prints two more characters AND silently puts the
  monitor's input echo into "Ctrl-O quiet" — subsequent letter
  keys are buffered but produce no echo until the user either
  presses Enter (ends the line, Mihin reports `KMON-F-Invalid
  command`) or alternates РУС/ЛАТ another two times to clear the
  quiet flag.

- **Why it happens** (analysed 2026-04-26 from ROM listings + Mihin
  RT11SJ.SYS / TT.SYS disassembly, see also the diagnostic findings
  in `project_keyboard_tests.md`):

  1. The MS7004 firmware sends scancode `0o262` for the РУС/ЛАТ key.
     Both ROMs route this to a small RUS/LAT handler that toggles
     a state bit in `0o157760` (bit 3 in ROM-A at address 0o176022;
     bit 2 in ROM-B at address 0o175730).  On every press the
     handler emits ONE byte into the OS input stream — `0o16` (SO,
     Ctrl-N) when entering RUS mode, `0o17` (SI, Ctrl-O) when
     leaving — and these bytes are what reaches the OS.
  2. **OSA / Omega** consume SO/SI silently — their monitors know
     SO/SI are charset-toggle codes and absorb them without echo.
     Russian input "just works".
  3. **Mihin** (Mihinsoft OS-16SJ, 1990) is a much thinner RT-11
     SJ derivative; its KMON has no special handling for SO/SI,
     so each byte goes through the standard RT-11 control-char
     echo (`^N`, `^O`).  Worse: 0x0F (= Ctrl-O) ALSO triggers RT-
     11's "quiet output" mode in Mihin's resident monitor, which
     suppresses input-echo until the next CR or Ctrl-O.
  4. Under ROM-B specifically the picture is partly redeemed:
     ROM-B's keyboard handler does the LAT→RUS letter translation
     internally (adds 0o40 to letter bytes when bit 2 is set),
     so after the FIRST РУС/ЛАТ tap subsequent letter keys really
     do produce KOI-8 Cyrillic codes (`A` → 0xC1 'а', etc.).
     Russian input works one-way.  Exit (second toggle) still
     trips the Ctrl-O quiet trap.
  5. Under ROM-A the ROM has no RUS letter translation at all;
     letter scancodes always produce Latin bytes regardless of
     the РУС/ЛАТ state, so Russian never appears even after the
     toggle.

- **Why we don't fix it**:
  - It is not an emulator bug — both the ROM behaviour and Mihin's
    response are faithful to the real hardware/OS.
  - Patching ROM-B to suppress SO/SI emission would change ROM
    behaviour for OSA/Omega too and risks breaking those configs.
  - Patching Mihin's RT11SJ.SYS to silently absorb SO/SI requires
    extensive reverse engineering of the resident monitor without
    symbol tables.

- **Cross-emulator confirmation (2026-04-26)**: Same disk image and
  ROM-B run under MAME 0.287's `ms0515` driver produce visually
  identical output: the entry indicator `чн` (KOI-8 0xDE 0xCE — `^N`
  rendered through Mihin's high-bit-set Cyrillic display mode), the
  exit indicator `^O` (0x5E 0x4F), Cyrillic letters in between, and
  the same Ctrl-O echo lock after the second toggle.  MAME runs the
  authentic MS7004 keyboard microcontroller firmware (Intel 8035 +
  2 KB ROM `mc7004_keyboard_original.rom`, CRC `69fcab53`) plus the
  same system ROMs we ship.  Identical bytes in screen RAM under
  both emulators confirm this is genuine Mihin behaviour and our
  high-level keyboard state machine is byte-accurate at the OS-
  visible interface.

- **Test impact**: The keyboard emulation suite parametrises every
  TEST_CASE over a `kConfigs` matrix; Mihin entries carry
  `hasRusMode = false` so the four РУС-mode TEST_CASEs skip them
  with a `MESSAGE` line.  The Mihin configs DO run all the LAT-mode
  scenarios (Latin letters, digits, punctuation, ФКС on letters,
  shift-immune positions) and pass identically to OSA / Omega.

## rodionov.dsk — copy protection (resolved with full DS dump)

- **ROM**: ROM-A only.  ROM-B stalls right after printing
  «НГМД готов, идет загрузка операционной системы…» both with the
  original `065_full.dsk` and with our trimmed `tests/disks/test_rod.dsk`
  fixture, so the boot suite marks `(ms0515-romb.rom, test_rod.dsk)` as
  known-bad.  The protection check is unrelated to the ROM-B stall;
  the disk simply was not built to boot under that ROM.
- **Disk**: RT-15SJ with ROSA Commander v1.3 by Rodionov S.A.,
  Voronezh, 1993.  Forum dumps circulate under several names
  (`rodionov.dsk`, `065.dsk`, `065_full.dsk`).  Single-sided dumps
  (409600 bytes) contain only the boot side and trip the protection;
  the genuine track-interleaved double-sided dump (819200 bytes)
  carries the protection payload on side 1 and boots cleanly.  Shipped
  in the repo as `emu/assets/disks/rodionov.dsk`.
- **Status**: solved.  Mounted via `--disk0 path/to/065_full.dsk`, the
  disk boots into ROSA Commander with no visual artefacts and reaches
  the RT-11 date prompt normally.
- **Boot command**:
  ```
  ms0515.exe --rom assets/rom/ms0515-roma.rom --disk0 path/to/065_full.dsk
  ```

### Protection mechanism

1. Trap vector 64 fires periodically; the handler at RAM `0o137374`
   reaches the protection block via RAM `0o141744`.
2. RAM `0o141770: TSTB @#63` — if byte `@#63` is zero the code runs
   the sector-3 loader + patch sequence; non-zero skips straight to
   `0o142102`.
3. Loader path (RAM `0o141776-142076`):
   - Builds an 8-byte param block at `0o142014`:
     `[sector/side=001430, drive/reg_a=001000, dest=000060, count=000002]`.
   - `ADD #110010, @142042` patches the JSR target (`0o160010` →
     `0o070020`).
   - `JSR PC, @#160010` calls the ROM sector-read trampoline at
     ROM `0o162652`.  Reads exactly **4 bytes** from FD2 (drive 0
     upper side) sector 3 into RAM `0o60..0o63`.  Destination and
     count are hard-coded; there is no length-prefix or second read
     driven by the data itself.
   - Four patches install a 4-byte instruction plus a forced TSTB:
     * `MOV @#60, @142110` — word @ `0o60` → instruction slot 1
     * `MOV @#62, @142112` — word @ `0o62` → instruction slot 2
     * `MOV #105737, @142114` — fixed `TSTB @#` opcode
     * `MOV #157760, @142116` — fixed operand → `TSTB @#157760`
   - `INCB @#63` marks "loader ran" so subsequent trap passes skip
     the loader and jump straight to `0o142102`.
4. Execution falls into the patched 4-byte instruction at `0o142110`,
   then the forced `TSTB @#157760` at `0o142114`, then
   `0o142120: BPL 142236`.  Both arms (fall-through at `0o142122`
   and BPL-taken at `0o142236`) write to VRAM through R2.

### What is in the four sector-3 bytes

From a genuine DS dump (`065_full.dsk` track 0 side 1 sector 3, first
four bytes):

```
c2 65 e0 01
```

Decoded as little-endian PDP-11 words:

```
0o062702 → ADD #imm, R2
0o000740 → imm = 480 (decimal)
```

i.e. **`ADD #740, R2`** — advance the VRAM destination pointer R2 by
480 bytes.  VRAM is 16 KB at `0o160000`; both display modes
(320×200 attribute and 640×200 monochrome) use 80 bytes per scan
line, so R2 moves exactly **6 rows down**.  The downstream `@R2`
writes therefore land 6 rows below where an unmodified R2 would put
them.

### Why the protection fails quietly on a copy

A disk without those 4 bytes (single-sided dump, brute-force patch,
or our historical `BR +3` workaround) leaves R2 at its original value,
and the subsequent `@R2` writes pollute the top-left corner of the
screen with two thin stripes.  The disk still boots, ROSA Commander
still renders Russian UI correctly, the OS prompt still appears —
only the stripes betray the broken copy.

This is a deliberately low-noise failure mode.  A `JSR @#real_loader`
scheme (which the previous KB hypothesis assumed) would have prevented
boot entirely when the bytes were missing — much louder, and easier
to diagnose.  `ADD #imm, Rn` is one of the most innocuous PDP-11
instructions imaginable; a cracker who has traced the loader and
landed on it would conclude "this didn't really do anything" and look
elsewhere.  The instruction is also exactly 4 bytes (one opcode word
+ one immediate word), fitting the patch-slot size with nothing to
spare.

### Historical workaround: rodionov2.dsk

Before we obtained the genuine DS dump, we shipped a hand-rolled
"partial bypass" disk pair:

- `rodionov2.dsk` (the upper side, also local-only) contained
  `03 01 01 01` at offset `0x400` (track 0 sector 3).  That encodes
  `BR +3` at RAM `0o142110`, jumping over the second slot and the
  forced `TSTB @#157760` straight to `0o142120`.  Byte `0o63 = 1`
  satisfied the non-zero check on subsequent passes.
- Booted via `--disk0-side0 rodionov.dsk --disk0-side1 rodionov2.dsk`.
- Boot completed, two thin stripes remained visible in the top-left
  column — exactly the expected effect of leaving R2 un-advanced.

Superseded by the genuine DS dump.  Kept here as a worked example of
how a content-only protection can be partially bypassed without
knowing the original payload.

### Building `tests/disks/test_rod.dsk` from scratch

The original `065_full.dsk` contains user applications and games that
are not ours to redistribute, so the test fixture is built from a
zeroed image using only OS commands plus a four-byte host-side patch
for the protection sector.  Reproduce as follows:

1. **Create a blank double-sided image** (819200 bytes):
   ```
   dd if=/dev/zero of=test_rod.dsk bs=1024 count=800
   ```
2. **Mount in the emulator** with `065_full.dsk` as drive 0 (source)
   and `test_rod.dsk` as drive 1 (target):
   ```yaml
   disk0: "065_full.dsk"
   disk1: "test_rod.dsk"
   rom:   "ms0515-roma-original.rom"
   ```
3. **Boot RT-15SJ from drive 0** and run, in order:
   ```
   .INIT/NOQUERY DZ1:
   .INIT/NOQUERY DZ3:
   .COPY/SYS DZ0:SWAP.SYS    DZ1:
   .COPY/SYS DZ0:RT15SJ.SYS  DZ1:
   .COPY/SYS DZ0:DZ.SYS      DZ1:
   .COPY/SYS DZ0:TT.SYS      DZ1:
   .COPY/SYS DZ0:VM.SYS      DZ1:
   .COPY/SYS DZ0:VS.SYS      DZ1:
   .COPY/SYS DZ0:DIR.SAV     DZ1:
   .COPY/SYS DZ0:DUP.SAV     DZ1:
   .COPY/SYS DZ0:PIP.SAV     DZ1:
   .COPY/BOOT DZ1:RT15SJ.SYS DZ1:
   .CREATE START.COM
   ```
   `CREATE START.COM` makes a 1-block empty file so RT-11 prints the
   `.` prompt without an error.  `COPY TT: file ^Z` does NOT work in
   ФОДОС — `COPY` does not accept an immediate EOF; use `CREATE`.
4. **Close the emulator**, then host-side install the four protection
   bytes:
   ```python
   with open('test_rod.dsk', 'r+b') as f:
       f.seek(0x1800)               # track 0 / side 1 / sector 3
       f.write(bytes([0xc2, 0x65, 0xe0, 0x01]))  # ADD #740, R2
   ```
   Offset `0x1800` is the FDC byte address: `image_offset(side1) +
   track*track_stride + (sector_reg-1)*512 = 5120 + 0 + 2*512`.  Note
   this is **not** the `0x1A00` you would compute from "block 796 of
   DZ3 under 2:1 OS-level interleave" — the protection's BIOS sector
   read uses raw `(track, sector)`, which under OS interleave maps to
   block 791 of DZ3, not block 796.  Off-by-one between FDC sector
   numbering (1-indexed) and physical sector index (0-indexed) is the
   trap to watch for.
5. **Boot test_rod.dsk standalone** (`disk0: test_rod.dsk`).  Without
   the four bytes the boot HALTs (the patched instruction collapses to
   `HALT HALT`); with them, RT-15SJ reaches the `.` prompt cleanly.

The boot suite mounts `test_rod.dsk` automatically via `--disk0`-style
DS handling: `runBoot` detects 819200-byte images and calls
`mountDisk(0)` for the lower side and `mountDisk(2)` for the upper
side so the protection's sector read returns real bytes.

### Reverse-engineering notes

- RT-15SJ is loaded at RAM base `0o66416` (file offset 0 → RAM
  `0o66416`).  Protection entry `0o141744`; param block `0o142014`;
  patch slots `0o142110/0o142112`; forced `TSTB @#157760` at
  `0o142114`; VRAM-writing arms `0o142122-140` and `0o142236-250`.
- Downstream `@R2` write sites: `ROM 0o165200` (`BISB #377, @R2`) and
  `RAM 0o146156` (`COMB @R2`), both gated on `TSTB @#157760 → BPL`.
  `@#157760` bit 7 is set by `ROM 0o174376` on each FD2 side-1
  selection.  These sites are benign — they write through R2 wherever
  R2 happens to point.  With the original `ADD #740, R2` they land
  6 rows below the visible top; without it they land at the top.
- Retracted hypothesis: we previously suspected the 4 bytes encoded
  `004737 <addr>` (`JSR PC, @#…`) into a deeper initialisation
  routine that populated `@#143056/@#143060` glyph data and
  `@#157712/@#157760` mode flags.  The genuine DS dump showed the
  bytes are a plain `ADD #740, R2`; no glyph table, no mode flag
  setup.  Of the experimental patches we tried during the
  wrong-hypothesis phase, `MOV #1430, R2` (`C2 15 18 03`) produced
  identical visual output to `BR +3` — in retrospect the strongest
  hint that the answer was a simple R2 adjustment, but we did not
  converge on the exact value `0o740` until the DS dump arrived.

## ROM monitor `D` command with no disk attached — silent BIOS spin

- **Scenario**: From the ROM monitor prompt, press `D` to boot from
  drive 0 (FD0 in the core's logical-unit naming) without any disk
  image mounted to that drive.
- **Symptom**: Emulator appears frozen.  CPU spins in
  ```
  163720: MOV #177640, R4
  163724: BITB #0o200, (R4)    ; test NOT_READY (bit 7) of FDC status
  163730: BNE 163724           ; loop while NOT_READY
  ```
  at priority 7, no interrupts can break it.  Not a HALT —
  `cpu.halted=false`.
- **Real-hardware behaviour**: pressing `D` without a disk evidently
  produced *some* response on the user's real machine — likely a
  short error indication (text or beep) before / instead of the
  silent poll loop.  Our ROM image (CRC `0x81c627ac`) and FDC model
  produce only the silent spin: `drive_ready()` keeps `NOT_READY`
  asserted as long as `image == NULL`, so the loop never exits.
- **Once a disk is mounted (via the File menu) the spin breaks
  immediately** — `drive_ready()` flips, `NOT_READY` clears, BIOS
  continues.  So the loop is effectively "wait for media".
- **What's in the ROM**: the image contains Cyrillic strings
  "ОШИБКА" / "ЗАГРУЗКИ" at logical `0o177332/0o177341` and
  "НГМД готов" at `0o177344`, but the silent-spin path at `0o163724`
  doesn't reference them — those messages cover different failure
  modes (bad sector, unbootable disk).  Real hardware probably also
  spins silently when no media is present; the user's recollection of
  "the machine reacted to closing the latch" is consistent with our
  current behaviour: mounting a disk while the spin is active flips
  `drive_ready()` and lets BIOS continue.
- **Possible improvements** (regardless of root cause):
  1. Surface a hint in the menu bar after the CPU has spent ~1 s in
     this loop ("Disk 0 not ready — Reset or mount a disk").
  2. Auto-pause and offer a mount dialog.
  3. Investigate the ROM string table for an unused error message.

## `type STARTS.COM` in Mihin OS-16SJ corrupts the disk image

- **Reproduction**: Boot Mihin OS, run `TYPE STARTS.COM` (a binary `.COM`
  file).  System prints a few bytes then halts at PC=`0o144032` (zero
  memory) after RTI from `0o151240` pops a corrupted stack frame.
- **Side effect**: tracks 2-8 of `mihin.dsk` end up overwritten with
  zeros — about 26 KB of damage out of a 410 KB image.  The OS-level
  loader subsequently fails to boot from this corrupted image until
  the disk is restored from `emu/assets/disks/mihin.dsk` (or a backup).
- **What we know**:
  - Our `write_sector()` only runs when the CPU has issued `WRITE_SECTOR`
    (cmd 0xA0/0xB0) — the FDC cannot write to disk on its own, so the
    writes did originate from OS code.
  - Event ring captures ~300 byte writes at PC=`0o157060` (the OS write
    loop), but the WRITE_SECTOR command bytes themselves were already
    rotated out of the ring by the time the snapshot triggered.
- **Likely cause** (unverified): OS-16SJ's TYPE command opens the file
  read-write and flushes dirty buffer pages on close; on a binary file
  it may flush uninitialised (zero-filled) buffer pages back to disk,
  corrupting unrelated tracks.
- **Mitigations**:
  - Restore `package/assets/disks/mihin.dsk` from `emu/assets/disks/`
    after an incident (`cp` or `Copy-Item`).
  - Consider adding a "read-only mount by default" frontend setting,
    or a "snapshot the disk on mount" feature that keeps the original
    pristine.
- **Investigation hint**: re-run with `history_size: 262144` and
  *no* read/write watchpoints in the yaml — this preserves earlier
  events long enough to catch the actual `WRITE_SECTOR` command issued
  by the OS before the data-write storm rotates them out.

## On-screen keyboard: physical Shift + OSK click sends wrong character for ШЩЧЭ

- **Scenario**: On-screen keyboard visible.  Latin mode (РУС/ЛАТ).
  Press *and hold physical Shift* on the host keyboard, then *click*
  one of the OSK keys Ш, Щ, Ч, Э (their Latin equivalents are
  `[`, `]`, `;`, `'` etc).
- **Symptom**: Wrong character — produces the *shifted* Latin variant
  (`{`, `}`, `:`, `"`).  The same OSK key works correctly when:
  - clicked alone (no Shift) → unshifted Latin character
  - clicked together with the OSK's own ВР button instead of physical
    Shift → unshifted Latin character (correct)
  - the key Ю — works correctly in all combinations.
- **Likely cause**: The OSK click path doesn't intercept the physical
  Shift state correctly: the host's Shift is already in the held set
  when the OSK click emits the regular scancode, so OS sees
  Shift+key.  But for some reason Ю escapes this path while ШЩЧЭ don't
  — likely a per-key emission difference in `OnScreenKeyboard.cpp`.
- **Investigation hints**: compare the key-emission path for Ю vs. one
  of ШЩЧЭ; check whether one releases the host Shift before sending
  and the other doesn't.

