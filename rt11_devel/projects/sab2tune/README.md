# SAB2TUNE - the Saboteur II title tune on the MS-0515

The 48K Spectrum *Saboteur II* (Durell, 1987) plays a beeper rendering
of Rob Hubbard's theme over its loading screen and again over the
high-score table.  The Lviv port of the game that the software
collection carries has neither - it was made from the 48K game and
left the tune out.  This project brings the tune over, the engine
routine for routine, and puts it into the repaired bodies of the port
where the original calls it.

## Files

```
rt11_devel/projects/sab2tune/
├── README.md        this file
├── SAB2TN.MAC       the engine: the score interpreter, the tone loop,
│                    the frame interrupt with the drums (see its header)
├── SABTUN.MAC       the tune as a program: R SABTUN, a key stops it
├── SAB2G.MAC        the tune as a blob inside the game (source/pack.py)
├── build.toml       the build: the two programs, with maps
├── source/
│   ├── sab2_dir.py       where the original's tape lives (SAB2_DIR)
│   ├── tap48.py          the tape's blocks, the game's code as memory
│   ├── engine_model.py   the model of the original's engine (the spec)
│   ├── gen_data.py       pre_build: SAB2DT.MAC and SAB2TN.REF from the tape
│   ├── pack.py           the blob into the collection's fixed bodies
│   ├── diff_ours.py      side-by-side listing of our level changes vs SAB2TN.REF
│   └── fit_costs.py      per-frame pairing + least-squares fit of the path costs
└── tests/           the harness: the program booted through the emulator
                     library, its speaker against the model's rendering
```

Build artifacts, never committed: `SAB2DT.MAC` (the tune's data),
`SAB2TN.REF` (the reference rendering), `SABTUN.SAV`, `SAB2G.SAV`, the
maps.

## The original's data

Nothing of the tune is in this repository.  The score, the instruments,
the drum patterns and the period table come from the original tape at
build time, the way MANICM's caverns do:

```
mkdir ../saboteur2 && cp SABOTEU2.TAP ../saboteur2/     # the 48K tape (or set SAB2_DIR)
pip install skoolkit                                    # for the Spectrum ROM the drums read
python rt11_devel/toolset/build.py rt11_devel/projects/sab2tune/build.toml
```

`gen_data.py` takes the engine's data block (0xfe24..0xff8f of the
Spectrum's memory) as it is, and derives two tables from it for the
machine: the delay of a half period by its low byte, and the spacing of
each drum's twenty clicks.

## The engine

The original (0xfb00..0xffff of the game) is self-modifying: every
parameter is an immediate patched in place.  `source/engine_model.py`
is the same machine written with named variables and the cost of every
path in T-states; it reproduces the Spectrum's speaker output to the
T-state - checked against a Z80 emulator running the original, all
66637 level changes of the tune.  `SAB2TN.MAC` follows the model, and
quotes the Spectrum address of each piece.

What the engine does: the score is a byte code of commands (call a
pattern, return, loop, transpose, pick an instrument, set the duty's
parameters) and note records (a count, then that many pitch bytes - the
semitone in bits 3-7, a duration index in bits 0-2).  A note is a square
wave whose halves differ: the period plus a duty offset while the
speaker is high, minus it while low, the offset drifting by one every so
many passes - that is the timbre.  The period itself moves every so many
half periods: vibrato bouncing round the note, or a slide that stops at
a target, by the instrument, and an instrument can hold the effect back
for the first frames of a note.  The 50 Hz interrupt counts the note's
frames, steps a 16-step drum pattern every fifth frame (a drum is twenty
clicks spaced by a run of ROM bytes) and stops everything at a key.

The pitch is the time the original's loops take, so every path here is
charged the Spectrum's cost less its own, in sixteenths of a 30-clock
turn (14 T-states exactly: a T-state at 3.5 MHz is 15/7 clocks at
7.5 MHz), and the wait makes up the difference; the fraction left over
carries into the next half period.  The tests compare the machine's
speaker with the reference interval by interval.

## Running

`R SABTUN` on any RT-11 volume: the tune, again and again, until a key.

## The tune in the game

`python rt11_devel/projects/sab2tune/source/pack.py` appends the blob
(`SAB2G.SAV` from 1000 to its high limit) to the software collection's
`software/games/sabot2/fixed/{osa,omega}/SABOT2.DAT` and patches two
jumps in each body, four bytes for four bytes:

- the start: the jump after the screen clear goes to the blob, which
  copies itself to 140000 (RAM the game never touches: RT-11's monitor
  was there), plays the tune until a key and jumps on to the intro - as
  the original plays it over the loading screen;
- the high-score table: the wait for a key at its end becomes a call of
  the blob's second entry, which plays the tune once and waits for the
  key only if the tune ended by itself - the original's second call.

The game's loader loads the whole file, whatever its length, so the
blob lands right after the body; the blob takes the game's own key
reader and wait-for-a-key from its head, which the packer fills for
each body.  The packer is idempotent: it drops whatever is beyond the
body's 85 blocks before appending.
