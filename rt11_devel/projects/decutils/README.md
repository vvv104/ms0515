# decutils — DEC's RT-11 V5.4 utilities and handlers, built from their sources

The MS 0515 kits that survived carry a handful of programs: `DIR`, `DUMP`,
`DUP`, `PIP`, `DATIME`, `HELP`, `RESORC`, `TERM`, `BINCOM`.  DEC's own
V5.4 distribution has the sources of nearly everything else, and the
machine can assemble and link them itself.  This project drives that: the
command files DEC shipped are typed into a running MS 0515, with the real
`MACRO` and `LINK`, and what comes out is a kit for the `dec` profile.

## What it builds

`build_util.py` follows a utility's own `.COM` file line for line:

    python build_util.py [--sy FILE[,FILE...]] UTIL [UTIL...] [OUTDIR]

Several utilities share one machine and one work volume — all their
sources are staged first, then each command file runs in turn.  A utility
that fails is reported and left behind; the rest still come out.  `--sy`
puts host files on the system volume over the ones the toolset brings,
which is how a tool built here takes over from the kit's.

`build_handler.py` does the same for device handlers, which had no command
file of their own (SYSGEN built them), so the recipe is the standard one —
assemble the source behind the system conditional file, link it with no
bitmap into `<DD>.SYS`:

    python build_handler.py [--answers FILE] DD [DD...] [OUTDIR]

The sources come from the software collection (`$MS0515_SOFTWARE`, else
`../ms0515-software` beside this repository), in `sources/rt11-v5.4`.

## The bootstrap

`LINK` and `LIBR` are built from their own sources first, and then used:

* `LINK` came out **byte for byte** the same as the kit's `LINK.SAV` —
  which is the proof that this machine's `MACRO` and `LINK` reproduce
  DEC's binaries exactly, not merely something that runs;
* `LIBR` exists in no MS 0515 kit at all, and is now in `toolset/build_tools`;
* `ULBLIB.OBJ`, the utility library the utilities link against, differs
  from the one DEC shipped in five bytes — the date it was made.

`MACRO` stays the kit's: DEC never shipped its sources.

## What the machine's own kits never had

Of the handlers, this machine's own (`DZ`, `HD`, `VM`, `TT`) are not DEC's
to build — DEC's `DZ` is a terminal multiplexer, not a floppy controller.
What is worth taking is what needs no hardware of its own: `NL`, `LD`,
`SL`, `SP`, `BA`, `EL`, `LP`, `LS`.

## Notes that cost time

* RT-11 prompts three ways: the monitor with `.`, a program run with `R`
  with the CSI's `*`, and a program can ask a question instead — LINK's
  `/D` ends the command line with `Duplicate symbol?` and reads names
  until an empty line.  Waiting for the wrong one waits forever.
* Empty lines in a command file are typed as they stand: that is how a
  list of library modules ends.
* `/C` on a listing asks MACRO for a cross-reference, which it makes by
  running `CREF.SAV` — a program of the kit we do not have.  It is
  stripped.
* The terminal mirrors the machine's screen, so a scroll reprints lines
  that had long gone by: an error left standing would be read again as the
  next utility's.  After a failure the screen is scrolled clean.
