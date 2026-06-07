# MS-0515 Disk Recovery

Durable knowledge base and reference vault for recovering data from
aging / damaged MS-0515 (Elektronika MS 0515) floppy disks.  This
directory is **format- and disk-agnostic**: it captures the general
method so any future disk dump can be recovered repeatably, without
re-deriving the technique each time.

It exists because the original recovery work lived only in throwaway
scripts and ad-hoc sessions; the *outputs* survived but the *recipe*
was nearly lost.  Everything load-bearing now lives here in English,
in the repo.

## What is where

| Path | Contents |
|------|----------|
| `README.md` | This file — orientation + the trust model. |
| `HOWTO.md` | Step-by-step usage guide: from empty `work/` to GUI review, every script and what it produces. Numbers verified by a live end-to-end run. |
| `METHODOLOGY.md` | The recovery playbook: source taxonomy, the consensus ladder, scoring metrics, donor rules, confidence tiers. General to any disk. |
| `PITFALLS.md` | Anti-patterns that produced wrong "recoveries" in the past. Read before trusting any merge. |
| `TOOLKIT.md` | The tooling: the built `ms0515-disk` format tools (create/init/put/rm/protect/unprotect/get/dir), how they are verified against the OS, and what recovery logic is still to be written. |
| `authoritative/` | Vault of disk images that have *passed* the bar in METHODOLOGY. These — and only these — may be used as donors for other recoveries. Starts empty until images are re-verified under the documented method. |

## On-disk format spec

This directory does **not** re-document the RT-11 / MS-0515 on-disk
format — that already lives in
[`../docs/hardware/filesystem.md`](../docs/hardware/filesystem.md):
the FDC physical geometry (size → single/double-sided), the one
universal `LBN → byte` driver mapping (2:1 interleave + per-track
skew), the home block, directory segments, RAD50, and the status-word
bits.  It also notes the two non-diskette container kinds (DS-spanning
volumes, LD containers).  Recovery code reads from that spec; this
directory is about *process*, not format.

## The trust model (read this first)

Recovery is only as trustworthy as its sources, and **no single
captured image is ground truth**.  Every dump — `.raw`, SAMdisk
`-final.dsk`, TeleDisk `.TD0`, a merged `*_authoritative.dsk` — is one
fallible observation of a physically decaying medium.  Two rules
follow:

1. **Never treat a merged/"authoritative" image as a source.** A merge
   already baked in decisions that may be wrong. Always go back to the
   independent physical captures.

2. **Confidence comes from agreement between *independent* sources**,
   not from any one source looking clean. Two captures of the *same
   physical media* (e.g. a `.raw` and an older TeleDisk of the same
   floppy) are **not** independent for the bytes they agree on — a
   sector the reader misreads identically on both passes agrees while
   still being wrong.

### Source tiers

| Tier | Meaning | Use |
|------|---------|-----|
| **own** | Disks the operator physically owns and read themselves, ideally on multiple readers / dates. | Primary. Cross-validate within this tier. |
| **foreign** | Disks from collections / other people. Provenance and read quality unknown. | Donor of *last resort* only, for blocks no own-source can supply. Always flagged lower-confidence. |

A foreign disk may legitimately carry a byte-identical copy of the
same file (software was copied between machines), which makes it a
*candidate* donor — but it must clear the donor bar in METHODOLOGY
(high full-file match + cross-file uniqueness) before its bytes are
accepted, or it contaminates the result.

## The goal

The deliverable is **a set of files recovered to a stated confidence**,
not a patched disk image. Clean, classified files are the authority;
bootable disk images are *rebuilt from* them afterward. A file is only
"100%" when independent sources positively agree on every byte — not
when one copy merely looks readable.
