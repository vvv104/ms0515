# What ms0515-run is made of

The files `ms0515-run` needs, kept here as they are so that the emulator
builds from its own files alone: nothing in its build reaches for another
repository.  Their bytes come from the software collection
(<https://github.com/vvv104/ms0515-software>).

- `system.dsk` - the diskette `ms0515-run` boots: RT-11 V5.4 as DEC built
  it for this machine - the monitor `RT11SJ.SYS` with its swap file, the
  handlers `DZ`, `TT` and `HD`, and `EM.SYS`, the emulator of the EIS/FIS
  instructions the processor lacks - and nothing of the system's
  utilities.  The build boots it, puts the starter on, and bakes the ROM,
  the diskette and the booted state into the binary (`../bake/main.cpp`),
  so `ms0515-run` stays one file that needs nothing else.  It is the
  collection's `dec` kit with OSA's `EM.SYS`, exactly what
  `ms0515-disk compose --repo <the collection> --system dec --media ss
  --add hd-rt11 --add em` writes.
- `DIR.SAV`, `PIP.SAV` - RT-11's DIR and PIP of the collection's `vvv`
  kit, as they are: the programs the tests run (`../tests`,
  `src/web/run_check.mjs`).

## Bringing them up to date

`tools/run_assets.py` takes them anew from the collection (beside this
repository, or `$MS0515_SOFTWARE_DIR`) - the diskette composed by this
repository's `ms0515-disk`, the programs copied - and writes the ones
that changed; `--check` only says whether they are current.  The change
then comes in as a pull request of its own, and CI bakes and tests it
like any other.
