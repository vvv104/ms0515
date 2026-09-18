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
  support only, 50 Hz - read back from the monitor's options word) and
  `DEVTBL.MAC` (SYSGEN's device-table text with Omega's devices: DZ, LS, NL,
  then VM and VS as user devices, four spare slots).
- **Each architectural difference is a module** `OMxxxx.MAC` of macros,
  gathered by `OMEGA.MAC`, which every part of the monitor assembles after
  `EDTGBL`.  DEC's files only call them where Omega changed them: those calls
  are the patches in `patches/`, one per difference, applied in the order of
  `patches/series`.

| patch | module | what |
|---|---|---|
| 01 | `OMTRAP.MAC` | the decoding trap: a trap to 10 on the reserved pair 176401,176402 decodes the program above the stack |
| 02 | - | `LOWMAP` also protects the vectors of the machine's devices (070, 130, 160, 164) |
| 03 | `OMCONS.MAC` | the console through the ROM: pseudo-registers at 300-306, keys and characters through the ROM's entries, the monitor interrupt as the output interrupt, 8-bit keys for KOI-8 |

`build_monitor.py` builds `RT11SJ.SYG` the way SYSGEN's MONBLD does - four
assemblies (`SJ`+`SYCND`+`EDTGBL`+`OMEGA`+ the part's files) and the LINK -
with the real MACRO and LINK in the emulator, on an HD image.

## The tools

| tool | what |
|---|---|
| `build_monitor.py [OUTDIR]` | the build; `$OMEGA_WORK` names a folder of working copies to build from instead of the patches |
| `compare.py BUILT OMEGA [--diff]` | per-block and aligned comparison with Omega's monitor |
| `regions.py BUILDDIR OMEGA PART` | every difference of one part, Omega's code beside DEC's source |
| `show.py`, `where.py`, `dis.py`, `words.py` | one difference; the DEC source line of a built address; a disassembly; words side by side |
| `mkpatches.py WORKREPO` | the working repository's commits (DEC's files, then one commit per difference) as `patches/` |

## Where it stands

92.3% of Omega's words are matched.  RMON is done up to the clock
interrupt; what is left: RMON's clock (40 words at `LKINT` - a count to 100
ticks and a write to 177600, likely the floppy motor), a message and a
branch after it; the bootstrap (the banner and the hardware set-up); KMON
and its overlays.
