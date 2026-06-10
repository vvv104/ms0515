# RT-11 / MACRO-11 build gotchas

Hard-won notes for building things inside the emulator with this toolset.
Read before debugging a mysterious build failure.

## MACRO-11 source must be pure ASCII (the em-dash trap)

MACRO-11 flags any non-ASCII byte with error **`I`** (illegal character)
— **even inside a comment**.  The usual culprits sneak in from prose
written on a modern editor:

- em-dash `—` (U+2014) / en-dash `–` — use a plain ASCII hyphen `-`
- "smart quotes" `“ ” ‘ ’` — use straight `" '`
- any Cyrillic letter — keep comments in English (project rule anyway)

Symptom: a single stray `I`-flagged line in `.LST`, the count off by one,
on a line that is "just a comment".  Don't trust your eyes — the bad
character often renders fine in the terminal.  Grep the source for
non-ASCII before assembling:

```
LC_ALL=C grep -nP '[^\x00-\x7F]' FILE.MAC
```

This has bitten us more than once.  When pasting a comment into a `.MAC`,
keep it 7-bit ASCII.

## SYSMAC.SML must live on SY: (the system/boot side)

MACRO auto-searches the system macro library `SYSMAC.SML` on **SY:** (the
boot device = side 0) for every `.MCALL`.  Staging it only on the work
side (DZ2 / DK) is not enough: the macros silently resolve to nothing and
you get a flood of undefined-symbol errors (e.g. `.DRDEF`, `.DRBEG`,
`.DREND` show as `****** GX` in the listing's symbol table).

Fix: put `SYSMAC.SML` on side 0 of `system.dsk` (done).  Driver builds
(`.DRDEF` and friends) and any program that `.MCALL`s system macros need
it there.

## STARTS.COM — the SJ startup command file (like autoexec.bat)

After boot the SJ monitor auto-runs `STARTS.COM` from SY:.  If it is
absent KMON prints a benign `?KMON-F-File not found DK:STARTS.COM` that,
due to timing, can land in an early command's output and trip the
toolchain's fatal-error detector.

Do **not** filter the error in `rt11.py` (the toolchain must keep
catching real `?...-F-...` diagnostics).  Instead provide the file: a
one-line `STARTS.COM` (`SET TT QUIET`) lives on side 0 of `system.dsk`.

## LINK with switches via `RUN DZ2:LINK`

`RUN DZ2:LINK/NOBIT/EXECUTE:HD.SYS HD` fails with `?KMON-F-Invalid
command`: KMON parses the leading `/NOBIT` as a switch on the RUN
*filespec*, not as LINK's command tail.  Put a space after the program so
the rest is passed through as the command line:

```
RUN DZ2:LINK /NOBIT/EXECUTE:HD.SYS HD
```

(MACRO's tail `HD,HD=HD` has no leading slash, which is why
`RUN DZ2:MACRO HD,HD=HD` always worked.)
