# OSK / virtual-keyboard behaviour rules

When users type into the emulated machine via the on-screen keyboard
or via the host's physical keyboard, four UX-friendly overrides apply
**on top of** the authentic MS 7004 firmware behaviour.  These exist
because real-hardware quirks are confusing for users who think in
modern keyboard semantics.  All four are explicit user preferences —
they prioritise convenience over hardware authenticity.

The rules are implemented in the C++ lib layer
(`emu/lib/src/KeyInputAdapter.cpp`), **not** inside the C core.  The
core (`ms7004.c`) emulates the firmware faithfully; the adapter
synthesises byte sequences with the deviations applied and pushes
them into the host USART's RX FIFO.

## The four rules

### Rule 1 — Cap-image semantics for symbol-on-letter keys (LAT)

The keys whose Latin slot is a single non-letter glyph
(Ш/[, Щ/], Э/\, Ч/¬) have only **one** Latin symbol on the cap.
Shift therefore must not change the output in ЛАТ mode.

- Real MS7004: Shift + [ → '{' (the ROM reuses the IBM-PC pairing).
- OSK / physical keyboard: Shift is suppressed for these caps in ЛАТ;
  the user always gets '[', ']', '\\', or '¬' regardless of Shift.

In РУС mode these positions are letters (Ш Щ Э Ч), and Shift behaves
normally (lowercase ↔ uppercase Cyrillic).

### Rule 2 — ФКС (CapsLock) affects letters only

ФКС inverts case for letter keys only.  Digits, punctuation, function
keys, arrows, the editing cluster, and the numpad are immune.

- Real MS7004: ФКС re-maps the entire keyboard, including digits.
- OSK / physical keyboard: ФКС is letters-only.

Which keys count as "letters" is mode-dependent: in РУС mode the
symbol-on-letter caps (Ш Щ Э Ч Ю Ъ) become letters, in ЛАТ they are
symbols.

### Rule 3 — ФКС + ВР (Shift) cancel for letter keys

When CapsLock is on AND Shift is held, the letter case reverts to the
mode default — modern CapsLock-cancel semantics.

- Real MS7004: ФКС + Shift still produces uppercase (the two stack).
- OSK / physical keyboard: they cancel back to the default case.

### Rule 4 — РУС/ЛАТ does not change non-letter keys

In РУС mode the OS's character table maps some symbol scancodes to
Cyrillic letters (e.g. `{` → Ш, `}` → Щ, `~` → Ч).  When users click
a non-letter cap on the OSK they expect the Latin symbol regardless
of mode.

- Real MS7004: { in РУС → Ш.
- OSK / physical keyboard: a momentary RUSLAT flip is sandwiched
  around non-letter emissions so the OS sees ЛАТ for one scancode.

## How the adapter implements them

```c
bool effectiveShift(key, shift_held):
    if !ruslat_on && isShiftImmuneInLat(key):     // Rule 1
        return false
    if isLetterKey(key, ruslat_on):
        return shift_held XOR caps_on             // Rules 2, 3
    return shift_held                              // Rule 2 (non-letters
                                                  //   ignore caps_on)

clickKey(key, shift, ctrl):
    sc = ms7004_scancode(key)
    want_shift = effectiveShift(key, shift)
    flip_ruslat = ruslat_on && !isLetterKey(key, ruslat_on)  // Rule 4

    if flip_ruslat: emit RUSLAT
    if ctrl:        emit CTRL
    if want_shift:  emit SHIFT
    emit sc
    if want_shift or ctrl: emit ALLUP             // clear modifiers
    if flip_ruslat: emit RUSLAT                    // restore mode
```

`caps_on` and `ruslat_on` are virtual states tracked inside the
adapter:
- `clickCaps()` toggles `caps_on` only — no scancode is emitted, the
  OS never sees 0o260 (it doesn't honour CAPS lock anyway, which is
  why the adapter applies it locally).
- `clickRuslat()` toggles `ruslat_on` AND emits 0o262 (the OS does
  honour the RUSLAT scancode).

## Layering

```
                    +-------------------------+
                    |   Physical keyboard     |  <-- SDL events
                    |   (PhysicalKeyboard)    |
                    +-----------+-------------+
                                |
                                v
                    +-------------------------+
                    |   On-screen keyboard    |  <-- mouse clicks
                    |   (OnScreenKeyboard)    |
                    +-----------+-------------+
                                |
                                v
                    +-------------------------+
                    |   KeyInputAdapter       |  <-- THE 4 RULES
                    |   (C++ lib)             |
                    +-----------+-------------+
                                |
                                v
                    +-------------------------+
                    |   ms0515_keyboard       |  <-- USART RX FIFO
                    |   (C core)              |
                    +-------------------------+
```

The adapter sits above the C core and below both input sources, so
the rules apply uniformly to physical typing and OSK clicks.  The C
core stays pure firmware — it has no knowledge of the user-facing
deviations.

(As of 2026-04-28, `OnScreenKeyboard.cpp` routes all clicks through
`KeyInputAdapter::clickKey / clickCaps / clickRuslat`.
`PhysicalKeyboard.cpp` routes ФКС / РУС/ЛАТ key presses through the
adapter (so the toggle state stays in sync with the OSK display) but
keeps its own matrix-based handling for regular keys, so SDL hold
events let the firmware do its native auto-repeat.  Both code paths
share the same single source of truth for `caps_on` / `ruslat_on`,
which lives on `Emulator`'s embedded adapter and is also serialised
to / restored from snapshots.)
