# RT-11 for the MS 0515, from sources

Everything the machine's RT-11 is made of, rebuilt or written here so that
it has sources: the monitors, the handlers, DEC's utilities, and the tools
they are built with.

| folder | what |
|---|---|
| [`monitor/`](monitor/README.md) | The monitors: DEC's RT-11 V5.4 sources, a series of patches with what the machine changes in them, the machine's own modules (`OM*.MAC`), and a set of SYSGEN answers per system — ОМЕГА (two), ОСА, Mihin's, Rodionov's, all byte for byte the kits', and `dec`/`dec-ru`, DEC's own RT-11 on this machine. `build_monitor.py`. |
| [`handlers/dz/`](handlers/dz/DZ.MAC) | The floppy handler, written for the machine from what the kits' binaries do (`docs/kb/dz_handler.md`): the first `DZ` here with a source, a primary driver of its own, the controller's address where DEC's FORMAT looks for it, and DEC's error logging. `validate.py` is its oracle. |
| [`handlers/tt/`](handlers/tt/README.md) | The terminal handler: DEC's `TT.MAC` and a patch of six lines, which is ОМЕГА's `TT.SYS` to the byte. |
| [`handlers/vm/`](handlers/vm/README.md) | The memory disk: seven of the extra banks as a 112-block volume through the ROM's bank routines.  Its source written back from the kits' binary, which it builds to the byte. |
| [`handlers/hd/`](handlers/hd/README.md) | The paravirtual hard disk `HD:` of the emulator: Patron's HD driver kit v2.0 adapted to the machine, with its own oracles.  It already carries `ERL$G`. |
| [`kit/`](kit/README.md) | The builders of the kit: `build_util.py` types DEC's own command files into a running machine, `build_handler.py` builds handlers — DEC's, DEC's with a patch, or the machine's own — and `verify_kit.py` uses what came out on a real system. |
| `tools/` | What all of it is built with, itself built from DEC's sources: `LINK`, `LIBR`, `SYSMAC.SML`, `SYSLIB.OBJ`.  Only `MACRO` comes from the toolset's kit, the V5.4 source distribution having no source for it.  Not bookkeeping: the toolset's `SYSLIB` is not DEC's, and a `PIP` linked against it builds without a complaint and dies of an overlay error. |

DEC's sources are not here: they come from the software collection
(`$MS0515_SOFTWARE`, else `../ms0515-software` beside this repository),
`sources/rt11-v5.4`.  What the machine changes in a file of DEC's is kept
as a patch over it, never as a copy.

A monitor takes only handlers whose sysgen word is its own, so the
conditionals of a system (`monitor/SYC*.MAC`) are the handlers'
conditionals too — `kit/build_handler.py` assembles every handler behind
the answers of the profile it is for.
