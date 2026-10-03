# SL.SYS — DEC's single-line editor on the MS 0515

DEC's `SL` of RT-11 V5.4, with its own logic and its own keys, built so
that it fits the machine's console.  It edits the monitor's command line
and the lines programs read through the CSI: the arrows move, characters
insert at the cursor, the last two lines can be had back.

## What stood in its way

Built as DEC ships it, `SL` asks the terminal what it is, gets no answer
from the ROM's console and talks VT100: every prompt left a `K` (the tail
of `ESC [ K`) and nothing moved the cursor.  What the console obeys was
read out of the ROM:

* **It is a VT52** (a VT62, even).  ROM-B's output routine (`160000`,
  its tables at `164140` and `164202`; ROM-A was not read, it passes the
  same tests) dispatches BS, TAB, LF, CR, BEL, SO and SI, and after ESC: `A` `B` `C`
  `D` (cursor), `H` (home), `J` and `K` (erase to the end of the screen,
  of the line), `Y` row column, `I` (reverse line feed), `T` / `U`
  (reverse video on / off - the VT62's), and some of its own.  A row
  out of range in `ESC Y` leaves the row as it is, which is the trick
  DEC's `SL` uses to set the column alone.
* **It does not know `ESC [`.**  An ESC followed by a character that is
  not in the table is dropped, so `ESC [ K` prints `K` and
  `ESC [ ? 2 l` prints `?2l`.  It answers no identification request.
* **The keyboard sends VT52 sequences**: the arrows `ESC A`..`ESC D`,
  PF1..PF4 `ESC P`..`ESC S`.

So `SL` is assembled for the VT52 alone - `SLCND.MAC`, `VT100$ = 0` and
`VT102$ = 0` ahead of the source: it asks nothing and sends nothing but
what a VT52 obeys.  DEC provided for that build and never finished it:
`SET SL ON` still reads the default terminal type, a byte that only the
builds for several types have (LINK: `Undefined globals: DVT100`), and
sends `ESC [ ? 2 l`.  `SL.diff` puts both under the conditional they
belong to - eight lines, none in the editor itself.

## The keys

DEC's, as on a VT52.  PF1 is GOLD: it gives the next key its second
meaning.

| key | what | after GOLD |
|---|---|---|
| Left, Right | move the cursor | to the start, the end of the line |
| Up | get the previous line | the line before it |
| Down | get the saved line | save this line |
| Delete (ЗБ) | delete before the cursor | put the deleted character back |
| Ctrl/U | delete to the start of the line | put the deleted line back |
| PF3 | delete to the end of the line | put the deleted line back |
| Backspace (Ctrl/H) | swap the character with the next | swap back |
| Ctrl/R, Ctrl/W | show the line again | |
| Return | enter the line | enter it cut at the cursor |
| PF2 | help: the picture of the keys, or the last error | |

PF4 is nothing: a VT52 has three such keys and DEC put the deletion on
the third.  `SET SL ON` / `OFF`, `SET SL TTYIN` (lines read with a plain
`.TTYIN` too), `SET SL LET` and the rest are DEC's
(*System User's Guide*, pp. 4-9 - 4-15).

## Build and test

    cmake -S . -B <build folder> -G Ninja
    cmake --build <build folder>

A CMake project on `ms0515-run` (`../../../../toolset/cmake/Rt11.cmake`):
DEC's `SL.MAC` is copied from `$MS0515_RT11_SOURCES` and patched, MACRO
assembles it behind `SYCDEC.MAC` (the dec monitor's conditionals) and
`SLCND.MAC`, LINK links it as DEC's `SL.COM` does - no bitmap, the
section `SETOVR` on a block's boundary.  `SL.SYS` (13 blocks) comes out
in `<build folder>/work/sl.sys`.

`tests/` is its harness in the emulator's test build (`sl_tests`): the dec
system composed from the software collection, the SL put on it, the keys
pressed and the screen read - `SET SL ON` leaving nothing behind, every
key of the table, the history, PIP's command line, the help picture.  It
tests the collection's `kits/dec/handlers/SL.SYS`; a fresh build is named:

    sl_tests --sl-sys=<build folder>/work/sl.sys

and `--sl-rom=` runs it on the other ROM.  Both pass.
