# STARS

A flight through a starfield, seen from the cockpit: the screen black,
white stars born near the centre and sliding outward as the ship flies
past them, the nearer the faster.  The first program written for the
machine in C with GCC (`rt11_devel/toolset/gcc`).

| key | the ship |
|---|---|
| Down | the nose up - the stars fall |
| Up | the nose down - the stars rise |
| Left | rolls counter-clockwise - the stars turn clockwise |
| Right | rolls clockwise |
| Q | back to the monitor |

## Building and flying

```
cmake -S rt11_devel/projects/stars -B rt11_devel/projects/stars/build -G Ninja
cmake --build rt11_devel/projects/stars/build
cd rt11_devel/projects/stars/build/sav && ms0515-run STARS
```

The compiler is `$MS0515_GCC/bin/pdp11-aout-gcc`, or on the PATH; on
Windows the build runs in WSL and `ms0515-run.exe` from there.

## How it works

`stars.c` keeps the stars in a box ahead of the ship - x right, y up, z
forward, 16-bit integers - and each frame brings them nearer, projects
them (`160 + x*32/z`, `100 - y*32/z` less a sixth for the CRT's pixels,
which are taller than wide) and plots them in the video memory a bit a
star, erasing the last frame's bit first.  The keys turn the box round
the ship the other way from the ship's turn, a 32nd of a radian a frame
by shifts, the cosine taken as one.  A key is held for as long as its
code keeps coming (the keyboard sends no release): nine frames after the
first code, four after a repeat.

`machine.s` is the machine: the VRAM window opened at 100000 with the
frame interrupt on (dispatcher 07377), register C to 320x200 colour,
the frame interrupt counted, the keyboard's bytes taken off vector 130
into a ring; everything put back on exit.  The pattern is MANICM's
`MS0515.MAC`, and `docs/programming.md` says why.

## Tests

`stars_tests` (`tests/`), built with the emulator's tests, flies
`build/sav/STARS.SAV` on ms0515-run's machine: the screen goes to
320x200 with white stars that move, the arrows change the flight, Q
brings the console back.  `--stars-sav=<folder>` names another build.
