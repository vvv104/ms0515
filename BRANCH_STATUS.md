# MS 7004 firmware-port — branch status: **UNDONE**

This branch contains 18 commits' worth of work replacing the
hand-rolled MS 7004 keyboard state machine with an emulation of the
real Intel 8035 + i8243 firmware running the original 2 KB ROM.

The work is **paused** because the practical UX has too many rough
edges to ship.  Captured here for archival and for partial reuse;
`main` will be reset to the pre-port state and resume from there.

## What works

- i8035 / i8243 cores: full instruction-set + expander emulation,
  151 / 21 unit-test assertions, no shortcuts.
- Real ms7004 firmware boots, scans the matrix, bit-bangs scancodes
  over its 4800-baud TX line, and answers host commands (ID probe
  0xAB → 0x01 0x00 verified on the wire).
- Captured scancodes match the hand-rolled `kScancode[]` table for
  every sample we tested (F1/F2/RETURN/SPACE/A/KP_5).
- `KeyInputAdapter` in lib/ implements the four documented OSK
  behaviour rules (`docs/kb/OSK_BEHAVIOUR_RULES.md`).
- Test suite: 242 / 242 green plus 1 pre-existing skip.
- Snapshot save/load survives the format swap via a new chunk ID.

## Known problems (the reason this is shelved)

1. **Synthetic-shift race in physical typing** — partially addressed.
   When the host-keymap → MS7004-keymap remapping needs to add or
   remove Shift in the same SDL event (host `'` → MS7004 Shift+7,
   host Shift+] → MS7004 ] without Shift, etc.), both matrix bits
   land in the same firmware scan pass.  The firmware then emits in
   column-scan order — keys at lower columns than SHIFT_L (col 12)
   come out before the Shift state-change, and the OS echoes the
   unshifted glyph.

   Fixed for ~16 host-keystroke combinations by deferring the target
   press one frame after the Shift state-change.  But the deferred
   press fires against an already-released matrix if the user tapped
   the host key in less than ~20 ms — that keystroke is silently
   dropped.  Symptom: typing `=` sometimes outputs nothing, typing
   `'` very fast occasionally produces `7` instead.

2. **Stuck modifiers when the host steals KEYUP** — partially
   addressed (LALT mapping removed, RALT / Shift / Ctrl still vulnerable).
   Window-manager hotkeys (Alt+Tab, Alt+F4, Alt+PrtScr, Win+key)
   commonly leak the KEYDOWN to SDL but eat the KEYUP, leaving a
   matrix bit stuck.  When that bit is for a modifier (COMPOSE for
   LALT, RUSLAT for RALT, SHIFT_L for LSHIFT) every subsequent
   keystroke is interpreted by the guest OS as `<modifier>+key` and
   produces garbage / `?KMON-F-Недопустимая команда` errors.

   Robust fix would require either polling SDL_GetModState every
   frame and forcing matrix release on mismatch, or releasing all
   matrix bits on SDL_WINDOWEVENT_FOCUS_LOST.

3. **Multiple matrix changes per scan** — the firmware was designed
   for a real human typing on real switch contacts, where one key
   transitions per ~50 ms.  Our frontend can fire two or three matrix
   changes inside a single event handler (modifier release + key
   press + sticky-shift restore).  Race conditions are lurking even
   beyond the synthetic-shift case (#1).

4. **Test gap vs running app** — the doctest suite passes 242 / 242
   but covers a small slice of typing scenarios.  Real-world use
   surfaces races, stuck modifiers, and timing edge cases that aren't
   exercised by the unit tests.  Building broader integration tests
   for typing patterns (especially modifier sequences) would expose
   these — but is a substantial addition.

5. **Auto-repeat partly broken** — for the deferred-press subset
   (the ~16 remapped keystrokes), holding the host key does not
   produce the firmware's native auto-repeat because we no longer set
   the matrix bit synchronously with the press.  Acceptable in
   isolation but inconsistent with the rest of the keyboard.

6. **CPU timing audit pending (orthogonal)** — independent of this
   branch but informs the bigger picture: a forum measurement of
   per-instruction cycle counts on the host CPU shows our emulator
   runs roughly 2× faster than spec.  Tracked separately as the
   first task on `main` after the reset.  See the new
   `docs/kb/CPU_TIMING_AUDIT.md` (added on `main`).

## What to keep when reusing this branch

The C-core pieces are independent and high-quality:

- `emu/core/include/ms0515/i8035.h`, `emu/core/src/i8035.c`
- `emu/core/include/ms0515/i8243.h`, `emu/core/src/i8243.c`
- `emu/tests/test_i8035.cpp`, `emu/tests/test_i8243.cpp`
- `tools/dasm8048.py` — MCS-48 disassembler
- `docs/kb/MS7004_WIRING.md` — wiring spec from MAME (data only)

Those have no dependency on the keyboard backend swap.

The frontend side (KeyInputAdapter, deferred press, OSK overrides
migration) is the part that needs rework before re-landing.

## How to retry

Start from `main` after the reset.  Pick the C-core artefacts above.
Then before grafting the firmware backend on top, decide:

- How to handle the synthetic-shift race deterministically (proper
  queue with completion, not best-effort defer).
- How to handle stuck modifiers (focus-lost matrix flush, modifier
  state polling).
- Whether to keep host-keymap remapping at all, or simplify by
  dropping it and asking the user to type via the MS7004 layout
  directly.
