# Toolkit

How the methodology is executed in code.  The **format layer** is built in
C++ inside `src/`, in the project's style (C++23, CMake + Conan, doctest,
`/W4 /WX`).  `disk_recovery/` holds knowledge and the verified-image vault
only — no build inputs, no scripts.

## Format tools (built and verified)

Library **`ms0515_disk`** — [`../src/disk/`](../src/disk/):

| Module | Job |
|--------|-----|
| `Layout` | `LBN → byte` geometry, mirroring the emulator FDC: size picks SS/DS, and one universal driver mapping (2:1 interleave + per-track skew). The source of truth is [`../src/core/src/floppy.c`](../src/core/src/floppy.c); see [`../docs/hardware/filesystem.md`](../docs/hardware/filesystem.md). |
| `Directory` | RT-11 home block + segment chain + RAD50 parse. |
| `Image` | Load a capture (size selects SS/DS + side), read files; `splitDoubleSided` / `mergeSides` reshape between an 800 KB DS image and two 400 KB SS images. |
| `Build` | `blankImage` (raw media), `initVolume` (format a side, byte-identical to OS INIT), `putFile` (add a file, like PIP — first-fit scan of empty entries, tail entries preserved), `removeFile` (delete a file; the freed slot becomes a reusable empty entry). |

Binary **`ms0515-disk`** — [`../src/tools/disk/`](../src/tools/disk/):

| Command | Job |
|---------|-----|
| `create <out> [--ds]` | Raw blank media (0xB6 0x6D), 400 KB or 800 KB. |
| `init <img> [--side N] [--volume-id ID] [--owner NAME] [--segments N]` | Format one side — byte-identical to the OS `INITIALIZE`. |
| `put <img> [--side N] <file\|glob>...` | Add host files (like PIP, inbound); `*` globs. First-fit picks the first empty slot that fits. |
| `rm  <img> [--side N] <name>...` | Delete files (PIP /DELETE); the freed blocks become an empty entry a later `put` can reuse. |
| `get <img> [--side N] [--out DIR] [pattern]...` | Extract files (PIP, outbound); `*` patterns. |
| `dir <img> [--side N]` | List the directory. |
| `split <ds> <s0> <s1>` | Split an 800 KB double-sided image into two 400 KB single-sided images. |
| `merge <s0> <s1> <ds>` | Merge two 400 KB single-sided images into one 800 KB double-sided image. |

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

## Recovery heuristics — not yet built

The recovery-specific logic — multi-source consensus, donor gating,
readability scoring, the bit-rot classifier, the TD0 natural-zero verdict
(all in [`METHODOLOGY.md`](METHODOLOGY.md)) — is **not** implemented.  It is
meant to live in Python under `disk_recovery/`, layered on the C++ format
primitives, and must be written fresh from the methodology: no committed
reference survives (earlier restored extraction scripts were removed once
`ms0515-disk` covered the format layer).

Ingest of other containers (TeleDisk `.TD0`, Extended-CPC `.dsk`, an LD
container, a DS-spanning whole-disk volume, an LBN-linear flat dump) also
belongs to that future layer: normalise to a plain SS/DS physical image
first, then run the format tools on it.

## Validation discipline

Per the project's TDD rule: design the interface, write doctest cases
first, then implement.  Format behaviour is additionally pinned against the
OS oracle above.  Any new recovery code must reproduce a file already known
from an independent source before its other output is trusted (see
[`PITFALLS.md`](PITFALLS.md)).
