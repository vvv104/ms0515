# FIST whole-game memory layout (the "memory-wall" decision)

The Spectrum game spans `$4000–$F801` (~46 KB).  The MS-0515 has 128 KB of
physical RAM (8 primary + 8 extended 8 KB banks, dispatcher-switched) and a
16 KB VRAM window at octal `040000–077777` (banks 2–3) when VRAM is enabled
(dispatcher bit 7).  The decision below lets the whole game fit in the
**primary 64 KB with no bank-switching**; the 64 KB of extended banks stay as
headroom.

## The key fact

The game's variables, tables and buffers live at Spectrum `$9C00–$F801`.
In octal that is `0116000–0174001` — entirely in **banks 4–7, which never
overlap the VRAM window** (banks 2–3).  Only the Spectrum *screen* (`$4000`,
octal `040000`) collides with the window, and that is already handled: the
display foundation keeps a Spectrum-format framebuffer in ordinary RAM
(`SCRBUF`) and `SPSCR` presents it to VRAM, so screen-pointer accesses
translate with `SCRBUF-40000(Rn)` (see `gen_fist.py`).

## Layout

```
region                       holds
---------------------------- ------------------------------------------------
low RAM (RT-11 .SAV image)   the ported PDP-11 code (game-logic routines,
                             decoder, bg engine, present routine) + SCRBUF
                             (the relocated Spectrum framebuffer, 6912 B) +
                             SROWS and other host-side tables
GSTATE  (one 23,554 B block) a mirror of Spectrum $9C00..$F801: the game's
                             RAM state ($9C00-$AAFF), the constant lookup
                             tables ($A073, $A900-$B4xx ...), and the work
                             buffers ($C427.., $F730 compose).  Constant
                             table regions are initialised from the (external,
                             non-committed) snapshot; everything else is zero.
VRAM window (040000-077777)  pure presentation; written only by SPSCR.
```

`GSTATE` is one contiguous block based at the symbol `GSTATE`, standing for
Spectrum address `$9C00`.  Every Spectrum address `V` in `$9C00..$F801` maps
to `GSTATE + (V - $9C00)`.  The generator emits that displacement as a
literal, so a Z80 `LD A,($AA04)` becomes `MOVB GSTATE+<3588.>,R0` and an
indexed `m[$A90D + d]` becomes `MOVB GSTATE+<3341.>(Rd),R0` — a mechanical,
1:1 transcription of the validated `gamelogic_ref.py` reference, which is
written in those same absolute addresses.

## Why this over the alternatives

- **vs. placing the game at absolute octal `0116000+`**: that would need the
  program to own those addresses bare-metal (RT-11 RMON/handlers live high in
  the 64 KB), fighting the RT-11 build/test path.  A relocatable `GSTATE`
  block that the linker places is RT-11-friendly and the VRAM oracle can load
  it like any `.SAV`.
- **vs. bank-switching the bulk data into extended banks**: unnecessary — the
  data already fits in primary banks 4–7 below the I/O page.  Manual banking
  adds complexity (saving/restoring the dispatcher around every cross-bank
  access) for no gain at the current size.  Extended banks remain available
  if later additions (sound samples, extra scenes) need them.

## Scope note

PDP-11 code is *not* placed at the Spectrum code addresses (`$8000-$C426`):
those bytes are Z80 instructions we replace with original PDP-11 routines of
a different size.  Only the *data* tables embedded in that region are mirrored
into `GSTATE`; the Z80 code bytes are never referenced.
