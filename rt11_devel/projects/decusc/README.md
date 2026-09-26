# DECUS C for the MS 0515, from the SIG tape

The C compiler the machine's own C programs were made with - the
collection's `RECODE.SAV` and the lyceum's `LINE.SAV` carry its run-time of
1980-82 - rebuilt here from its sources on the `dec` system, with the
machine's MACRO-11, LINK and LIBR, by the command files that came with it.

DECUS C is not DEC's: it is the compiler David Conroy wrote in 1978 and
Martin Minow and the DECUS "Structured Languages" SIG kept, distributed
as DECUS program 11-SP-18.  The sources here are the extract for RT-11
that Thomas J. Shinal put on the RT-11 SIG tape of Fall 1983 (`11SP59`),
as the bitsavers copy of the "RT-11 Freeware" CD of 1999 has it
(`/SPLIT/SIGTAPES/11SP59`).  `$MS0515_DECUSC_SOURCES` names that folder
(the one holding `501C`, `503A`, `503B`, `504`, `505`, `601A`, `601B`,
`602A`, `602B`, `602C`); without it the scripts look in `sources/decus-c`
of the software collection.

## What is built, and how

The compiler itself is MACRO-11, 45 modules by hand (`503A`, `503B`); its
output is assembler text in a Unix-like syntax that only its own `AS`
reads, and `AS` writes RT-11 objects for LINK.  The library is 184
modules, one function each (`504`, `505`), plus `SUPORT` (the start),
`DTOA` and `ATOF` kept apart.

| stage | DEC's command file | makes |
|---|---|---|
| `cc` | `TMAKCC.COM` (with `TCOMLB.COM`, `TCCBLD.COM`) | `TCOMLB.OBJ`, `CC.SAV` |
| `as` | `TMAKAS.COM` (with `TASBLD.COM`) | `AS.SAV` |
| `clib` | `TCLBAS.COM` + `TILBAS.COM`, then `TMAKLB.COM`'s tail | `CLIB.OBJ`, `SUPORT.OBJ`, `DTOA.OBJ`, `ATOF.OBJ` |
| `cc-e`, `clib-e` | the same with `RT11.EIS` (`TCLEIS`, `TILEIS`) | `CCE`, `TCOMLE`, `CLIBE`, `SUPRTE`, `DTOAE`, `ATOFE` |
| `check` | - | `HELLO.C` by each `CC`, against its library, run |

`CC` and `CCE` differ in one byte: the default of the `-E` toggle, so `CCE`
makes inline EIS unless told `/N` and `CC` calls the library unless told
`/E`.  The compilers' own code has no EIS in it (`CC202.MAC`, edit 9 of
1981: "removed the last of the C$$EIS stuff"), and `AS` assembles the same
from either header, so there is one `AS`.

Every module is assembled with the tape's `RT11.MAC` first: it sets
`C$$SXT = 1` and `C$$EIS = 0`, since the machine's T11 has `SXT` and `SOB`
but no `MUL`, `DIV` or `ASH`.  The second build reads `RT11.EIS`
(`C$$EIS = 1`) instead: its code has the instructions inline and runs only
under `EM.SYS`, the instruction emulator (`SET EM SYSGEN`, `SET EM ON`).
The kit on the tape (`MISC/DECUSC`) was the EIS build: its `CLIB` traps on
`i*i` here, which is why the library is rebuilt at all.

The command files are typed at the machine as they are, with three
differences the machine makes: the logical names (`SR:`, `OB:`, `OU:`,
`MP:`) are assigned at boot to the work volume, the listings go to `NL:`,
and `/C` - a cross-reference listing, which needs a `CREF.SAV` the kit has
not - is dropped from the MACRO lines (on a LINK line `/C` means "continue"
and stays).  The work volume is an `.hd` image: on a folder every tentative
file a compiler opens swells to half the free space.

## The tools

`build_tools.py` builds the programs of the tape's `601` ("software tools":
`GREP`, `DIFF`, `WC`, `SORTC`, `UNIQ`, `OD`, `PR`, `MC`, `MP`, `NM`, `T`,
`KWIK`, `XRF`, `BUILD` ...) by `TTOOL.COM`, the command file `BUILD` wrote
for them, and the one-file programs of `602` the RT-11 kit shipped
(`BANNER`, `CALEND`, `CRYPT`, `DUAL`, `E`, `GRAB`, `HACK`, `KALEID`, `NC`,
`PHBOOK`, `PTR`, `SH`) by the same three lines each.  Twice: against `CLIB`
as the compiler makes code for the T11, and with `/E` against `CLIBE`.

## What came out (2026-09-25)

| | blocks | against the RT-11 kit on the tape |
|---|---|---|
| `CC.SAV` / `CCE.SAV` | 124 | the kit's is 123, an EIS-and-FPU build |
| `AS.SAV` | 36 | 36; 1964 bytes apart (both hold the opcode table) |
| `CLIB.OBJ` / `CLIBE.OBJ` | 106 / 107 | 107, EIS |
| tools, each variant | 39 programs | 33 in the kit, the same sizes to a block (`MP` 42 vs 27) |

`HELLO` runs by `CC` with `CLIB` on the plain system and by `CCE` with
`CLIBE` under `EM`; `ECHO`, `WC`, `GREP`, `UNIQ`, `DETAB`, `BANNER` and
`CALEND` of each variant run the same way (`verify_tools.py`).  Two of the
tape's 40 are not there: `TR` needs the `VSTRING` library of `606`, which
`TTOOL.COM` does not build (the kit has no `TR` either), and `UNIQ.C`
compiles only without `CTYPE.H` in reach - with it `CC` loses the
`register char **dp` of its usage() ("No definition for dp"); the `UNIQ`
here is the build without, its `is...()` from the library, and it runs.
`SORTS` in the kit is a library the kit's makers made a program of.

## Running

```
python build.py            # all stages, into $TEMP/decusc_build/out
python build.py --keep cc  # one stage again on the staged volume
python build_tools.py      # the tools, both ways, into $TEMP/decusc_tools
```

`MS0515_DECUSC_BUILD` moves the build folders elsewhere.  A build takes a
quarter of an hour on the CLI emulator, which runs the machine as fast as
the host allows; the compiler assembles in silence, so the step timeouts
(`DECUTIL_QUIET`, `DECUTIL_CAP`) are long.

## Using the compiler

```
RUN C:CC          CC> PROG           (PROG.C to PROG.S; /E for inline EIS)
RUN C:AS          AS> PROG/D         (PROG.S to PROG.OBJ, the .S deleted)
R LINK            *PROG=PROG,C:SUPORT,C:CLIB/B:2000
RUN PROG          Argv:              (Enter: no arguments)
```

`C:` is the device the headers and the library live on.  The compiler is
K&R C of 1978: no `#define` with arguments (the tape's `MP` preprocessor
does those), no `unsigned`, floating point only through the library.  Its
one optimisation is `register` (R4, R3, R2 for the first three declared):
the same loop runs a third faster with it.  A program that does not need
`stdio` links a start of its own in place of `C:SUPORT` and comes out at
three blocks instead of twelve; the `Argv:` prompt goes with
`int $$narg = 1;`.  `docs/programming.md` has the machine's side.
