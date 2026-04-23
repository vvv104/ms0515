# Known Issues

## ms0515-roma-original.rom + omega-lang.dsk — pink screen, tight loop

- **ROM**: ms0515-roma-original.rom (original unpatched ROM-A)
- **Disk**: omega-lang.dsk (Omega — language disk)
- **Symptom**: Screen fills with pink background, CPU enters tight loop.
  The patched ROM-A (`ms0515-roma.rom`) boots this disk successfully.
- **Likely cause**: Original ROM-A initialises video or memory differently,
  causing the Omega loader to write to wrong addresses or misconfigure
  the display mode.

## mihin.dsk — RUS/LAT switch interpreted as ^O

- **ROM**: all ROMs
- **Disk**: mihin.dsk
- **Symptom**: Switching from RUS to LAT mode is interpreted by the OS
  as Ctrl+O (^O) followed by a carriage return.  After that the system
  stops responding to key presses until the user switches back to RUS
  and then to LAT again.
- **Likely cause**: The RUSLAT toggle scancode or its timing is being
  misinterpreted by the keyboard driver as a control character sequence.

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
             --fd0 assets/disks/rodionov.dsk \
             --fd2 assets/disks/rodionov2.dsk
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

## TODO: FDC synchronous command execution (busy delay hack)

- **File**: `emu/core/src/floppy.c`, `finish_command()`
- **Symptom**: Without an artificial `busy_delay = 4` ticks after command
  completion, the BIOS poll loop (write command → wait BUSY=1 → wait
  BUSY=0) never sees BUSY rise and hangs forever.
- **Root cause**: All FDC commands execute synchronously in a single call
  to `fdc_write()`.  The real WD1793 takes milliseconds: Type I commands
  3–30 ms depending on step rate (r1r0 bits), Type II commands ~200 ms
  per disk revolution at 300 RPM.
- **Proper fix**: Implement a state machine in `fdc_tick()`.  Commands
  transition the FDC into states like `SEEKING`, `READING`, `WRITING`;
  the tick function counts down real timing delays and advances the FSM.
  BUSY is held naturally for the duration of the command.  Step rate
  should be derived from the command's r1r0 bits (6/12/20/30 ms at
  7.5 MHz = 45000/90000/150000/225000 ticks).
