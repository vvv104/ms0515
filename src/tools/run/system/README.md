# The system ms0515-run carries

`system.dsk` is the diskette `ms0515-run` boots: RT-11 V5.4 as DEC built it
for this machine - the monitor `RT11SJ.SYS` with its swap file, the
handlers `DZ`, `TT` and `HD`, and `EM.SYS`, the emulator of the EIS/FIS
instructions the processor lacks - and nothing of the system's
utilities.  The build boots it, puts the starter on, and bakes the ROM,
the diskette and the booted state into the binary (`../bake/main.cpp`),
so `ms0515-run` stays one file that needs nothing else.

It is kept here as it is so that the emulator builds from its own files
alone: nothing in this repository reaches for another one.  Its
contents come from the software collection
(<https://github.com/vvv104/ms0515-software>) - the `dec` kit and OSA's
`EM.SYS` - and it is exactly what

    ms0515-disk compose --repo <the collection> --system dec --media ss --add hd-rt11 --add em system.dsk

writes.

## Bringing it up to date

When the collection's `dec` kit or `EM.SYS` changes, the collection's
`tools/run_system.py` composes the diskette anew with this repository's
`ms0515-disk` and writes it here; the change then comes in as a pull
request of its own, and CI bakes and tests the new system like any other
change.
