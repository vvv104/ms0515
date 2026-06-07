# SAPER for MS-0515 / RT-11 SJ V5.04

A MACRO-11 reimplementation of the 1995 Pascal SAPER (a minesweeper clone
for the Soviet MS-0515 personal computer running RT-11).

## Layout

```
rt11_devel/projects/saper/
├── README.md          this file
├── build.toml         declarative build manifest (consumed by ../../toolset/build.py)
├── SAPER.MAC          generated MACRO-11 source (committed for reference)
├── SAPER.SAV          built .SAV image — drop on a boot disk and RUN SAPER
├── SAPER.HLP          help-text file (KHLP[4096] + NSEQ[3680] padded to 16 blk)
├── SAPER.DAT          sprite atlas (= original Pascal SAPER's K.DAT verbatim)
└── source/            hook scripts + raw inputs
    ├── gen_saper.py     pre_build hook — emits SAPER.MAC from a template
    ├── make_artifacts.py post_build hook — builds SAPER.HLP, copies SAPER.DAT
    ├── K.HLP            original encrypted help text (recovered from VVV disk1)
    ├── K.DAT            original sprite atlas
    └── NSEQ.BIN         captured FORLIB RAN() noise used to decrypt K.HLP
```

`SAPER.MAC` in the root is treated as a build output — it is regenerated
every time the build runs.  Editing it by hand is fine for one-off
experiments but `gen_saper.py` will overwrite the file on the next build.

## Building

```
python rt11_devel/toolset/build.py rt11_devel/projects/saper/build.toml
```

The manifest declares `language = "macro11"` plus a pre/post hook; the
universal driver in `rt11_devel/toolset/build.py` does the rest:

1. Runs `source/gen_saper.py` (pre_build) → emits a fresh `SAPER.MAC`.
2. Copies the committed `rt11_devel/toolset/system.dsk` to a scratch image.
3. Stages `SAPER.MAC` + the MACRO-11 recipe (`MACRO.SAV`, `LINK.SAV`,
   `SYSMAC.SML`, `SYSLIB.OBJ`) on side 1 of the scratch.
4. Boots `ms0515-cli` on the scratch and drives RT-11:
   `ASSIGN DZ2 DK`, `RUN DZ2:MACRO SAPER`, `RUN DZ2:LINK SAPER`.
5. Extracts `SAPER.SAV` back to this directory.
6. Runs `source/make_artifacts.py` (post_build) → combines `K.HLP + NSEQ.BIN`
   into `SAPER.HLP` and copies `K.DAT` to `SAPER.DAT`.

## Running

Put all three release files (`SAPER.SAV`, `SAPER.HLP`, `SAPER.DAT`)
on the boot disk of an MS-0515 with RT-11 SJ V5.04, then `RUN SAPER`
at the dot prompt.  `SAPER.HLP` is required: help text is `.READW`-loaded
into upper RAM at startup; without the file the program traps.

## Controls

| Key         | Action                                       |
|-------------|----------------------------------------------|
| F1          | Help (2 pages, ↑/↓ to switch)                |
| F2          | New game with current difficulty             |
| F3 / F4 / F5| Beginner (8×8/10) / Intermediate (16×16/40) / Expert (30×16/99) |
| F6          | Custom dialog (8..38 × 8..20, 1..759 mines) |
| F7          | Marker (flag) mode toggle                    |
| F8          | Best times                                   |
| F9          | Open menu                                    |
| F10         | Exit to RT-11 monitor                        |
| Arrows      | Move cursor                                  |
| Space       | Open cell                                    |
| Enter       | Cycle: closed → flag → question mark         |
| N           | New game (after game over / win)             |

## Memory layout at runtime

```
Address (octal)   Contents
─────────────────────────────────────────────────────────────
000000 – 000777   RT-11 vectors (kept intact)
001000 – ~22500   SAV image: code + data + A[] + sprite atlas + font
~22500 – 037777   free user RAM (below VRAM window)
040000 – 077777   VRAM virtual window (VEN=1, VW=01)
100000 – 106777   KHLP buffer (encrypted help text, 3680 B)
110000 – 117777   NSEQ buffer (decryption noise sequence, 3680 B)
~120000 – 125776  stack (descending from .SETTOP-granted top)
125776 – 127777   RT-11 USR (loaded above the .SETTOP boundary)
130400 – 137777   RT-11 KMON resident (protected)
140000 – 157777   RT-11 monitor extension
160000 – 177377   ROM
177400 – 177777   I/O page
```

## Key RT-11 conventions used

1. `.SETTOP` (EMT 354) declares the program's memory top; RT-11 swaps USR
   above that line so the stack never overlaps it.
2. The hardware dispatcher value `#3377` (VEN=1, VW=01, banks 0–6
   primary, monitor + timer IRQs enabled) is saved at startup and
   restored at `.EXIT`.
3. System Register C bits 0–2 (border colour) are saved and restored.
4. Timer (`@#100`), VBlank (`@#066`) and keyboard (`@#132`) vector PSWs
   are saved and restored.
5. The custom timer ISR (`TISR`) is `INC TKR; RTI` — nothing more.
6. `MTPS #240` lowers CPU priority to 5 during play so the timer IRQ
   (priority 6) fires while keyboard (priority 5) does not; the keyboard
   is polled directly via I/O at `@#177442` / `@#177440`.

## Credits

Inspired by Pascal SAPER by Voronkov V.V. (1995), recovered from the
VVV floppy archive in `disk_recovery/`.  MACRO-11 rewrite + host-side
build glue: this project.
