# TT.SYS — the terminal handler of the MS 0515, as a patch over DEC's

DEC's `TT:` handler is driven by the transmitter's interrupt: it sets the
interrupt enable and the monitor's terminal service calls back for each
character.  The MS 0515 has no DL11 — its console is the ROM's, behind
pseudo-registers — so nothing ever interrupts, and output through a `TT:`
built from DEC's source stands still: `DUMP` to `TT:` prints nothing while
the same `DUMP` to a file finishes.

The kits' `TT.SYS` turned out to be DEC's `TT.MAC` with two things added,
found by laying its words against a build of DEC's:

* the `^` it writes to the pseudo-register for a read of block 0 is handed
  to the ROM's console output at `160000`;
* where DEC sets the interrupt enable, the monitor's own interrupt is
  asked for instead — bit 8 of the memory dispatcher, set through its copy
  at `157700` (`docs/programming.md`).

`TT.diff` is those two changes, in the form the monitor's patch series has
(`../../monitor/patches`).  Built from it the handler is ОМЕГА's `TT.SYS` **to the
byte**, which is the whole of its verification — and unlike the kit's
binary it can be built with the profile's conditionals (`ERL$G`), which a
monitor insists on matching its own:

    python ../../kit/build_handler.py --patch TT.diff TT OUTDIR
