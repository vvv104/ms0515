# RT-11 for the MS 0515, from sources

Everything the machine's RT-11 is made of, rebuilt or written here so that
it has sources: the monitors, the handlers, DEC's utilities, and the tools
they are built with.

| folder | what |
|---|---|
| [`monitor/`](monitor/README.md) | The monitors: DEC's RT-11 V5.4 sources, a series of patches with what the machine changes in them, the machine's own modules (`OM*.MAC`), and a set of SYSGEN answers per system — ОМЕГА (two), ОСА, Mihin's, Rodionov's, all byte for byte the kits', and `dec`/`dec-ru`, DEC's own RT-11 on this machine. `build_monitor.py`. |
| [`handlers/dz/`](handlers/dz/DZ.MAC) | The floppy handlers - `DZ`, and `DV` and `MZ` from the same source (a prefix file picks the kind), written for the machine from what the kits' binaries do (`docs/kb/dz_handler.md`): the first `DZ` here with a source, a primary driver of its own, the controller's address where DEC's FORMAT looks for it, and DEC's error logging. `validate.py` is its oracle. |
| [`handlers/tt/`](handlers/tt/README.md) | The terminal handler: DEC's `TT.MAC` and a patch of six lines, which is ОМЕГА's `TT.SYS` to the byte. |
| [`handlers/vm/`](handlers/vm/README.md) | The memory disk: seven of the extra banks as a 112-block volume through the ROM's bank routines.  Its source written back from the kits' binary, which it builds to the byte. |
| [`handlers/ex/`](handlers/ex/README.md) | The electronic disk of the expansion board, 512 KB the machine can boot from.  Its source written back from the kit's binary, which it builds to the byte - the table of one board's bad pages included, or left out for a sound board. |
| [`handlers/hd/`](handlers/hd/README.md) | The paravirtual hard disk `HD:` of the emulator: Patron's HD driver kit v2.0 adapted to the machine, with its own oracles.  It logs nothing, and `SET HD ERLG=`/`TIMIT=` turn its sysgen word to whatever monitor it is loaded under. |
| [`kit/`](kit/README.md) | The builders of the kit: `build_util.py` types DEC's own command files into a running machine, `build_handler.py` builds handlers — DEC's, DEC's with a patch, or the machine's own — and `verify_kit.py` uses what came out on a real system. |
| `tools/` | What all of it is built with, itself built from DEC's sources: `LINK`, `LIBR`, `SYSMAC.SML`, `SYSLIB.OBJ`.  Only `MACRO` comes from the toolset's kit, the V5.4 source distribution having no source for it.  Not bookkeeping: the toolset's `SYSLIB` is not DEC's, and a `PIP` linked against it builds without a complaint and dies of an overlay error. |

DEC's sources are not here: they come from the software collection
(`$MS0515_SOFTWARE`, else `../ms0515-software` beside this repository),
`sources/rt11-v5.4`.  What the machine changes in a file of DEC's is kept
as a patch over it, never as a copy.

## Error logging: in the sources, not in the collection

`ERL$G` is a SYSGEN conditional, all or nothing: the monitor and every
handler are built with it or none is, because the monitor fills one more
pointer at a handler's end and the layout has to agree.  DEC's distributed
monitors did not have it - none of the answer files of the distribution
sets it - and neither do `dec` and `dec-ru`: a monitor with it refuses
every kit handler, which is too much to pay for a log that stays empty on
sound media and needs `LOAD EL` and `SET EL LOG` to fill at all.

The support is here all the same, where there is hardware to fail: the
floppy handlers report every failed try with the controller's registers,
and a request that came through, as DEC's disk handlers do (`DX.MAC`).
The rest carry the conditional with no code, as DEC's `VM`, `TT`, `NL`,
`LD` do.  A diagnostic set for a real machine is a copy of the answers
with `ERL$G = 1` and one command:

    python monitor/build_monitor.py OUT --profile dec      # with the answers edited
    python kit/build_handler.py --answers ANSWERS.MAC \
           --source handlers/dz --source handlers/vm --source handlers/hd \
           --patch handlers/tt/TT.diff \
           DZ MZ=MZPRE,DZ DV=DVPRE,DZ VM TT HD EL NL LD OUT

It was tried: with all of it built that way and an empty drive asked for a
directory, `ERROUT` reported the eight tries, the registers, the function
and the block (`docs/kb/dz_handler.md`).

A monitor takes only handlers whose sysgen word is its own, so the
conditionals of a system (`monitor/SYC*.MAC`) are the handlers'
conditionals too — `kit/build_handler.py` assembles every handler behind
the answers of the profile it is for.

## Handlers DEC already has

`LD`, `LP`, `LS`, `SL`, `NL`, `SP` come from DEC's sources as they are
(`kit/build_handler.py LD LP ...`); they get no project here.  What the
kits' copies of them turned out to be, for the record:

* **`LD` (omega)** is the `LD.MAC` of FODOS-3 (audit `B03`), the same
  code.  The words that differ are its table of mounted logical disks,
  which `MOUNT` writes back into the handler's file: `LD0` = `MZ:MARKET.DSK`,
  `LD1` = `MZ:VERA.DSK`, `LD2` = `MZ:SY7.DSK` - someone's working session,
  not a modification.  Mihin's is DEC's V5 `LD`, version 04.
* **`LP` (omega)** carries DEC's audit (`V05`, version 06) but is not a
  driver of the parallel printer port: it prints through the machine's
  serial interface (`177700`/`177702` in, `177720`/`177722` out, vectors
  `110`/`114`), programs the 8251 itself and obeys XON/XOFF.  `SET LP
  HANG`, `CSR`, `VECTOR` and `BIT8` are gone; `SET LP WAIT` (the delay
  between the 8251's commands) and `SET LP ROBOT` are added.  DEC's own
  answer to a serial printer is `LS`.  Note that the emulator's
  paravirtual disk takes `177720`/`177722` while it is enabled.
* **`SL`**: osa's and Mihin's are V8.00 (RT-11 V5.6, no source in the
  V5.4 kit), omega's is `B03` version 7 with no `SL.MAC` in the FODOS-3
  sources either.  DEC's V5.4 `SL` (version 46) builds and is what goes
  into the kit.
* **`NL` (rodionov)** is not a handler at all - a scrap of a command
  file under the name.
