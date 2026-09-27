# Toolkit

How the methodology is executed in code.  The **format layer** is built in
C++ inside `src/`, in the project's style (C++20, CMake + Conan, doctest,
`/W4 /WX`).  `disk_recovery/` holds knowledge, the verified-image vault, and
the recovery pipeline's own Python scripts under `tools/` (no build inputs -
`work/` is gitignored).

## Format tools (built and verified)

Library **`ms0515_disk`** — [`../src/disk/`](../src/disk/):

| Module | Job |
|--------|-----|
| `Layout` | `LBN → byte` geometry, mirroring the emulator FDC: size picks SS/DS, and one universal driver mapping (2:1 interleave + per-track skew). The source of truth is [`../src/core/src/floppy.c`](../src/core/src/floppy.c); see [`../docs/hardware/filesystem.md`](../docs/hardware/filesystem.md). |
| `Directory` | RT-11 home block + segment chain + RAD50 parse. |
| `Image` | Load a capture (size selects SS/DS + side), read files; `splitDoubleSided` / `mergeSides` reshape between an 800 KB DS image and two 400 KB SS images. |
| `Build` | `blankImage` (raw media), `initVolume` (format a side, byte-identical to OS INIT), `putFile` (add a file, like PIP — first-fit scan of empty entries, tail entries preserved; `PutOptions` carries `date` + `readOnly`), `removeFile` (delete a file; the freed slot becomes a reusable empty entry), `squeeze` (defragment, RT-11 SQUEEZE analogue), `setProtected` / `setEntryDate` (in-place edits of a permanent entry's flags and date), `encodeDate` (pack year/month/day into the RT-11 date word). |

Binary **`ms0515-disk`** — [`../src/tools/disk/`](../src/tools/disk/):

| Command | Job |
|---------|-----|
| `create <out> [--ds]` | Raw blank media (0xB6 0x6D), 400 KB or 800 KB. |
| `init <img> [--side N] [--volume-id ID] [--owner NAME] [--segments N]` | Format one side — byte-identical to the OS `INITIALIZE`. |
| `put <img> [--side N] [--date YYYY-MM-DD] [--protected] <file\|glob>...` | Add host files (like PIP, inbound); `*` globs. First-fit picks the first empty slot that fits. `--date` writes the entry's RT-11 date word; `--protected` sets the /PROTECT flag. |
| `rm  <img> [--side N] <name>...` | Delete files (PIP /DELETE); the freed blocks become an empty entry a later `put` can reuse. |
| `squeeze <img> [--side N]` | Defragment (RT-11 SQUEEZE): move every permanent file's data left to be contiguous, leave one trailing empty covering all freed space.  Metadata (status, date) is preserved across the move. |
| `protect / unprotect <img> [--side N] <name>...` | Toggle the entry's /PROTECT flag in place (status only, data and date untouched). |
| `get <img> [--side N] [--out DIR] [pattern]...` | Extract files (PIP, outbound); `*` patterns. |
| `dir <img> [--side N]` | List the directory. |
| `boot <img> [--side N \| --dv] [MONITOR]` | Write the bootstrap (RT-11 COPY/BOOT) for the named monitor - the volume's own DZ.SYS and monitor file; defaults to the one `.SYS` that is a monitor. |
| `system <target> --from <img> [--side N \| --dv] [extra]...` | Build a system volume: the kit (monitor, SWAP, DZ, TT, PIP, DUP, DIR, RESORC - protected) copied from a bootable image, plus the named extras, a fresh startup `.COM`, then the bootstrap; the target must already be initialised. |
| `setdate <img> [--side N] --date YYYY-MM-DD <name>...` | Write the directory date of an entry in place. |
| `split <ds> <s0> <s1>` | Split an 800 KB double-sided image into two 400 KB single-sided images. |
| `merge <s0> <s1> <ds>` | Merge two 400 KB single-sided images into one 800 KB double-sided image. |
| `compose --repo DIR ...` | Build a bootable disk from the software collection's `disks.toml` (`--list`, `--preset KEY <out>`, `--all <dir>`, or `--system KEY --media ss\|dz\|dv <out>`). |

Geometry follows the image **size** (409600 = single-sided, 819200 = double-
sided; `--side` picks a side).  There is no layout flag — the physical
mapping is not a per-OS choice (see filesystem.md).

## Verified against the OS, not just self-consistent

The format tools are cross-checked against the real OS running in the
emulator (the authoritative oracle), in
[`../src/lib/tests/test_dir_vs_os.cpp`](../src/lib/tests/test_dir_vs_os.cpp):

- **DIR vs OS** — the tool's directory parse matches the OS's own `DIR`
  for OSA / Omega / Mihin (SS) and rodionov (DS).
- **content oracle** — the OS INITs a blank and PIPs a real file onto it;
  the tool extracts byte-for-byte identical content (only the OS's true
  geometry makes this hold).
- **build == INIT** — `blankImage + initVolume` reproduces a real OS
  `INIT` byte-for-byte, for SS and DS, so a built volume is readable *and*
  writable by the OS.

## Recovery heuristics — built

The recovery-specific logic — multi-source consensus, donor gating,
readability scoring, the bit-rot classifier, the TD0 natural-zero verdict
(all in [`METHODOLOGY.md`](METHODOLOGY.md)) — is implemented in Python
under [`tools/`](tools/README.md), layered on the C++ format primitives:
`import_images.py` / `identify.py` / `convert_samdisk.py` /
`convert_teledisk.py` / `read_spanning.py` ingest and normalise a capture,
`build_corpus.py` / `analyze_corpus.py` / `consensus.py` reconcile it into
the unique-file corpus, `report.py` / `export.py` / `decide.py` /
`review.py` turn that into a confidence matrix and a place to pick
canonical versions, and `verdict.py` / `donor.py` hold the shared model and
the donor search.  See `tools/README.md` for the full pipeline and
`HOWTO.md` for the walkthrough.

Ingest of other containers (an LD container, an LBN-linear flat dump) still
has no converter: normalise to a plain SS/DS physical image first, then run
the format tools on it, the way `convert_samdisk.py` / `convert_teledisk.py`
/ `read_spanning.py` already do for Extended-CPC, TeleDisk and DS-spanning
volumes.

## Validation discipline

Per the project's TDD rule: design the interface, write doctest cases
first, then implement.  Format behaviour is additionally pinned against the
OS oracle above.  Any new recovery code must reproduce a file already known
from an independent source before its other output is trusted (see
[`PITFALLS.md`](PITFALLS.md)).
