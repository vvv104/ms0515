# FORMAT for the diskettes of the MS 0515

DEC's `FORMAT` is a root and a module for each device, put together at
link time — "new format routines can be added without changes to the
root", its source says, and that is how this one goes in.  The root is
DEC's as it is; it already knows a `DZ` with device code 52, because that
is what DEC called the RX50 of the Professional 350, a diskette of the
same 80 tracks of 10 sectors.  As built from DEC's sources it answers

    .FORMAT DZ1:
    DZ1:/FORMAT-Are you sure? Y
    ?FORMAT-F-Invalid device for FORMAT

since DEC's `FMTDZ` is a stub (the Professional's controller could not
format), while `FORMAT/VERIFY:ONLY DZ1:` works with our `DZ.SYS` as it
stands — that part goes through the handler.

| file | |
|---|---|
| `FMTDZ.MAC` | The module, written for the machine's WD1793: a track's image built once — gaps, ten ID fields, ten data fields of `E5` — and handed to the controller's WRITE TRACK for each of the 80 tracks with the track and side stamped into the IDs. |
| `build_format.py` | Builds `FORMAT.SAV` by DEC's own `FORMAT.COM`.  The macros every FORMAT module opens with are taken from DEC's `FMTDZ.MAC` and put in front of ours, so none of DEC's text is kept here; `FMTDEV.MAC`, the table of devices, gets a line for `MZ`. |
| `validate.py` | The oracle: a diskette full of rubbish is formatted on the machine and the host reads the image. |

    python build_format.py OUTDIR
    python validate.py KIT OUTDIR/FORMAT.SAV ../../handlers/dz/DV.SYS ../../handlers/dz/MZ.SYS

## What had to be found out

* **DZ, DV and MZ are one routine.**  The three handlers differ in how
  they number the blocks, not in what is on a track.  A `DZ` unit is a
  side (bit 0 the drive, bit 1 the side); `DV` and `MZ` take the diskette
  whole, so both sides of every track are written.
* **The root finds a device by the code its handler answers `.DSTATUS`
  with, not by its name**, and the machine's handlers were given their
  codes carelessly: `DV` answers with `DZ`'s 52, and so does the memory
  disk `VM`.  The codes stay as the kits have them - nothing but FORMAT
  looks at them, and the same `FORMAT.SAV` then serves the kits' `DV.SYS`
  as well as ours - so `DV` has no entry of its own in the table and
  cannot have one.  What comes in as `DZ` is told apart by the size of the
  volume in the same answer, which the root keeps at the global `OUTSPC`:
  800 blocks are a side, 1600 the diskette, anything else is not a
  diskette and the routine refuses without touching the drive.  `MZ` has
  a code of its own (55) and a line of its own in the table, for 1600
  blocks only.
* **The name in the question comes from the table too** ("`DZ1:/FORMAT-Are
  you sure?`" for `DV1:`).  The root calls a module's load point before it
  asks, so ours is not the empty `RTS PC` of DEC's macro: it puts the
  entry's name right for a diskette that came in as `DV`.
* `FORMAT VM:` from a system booted off `DZ0:` never reaches the module:
  same code, same unit, and the root takes it for the system volume.
* **The gap after a data field is 30 bytes.**  With the 54 of the textbook
  layout ten sectors of 512 are longer than a revolution (6250 bytes at
  250 kbit/s and 300 rpm) and the index cuts the tenth short — on the
  real controller as in the emulator, where a core test pins it down.
* **The root asks for no memory before it formats**, only before a
  verify, so the module asks for the 6400 bytes of its track image itself
  (`.SETTOP`).
* **The controller's address is a constant.**  For a device with `DZ`'s
  code the root does not read the handler's header for it but asks the
  monitor the Professional's way, which is nothing to rely on here.
* A track is written with interrupts held off, as a sector is in the
  handler: a byte not given in time is a byte lost.  The motor's time-out
  in the ROM's copy of Register A is pushed back at every track.

The emulator's controller had no WRITE TRACK until this was needed
(`src/core/src/floppy.c`); it reads the formatter's stream as it arrives
and writes the sectors the ID fields name.
