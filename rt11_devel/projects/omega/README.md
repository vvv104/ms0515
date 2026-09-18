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

## What Omega changed

| patch | module | what |
|---|---|---|
| 01 | `OMTRAP.MAC` | **the decoding trap**: a trap to 10 on the reserved pair 176401,176402 decodes the memory above the stack and resumes the decoded program |
| 02 | - | **protected vectors**: `LOWMAP` also protects 070, 130, 160, 164 and the words 300-306, 320-336 |
| 03 | `OMCONS.MAC` | **the console through the ROM**: pseudo-registers at 300-306, keys (`160004`) and characters (`160000`) through the ROM, the monitor interrupt as the output interrupt, 8-bit keys and characters for KOI-8, SO/SI shown at once, the MS 7007 rows released after each key and EMT (RMON, USR, KMON), both terminal interrupts at priority 7 |
| 04 | `OMCLOK.MAC` | **the clock**: each tick reads 177770 twice and runs the floppy motor's time-out (off 100 ticks after the last use); the clock interrupt at priority 7 |

The SYSGEN answers account for the rest of what differs from DEC's
distributed monitors - above all no user command linkage (`U$CL`), which
takes the UCF code out of KMON.

**The one difference kept on purpose**: Omega gave the fatal message
`?MON-F-System read failure halt` in Russian (KOI-8, «Останов по системной
ошибке чтения»); the owner's decision is to keep DEC's English text.  The
Russian text is 10 bytes longer and pushed a `BR E16.7A` after it out of
reach, so Omega has a `JMP` there; with the English text the `BR` stays.
From that message on, RMON is 12 bytes shorter than Omega's, and the
pointers into it differ by that much - `deltas.py` shows them as runs of
`14`/`177764`.

## The tools

| tool | what |
|---|---|
| `build_monitor.py [OUTDIR]` | the build; `$OMEGA_WORK` names a folder of working copies to build from instead of the patches |
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

96.5% of Omega's words are matched.  RMON, USR and KMON's root match but
for the message above.  Left: the bootstrap (the banner, the hardware
set-up) and KMON's overlays.
