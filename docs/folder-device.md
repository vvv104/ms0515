# Folder-Backed Block Devices (`.rtfs`)

## Concept

A host folder can be mounted as an emulator block device — the paravirtual
hard disk (`HD:`) or a floppy (`DZn:`) — on any host OS.  The folder
contains a plain-text **descriptor** file (extension **`.rtfs`**, any base
name) that defines the device and maps host files to RT-11 files.  The
user mounts the device by pointing the regular mount UI/flags at the
descriptor: the extension decides image (`.dsk`/`.hd`) vs folder
(`.rtfs`) — there is no separate menu.  Mounting a descriptor whose
`device:` type does not match the slot is an error.

There is no geometry — a device is fully described by its size in 512-byte
blocks (`blocks:`).  A floppy descriptor must say exactly 800 blocks (one
single-sided diskette); an HD descriptor may say anything up to the RT-11
limit of 65535.

## The descriptor is the source of truth

```
# MS0515 folder-backed block device
device: hd                 # hd | floppy
blocks: 20000
volume-id: MYVOL
boot: boot.bin             # floppy only; hidden from RT-11; fills LBN 0 + 2..5
file: SWAP.SYS   | swap.sys        | date=1994-02-18
file: RT11SJ.SYS | rt11sj.sys      | date=1994-02-18 protected
file: MINESW.PAS | minesweeper.pas | date=1995-04-01
file: OLD.TXT    | old-notes.txt   | deleted
```

- One `file:` line per file: RT-11 name | host name | attributes.
- **Line order = block order.**  Start blocks and lengths are never stored;
  they are derived (sequentially from the first data block, length = host
  file size rounded up to blocks).  A host file changing size therefore
  reshuffles everything after it by construction — entries can never
  overlap.
- The RT-11 home block and directory segments are **generated on the fly**
  from the descriptor; they are not stored in the folder.
- The descriptor itself and the boot file are never visible inside RT-11.
- `deleted` keeps the host file but hides the entry from RT-11 (guest
  deletions set this flag rather than deleting host data).
- A descriptor entry whose host file has disappeared is simply **dropped**
  on the next folder rescan: anything can happen outside, and the folder
  is accepted as it is.  A renamed host file therefore re-enters as a new
  file (fresh RT-11 name, no carried metadata); removing the system
  monitor from under a system folder is the user's own risk.
- `volume-id:` and `owner:` feed the generated home block; a guest
  `INIT` that writes a new home block updates them in the descriptor.

## Auto-fill

Mounting a descriptor that has no `file:` lines while the folder contains
files populates the descriptor: every regular file in the folder (except
the descriptor and the boot file), `.SYS` files first (`SWAP.SYS`,
`RT11SJ.SYS`, then the remaining `.SYS`), then everything else in
directory-enumeration order.  Host names are mangled to valid RT-11 6.3
RAD50 names (uppercase, alphabet `A-Z 0-9 $`, truncated, collisions
resolved with a numeric tail).  Files that do not fit on the device are
skipped (with a warning) — fitting is the user's responsibility.

`.SYS` entries are pinned at the front so a folder can serve as a system
disk whose system area stays put.

## Reads, writes, synchronization

- **Data blocks** map straight onto host files: guest reads `fseek` into
  the host file (external edits are visible immediately); guest writes go
  to the host file write-through (so e.g. `SWAP.SYS` swapping works and
  survives a crash).  All content is byte-exact — no encoding conversion
  (deliberately: many host files are original recovered artifacts).
- **Directory blocks**: a guest read regenerates the directory after a
  cheap folder rescan (new/changed/missing host files get picked up at the
  natural RT-11 rhythm — every directory operation).  A guest write to a
  directory block is re-parsed and diffed against the descriptor:
    - new entry → a host file is materialized (name de-mangled to
      something readable, recorded in the descriptor),
    - entry gone → the descriptor line gets `deleted` (host file kept),
    - renamed → the RT-11 name in the descriptor is updated.
- Conflicts (same file changed inside and outside simultaneously) resolve
  as last-writer-wins; no locking.

## Staging

1. **Folder-as-HD** end to end (linear device, no boot): descriptor +
   scan/mangle/layout in `ms0515_disk` (offline TDD), generic block-backend
   callbacks in the core HD device, `FolderVolume` wiring in lib,
   extension routing in libapp/frontend, OS-oracle validation.
2. **Folder-as-floppy**: FDC backend (track/sector→LBN via the existing
   Layout), boot file, bootable system-disk support.
3. Edge polish: rename heuristics, conflict tests, `.BAD` flows.

Open (parked) questions: text-encoding conversion (host UTF-8 ↔ KOI-8R /
cp866) — deliberately out of v1.
