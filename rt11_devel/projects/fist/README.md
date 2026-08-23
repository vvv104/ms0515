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
- the original's status strip in the Spectrum ROM font (read from the ROM
  at build time, not committed - like the rest of the art): the score and
  the clock at the top, the yin-yang symbols, "1 PLAYER" and the high
  score, the rank line - printed as the original prints, pixels over the
  dojo at its positions; the cyan border; the intro tune at every opponent
  presentation and the sound effects, both on reg C bit 5 bit-banged as the
  original's beeper; the high score (kept for the session); the settings
  screen ("0" in the demo: key redefinition, sound on / off); the
  2-player game ("2"); MS7004 keyboard control of both players (8
  directions + fire each, see Controls); the original's pace - a frame
  every 1/13 s, timed on timer channel 1 (its clock counts 13 frames a
  second, so a 30 s round is 30 s); the attract demo's own round flow
  (`$ABC8`: a round per dojo, three and it starts over).  The noise
  effects read the very ROM bytes the original reads and the rumble runs
  its `$B2D7` shift register, so each effect sounds as it does there.

Not ported: the settings screen's joystick choices (Sinclair / Kempston)
have no joystick on the MS-0515 and fall back to the default keys.  What
stays different by nature: the MS7004 sends no key-release codes, so a
held key is a timer refreshed by its auto-repeat (a key released less than
~0.2 s before the next press still counts as held); the AI's randomness
is an LFSR where the original reads the Z80's R register; the loading
screen holds ~3 s (or until fire) where the original's stayed for the
tape's minutes; a scene heavier than the MS-0515 can draw in 1/13 s (the
demo's two AI fighters changing pose every frame) runs below the pace.

## Layout

```
rt11_devel/projects/fist/
├── README.md          this file
├── LAYOUT.md          the whole-game memory layout decision
├── build.toml         declarative build manifest (macro11)
├── FIST.MAC           generated MACRO-11 source (build artifact)
├── FIST.SAV           built .SAV image - `R FIST` at the dot prompt
├── FIST.DAT            game-state data the loader reads (build artifact)
├── validate.py        OS-oracle smoke test (boots, runs, checks clean exit)
└── source/
    ├── gen_fist.py      pre_build hook: dispatches on FIST_MODE, emits FIST.MAC
    ├── gamelogic_mac.py the routine-level generator + verification images (FIST_GL=...)
    ├── game_build.py    the full game (FIST_GL=gamebg): captures the state, assembles
    ├── game_*.py        its MACRO text per subsystem: loader, dojo, compose, round,
    │                    hud, sound, keys; gst_addr.py the GST address helper
    ├── gamelogic_ref.py Python reference of the game logic (validated vs the sim)
    ├── fighter_mac.py   the fighter decoder port; decoder_ref.py its reference
    ├── setup_ref.py     the draw set-up chain reference ($C101 / $BF13 ...)
    ├── bg_data.py       background table extraction; bg_reference.py the engine
    ├── bg_expect.py     expected VRAM of each background (for the lib test)
    ├── sim_capture.py / trace_sprites.py   Z80-simulation capture tools
    └── preview.py / render_vram.py        host-side renderers (PNG)
```

## Building

The generator needs the original - never committed - next to the
disassembly it follows, pobtastic's
<https://github.com/pobtastic/wayoftheexplodingfist>: the tape, the
runtime snapshot and a mid-attract frame of it.  `prepare_wotef.py` makes
all three in a checkout of that repository (SkoolKit 10: `pip install
skoolkit`; the tape is fetched from World of Spectrum by the checkout's own
`tap2sna` script), and the built emulator must be in `package/`
(`cd src && conan build . --build=missing`):

```
git clone https://github.com/pobtastic/wayoftheexplodingfist
export WOTEF_DIR=$PWD/wayoftheexplodingfist          # the default is C:\Users\voron\wotef
python rt11_devel/projects/fist/source/prepare_wotef.py
FIST_MODE=gamelogic FIST_GL=gamebg python rt11_devel/toolset/build.py rt11_devel/projects/fist/build.toml
```

The attract frame the state is taken from is whatever the simulator is at
after 40 M T-states; any such frame makes a valid game (the start-up
re-initialises everything the match uses), only the bytes of `FIST.DAT`
differ from one to another.

This runs `gen_fist.py` (emits `FIST.MAC` and `FIST.DAT`), then assembles
and links with the real RT-11 SJ V5.04 `MACRO`/`LINK` inside the emulator,
producing `FIST.SAV`.  Other `FIST_GL` values build the verification
images (single routines, the combined frame, the draw chain, demos) that
the byte-exact oracles compare against the Python references.

## Running

`FIST.SAV` + `FIST.DAT` also live on `src/assets/disks/osa.dsk` (next to
SABOT2, the other ported game; omega-games.dsk had 115 free blocks for the
148 needed): boot it and `R FIST`.  Or mount
`package/assets/disks/fist_game.dsk` in the GUI emulator and boot -
it auto-runs `FIST` (via `STARTS.COM`).  As on the tape: the loading screen
shows while the game state loads, then the attract demo runs (two computer
fighters, "DEMO" on the strip); **fire (Space) or "1" starts a 1-player
game** - you are the left fighter, the opponent is the computer, its
personality climbs with the rank.  A lost round is game over and returns to
the demo.

### Controls

The original reads a joystick (or 8 definable direction keys + fire) and
resolves the control bits through its `$98DD` table *relative to the way the
fighter faces*.  The MS7004 sends make codes only (no release codes, auto-
repeat for the last key pressed), so the port keeps a hold timer per control
and treats keys pressed together as a chord: arrows + Space work like a
joystick (up+right, right+fire ...), held as long as any key of the chord
repeats; VR (Shift) and SU (Ctrl) are fire keys too, and the keypad 1-9
gives a diagonal in one key.  The one quirk of such a keyboard: a key
released less than ~0.2 s before the next press still counts as held.
"Forward" is towards the opponent.

| direction          | player 1             | player 2 | no fire                | with fire                |
|--------------------|----------------------|----------|------------------------|--------------------------|
| up                 | KP8, Up              | W        | jump                   | high punch               |
| up + forward       | KP9 (facing right)   | E        | forward somersault     | flying kick              |
| forward            | KP6, Right           | D        | walk forward           | front kick               |
| down + forward     | KP3                  | C        | foot sweep             | low kick                 |
| down               | KP2, Down            | X        | crouch                 | low punch                |
| down + back        | KP1                  | Z        | reverse (back) sweep   | spinning back kick       |
| back               | KP4, Left            | A        | walk back              | roundhouse kick          |
| up + back          | KP7                  | Q        | backward somersault    | reverse high kick        |
| fire               | KP5, Space, VR, SU   | S        | nothing alone          |                          |

The original's nine definable keys, as two 3x3 blocks with fire in the
middle: player 1 on the keypad (the arrows and Space / VR / SU too),
player 2 on Q W E / A S D / Z X C.  The moves are the port's own map
(chosen by the user).  `FIST_ORIG_KEYS=1` at build time emits the original's
`$98DD` map instead (up+forward = high punch, up+back = forward somersault,
down+forward = low punch, down+back = backward somersault; fire+up = flying
kick, fire+down = foot sweep, fire+back = spinning back kick, fire+up+forward
= roundhouse, fire+up+back = reverse high kick, fire+down+forward = low kick,
fire+down+back = reverse sweep).

(With the fighter facing left the keypad diagonals mirror: KP7 = up+forward
etc. - exactly the original's two table halves.)

Space (or "1") starts a 1-player game from the attract demo, "2" a
2-player game (the original's `$AD9C`: three 30 s rounds on the three
dojos, no yin-yang - only the points count, the higher score wins and
bows, the 2UP high score is its own); "G" and "H" held together quit a
game back to the demo (the original's `$9827`).

"0" in the demo opens the original's settings screen (`$8C54`): "1" -> the
controls menu of player 1 / 2 ("1" the default keys above, "4" redefine
the nine controls one key each: up, up-right, right, down-right, down,
down-left, left, up-left, fire), "3" / "4" the sound on / off, "E" back.
As in the original, one choice and the screen is over.

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
`squeeze`) of `FIST.SAV` and `FIST.DAT`, and verify with `get` + compare.
The game must be started as a command (`R FIST`), not `RUN FIST` - the
loader's file I/O (`.LOOKUP`/`.READW`) is rejected under `RUN`.

## Memory layout (runtime)

See `LAYOUT.md` for the decision; the live game uses:

| octal            | banks   | holds                                          |
|------------------|---------|------------------------------------------------|
| 01000-037777     | 0-1     | code, per-fighter compose copies, stack         |
| 040000-057777    | 2       | the three backgrounds' tables (read with the VRAM window off) |
| 060000-077777    | 3       | `FIST.DAT` read buffer (top 4 KB) during the load |
| 040000-077777    | VRAM    | the video window while the game runs (03217 / 03377) |
| 0100000-0157777  | 4-6     | background engine + `SCRBUF` + `DOJOBUF` (primary) |
| 0100000-0157777  | 12-14   | the game state `$9C00..$F801` (extended, from `FIST.DAT`) |

One dispatcher bit flips slots 4-6 between the dojo (primary) and the game
state (extended); the logic and the decode run at 03217, the compositor
and the HUD at 03377.

## Tests

`tests/` is the port's own harness, a doctest binary built with the
emulator's test build (`fist_tests`, under `src/build/<config>/rt11_devel/
projects/fist/tests/`).  It boots the built game headlessly through
`ms0515_lib` on a folder device (`FistGame.hpp`) and checks it from the
outside - VRAM and the game state in the extended banks:

- `test_game.cpp` - the render, the yin-yang / score strip, the real `.dsk`,
  the loading screen -> demo -> fire sequence, the keyboard (walking, the 27
  key combinations of the control map, fire alone), a long real fight
  (scoring, decided rounds, the hold and the reset), and the dojo following
  the rank pixel-exactly (needs `bg_expect.py`'s `bg{1,2,3}_vram.bin`);
- `test_oracle.cpp` - the VRAM oracle for the verification builds (a `.SAV`
  loaded straight into RAM, run, VRAM dumped for `gl_check.py` /
  `render_vram.py`);
- `test_diag.cpp` - diagnostics, each on by a `--fist-<name>=...` option:
  `match-log` (+ `match-dumps`, `match-back`, `frames`), `profile-out`
  (+ `profile-steps`; with a `FIST_SYMTAB=1` build and `profile_agg.py`),
  `keylat`, `sndlog`, `moves-dir` (with a `FIST_DBGMOVE=1` build).

All paths default to the repo layout (`--fist-sav`, `--fist-dat`,
`--fist-system`, `--fist-dsk`, `--fist-expect` override them) and every test
skips itself when the game is not built:

```
src/build/Release/rt11_devel/projects/fist/tests/fist_tests.exe
src/build/Release/rt11_devel/projects/fist/tests/fist_tests.exe --fist-keylat -tc="*latency*"
```

These are tests of the game, not of the emulator; the emulator's own suites
(`ms0515_core_tests`, `ms0515_lib_tests`) know nothing about FIST.
