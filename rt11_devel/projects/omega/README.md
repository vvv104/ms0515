# The kits' monitors, rebuilt from DEC's sources

The RT-11 monitors of the MS 0515 kits survived without their sources.
Each is a SYSGEN of DEC's RT-11 V5.4 sources with the machine's own code
added, so its sources can be rebuilt: DEC's files, the answers its SYSGEN
was given, and the changes the kit made - until the build is the kit's
monitor byte for byte.  Done for the ОМЕГА builds (two), ОСА and Mihin's
OS-16SJ; the same sources give DEC's RT-11 on the MS 0515.

## How it is made

- **DEC's sources** come from the software collection, `sources/rt11-v5.4/`
  (`$MS0515_SOFTWARE`, else `../ms0515-software` beside this repository).
- **The SYSGEN answers** are a file for each build (`SYCND.MAC`,
  `SYCOM2.MAC`, `SYCOSA.MAC`, `SYCDEC.MAC`, `SYCMIH.MAC`) - read back from
  the monitor's options word and its code - and `DEVTBL.MAC` (SYSGEN's
  device-table text with the kits' devices: DZ, LS, NL, then VM and VS as
  user devices, four spare slots; Mihin's RK, DU, DX, VM, DZ, DW, MT, LP,
  LS, NL and two spare).
- **Each architectural difference is a module** `OMxxxx.MAC` of macros,
  gathered by `OMEGA.MAC`, which every part of the monitor assembles after
  `EDTGBL`.  DEC's files only call them where the kits changed them: those
  calls are the patches in `patches/`, one per difference, applied in the
  order of `patches/series`.  A one-line change of DEC's own line (a
  constant, a priority) is made in the patch itself, commented with the kit.
- **The answers file says whose monitor the build is** (`OM$KIT`: 0 DEC's,
  1 Omega's, 2 OSA's, 3 Mihin's) and which of the choices of their own it takes: the
  texts in Russian (`OM$RUS`: none, Omega's two, OSA's all - a choice of
  its own, so that any build can take them), the decoding trap (`OM$TRP`),
  the cursor blink (`OM$BLK`).  See `OMEGA.MAC`.

## The builds

`build_monitor.py --profile P`:

| profile | answers | the monitor | built |
|---|---|---|---|
| `omega` | `SYCND.MAC` | ОМЕГА SJ(S) V05.04, the 059 disk (`RT11SJ.SYS` sha `ad6d31b`, the same on 062 and 063) | byte for byte |
| `omega2` | `SYCOM2.MAC` | the other ОМЕГА build, the vvv104 disks (sha `2c1f616`: `disk3`, `h0`, `PAPER`'s head 0) | but for a flipped bit its copies carry |
| `osa` | `SYCOSA.MAC` | ОСА Версия 1.0, the 058 disk (`MON8SJ.SYS` sha `17e8d86`, the same on `osa`, `System`, `System3`) | byte for byte |
| `mihin` | `SYCMIH.MAC` | OS-16SJ (C)Mihinsoft, the collection's `systems/mihin/RT11SJ.SYS` (sha `bc9b0f4`) | byte for byte |
| `dec` | `SYCDEC.MAC` | DEC's RT-11 V5.4 SJ on the MS 0515 | - |

The five build at once (each on an image of its own) in about two and a
half minutes.

The collection's `systems/*.dsk` carry these monitors with the
collection's own startup patch: the startup command baked into the
bootstrap made `@START` (see the collection's `systems/README.md`).  The
originals start `STARTS.COM` (Omega, like DEC) and `ST.COM` (OSA); so do
the builds here.

**`dec`** is the owner's aim: DEC's RT-11 with only what the machine needs.
Its SYSGEN answers are DEC's own (`SJFB.CND`: user command linkage on, no
"(S)" in the banner, no SJ timer support), `STARTS.COM`.  Out of the kits'
changes it leaves the decoding trap (no program of the collection carries
its pair), the NOP and HALT filler, the repeated CONFIG bits and the
Russian texts (the banner reads DEC's `RT-11SJ`); it keeps the 8-bit
terminal for Cyrillic (the owner's choice), and DEC's interrupt priorities
(4 for the terminal, 6 for the clock, where the kits have 7: DEC's run the
same under load, ^S/^Q included).  The console through the ROM sits in
DEC's terminal code without timer support the way OSA's monitor has it.

Tried and kept from the kits: the five silenced vectors (070, 104, 110, 134,
140).  Without them the emulator runs as well, but it raises no interrupt
through those vectors, so it cannot tell whether the iron needs them; they
stay, a guard against the machine's devices.

Boot disks in `package/assets/disks/`: `omega-dec.dsk` (profile omega as it
was before the Russian texts came in) and `dec-ms0515.dsk` (profile dec).
Both boot, list, copy and run programs.

## What the kits changed

| patch | module | what |
|---|---|---|
| 01 | `OMTRAP.MAC` | **the decoding trap** (Omega's only, `OM$TRP`): a trap to 10 on the reserved pair 176401,176402 decodes the memory above the stack and resumes the decoded program |
| 02 | - | **protected vectors**: `LOWMAP` also protects 070, 130, 160, 164 and the words 300-306, 320-336 (Mihin's: all of 060-076, 100-106, 130, 300-306) |
| 03 | `OMCONS.MAC` | **the console through the ROM**: pseudo-registers at 300-306, keys (`160004`) and characters (`160000`) through the ROM, the monitor interrupt as the output interrupt, 8-bit keys and characters for KOI-8, the MS 7007 rows released after each key and EMT (RMON, USR, KMON), the terminal interrupts at priority 7.  In DEC's terminal code with timer support (Omega: SO/SI shown at once) and without it (OSA: `OMTTI0`; no SO/SI shown, DEC's `SPL 0` kept) |
| 04 | `OMCLOK.MAC` | **the clock**: each tick reads 177770 (twice in Omega's, once in OSA's) and runs the floppy motor's time-out (off 100 ticks after the last use); Omega's clock interrupt at priority 7 |
| 05 | `OMBOOT.MAC` | **the bootstrap**: the timer interrupt on from the start and acknowledged in the bootstrap's handlers; priority 7 for the clock, the traps and the terminal output; the traps a T-11 does not take (no PSW address, no KT-11) taken by hand; memory sized up to 154000; no option probes and no KT-11 set-up (NOPs in their room: Omega 90 and says CONFIG's clock again, OSA 96); CONFIG says the clock exists (Omega: in the bootstrap, three times; OSA: in RMON's CONFIG itself); the console's input at vector 130; the keyboard rows reset; vectors 140, 070, 104, 134, 110 silenced; stop on an 11/23 or a J-11; OSA: `ST.COM` as the startup file, and a slip (below) |
| 06 | `OMRUS.MAC`, `OMRUSB/U/K.MAC` | **the banners and the texts in Russian**: see below |
| 07 | `OMBLNK.MAC` | **the cursor blink** (the vvv104 build only, `OM$BLK`): the clock interrupt counts ticks in a word of its own and calls the ROM's slot 160014 every sixteenth |
| 08 | - | **exit without a RESET** (Mihin's): `ZAP` leaves out DEC's delay, the `RESET` and the console's restore |
| 09 | - | **the default editor K52** (Mihin's): `PROGDF` names K52, KED for a VT52 |
| 10 | - | **`SPL` as `MTPS`** (Mihin's): each priority change an `MTPS` in place, off the `PSWLST` chain |

Patches 02-06 carry Mihin's variants too (see [Mihin's monitor](#mihins-monitor)).

The SYSGEN answers account for the rest of what differs from DEC's
distributed monitors: no user command linkage (`U$CL`) in any kit, which
takes the UCF code out of KMON; SJ timer support in Omega's and Mihin's,
none in OSA's; Mihin's also has device time-out (`TIM$IT`), the month
rollover of the date (`ROL$OV`) and an input ring of 80 characters, not
134.

## The banners and the texts

The banner is the kit's own: Omega's names the monitor ОМЕГА where DEC's
names it RT-11SJ, the same length, the rest DEC's («ОМЕГА SJ(S) V05.04»);
OSA's replaces the whole line («OCA    Версия 1.0», "OCA" in Latin), and
RMON's RAD50 name of the monitor reads `OCA` too.

The texts in Russian are a choice of their own (`OM$RUS`).  MACRO takes
ASCII only, so the letters (KOI-8) are bytes in the modules:

- **Omega's** (`OMRUS.MAC`): the fatal message, «Останов по системной
  ошибке чтения» for `?MON-F-System read failure halt`.
- **OSA's**: every message and prompt - 97 places of DEC's texts, 87
  texts, in three modules, one for each part of the monitor
  (`OMRUSB.MAC` BSTRAP, `OMRUSU.MAC` USR, `OMRUSK.MAC` KMON and its
  overlays), each included by its DEC file where `RU$ALL` is set: MACRO's
  work file holds only so many macros, and KMON's assembly is the largest.
  They are generated from OSA's monitor by `kitmsg.py`: a macro `RUnnn` for
  each text (it gives its length too, for KMON's tables), DEC's five text
  macros (`KMEROR`, `KMRTMG`, `PTXT`, `ERRMSG`, `CSIERR`) take it as
  `RU=RUnnn`, and the lines of text that stand alone call it themselves.
  Its fatal message is OSA's own: «Останов по ошибке чтения системы».

  **What OSA left in English**: the prefixes (`?MON-F-`, `?KMON-F-`,
  `?KMON-U-`, `?CSI-F-`, `?BOOT-W-`), the error levels (`IWEFU`), the
  program names KMON runs (RESORC, PIP, DIR, DUP, LINK, FORTRA, MACRO,
  DUMP, LIBR, SRCCOM, FILEX, DICOMP, FORMAT, BINCOM, ERROUT, QUEMAN), the
  commands KMON puts together (`RUN `, `R `, `:IND`, `!RUN SY:LD.SYS
  /C:-1`), the default file types (`.SYS.LST.BOT`), and within its
  messages the names that are names (TT.SYS, SWAP.SYS, USR, EMT, .FETCH,
  SYSGEN, NO).  The utilities' own texts are theirs, not the monitor's.

Either fatal message is longer than DEC's and pushes a `BR E16.7A` after
it out of reach: Omega has a `JMP` there, OSA a second `BR` - a step
placed just before `U$NLOK`.

## OSA's monitor

OSA's SYSGEN had no SJ timer support, so its MS 0515 code sits in DEC's
other variant of the terminal and clock code; the same macros serve both
kits.  Beyond that and its texts:

- **No SO/SI shown at once**: the RUS/LAT shift keys go to the ring like
  any key.
- **The timer is read once** a tick (Omega reads it twice).
- **No decoding trap.**
- **`BIS 100000,@#157720`** in the bootstrap, before it reads the
  directory: meant as `BIS #100000` - the floppy motor's time-out armed -
  it lacks the `#` and ORs the word at address 100000 into the ROM's copy
  of System Register A instead.  Kept (`OMLKRD`), it is the monitor.
- **`ST.COM`** as the startup file: `@ST` and four spaces over the room of
  DEC's `@STARTS` - the same length, as a patch of the file would do it.
  Other copies of this monitor in the collection read `@START` and `PMK`.

## Mihin's monitor

OS-16SJ, «Mihinsoft & SPF "Sensor" 1990», is a SYSGEN of its own: SJ
timer support and device time-out, the month rollover, an input ring of
80, and a device table of its own (RK, DU, DX, VM, DZ, DW, MT, LP, LS,
NL).  Its MS 0515 code is Omega's kind - DEC's terminal and clock code
with timer support - written again with changes of its own:

- **The ROM through a table**: the bootstrap writes `JMP @#160000` and
  `JMP @#160004` at 157400, and the terminal interrupts call 157400 and
  157404, not the ROM.  So the console can be taken over without touching
  the monitor.  The memory is sized up to that table, 157400 (in steps of
  64 words), not 154000, and the bootstrap clears the ROM's word 157676.
- **Seven bits**, as DEC's: the output strips the top bit (KOI-7: the
  Russian letters come by SO/SI), no SO/SI shown at once, no keyboard
  rows released after a key or an EMT (KMON's and USR's releases stay).
- **Priorities**: the keyboard at 5, the output and the clock at DEC's 4
  and 6; the output vector's own PS 7, as the kits'.
- **The clock** runs the motor's time-out first - 128 ticks, not 100 -
  then reads 177770 once.
- **`SPL` is `MTPS`** in place (patch 10); `GETPSW`/`PUTPSW` stay on DEC's
  `PSWLST` chain.
- **No `RESET` on exit** (patch 08), and **K52** for EDIT (patch 09).
- **The devices' vectors silenced by the table**: 070, 074, 104, 110, 120,
  124, 134 are entries of `VECHI` pointing at an RTI of their own; only
  FALCON's 140 is done by code.  `LOWMAP` protects all of 060-076,
  100-106, 130, 300-306.
- **The banner is encoded**: «OS-16SJ (C)Mihinsoft» is kept as the
  complement of each character plus a key (153, growing by 235), and the
  bootstrap decodes it where the others reset the keyboard rows - so the
  line cannot be read or changed in the file.
- CONFIG says the clock exists in RMON itself (as OSA's); the floppy
  motor's time-out is armed before the bootstrap reads the directory, with
  the `#` OSA's lacks.

Every macro defined takes room in MACRO's work file, and KMON's assembly
with OSA's texts has little: the macros only Mihin's build calls are
defined for it alone (`OMBOOT.MAC`).

## The vvv104 build

The vvv104 disks (the collection's `systems/omega2.dsk`) carry another
build of Omega's monitor: the same banner, the same SYSGEN answers, one
difference in the code, and one flipped bit that came with its copies.
The `omega2` profile is its `RT11SJ.SYS` but for that bit, which it
builds as DEC's.

- **The cursor blink** (`OMBLNK.MAC`).  At the head of the clock
  interrupt's DEC part, before DEC's `TIKCTR`, the monitor counts ticks in
  a word of its own and calls `@#160014` on every sixteenth.  In ROM-B that
  slot is the cursor blink (163440): it inverts the cursor cell and flips
  bit 5 of the ROM's flags word (157760).  The ROM's own timer does not
  call it; it is there for the monitor to call once RT-11 owns vector 100.
  The 059 Omega (`omega`) has no blink.
- **SET TT HOLD's flipped bit.**  The first word of `SETTTH` in the KMON
  overlays is `045303` where DEC has `DEC R3` (`005303`): bit 14 set.  The
  word makes no sense as a change: `BIC -(R3),R3` turns the `'\` from the
  command table into an address and the VT52 `ESC [` into rubbish.  Every
  copy of this monitor has it - the images of PAPER, PBF and LANG, the vvv104
  raw reads of disks 1 and 3, baspasfor - and no other monitor does (omega,
  OSA, Mihin, Rodionov have `DEC R3`).  So the bit flipped in the copy all of
  them were made from, before they spread: an error that came with the
  copies, not a part of the build, so the profile builds DEC's `DEC R3`
  and differs from the original monitor in that one byte (067171).

  Searched for an unflipped copy everywhere: every image, file and
  unpacked archive of the collection and the recovery work, and every
  sector of every TD0 read - CRC failures and retries included.  Each
  copy of `SETTTH` tells its own monitor by a word in the same sector
  (076 bytes on, an offset into RMON: `003410` in the vvv104 build, 16
  bytes longer by the blink, `003370` in the 059 one).  All 135 copies with
  `003410` have `045303`; none has `005303`.  The one near-miss,
  `corpus/files/2369ce55...bin` (`005303`, `003010`), is not a read: of its
  122 bytes that differ from the vvv104 monitor, 113 are the 059 Omega's -
  a hybrid of a May 2026 recovery run with the 059 build as the donor.

**Why this build hangs on ROM-A** (docs/kb/KNOWN_ISSUES.md): it was made
for ROM-B.
The blink is the only difference that touches the ROM, and it calls the slot
without asking which ROM is there.  ROM-A has six slots, not eight, and its
160014 is the cassette loader (162360): it waits for the tape's edges on
177602 and jumps into what it loaded - with no tape it never comes back,
and it is entered from the clock interrupt at priority 7.  ROM-A has no
cursor blink at all (no code inverts the cursor cell), so there is nothing
to point the call at instead.  Of the five system disks only this monitor
calls 160014.  The builder added the blink as code of its own, so the
machine it was built on had ROM-B's slot 160014 - and ROM-B it was: the
same owner's NC.PAS draws with ROM-B's pseudographics, which ROM-A has
not.  The emulator now ships ROM-A as dumped (its old `RTS` patch at 160014
is gone), so the pair hangs here as on the iron.  The 059 Omega, without
the blink, runs on either ROM.

## The tools

| tool | what |
|---|---|
| `build_monitor.py [OUTDIR] [--profile P] [--list]` | the build (about 95 s; `--list` adds the listings `where.py` and `regions.py` read, 115 s); `$OMEGA_WORK` names a folder of working copies to build from instead of the patches |
| `compare.py BUILT KIT [--diff]` | per-block and aligned comparison with a kit's monitor |
| `regions.py BUILDDIR KIT PART` | every difference of one part, the kit's code beside DEC's source |
| `deltas.py BUILT KIT PART` | the changed single words of a part by their delta: a run of equal deltas is a shifted pointer |
| `msgmap.py BUILT KIT [--all]` | the kit's texts beside DEC's, text by text: its own, kept, or to look at by hand |
| `kitmsg.py BUILT KIT WORKDIR OUTDIR` | the kit's texts as modules of macros and DEC's sources calling them (patch 06's generated part); runs again over its own output |
| `koi8mac.py FILE START END` | a stretch of bytes as MACRO lines (`.ASCII` and `.BYTE`) |
| `show.py`, `where.py`, `dis.py`, `words.py` | one difference; the DEC source line of a built address; a disassembly; words side by side |
| `mkpatches.py WORKREPO` | the working repository's commits (DEC's files, then one commit per difference) as `patches/` |

Reading the monitor file, remember the `PSWLST` chain (see
`docs/programming.md`, "What the monitor adds to DEC's"): the file has
link addresses where the running monitor has `MTPS`.

## Fixed along the way

- `tools/pdp11_disasm.py` named the branches 101000-103400 wrong (BCC came
  out as BHI); the first modules were written from its misreadings and
  corrected.
- `ms0515-disk put` added a second entry beside a file of the same name
  (branch `disk-put-replace`).
- `where.py` took the `.CSECT`s KMON's error texts go to for the section
  before them.
- The `.rtfs` folder device keeps a file the guest closed at its full
  tentative size and leaves MACRO's work files behind - open; the build
  uses an HD image instead.

## Where it stands

Complete for both Omega builds, OSA's and Mihin's.  Next: Rodionov's.
