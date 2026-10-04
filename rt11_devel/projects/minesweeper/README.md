# The Minesweeper Game, Ver. 1.01

V. V. Voronkov's minesweeper for the MS 0515, written in OMSI Pascal in
1995 - «The Minesweeper Game, Copyright (C) 1995 Ver. 1.01, Production by
Voronkov Software, Russia, Voronezh» - built from his sources, as far as
they go.  Three levels, a menu, a help screen kept enciphered, a check of
the program's own file.

No binary of this version survived, and the sources that did are not the
last word: they were caught in the middle of a rearrangement, and three
things the help describes are in none of them (below).  What is here is
those sources made to build and run, every change to them listed.

## Playing

    cmake -S . -B build -G Ninja
    cmake --build build
    cd build/game
    ms0515-run MINE

| key | |
|---|---|
| arrows | move over the field |
| Space | open the cell |
| Return | mark a mine, and take the mark off |
| F1 | help: two screens, Up and Down turn them, any other key returns |
| F2 | a new field of the same size |
| F3, F4, F5 | Beginner 8x8 with 10 mines, Intermediate 16x16 with 40, Expert 30x16 with 99 |
| F6 | Custom: the size and the count of mines typed in |
| F9 | the menu: the same and Marker, Best Times, Version |
| F10 | leave |

A mine opened ends the game with the mines shown; so does the last cell
without one, with the mines flagged.  Either way the next key leaves the
program - there is no second game without starting it again.

The author's program was `K`; here it is `MINE.SAV`, with its help
`MINE.HLP` - his word, in 2026 - and the one line of the main program that
names both says so.  The key list is for ROM-B, the author's machine:
ROM-A does not hand F1-F10 to a program at all, so on it the arrows, Space
and Return work and the rest does not.

## The sources

They are the author's, read out of his logical-disk container `PROGS.DSK`
and kept as they were in the software collection
(`programs/vvv/minesweeper/v1.01/`).  How the work went is in the files
themselves:

1. a single-file `K.PAS` with one field of 16x16 (the collection has it,
   and the `K.SAV` an earlier text of it gave);
2. `K.PAS` and a module `MS.PAS` - the sprites, the field, the help, the
   levels;
3. `MS.PAS` cut into `SPR.PAS` (the sprites and their drawing) and
   `RND.PAS` (the game), with `K.PAS`'s own procedures moving into
   `RND.PAS` too.

`K.PAS` is caught in the middle of step 3: its list of EXTERNAL
declarations is written and ends in the `}` of a comment that is no longer
opened, and under it still lie the copies of what `RND.PAS` holds by now,
CUSTOM's already emptied.  And `CHECK.PAS` is the step after: the main
program has `{ CHECK(0,FILNAME,S);}` commented out beside the
`GETTABSPR(S)` it was to replace.

## What was changed, and why

`MINE.PAS` (the collection's `K.PAS`), `SPR.PAS` and `RND.PAS` here are the
collection's with these changes and no others; the texts are KOI-8 with CR LF, as PAS1 reads them.

| file | change | why |
|---|---|---|
| `MINE.PAS` | `FILNAME:='K     '` reads `'MINE  '` | the program's new name: it opens `MINE.SAV` and `MINE.HLP` by it |
| `MINE.PAS` | the procedures between the declarations and the main program are gone; the stray `}` after `MENU; EXTERNAL;` too | they are the copies left from the move into `RND.PAS`, which defines every one of them; PAS1 takes neither the duplicates nor the brace |
| `MINE.PAS` | `GETTABSPR(S)` gives way to `CHECK(0,FILNAME,S)`, its declaration likewise | the author's own next step, as his commented line has it |
| `SPR.PAS` | the VAR block, commented out, is declared | the modules share their globals by declaring the same block; PAS1 does not take an empty VAR |
| `SPR.PAS` | `GETTABSPR` - the sprite table as 288 assignments, 5.5 KB of code - is replaced by the text of `CHECK.PAS` | `CHECK` reads the table from the program's file; with both the program does not fit (below) |
| `SPR.PAS` | `SETSPR` copies the sprite into a local and works its address out before it opens the video window | below |
| `RND.PAS` | five EXTERNAL lines for what it calls in `SPR.PAS`: `RND`, `PAUSE`, `SETSPR`, `PUTCUR`, `RESCUR` | it was never compiled apart from them |

### SETSPR and the video window

`SETSPR` draws a sprite by switching the video memory into the addresses
040000-077777 and writing eight words there.  While that window is open,
whatever lies at those addresses in ordinary memory is out of reach - and
OMSI Pascal puts a program's code first, the run-time library after it and
the globals after that.  The single-file game was small enough to lie
whole under 040000.  This one is not: its run-time library and its sprite
table `S` are in the window's range, and the original `SETSPR` - which
multiplied and indexed `S` with the window open - ran into video memory
and stopped the machine.

So `SETSPR` now takes the sprite into a local variable and computes the
address of its first line before the switch; inside, it only moves words,
the array checks off for those lines.  Its drawing is the same.  PASLIB's
own line drawing, which opens the window too, must lie under 040000 as its
manual says, and does - which is why the sprite table cannot stay as
assignments.

### MINE.SAV's last two blocks

`CHECK` opens `MINE.SAV`, sums the words of all its blocks but the last two,
compares the sum with the word that follows, skips eight words and reads
63 sprites of 8 words to the end of the file.  So the game's file is what
LINK makes and two blocks after it: the sum, seven words unused, and the
table - `MINE.DAT`, the author's `K.DAT`.  The author's way of writing them has not survived;
`tail.py` does what the reading implies.

`CHECK(0,...)` is the author's line as it stands: with 0 a wrong sum is
found and nothing is done about it; with 1 the game would stop.  The sum
is right - the tests compare what `CHECK` counted with what is stored.

## What the sources do not have

The help text (`MINE.HLP`, the author's `K.HLP`; deciphered in the collection's
README) speaks of
three things that are in none of the surviving sources:

* **the count of mines and the clock** over the field.  The sprites for
  them are drawn - 25 to 35, the halves of seven-segment digits - and the
  panel they belong in is there on the screen, empty;
* **the «?» mark.**  The menu has Marker on/off and keeps the flag, sprite
  15 looks made for it, and `POMMIN` does not look at either;
* **Best Times.**  `BESTTIMES` is an empty procedure.

Whether the game was ever finished, in a text that did not survive, the
files do not say.

## Build and test

A CMake project on `ms0515-run` (`../../toolset/cmake/Rt11.cmake`): PAS1
and MACRO for each of the three units, LINK in the order of the author's
`K.COM` - `MINE,PASLIB,SPR,RND,PAS1,FORLIB` - and `tail.py`.  The tools come
from the software collection: the compiler and its libraries, FORTRAN's
library for `RAN`, which the help's cipher is made with.

`tests/` is its harness in the emulator's test build (`mine_tests`): the
game started from `build/game` by the machine of `ms0515-run` and played -
the field of every level, a cell opened, a mine marked, a game lost and a
game won, the menu, the two screens of the help, the sum.  The field is
read from the program's memory to know where the mines are; what the
player sees, from video memory against `MINE.DAT`.
