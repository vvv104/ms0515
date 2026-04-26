# Known Issues

## ms0515-roma-original.rom + omega-lang.dsk — pink screen, tight loop

- **ROM**: ms0515-roma-original.rom (original unpatched ROM-A)
- **Disk**: omega-lang.dsk (Omega — language disk)
- **Symptom**: Screen fills with pink background, CPU enters tight loop.
  The patched ROM-A (`ms0515-roma.rom`) boots this disk successfully.
- **Likely cause**: Original ROM-A initialises video or memory differently,
  causing the Omega loader to write to wrong addresses or misconfigure
  the display mode.

## Mihin (OS-16SJ) — РУС/ЛАТ key prints `^N`/`^O`, locks input on exit

- **ROMs**: ROM-A and ROM-B both affected (different flavours)
- **Disks**: any Mihinsoft / OS-16SJ image (`mihin.dsk`, `kbtest_mihin.dsk`)
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

## rodionov.dsk — copy protection (self-modifying code, partial bypass)

- **ROM**: all ROMs
- **Disk**: rodionov.dsk (forum collection, RT-15SJ with ROSA Commander v1.3
  by Rodionov S.A., Voronezh, 1993)
- **Protection mechanism (fully reversed)**:
  1. Trap vector 64 fires periodically; the handler at RAM `0o137374`
     reaches the protection block via RAM `0o141744`.
  2. RAM `0o141770: TSTB @#63` — if byte `0o63` is zero, the code runs
     the sector-3 loader + patch sequence; non-zero skips straight to
     `0o142102`.
  3. Loader path (RAM 141776-142076):
     - Builds an 8-byte param block at `0o142014` with fields
       `[sector/side=001430, drive/reg_a=001000, dest=000060, count=000002]`.
     - `ADD #110010, @142042` patches the JSR target (160010 → 070020).
     - `JSR PC, @#160010` calls the ROM's generic sector-read routine
       (ROM 160010 is a JMP trampoline to ROM `0o162652`).
     - The read pulls exactly **4 bytes** from FD2 side-1 sector 3 into
       RAM `0o60–0o63`.  Destination and count are hard-coded in the
       param block; there is **no length-prefix or second read** driven
       by the data itself.
     - Four patches fire in sequence:
       * `MOV @#60, @142110` — word@60 becomes the first instruction slot
       * `MOV @#62, @142112` — word@62 becomes the second instruction slot
       * `MOV #105737, @142114` — forces `TSTB @#` opcode
       * `MOV #157760, @142116` — forces operand (→ `TSTB @#157760`)
     - `INCB @#63` marks "loader already ran" so subsequent trap passes
       skip the loader and land directly at `0o142102`.
  4. Execution then falls into the patched instructions at `0o142110`
     followed by the forced `TSTB @#157760` at `0o142114`, then
     `0o142120: BPL 142236`.  Both paths (fall-through at 142122 and
     BPL-taken at 142236) write to VRAM via `R2`, using data from
     `@#143056`/`@#143060` or `BIC #100000, @R2`.
  5. The 4 sector-3 bytes are thus **two 16-bit instruction words**
     that the original disk almost certainly encoded as
     `JSR PC, @#<loader>` — a call into a deeper initialisation routine
     that populated `@#143056/@#143060` with real logo bitmap data and
     left `@#157712`/`@#157760` in a self-consistent state before
     returning.  Without that call, the pixel-writing arms at 142122-140
     and 142236-246 run on uninitialised scratch.
- **Partial bypass**: `emu/assets/disks/rodionov2.dsk` contains bytes
  `03 01 01 01` at offset `0x400` (track 0 sector 3).  That encodes
  `BR +3` at RAM 142110, which jumps over both 2nd-slot and
  `TSTB @#157760` straight to `0o142120`.  Byte `0o63 = 1` satisfies
  the non-zero check on subsequent passes.  Boot completes, ROSA
  Commander renders Russian UI correctly, two thin stripes remain
  visible in the top-left column.
- **Residual stripes are unreachable from this patch slot**:
  - Last writes at the stripe offsets come from **outside** the
    protection block: `ROM 165200: BISB #377, @R2` and
    `RAM 146156: COMB @R2`.  Both are gated on `TSTB @#157760 → BPL`
    inside their own caller.
  - Bit 7 of `@#157760` is set by `ROM 174376: BIS #200, @#157760`,
    which is the side-1 (FD2) selector in the floppy driver — it
    re-asserts on every FD2 access and overwrites any `CLR @#157760`
    we install at 142110.
  - Attempted fix `CLR @#157760` (`1F 0A 70 DF`) added *more* stripes:
    the BPL-taken arm at 142236 then runs `BIC #100000, @R2` with
    `R2` undefined, planting fresh garbage writes in VRAM while the
    ROM side-drivers continued to redraw the original stripes.
  - Attempted fix `JMP @#142444` broke boot by skipping mandatory
    state stores (`MOV R3, @142166` and `MOV @X(PC), @#141740` at
    142164-142176) that downstream code depends on.
  - Attempted fix `MOV #1430, R2` (`C2 15 18 03`) produced identical
    output to `BR +3` (0 pixels different) — confirming the residual
    stripes do **not** originate inside the patch block, they come
    from the ROM/RAM sites above.
- **What would fix the residual stripes**: the original 4 bytes —
  almost certainly `004737 <addr>` — that `JSR`'d into a loader which
  set `@#143056/@#143060` (glyph data) and `@#157712/@#157760` (mode
  flags) to values consistent with a real logo rendering, so the
  `BISB`/`COMB` paths become logo draws instead of artefacts.
  Brute-forcing all 2³² combinations against expected visual output
  is the only remaining avenue without the source disk.
- **Boot command**:
  ```
  ms0515.exe --rom assets/rom/ms0515-roma.rom \
             --disk0-side0 assets/disks/rodionov.dsk \
             --disk0-side1 assets/disks/rodionov2.dsk
  ```

### Reverse engineering notes

- RT-15SJ is loaded at RAM base `0o66416` (file offset 0 → RAM
  `0o66416`).  Protection entry `0o141744`; param block `0o142014`;
  patch slots `0o142110/0o142112`; forced `TSTB @#157760` at
  `0o142114`; VRAM-writing arms `0o142122-140` and `0o142236-250`.
- Downstream stripe sites: `ROM 0o165200` (BISB #377, @R2) and
  `RAM 0o146156` (COMB @R2), both gated on `TSTB @#157760 → BPL`.
  `@#157760` bit 7 is set by `ROM 0o174376` on each FD2 side-1
  selection.
- The `--trace <path>` CLI flag (async spdlog-backed) records I/O
  access, memory dispatcher changes, reg_a writes, trap vectors,
  HALTs, and non-zero VRAM byte writes.  Ground truth for the
  analysis above came from diffing VRAM-write offsets between a
  `BR +3` run and candidate patch runs, then correlating the
  differing offsets back to the writing PC.

## ROM monitor `D` command with no disk attached — silent BIOS spin

- **Scenario**: From the ROM monitor prompt, press `D` to boot from FD0
  without any disk image mounted to that drive.
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
     this loop ("FD0 not ready — Reset or mount a disk").
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

