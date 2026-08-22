# FIST - The Way Of The Exploding Fist for MS-0515 / RT-11 SJ V5.04

A faithful port of the 1985 ZX Spectrum game *The Way Of The Exploding
Fist* (Melbourne House) to the Soviet MS-0515.  The port follows the
pobtastic SkoolKit disassembly routine-for-routine: same mechanics, same
graphics, re-expressed in MS-0515 MACRO-11.

The original game's code and artwork are **external, non-committed data**
(the SAPER `K.DAT` pattern) - this repo never vendors them.  The build
reads the original tape image / runtime snapshot and emits the MACRO-11
source plus the game-state data file.

## Status

**Playable 1-player game.**  `FIST_GL=gamebg` builds the standalone game:

- the whole per-frame engine (`$9745`: round timer, hit detection and
  application for both fighters, the animation / recovery chains, the
  computer opponent's move selection and its scratch-state context switch)
  ported routine by routine and verified byte-exact against a Python
  reference that was itself validated against the Z80 simulation;
- the procedural fighter renderer (`$8833` / `$8A30` pose decoder in all
  four modes, `$BF13` logic-to-graphics bridge, per-fighter boxes) and the
  background engine (all three dojos), composited flicker-free over the
  scene with the 1:1 centred Spectrum framebuffer presentation;
- the 1UP match structure (`$AD18` round loop, `$AC5F` opponent loop):
  exchanges end on a knockdown or the 30 s clock, clean hits score half /
  full yin-yang, two yin-yang win the round, the clock is paid out as
  points, two rounds beat an opponent, the rank (`NOVICE`, `1ST DAN` ..
  `10TH DAN`) climbs and the dojo changes with it (`$AF34` -> `$9200`), a
  lost round is game over and a new game starts;
- a status strip (yin-yang, rank, six-digit score, clock) in an own 8x8
  font (the original's text uses the Spectrum ROM font), sound effects on
  reg C bit 5 bit-banged as the original's beeper effects, MS7004 keyboard
  control of player 1 (8 directions + fire, see Controls).

Not (yet) ported: the menu / attract screen, the 2-player mode and key
redefinition, the intro music, the winner's bow and get-up animations
(the final frame holds instead).

## Layout

```
rt11_devel/projects/fist/
├── README.md          this file
├── LAYOUT.md          the whole-game memory layout decision
├── build.toml         declarative build manifest (macro11)
├── FIST.MAC           generated MACRO-11 source (build artifact)
├── FIST.SAV           built .SAV image - `R FIST` at the dot prompt
├── GST.DAT            game-state data the loader reads (build artifact)
├── validate.py        OS-oracle smoke test (boots, runs, checks clean exit)
└── source/
    ├── gen_fist.py      pre_build hook: dispatches on FIST_MODE, emits FIST.MAC
    ├── gamelogic_mac.py the game generator (FIST_GL=gamebg = the full game)
    ├── gamelogic_ref.py Python reference of the game logic (validated vs the sim)
    ├── fighter_mac.py   the fighter decoder port; decoder_ref.py its reference
    ├── setup_ref.py     the draw set-up chain reference ($C101 / $BF13 ...)
    ├── bg_data.py       background table extraction; bg_reference.py the engine
    ├── bg_expect.py     expected VRAM of each background (for the lib test)
    ├── sim_capture.py / trace_sprites.py   Z80-simulation capture tools
    └── preview.py / render_vram.py        host-side renderers (PNG)
```

## Building

The generator needs the original tape and runtime snapshot (not
committed).  Point it at your WotEF checkout with `WOTEF_DIR` (default
`C:\Users\voron\wotef`); SkoolKit 10 is used to read the snapshot.

```
FIST_MODE=gamelogic FIST_GL=gamebg python rt11_devel/toolset/build.py rt11_devel/projects/fist/build.toml
```

This runs `gen_fist.py` (emits `FIST.MAC` and `GST.DAT`), then assembles
and links with the real RT-11 SJ V5.04 `MACRO`/`LINK` inside the emulator,
producing `FIST.SAV`.  Other `FIST_GL` values build the verification
images (single routines, the combined frame, the draw chain, demos) that
the byte-exact oracles compare against the Python references.

## Running

Mount `package/assets/disks/fist_game.dsk` in the GUI emulator and boot -
it auto-runs `FIST` (via `STARTS.COM`).  Player 1 is the human, the opponent
is the computer.

### Controls

The original reads a joystick (or 8 definable direction keys + fire) and
resolves the control bits through its `$98DD` table *relative to the way the
fighter faces*.  The MS7004 sends make codes only (no release codes, auto-
repeat for the last key), so the port takes the direction from the keypad /
arrows and FIRE as a pulse from Space, VR (Shift) or SU (Ctrl) - hold the
direction and tap the fire key (or press fire first).  "Forward" is towards
the opponent.

| direction          | keys                 | no fire                | with fire (Space / Shift / Ctrl) |
|--------------------|----------------------|------------------------|----------------------------------|
| up                 | KP8, Up              | jump                   | flying kick                      |
| up + forward       | KP9 (facing right)   | high punch             | roundhouse kick                  |
| forward            | KP6, Right           | walk forward           | front kick                       |
| down + forward     | KP3                  | low punch              | low kick                         |
| down               | KP2, Down            | crouch                 | foot sweep                       |
| down + back        | KP1                  | backward somersault    | reverse (back) sweep             |
| back               | KP4, Left            | walk back              | spinning back kick               |
| up + back          | KP7                  | forward somersault     | reverse high kick                |
| fire alone         |                      | nothing                |                                  |

(With the fighter facing left the keypad diagonals mirror: KP7 = up+forward
etc. - exactly the original's two table halves.)

**Blocking** is automatic: step *back* (KP4 / Left) while the opponent's
attack is in range and the fighter raises the matching guard - a high block
(action 20, arm up) against high attacks, a low block (19) against low ones
(`$9920`/`$9964`: the walk-back move 3 takes the `$A926` guard for the
incoming attack).  The guard pose then leaves the attack out of reach (the
reach tables are indexed by the defender's pose).  Crouching ducks high
attacks, jumping clears sweeps, somersaults evade; a connecting hit scores a
full or half yin-yang by distance.  The computer also holds an on-guard
stance (18) of its own.

```
package/ms0515.exe --disk0 package/assets/disks/fist_game.dsk
```

Refresh the disk after a build with `ms0515-disk rm` + `put` (not
`squeeze`) of `FIST.SAV` and `GST.DAT`, and verify with `get` + compare.
The game must be started as a command (`R FIST`), not `RUN FIST` - the
loader's file I/O (`.LOOKUP`/`.READW`) is rejected under `RUN`.

## Memory layout (runtime)

See `LAYOUT.md` for the decision; the live game uses:

| octal            | banks   | holds                                          |
|------------------|---------|------------------------------------------------|
| 01000-037777     | 0-1     | code, per-fighter compose copies, stack         |
| 040000-057777    | 2       | the three backgrounds' tables (read with the VRAM window off) |
| 060000-077777    | 3       | `GST.DAT` read buffer (top 4 KB) during the load |
| 040000-077777    | VRAM    | the video window while the game runs (03217 / 03377) |
| 0100000-0157777  | 4-6     | background engine + `SCRBUF` + `DOJOBUF` (primary) |
| 0100000-0157777  | 12-14   | the game state `$9C00..$F801` (extended, from `GST.DAT`) |

One dispatcher bit flips slots 4-6 between the dojo (primary) and the game
state (extended); the logic and the decode run at 03217, the compositor
and the HUD at 03377.

## Tests

`src/lib/tests/test_fist_game.cpp` boots the game headlessly on a folder
device and checks it from the outside (VRAM + the extended-bank state):
render, forced-score HUD, keyboard, scoring over a long fight, and that
the background follows the rank pixel-exactly (needs `bg_expect.py`'s
dumps).  All opt in through env vars:

```
FIST_GAME_SAV=rt11_devel/projects/fist/FIST.SAV FIST_GAME_DAT=rt11_devel/projects/fist/GST.DAT \
FIST_SYSTEM_DIR=rt11_devel/toolset/system FIST_BG_EXPECT_DIR=rt11_devel/projects/fist \
FIST_GAME_VRAM_OUT=rt11_devel/projects/fist/game_run.bin \
src/build/Release/lib/tests/ms0515_lib_tests.exe --test-case="fist: *"
```

`FIST_DSK=package/assets/disks/fist_game.dsk` additionally boots the real
disk.  `render_vram.py` turns a VRAM dump into a PNG to eyeball a frame.
`test_fist_screen.cpp` is the byte-exact oracle the verification images
use (loads a `.SAV` directly, runs it, compares windows of memory).
