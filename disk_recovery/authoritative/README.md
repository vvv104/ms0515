# Authoritative image vault

Disk images here have **passed the bar** in
[`../METHODOLOGY.md`](../METHODOLOGY.md) and may be used as donors when
recovering other disks. Nothing else may. An image's presence here is
a claim that its files are verified, so the bar is deliberately strict.

This vault starts **empty**. Prior `*_authoritative.dsk` builds are
*not* admitted on the strength of their name — they were earlier merges
whose recipe was lost and whose residual differences are unresolved
(that is the whole reason this knowledge base exists). They are
re-verified under the documented method before promotion, or not at
all.

## Bar for inclusion

An image is admitted only when **all** hold:

1. Every `PERM` file extracts cleanly through the correct layout for
   the disk's OS.
2. Each file is classified **100% (verified)** or **recovered** per
   METHODOLOGY — i.e. independent own-tier sources positively agree, or
   damage was resolved by consensus / bit-rot fix / own-tier donor.
   No file rests on a foreign-tier donor or has residual loss.
3. The image boots in the emulator (if it is a system disk) **and**
   its files re-extract byte-for-byte after a round-trip.
4. Provenance is recorded in `INDEX.md` (below).

A disk with any **residual-loss** file does not belong here — keep it
with its recovery report instead, and list precisely what is lost.

## INDEX.md

Each admitted image gets a row recording how its authority was earned:

| Image | OS / layout | Source captures merged | Files (100% / recovered) | Verified (boot + re-extract) | Date |
|-------|-------------|------------------------|--------------------------|------------------------------|------|

(No images admitted yet.)

## Why a vault at all

Recovering disk *N+1* is far easier when disks `0..N` are trusted: a
lost block on a new disk can be donored from a vault image with known
provenance instead of from an unvetted foreign capture. The vault is
the compounding asset — each verified disk makes the next recovery
cheaper and safer.
