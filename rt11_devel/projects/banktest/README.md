# BFLIP — park-RMON bank-flip proof

The MS-0515 has 128 KB of RAM as 8 primary + 8 extended 8 KB banks, but the CPU
only sees 64 KB (banks 0–7) at once.  Under RT-11 the high banks hold the USR
and RMON, leaving little room for a full-memory game.  The way out (the SABOT2
loader pattern) is to **park** the banks that hold the OS into the *extended*
set: flip them out, run in the fresh 64 KB, flip them back, and the OS is intact.

This directory proves that mechanism end to end.

## What it proves

`bankproof.c` — pure-core proof (no CPU, no OS): writing the extended set leaves
the primary banks (RMON) byte-for-byte untouched, and the extended banks retain
their own data.  Extended banks are genuinely separate storage.

`BFLIP.MAC` → `BFLIP.SAV` — the same thing under **live RT-11 SJ V5.04**.  At the
dot prompt, `RUN BFLIP`:

1. masks interrupts,
2. parks banks 4–6 (RT-11's USR + RMON) into the extended set — banks 0–3 (this
   code) stay primary, so execution continues inline across the flip,
3. writes a marker to bank 6 (now fresh extended RAM) and reads it back,
4. unparks (RMON restored), and
5. returns cleanly to the monitor **only if** the extended read-back matched
   (a mismatch spins forever — a visible hang).

Verified: no trap, clean return, and RT-11 stays fully usable afterwards
(`RUN BFLIP` works repeatedly).  A harmless `?MON-F-No device` is printed once
per run and self-heals; the real loader runs the game / reboots rather than
returning to KMON, so it never surfaces there.

## Memory map under a running RT-11 SJ V5.04 (measured)

```
banks 0–3  001000–077777  user program area   FREE, keep PRIMARY
banks 4–5  100000–137777  RT-11 USR           park
bank  6    140000–157777  RMON                park
bank  7    160000–177777  ROM / I/O           (not bankable)
```

Active monitor dispatcher = `003177` (banks 0–6 primary; idle is `002177`).

## Gotchas found the hard way

* The dispatcher at `0177400` is **write-only** for our purposes: a *word* read
  returns the low byte duplicated into the high byte (`0177777`), not the real
  value (`io_read_byte` returns the low byte for both offsets).  Hardcode or
  track the value; never `MOV @#177400,Rn`.
* `0157700` is **bank-6 RAM (RMON)**, not a dispatcher shadow — writing it
  corrupts RMON.  Only `0177400` controls banking.
* Banks 4–5 are **not free** — RT-11's USR lives there.  Putting a stub/marker
  at `0100000`/`0120000` overwrites live OS code.  Use banks 0–3 for the loader.

## Build / run

```
python rt11_devel/toolset/build.py rt11_devel/projects/banktest/build.toml
```

`bankproof.c` is a standalone host program linked against `src/core/src/memory.c`.
