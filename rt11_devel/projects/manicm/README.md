# MANICM - Manic Miner for the MS-0515 / RT-11

A port of the 1983 ZX Spectrum game *Manic Miner* (Matthew Smith,
Bug-Byte) to the Soviet MS-0515, routine for routine, after Richard
Dymond's SkoolKit disassembly (<https://skoolkit.ca/disassemblies/manic_miner/>).
The same twenty caverns, the same guardians, the same two-voice theme on a
one-bit speaker - re-expressed in MACRO-11 for a PDP-11 under RT-11.

Unlike FIST, the source here is written by hand and meant to be read: the
game is one file, the machine another, and whoever ports the game to a DVK
or any other PDP-11 rewrites the second file and leaves the first alone.

## Files

```
rt11_devel/projects/manicm/
├── README.md        this file
├── MANICM.MAC       the game - every routine named for the original's address
├── MS0515.MAC       the machine: screen, keys, speaker, clock (see its top)
├── build.toml       the build: two objects, linked with a map
├── source/
│   ├── mm_dir.py       where the original lives (MANICM_DIR, or ../manicminer)
│   ├── prepare_mm.py   makes the original's snapshot there with tap2sna.py
│   └── gen_data.py     pre_build: MANICM.DAT out of the snapshot
└── tests/           the harness: the game booted through the emulator library
```

Build artifacts, never committed: `MANICM.SAV` (the program, 20 blocks),
`MANICM.DAT` (the original's data, 54 blocks), `MANICM.MAP` (the link map).

## The original's data

Nothing of the game's content is in this repository.  The caverns, the
sprites, the two tunes, the title screen and the Spectrum's character set
come from the original tape at build time, the way FIST's art does:

```
git clone https://github.com/skoolkid/manicminer ../manicminer   # the disassembly
pip install skoolkit
python rt11_devel/projects/manicm/source/prepare_mm.py            # the snapshot (tap2sna.py)
python rt11_devel/toolset/build.py rt11_devel/projects/manicm/build.toml
```

`gen_data.py` writes `MANICM.DAT`: a run of 512-byte blocks holding the
original's bytes exactly as they sit in the Spectrum's memory, so the
disassembly's description of each applies unchanged (the script's docstring
and `MANICM.MAC`'s `DMISC..DFONT` say what is where).  The game reads the
file at run time - the tables once, a cavern at a time, 1024 bytes each -
so the program image stays small and the data stays the original's.

## Running

`MANICM.SAV` and `MANICM.DAT` on one RT-11 volume, `R MANICM`.  The keys
are the original's, on the MS7004:

| what | keys |
|---|---|
| left | O, U, Q, E, T, 5, the left arrow, keypad 4; the joystick |
| right | P, I, Y, W, R, 8, the right arrow, keypad 6; the joystick |
| jump | SPACE, SHIFT, Z X C V B N M, 0, 7, the up arrow, keypad 8 / 5; fire or up on the joystick |
| start | ENTER (keypad ENTER), or the joystick's fire, at the title |
| music on / off | H, J, K, L, ENTER |
| pause | A, S, D, F, G; any other key resumes |
| back to the title | SHIFT with SPACE |
| leave for RT-11 | SHIFT with SPACE at the title (the port's one addition) |

The 6031769 cheat is there too: type it, and the boot appears by the lives;
then 6 with a cavern's number spelt on 1-5 as bits (1 = 1, 2 = 2, 3 = 4,
4 = 8, 5 = 16) teleports there.

The MS7004 sends no key-up codes, so a key counts as held for a few passes
after its last code, and keys pressed together stay held while any one of
them repeats (`KEYS` in `MS0515.MAC` says how).  A key let go just before
another is pressed still counts as held with it - the price of that
keyboard.

## The port

**Memory.**  The original works in 16384..32767: the display file, its
attribute and screen buffers, the cavern in play.  The port keeps that
block whole, moved down by `SHIFT` (4096) to `DFILE` (30000 octal), so every
address the cavern data carries is the original's less `SHIFT` and every
trick the original plays on address bytes still holds.  The code and the
tables sit below, from 1000; the block above the image is claimed with
`.SETTOP`.  The whole thing ends at 70000 octal, under Rodionov's RT15SJ,
whose monitor starts at 72000.

**The screen.**  The game draws into a Spectrum display file and buffers,
exactly as the original did; `PRESNT` turns rows of it into the MS-0515's
video memory (a word a byte: the attribute over the pixels, the Spectrum's
attribute byte being the MS-0515's bit for bit).  The cavern goes straight
from the screen buffer, saving the copy the original made.

**Sound.**  The speaker is register C bit 6, driven as the original drove
port 254 - every effect and the in-game tune are the original's loops with
their delays converted from T-states to this processor's clocks
(`MS0515.MAC` says how, and the core's timing tables are the source).  The
theme is the one deliberate departure: the original XORed its two voices
into the one speaker bit, which sounds their sum and difference rather
than the two pitches (the melody an octave up with the pair's beat
breathing through it, the bass pairs as a rumble - a recording of a real
Spectrum confirms it), and this port plays the two pitches the tune table
spells instead, each voice a pulse train a quarter of its period wide on
the same bit.  Same notes, same timing, the tune as written.

**Pace.**  The original's main loop takes 312315 T-states, 89 ms; `PACE`
holds a pass to that on the 50 Hz frame interrupt, which the port owns
while it runs (RT-11 has no clock of its own on this machine).

**What differs, and why:** the keyboard model above; the game-over
glisten and the escape's colour cycling are shown every few steps rather
than every step (each step of the original is a millisecond, a screen here
is more); the demo, the title's message and the piano are the original's.

## Porting further

`MS0515.MAC` is 500 lines and documents its interface at the top: take the
machine, present rows of the display file, read the keys, three note loops,
a delay, a pace.  Another PDP-11 needs its own of those and nothing else -
`MANICM.MAC` has no address of the machine in it.  The EIS is not used
(neither the KR1807VM1 nor the DVK's processors have it): shifts by a count
are the `ASLN`/`ASRN` macros.

## Tests

`tests/` boots the built game through the emulator library, the way FIST's
harness does, and checks it from outside: the title, ENTER, Willy walking
and jumping, the demo moving through the caverns, the cheat.  The suite
skips itself when the game is not built.  `MANICM.MAP` names the state the
tests read (`CAVNUM`, `WILLYA`...).
