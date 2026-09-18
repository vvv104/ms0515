# Omega's sources, rebuilt from DEC's

The ОМЕГА SJ(S) V05.04 monitor (`RT11SJ.SYS` of the collection's
`systems/omega.dsk`) survived without its sources.  It is a SYSGEN of DEC's
RT-11 V5.4 sources with the MS 0515's own code added, so its sources can be
rebuilt: DEC's files, the answers its SYSGEN was given, and the changes
Omega made - until the build is the Omega monitor byte for byte.

## How it is made

- **DEC's sources** come from the software collection, `sources/rt11-v5.4/`
  (`$MS0515_SOFTWARE`, else `../ms0515-software` beside this repository).
- **The SYSGEN answers** are Omega's own files here: `SYCND.MAC` (timer
  support only, 50 Hz, no user command linkage - read back from the
  monitor's options word and its code) and `DEVTBL.MAC` (SYSGEN's
  device-table text with Omega's devices: DZ, LS, NL, then VM and VS as
  user devices, four spare slots).
- **Each architectural difference is a module** `OMxxxx.MAC` of macros,
  gathered by `OMEGA.MAC`, which every part of the monitor assembles after
  `EDTGBL`.  DEC's files only call them where Omega changed them: those calls
  are the patches in `patches/`, one per difference, applied in the order of
  `patches/series`.  A one-line change of DEC's own line (a constant, a
  priority) is made in the patch itself, commented `Omega:`.

## The builds: Omega's two, and DEC's for the MS 0515

The same sources give three monitors, chosen by `--profile` (the answers
file sets `OM$EXA` and the flags of one build, see `OMEGA.MAC`):

- **`omega`** (`SYCND.MAC`, `OM$EXA = 1`, `OM$RUS = 1`) - Omega's monitor
  as the 059 disk has it (`RT11SJ.SYS` sha `ad6d31b`, the same on 062 and
  063) byte for byte: the reference every change is checked against.
- **`omega2`** (`SYCOM2.MAC`: `omega`'s answers and `OM$BLK = 1`) - the
  other Omega build, of the vvv104 disks (sha `2c1f616`: `disk3`, `h0`,
  `PAPER`'s head 0); see "The vvv104 build" below.

The collection's `systems/omega.dsk` and `systems/omega2.dsk` carry these
monitors with one byte of the collection's own changed: the startup
command baked into the bootstrap, `@STARTS` made `@START` (see the
collection's `systems/README.md`).  The originals, like DEC's, start with
`STARTS.COM`, and so do the builds here.
- **`dec`** (`SYCDEC.MAC`, `OM$EXA = 0`) - DEC's RT-11 V5.4 SJ on the MS 0515,
  the owner's aim: only what the machine needs, DEC's own SYSGEN answers
  (`SJFB.CND`: user command linkage on, no "(S)" in the banner),
  `STARTS.COM`.  Out of Omega's changes it leaves the decoding trap (no
  program of the collection carries its pair), the NOP and HALT filler
  and the repeated CONFIG bits, and the Russian strings (the
  banner reads DEC's `RT-11SJ`); it keeps the 8-bit terminal
  for Cyrillic (the owner's choice), and DEC's interrupt priorities (4 for
  the terminal, 6 for the clock, where Omega has 7: DEC's run the same
  under load, ^S/^Q included).  One answer differs from DEC's: SJ timer
  support (`TIME$R`) - the console Omega adapted is the timer variant of
  DEC's terminal code, and without it the monitor halts at boot.

  Tried and kept from Omega: the five silenced vectors (070, 104, 110, 134,
  140).  Without them the emulator runs as well, but it raises no
  interrupt through those vectors, so it cannot tell whether the iron
  needs them; they stay, a guard against the machine's devices.

Boot disks made from `systems/omega.dsk` with each monitor, in
`package/assets/disks/`: `omega-dec.dsk` (profile omega as it was before
the Russian strings came in, DEC's strings) and `dec-ms0515.dsk` (profile
dec, `STARTS.COM`).  Both boot, list, copy and run programs.

## What Omega changed

| patch | module | what |
|---|---|---|
| 01 | `OMTRAP.MAC` | **the decoding trap**: a trap to 10 on the reserved pair 176401,176402 decodes the memory above the stack and resumes the decoded program |
| 02 | - | **protected vectors**: `LOWMAP` also protects 070, 130, 160, 164 and the words 300-306, 320-336 |
| 03 | `OMCONS.MAC` | **the console through the ROM**: pseudo-registers at 300-306, keys (`160004`) and characters (`160000`) through the ROM, the monitor interrupt as the output interrupt, 8-bit keys and characters for KOI-8, SO/SI shown at once, the MS 7007 rows released after each key and EMT (RMON, USR, KMON), both terminal interrupts at priority 7 |
| 04 | `OMCLOK.MAC` | **the clock**: each tick reads 177770 twice and runs the floppy motor's time-out (off 100 ticks after the last use); the clock interrupt at priority 7 |
| 05 | `OMBOOT.MAC` | **the bootstrap**: the timer interrupt on from the start and acknowledged in the bootstrap's handlers; priority 7 for the clock, the traps and the terminal output; the traps a T-11 does not take (no PSW address, no KT-11) taken by hand; memory sized up to 154000; no option probes and no KT-11 set-up - CONFIG says the clock exists; the console's input at vector 130; the keyboard rows reset; vectors 140, 070, 104, 134, 110 silenced; stop on an 11/23 or a J-11 |
| 06 | `OMRUS.MAC` | **two strings in Russian** (`OM$RUS`): see below |
| 07 | `OMBLNK.MAC` | **the cursor blink** (the vvv104 build only, `OM$BLK`): the clock interrupt counts ticks in a word of its own and calls the ROM's slot 160014 every sixteenth |

KMON's overlays are DEC's, unchanged.

The SYSGEN answers account for the rest of what differs from DEC's
distributed monitors - above all no user command linkage (`U$CL`), which
takes the UCF code out of KMON.

**Two strings are in Russian** (`OMRUS.MAC`, KOI-8 as bytes, since MACRO
takes ASCII only) -

- the fatal message `?MON-F-System read failure halt`, «Останов по
  системной ошибке чтения».  It is 10 bytes longer and pushed a `BR E16.7A`
  after it out of reach, so Omega has a `JMP` there; with the English text
  the `BR` stays, RMON is 12 bytes shorter from there on, and the pointers
  into it differ by that much (`deltas.py` shows them as runs of
  `14`/`177764`);
- the banner: Omega's `BSTRNG` reads «ОМЕГА SJ(S) V05.04» where DEC's reads
  `RT-11SJ (S) V05.04` (the first eight characters, the same length).

At first the strings were left DEC's, and a control run put them into a
copy of the sources; since the build then was Omega's monitor byte for
byte, they became a patch of their own, on in both Omega profiles, so each
of them builds its monitor exactly.  So nothing else differs: DEC's V5.4
sources, the SYSGEN answers here, patches 01-06 are the whole of the Omega
monitor.

## The vvv104 build

The vvv104 disks (the collection's `systems/omega2.dsk`) carry another
build of the same monitor: the same banner, the same SYSGEN answers, one
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

**Why this build hangs on ROM-A** (docs/kb/KNOWN_ISSUES.md, "Omega-pink").
The blink is the only difference that touches the ROM, and it calls the slot
without asking which ROM is there.  ROM-A has six slots, not eight, and its
160014 is the cassette loader (162360): it waits for the tape's edges on
177602 and jumps into what it loaded - with no tape it never comes back,
and it is entered from the clock interrupt at priority 7.  ROM-A has no
cursor blink at all (no code inverts the cursor cell), so there is nothing
to point the call at instead.  Of the five system disks only this monitor
calls 160014.  The builder added the blink as code of its own, so the
machine it was built on had ROM-B's slot 160014.  The emulator's ROM-A has
that slot patched to `RTS`, which is why the pair boots here.  The 059
Omega, without the blink, runs on either ROM.

## The tools

| tool | what |
|---|---|
| `build_monitor.py [OUTDIR] [--profile P] [--list]` | the build (about 95 s; `--list` adds the listings `where.py` and `regions.py` read, 115 s); `$OMEGA_WORK` names a folder of working copies to build from instead of the patches |
| `compare.py BUILT OMEGA [--diff]` | per-block and aligned comparison with Omega's monitor |
| `regions.py BUILDDIR OMEGA PART` | every difference of one part, Omega's code beside DEC's source |
| `deltas.py BUILT OMEGA PART` | the changed single words of a part by their delta: a run of equal deltas is a shifted pointer |
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
- The `.rtfs` folder device keeps a file the guest closed at its full
  tentative size and leaves MACRO's work files behind - open; the build
  uses an HD image instead.

## Where it stands

Complete for both Omega builds: profile `omega` is the 059 Omega monitor
byte for byte, profile `omega2` the vvv104 one but for its flipped bit.
Next:
the monitors of OSA, Mihin and Rodionov.
