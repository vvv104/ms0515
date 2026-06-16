# FIST - The Way Of The Exploding Fist for MS-0515 / RT-11 SJ V5.04

A faithful port of the 1985 ZX Spectrum game *The Way Of The Exploding
Fist* (Melbourne House) to the Soviet MS-0515.  The port follows the
pobtastic SkoolKit disassembly routine-for-routine: same mechanics, same
graphics, re-expressed in MS-0515 MACRO-11.

The original game's code and artwork are **external, non-committed data**
(the SAPER `K.DAT` pattern) - this repo never vendors them.  The build
reads the original tape image and emits the MACRO-11 source.

## Status

**Stage 1 - display foundation (done).**  The core display primitive is in
place: a Spectrum 256x192 framebuffer is shown 1:1 (pixel for pixel),
centred in the MS-0515's larger 320x200 medium-resolution colour screen.
As a runnable proof, `FIST.SAV` displays the game's loading screen and
returns to the monitor on any key.

The 1:1 mapping is exact because the two machines' hardware attribute
models coincide:

| Spectrum (per 8x8 cell) | MS-0515 VRAM high byte (per 8x1 word) |
|-------------------------|----------------------------------------|
| bit 7 FLASH             | bit 7 F (flash)                        |
| bit 6 BRIGHT            | bit 6 I (intensity)                    |
| bits 5-3 PAPER (G,R,B)  | bits 5-3 G' R' B' (background)          |
| bits 2-0 INK (G,R,B)    | bits 2-0 G R B (foreground)            |

So the present routine `SPSCR` is a pure copy: VRAM word =
`(attr_byte << 8) | pixel_byte`.  A pixel byte's bit 7 is the leftmost
pixel on both machines, so no bit reversal is needed.  The only real work
is de-interleaving the Spectrum screen's thirds-major row order, which
`gen_fist.py` precomputes into the `SROWS` table.

Centring: 256 px = 32 word-columns inside a 40-column MS-0515 line (a
4-word margin each side); 192 lines inside 200 (a 4-line margin top and
bottom).  The margin shows as a border.

**Next - one-to-one routine port.**  The game engine is being translated
subsystem by subsystem from `wotef.skool`, each routine assembled and
verified against the source as it lands.

## Layout

```
rt11_devel/projects/fist/
├── README.md         this file
├── build.toml        declarative build manifest (macro11)
├── FIST.MAC          generated MACRO-11 source (build artifact)
├── FIST.SAV          built .SAV image - RUN FIST at the dot prompt
├── validate.py       OS-oracle smoke test (boots, runs, checks clean exit)
├── fist_screen.png   host-rendered preview of the 1:1 screen
└── source/
    ├── gen_fist.py     pre_build hook - extracts the loading screen, emits FIST.MAC
    └── preview.py      renders the expected VRAM to PNG (display cross-check)
```

## Building

The generator needs the original tape (not committed).  Point it at your
WotEF checkout with `WOTEF_DIR` (default `C:\Users\voron\wotef`):

```
python rt11_devel/toolset/build.py rt11_devel/projects/fist/build.toml
```

This runs `gen_fist.py` (emits `FIST.MAC` from the tape's loading screen),
then assembles and links it with the real RT-11 SJ V5.04 `MACRO`/`LINK`
inside the emulator, producing `FIST.SAV`.

## Running

**Dojo demo (a karate fighter standing in the full-colour dojo):** mount
`package/assets/disks/fist_fighter.dsk` in the GUI emulator and boot — it
auto-runs `FIST` (via `STARTS.COM`), renders the dojo background (Buddha,
pagoda, mountains, blossom) and overlays a karate fighter.  Any key returns to
the dot prompt.

```
package/ms0515.exe --disk0 package/assets/disks/fist_fighter.dsk
```

Build it with `FIST_MODE=gamelogic FIST_GL=demobg python rt11_devel/toolset/
build.py rt11_devel/projects/fist/build.toml`, then refresh the disk's
`FIST.SAV`.  `FIST_GL=demo` (without `bg`) is the plain fighter-on-black variant.
(The full game GST overlaps RMON under RT-11, so the demo trims the GST to a low
pose below RMON and relocates/overlaps the compose buffer to low RAM — see
`source/gamelogic_mac.py:main_demo_bg`.)

**Loading-screen demo:** mount `package/assets/disks/fist_demo.dsk`, boot, and
at the dot prompt `RUN FIST`.  Press any key to return to the monitor.

To preview the screen without the emulator:

```
python rt11_devel/projects/fist/source/preview.py fist_screen.png
```

## Pixel-exact verification (VRAM oracle)

`src/lib/tests/test_fist_screen.cpp` runs the built `FIST.SAV` in the real
emulator (headless) and dumps the 16 KB VRAM, giving pixel-exact proof of
what the MACRO-11 code actually draws.  After building `FIST.SAV` and the
test suite (`cd src && conan build . --build=missing`):

```
src/build/Release/lib/tests/ms0515_lib_tests.exe --test-case="fist: VRAM oracle"
python rt11_devel/projects/fist/source/render_vram.py \
    src/build/Release/lib/tests/fist_vram.bin fist_emu_vram.png
```

The test skips itself when `FIST.SAV` is absent.
