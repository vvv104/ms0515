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

`stars.c` keeps 110 stars in a cone ahead of the ship - x right, y up, z
forward, 16-bit integers, |x| and |y| up to 15/8 of z, half as wide
again as the screen sees - and every pass of its loop, two frames (25 a
second), brings them nearer, projects the ones in view (`160 +
x*128/z`, `100 - y*128/z` less a sixth for the CRT's pixels, which are
taller than wide: a field of view of some 100 degrees; `draw.s`, an
eight-step division of its own) and plots them in the video memory a
bit a star, erasing the last pass's bit first.  The keys turn the cone
round the ship the other way from the ship's turn, a 16th of a radian
a pass by shifts, the cosine taken as one; a star a turn takes out of
the cone comes in at the opposite edge, so the field stays even however
the ship turns, and one turned past the far wall is born there anew.  A
key is held for as long as its code keeps coming (the keyboard sends no
release): nine frames after the first code, four after a repeat.

The machine does an instruction in some three microseconds and has no
multiply or divide, so the arithmetic is kept off the pass: while no
key is held only z changes, and the depths at which a star goes out of
view and leaves the cone are settled once, when it is born or when a
turn ends; a turning pass decides a star's visibility outright and
turns the stars out of view every other pass, by twice the angle.  On
its way out the program prints how many passes it made in how many
frames; the tests hold it to a pass every two frames, turning or not.

`machine.s` is the machine: the VRAM window opened at 100000 with the
frame interrupt on (dispatcher 07377), register C to 320x200 colour,
the frame interrupt counted, the keyboard's bytes taken off vector 130
into a ring; everything put back on exit.  The pattern is MANICM's
`MS0515.MAC`, and `docs/programming.md` says why.

## Tests

`stars_tests` (`tests/`), built with the emulator's tests, flies
`build/sav/STARS.SAV` on ms0515-run's machine: the screen goes to
320x200 with white stars that move, the arrows change the flight, the
loop keeps its pace straight on and turning, Q brings the console back.
`--stars-sav=<folder>` names another build.
