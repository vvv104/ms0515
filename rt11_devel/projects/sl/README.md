# SL.SYS with Tab and a history — DEC's single-line editor, and ours

DEC's `SL` as [`../rt11/handlers/sl`](../rt11/handlers/sl/README.md) builds
it for the machine's console, with two things of our own: **Tab completes
the word** in the monitor's command line, and **the history is a ring** of
as many lines as fit where DEC kept two.  The rest is DEC's, keys and all.

| | on disk | in memory |
|---|---|---|
| DEC's SL | 13 blocks | 3624 bytes |
| this one | 21 blocks | 4090 bytes |

The eight blocks are the completion overlay, read from SL.SYS when Tab is
pressed into memory nobody is using at that moment.  The 466 bytes are what
reads it and hands it DEC's own routines, and the ring's code; the ring
itself is DEC's two buffers.  The resident part is kept under 4096 bytes:
past that the handler takes a ninth block of memory and SL.SYS a
twenty-second on disk.

## Tab

* The first word completes from KMON's command table: `TYP` becomes `TYPE`.
* A word after a `/` completes from that command's switches: `DIR/FU`
  becomes `DIR/FULL`, `DIR/` Tab lists DIR's switches.  The command may be
  abbreviated as KMON allows (`TY` is TYPE).
* A word after the command — after a blank, a comma or an equals sign — is
  a file: `TYPE STAR` becomes `TYPE STARTS.COM`, from the directory of the
  device typed with it (`SY:STA`, `DZ1:`), DK: when none is.
* Except where the command has words of its own: `SHOW DEV` becomes `SHOW
  DEVICES`; `SET` offers its subjects (KMON's EDI, WIL, ERR, TER, KMO, EXI,
  USR, TT and the installed devices whose handlers have SET options) and
  then the subject's options, the NO forms too, after commas as well (`SET
  TT NOQ` becomes `SET TT NOQUIET`).

One match is completed; several are completed as far as they agree and
listed at once, the prompt and the line shown again under the list.  A Tab
right after a Tab only beeps, so the list cannot be repeated by holding the
key.  Every list is the running monitor's, read at the moment Tab is
pressed.  Only at the end of the line, and only in KMON's own line: in a
program's line Tab is what DEC made it, a space.

## The history

DEC keeps the previous line (Up) and the one before it (GOLD Up), a buffer
of 80 bytes each.  Here the same 160 bytes are one ring: the lines run,
each with a zero after it, so a dozen short commands fit; the oldest go
when a new one does not.  An empty line is not kept, nor a line run twice
in a row.

* **Up** gets the next older line, and only beeps past the oldest; **GOLD
  Up** is Up.
* **Down** gets the next newer one; under the newest is an empty line - the
  line as it was before the first Up is not kept.
* On a line the history was not walked from, Down is DEC's: the line saved
  with GOLD Down.

That is the one place this SL parts from DEC's logic, and DEC's own test of
GOLD Up (`sl_tests`) does not pass on it.

## How it is made

Our files over DEC's `SL.MAC`, which stays outside the repository:

* **`SLTAB.diff`** — the hooks, applied after the console patch of
  `../rt11/handlers/sl`: DEC's Tab routine jumps to ours; the impure area's
  spare byte is cleared with the area and shifted out at every key, as DEC
  does with GOLD, so that a Tab right after a Tab is known; our two sources
  are included.
* **`SLTAB.MAC`** — what stays in memory.  `TABK` looks at KMON's running
  flag (RMON+450); `OVGET` finds the overlay or reads it; five entries give
  the overlay DEC's `Insert`, `Refrsh`, the bell and the screen.
* **`SLOVL.MAC`** — the overlay, in the section of DEC's SET overlays, on a
  block of its own after them.  SL.SYS's place on SY: is its `$DVREC` word
  in RMON's tables, SY: is opened whole, and the overlay is read to 16
  blocks under the USR ([RMON+266]) less its size — above the image of the
  last program (its top in word 50).  Nobody owns that memory while KMON
  reads a line, and the next program just overwrites it: the next Tab finds
  the overlay by its address and the sum of its code, and reads it again
  only when it is gone.  Its variables follow it in memory, not in the
  file.

* **`SLHIS.diff`** and **`SLHIS.MAC`** — the ring: DEC's `SavOld`, `Up` and
  `Down` jump to ours, GOLD Up's routine is gone, the word that pointed at
  the second buffer says where Up and Down stand.
* **`SLOPT.MAC`** — `Job$ = 1`: DEC's table of impure areas, eight jobs by
  default, for the one job a single-job monitor has.

What the overlay reads, all of it the running monitor's:

* KMON's command table, through KMON's parser — found by its first words
  under the word RMON+352 points at.  The names are 6-bit characters, bit
  200 on a name's last, bit 100 where an abbreviation may not stop yet.
* The switches live in KMON's overlays — two blocks of the monitor file
  each, read into KMON's overlay area only while their command runs.  The
  overlay walks KMON the way KMON walks itself: the command's dispatch word
  gives its overlay and entry (the dispatch code is looked for after the
  parser: a monitor generated with UCL has it further on); that overlay is
  read from SY: into the USR's buffer; the entry's `JSR R1,.+14` and `CALL
  SETUP` give the command's descriptors, parse program and names.  SHOW's
  keywords and SET's subjects come from their overlays the same way, the
  devices from RMON's `$PNAME`, a handler's options from block 0 of its
  file.  A monitor the overlay does not recognise leaves SL without that
  completion, nothing else.
* File names come from the device's directory, read by the overlay itself:
  `.DSTATUS` first (a `.LOOKUP` of a device there is not is the monitor's
  fatal error), the device opened whole, its segments read into the USR's
  directory buffer — BLKEY (RMON+256) cleared first, so the USR reads its
  own segment afresh — and the channel purged.

The overlay came from the SL written from scratch before DEC's source was
at hand (branch `sl-editor`), where it served five monitors found by
reverse engineering; the lookups above are that code's.

## Build and test

    cmake -S . -B build -G Ninja
    cmake --build build

A CMake project on `ms0515-run` (`../../toolset/cmake/Rt11.cmake`), DEC's
sources named by `$MS0515_RT11_SOURCES`.  `SL.SYS` comes out in
`build/work/sl.sys`, its listing and map beside it.

`tests/` is its harness in the emulator's test build (`sltab_tests`), on
the machine of DEC's SL's tests — the dec system composed from the software
collection — with this build on it: the commands, the switches, SET and
SHOW, the files, the overlay's place in memory, a program's line, the
ring.  It tests
`build/work/sl.sys`; another build is named: `sltab_tests --sl-sys=<file>`.
DEC's own keys are tested on the same file by
`sl_tests --sl-sys=<file>`.
