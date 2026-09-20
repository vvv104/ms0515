# The MS 0515 floppy handler, taken apart

What the kits' `DZ.SYS` does, read out of the binaries with
`tools/rt11_handler.py`.  Written down because we have no source for any
of them and will be writing our own.

## The shape of a handler file

An RT-11 V5 handler is a memory image whose block 0 is a header the
`SYSMAC` macros lay out at fixed offsets, and whose code — position
independent — begins at block 1.  Offsets in the file and addresses in
the listing are therefore the same thing.

| offset | what | macro |
|---|---|---|
| `0` | RAD50 `"HAN"`, the V5 signature | `.DRPTR` |
| `2` `4` `6` `10` `12` `14` | FETCH, RELEASE, LOAD, UNLOAD, **FORMAT**, SHOW | `.DRPTR` |
| `20` | device class (`4` = disk), `21` modifier bits | `.DREST` |
| `52` | length of the code | `.DRBEG` |
| `54` | blocks on the volume | `.DRBEG` |
| `56` | device status word | `.DRBEG` |
| `60` | `ERL$G + MMG$T*2 + TIM$IT*4 + RTE$M*10` | `.DRBEG` |
| `176` | address of the controller's registers | `.DRDEF CSR=` |
| `400`+ | the `SET` options table | `.DRSET` |
| `1000` | vector, offset to the interrupt entry, priority, two queue words, `NOP` | `.DRBEG` |

Two of those matter for what we want to build and are **empty in every
kit handler**: the FORMAT entry point at `12` and the controller address
at `176`.  DEC's `FORMAT` reads the latter (`OFFCSR = 176`) to find the
hardware, which is why no kit handler can be formatted by it.

None of the kits' handlers carries `ERL$G` either — the word at `60` is
zero in ОМЕГА's, ОСА's and Rodionov's, and `4` (`TIM$IT`) in Mihin's,
which is what makes his load only under his own monitor.

## Two schools

**ОСА's handler is a shim over the ROM.**  Its whole code is 62 bytes,
fourteen instructions, and it never touches the controller:

    MOV  DZCQE, R5            ; the queue element
    TST  6(R5)                ; no words to move? nothing to do
    BEQ  done
    MOV  R5, @#157730         ; hand the element to the ROM
    CALL @#160010             ; the ROM's disk routine does the work
    BCC  done
    MOV  DZCQE, R5
    BIS  #1, @-(R5)           ; carry set: raise the hard-error bit
    BR   done
    done: MOV PC, R4          ; .DRFIN - back through the monitor
          ADD #177732, R4
          MOV @#54, R5
          JMP @270(R5)

`160010` is the ROM entry we already knew as the sector-read trampoline
(`docs/kb/DISK_COPYING.md`), `157730` the cell it takes the queue element
in.  Seeking, retrying and the sector interleave all happen inside the
ROM, so this handler's behaviour is the ROM's — and the machines were
modified, so that is not one behaviour but several.

**ОМЕГА's, Mihin's and Rodionov's drive the controller themselves**, 658
bytes of it, through `177640`-`177646`.  They depend on no ROM routine.

## The shadow of System Register A

Register A (`177600` — drive, motor, side) is write-only, so the ROM keeps
a copy at `157720` and everyone changes the copy and pushes it out.  The
handler observes that discipline throughout:

    BIC  #13, @#157720        ; clear the drive and side bits in the copy
    BIS  R0,  @#157720        ; set this request's drive and side
    MOVB @#157720, @#177600   ; push the copy to the register
    BIT  #4,  @#157720        ; test a bit by the copy, never by the register
    BIS  #40000, @#157720     ; interrupt: mark the drive as just used
    BIC  #40000, @#157720

Bits 13-15 are the ROM's motor flags (15 time-out armed, 14 drive just
used, 13 counting); the monitor's clock interrupt stops the motor 100
ticks after the last access.  ОМЕГА touches the copy in 11 places,
Rodionov in 12, Mihin in 9, ОСА in none.

## How ОМЕГА's handler serves a request

The queue element arrives through `DZCQE`; the handler reads it in order —
block number, function and unit byte, buffer address, word count — and
then:

* the word count's sign picks the command: `#200` (read sector) or, when
  negative, `|#240` (write sector), stored into the cell the FDC command
  is later issued from;
* the drive number and side go into the shadow register as above;
* the block number is divided by ten — the sectors on a track — by
  shift-and-subtract, eight rounds against `2400` (ten shifted left
  seven), leaving the track in the low byte and the sector in the high;
* the sector is then doubled modulo ten (`ASL` then subtract `12` while
  positive), which is the 2:1 interleave, and adjusted per track for the
  skew;
* buffer address, track and count are saved by storing registers into the
  instruction stream itself (`MOV R0,(PC)+`), and read back later as
  PC-relative data — a space-saving idiom, not a disassembly artefact;
* the track register of the controller is compared with the wanted track
  and a seek issued only when they differ;
* on error the request is retried, the retry counter living in another
  inline cell.

The interrupt entry sits at `2200` (the offset at `1002` plus its own
address), runs at priority `340`, marks the drive used in the shadow
register and hands the result back through the monitor's completion
routine.

## What our own handler has to do differently

* fill the controller address into `176` and a FORMAT routine into `12`,
  so DEC's `FORMAT` can find and use it;
* be of ОМЕГА's school, not ОСА's: the ROM trampoline can read and write
  sectors but has no Write Track, so formatting cannot go through it;
* keep the shadow-register discipline exactly, or the ROM's motor
  time-out will fight it;
* assemble with `ERL$G` so failures reach the error logger, the way our
  own `HD.MAC` already does.

## DV and MZ are one handler with one constant changed

Their listings are the same instruction for instruction but for three
things: the seed of the division loop (`MOV #770,R4` against `#771`), the
branch that ends it (`BNE` against `BLE`, which only makes both run eight
rounds and leave a different value behind), and the device status word
(`100055` against `100052`).  The seed is also the addend: `ADD R4,R5`
afterwards adds one cylinder more for DV than for MZ.

Where they both differ from `DZ` is the side.  `DZ` takes it from the
unit number - bit 1, shifted into the side bit of the shadow register, so
`DZ0:`/`DZ1:` are one side and `DZ2:`/`DZ3:` the other, 800 blocks each.
`DV` and `MZ` keep only the two drive-select bits and work out the side
from the address instead: the index runs 0..159 over the whole diskette,
is wrapped (`CMPB R3,#237` / `SUB #240`) and halved (`RORB R3`), the bit
that falls out being the side and the rest the cylinder - 1600 blocks,
one volume.

Measured on the machine, with a diskette whose every block says which
block it is, dumped through each handler:

| handler | its block 0 | block 1 | block 10 |
|---|---|---|---|
| `MZ1:` | 0 | 1 | 10 |
| `DV1:` | 20 | 21 | 30 |
| `DZ1:` | 20 | 22 | 42 |

So `MZ` is the identity over a track-interleaved image and `DV` is that
rotated by a constant twenty blocks - one cylinder - which is what
`kDvRotate` in `Layout.hpp` has always said.  Reading the code alone the
addend looks like one unit; the unit is a cylinder, because the divisor
`2400` is twenty shifted left six, not ten shifted left seven.  `DZ`'s
step of two between consecutive blocks is the 2:1 interleave, and block
10 landing on 42 rather than 40 is the skew of the next track.
