# Toolkit

How the methodology is executed in code. The recovery tools are being
built in **C++**, in the same style as the rest of the project (C11
core idioms, C++23, CMake + Conan, doctest), and live **inside
`emu/`** as a new sublibrary + binaries — no dependency on any
directory above the source tree. `disk_recovery/` holds knowledge and
the verified-image vault only; it contains no build inputs.

> If `emu/` is later renamed to `src/`, the tool location moves with
> it — nothing here points outside the source tree.

## Target C++ architecture

A new disk-tooling layer alongside `core/` / `lib/` / `libapp/`:

```
emu/disk/                  (proposed name)
  include/ms0515/disk/     RT-11 image model — public headers
  src/
    Layout.cpp             the five LBN→byte mappings + DS-spanning
                           recombine (formulas: docs/hardware/filesystem.md)
    Image.cpp              load a capture, classify size/geometry
    Directory.cpp          RT-11 home block + segment chain + RAD50 parse
    Extract.cpp            pull a file's blocks through the file-layout
    Td0.cpp                TeleDisk decoder + per-sector flag map
    Consensus.cpp          per-byte multi-source vote (independence-aware)
    Donor.cpp              anchor-pair search + ≥80% / uniqueness gates
    Readability.cpp        KOI-8R + ASCII readable-byte score
  tests/                   doctest, mirrors the layering elsewhere
```

Binaries (thin, over the lib):

| Binary | Job |
|--------|-----|
| `ms0515-dir` | List a volume's directory (any layout / DS-spanning). |
| `ms0515-extract` | Pull files out of one capture into a folder. |
| `ms0515-recover` | Run the full per-file consensus over a `sources.json` manifest; emit the confidence matrix + clean files. |

### What is reused from the emulator

- **Layout formulas** — pure functions, ported 1:1 from
  `docs/hardware/filesystem.md` (and the legacy `tools/rt11_dir.py`).
- **`libapp`** — filesystem path helpers, config/manifest loading.
- **Build + test idioms** — the same CMake targets, `/W4 /WX` clean,
  doctest suites per the project rules.
- The emulator's FDC is **not** reused for offline extraction: file
  recovery does not run the machine, it reads sectors directly. (The
  emulator only ever feeds raw physical sectors to the guest OS; RT-11
  filesystem parsing has never lived in the core, so it is new here —
  but small, ~250 lines of logic in the Python reference.)

## Legacy Python reference (in `../tools/`)

These are the working *reference implementation* to port from, not the
long-term tools. They are collection-oriented (operate over a
populated `collection/ss/`), and notably the DS-spanning extractor was
disabled (see PITFALLS §5).

| Script | Role | Port target |
|--------|------|-------------|
| `rt11_dir.py` | Layout mappings + directory/RAD50 parse + single-file extract. | `Layout` + `Directory` + `Extract` |
| `td0_decode.py` | TeleDisk reader (methods 0/1/2, `.badmap` flags). | `Td0` |
| `extract_files.py` | Bulk file extraction with mapping auto-detect. | `ms0515-extract` |
| `fingerprint_systems.py` | Cluster disks by boot-block hash. | `ms0515-dir --scan` |
| `build_collection.py` | Ingest raw/TD0/Extended-DSK into a normalised collection. | ingest path of `ms0515-recover` |
| `convert_extended_dsk.py` | SAMdisk Extended-CPC-DSK parser. | `Image` (final-dsk path) |

The recovery-specific logic (consensus, donor gating, readability,
bit-rot classifier, TD0 verdict) has **no** Python reference that
survived — it must be written fresh in C++ from METHODOLOGY.

## Validation discipline

Per the project's TDD rule: design the `disk/` interface, write doctest
cases first (a known file extracted byte-for-byte; a synthetic bit-rot
block classified correctly; a circular-donor rejected), then implement.
A DS-spanning extractor must reproduce a file already known from an
independent source before any of its other output is trusted
(PITFALLS §5).
