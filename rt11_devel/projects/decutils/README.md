# decutils — DEC's RT-11 V5.4 utilities and handlers, built from their sources

The MS 0515 kits that survived carry a handful of programs: `DIR`, `DUMP`,
`DUP`, `PIP`, `DATIME`, `HELP`, `RESORC`, `TERM`, `BINCOM`.  DEC's own
V5.4 distribution has the sources of nearly everything else, and the
machine can assemble and link them itself.  This project drives that: the
command files DEC shipped are typed into a running MS 0515, with the real
`MACRO` and `LINK`, and what comes out is a kit for the `dec` profile.

## What it builds

`build_util.py` follows a utility's own `.COM` file line for line:

    python build_util.py [--sy FILE[,FILE...]] [--with FILE[,FILE...]]
                         UTIL [UTIL...] [OUTDIR]

Several utilities share one machine and one work volume — all their
sources are staged first, then each command file runs in turn.  A utility
that fails is reported and left behind; the rest still come out.  `--sy`
puts host files on the system volume, `--with` on the work volume beside
the sources.

`build_handler.py` does the same for device handlers, which had no command
file of their own (SYSGEN built them), so the recipe is the standard one —
assemble the source behind the system conditional file, link it with no
bitmap into `<DD>.SYS`:

    python build_handler.py [--sy FILE[,FILE...]] [--answers FILE]
                            DD [DD...] [OUTDIR]

The sources come from the software collection (`$MS0515_SOFTWARE`, else
`../ms0515-software` beside this repository), in `sources/rt11-v5.4`.

## The system volume is DEC's, except MACRO

`tools/` holds what the build runs on, all of it built here from DEC's
sources: `LINK.SAV`, `LIBR.SAV`, `SYSMAC.SML`, `SYSLIB.OBJ`.  Only
`MACRO.SAV` comes from the toolset's kit, because the V5.4 source
distribution has no source for it (`BINKIT.DAT` lists `MACRO.SAV` and
`CREF.SAV` as binaries; DEC did publish MACRO's sources, but for V02C, ten
years older).

That distinction is not bookkeeping.  Against the kit's own libraries the
build looks fine and comes out broken: `PIP`, the one utility with two
overlay regions, linked and ran straight into `?MON-F-Overlay error`.
Built against DEC's `SYSLIB` the same sources give a `PIP` that works —
3057 bytes apart.  The kit's `SYSLIB` is not DEC's (2365 bytes and 1 KB of
length apart); its `SYSMAC`, as it turns out, is (5 bytes, the date).

What the comparison says about the rest:

* `LINK` built from `LINK0-8.MAC` came out **byte for byte** the kit's
  `LINK.SAV` — the proof that this machine's `MACRO` and `LINK` reproduce
  DEC's binaries exactly, not merely something that runs;
* `LIBR` exists in no MS 0515 kit at all;
* `ULBLIB.OBJ` and `SYSMAC.SML` differ from DEC's in five bytes, the date
  they were made;
* of the utilities, `DUMP` came out byte for byte the kits' and `DIR`,
  `DUP` and `RESORC` within a handful of bytes; the kits' `PIP` is a
  patched one and differs throughout.

## What the machine's own kits never had

Of the handlers, this machine's own (`DZ`, `HD`, `VM`, `TT`) are not DEC's
to build — DEC's `DZ` is a terminal multiplexer, not a floppy controller.
What is worth taking is what needs no hardware of its own: `NL`, `LD`,
`SL`, `SP`, `BA`, `EL`, `LP`, `LS`.  They take nothing from the libraries:
built against the kit's and against DEC's they come out identical.

## Notes that cost time

* RT-11 prompts three ways: the monitor with `.`, a program run with `R`
  with the CSI's `*`, and a program can ask a question instead — LINK's
  `/D` ends the command line with `Duplicate symbol?` and reads names
  until an empty line.  Waiting for the wrong one waits forever.
* Empty lines in a command file are typed as they stand: that is how a
  list of library modules ends.
* `/C` on a listing and `/CROSSREFERENCE` on a command both ask for a
  cross-reference, which is made by running `CREF.SAV` — a program of the
  kit we do not have.  They are stripped.
* KMON reads 80 characters of a command and refuses the rest.  IND's link
  line is 90, so the logical device names come off it — but only where a
  file specification starts, or `/MAP:MAP:IND` becomes `/IND`.
* A command line carries its files in its switches
  (`LINK/LINKLIBRARY:OBJ:ULBLIB`), and a source pulls in more sources with
  `.INCLUDE`; both have to be followed or the volume comes up short.
* The terminal mirrors the machine's screen, so a scroll reprints lines
  that had long gone by: an error left standing would be read again as the
  next utility's.  After a failure the screen is scrolled clean.
* The ROM's own screen ends in dots, so a dot is no proof that a monitor
  came up.  A test that assumes it will pass on a machine that never
  booted.
