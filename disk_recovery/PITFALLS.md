# Pitfalls

Concrete anti-patterns that have produced *wrong* recoveries on
MS-0515 disks. Each one looked correct at the time. Re-read before
trusting any merge.

## 1. Circular donor — "confirmed" by the same physical media

Using a capture to validate another capture of the **same floppy**
proves nothing. Example: an older TeleDisk and a newer `.raw` of the
same disk agree on a sector → that does **not** confirm the sector;
the reader can misread the same physical defect identically on both,
and they agree while both wrong.

Rule: when validating disk *X*, exclude from the corpus **all** of:
- *X* itself,
- earlier/other captures of the same physical floppy,
- every derivative build (`*_authoritative`, `*_recovered`, `*_ultimate`),
- per-side splits of the same disk (`_Head0/_Head1`, `_s0/_s1`).

Filter by family-name prefix, not just exact filename.

## 2. Fake recovery — pattern match without a uniqueness gate

Anchor-pattern donor search that accepts a donor on a *partial* byte
match grabs a *different* file that merely shares common code/text
runs. Real cases: a block of `FORTRA` "recovered" into `MSDOS`; blocks
of `065`'s `183107.EXE` (only 7% full-file match) injected into a
different `183107.EXE`. Both were contamination, both reverted.

Rule: a donor must match the target file at **≥ 80% over surviving
blocks** (proves same binary/version) **before** any single block is
taken from it. The anchor must be unique on the *target* disk.

## 3. raw == final agreement does **not** mean the block is correct

SAMdisk can fail the same sector identically on both the `.raw` and
the `-final.dsk` pass. A disagreement-only adjudication then leaves the
shared error in place because "both agree". Audit blocks where
`raw == final == <suspicious>` against an *independent* source too,
especially zero blocks (Step 7) and known-text regions.

## 4. Ranking candidates by non-zero count instead of readability

A garbage block has more non-zero bytes than a correct text block full
of spaces and zero padding, so a "most non-zero wins" merge picks the
garbage. (This corrupted a `.DOC` recovery once.) Always score text by
**readable bytes** = printable ASCII + KOI-8R Cyrillic + CR/LF/TAB.
See METHODOLOGY Step 4.

## 5. DS-spanning recombine is easy to get subtly wrong

A **DS-spanning** disk addresses both sides as one ~1600-block volume
(distinct from a normal double-sided diskette, which is two independent
800-block sides — see filesystem.md). Recombining it interleaves sides
per track and wraps cylinder 0 to the end; getting the head/track order
or the cyl-0 wrap wrong yields files that *look* plausible but disagree
with confirmed disks. An earlier Python extractor had its DS-spanning
branch **disabled** for exactly this reason. The emulator does not read
spanning volumes at all, and the format has not been re-verified under
the FDC model — so validate any spanning extractor by re-extracting a
file known from another source and checking it byte-for-byte before
trusting the rest.

## 6. The word "authoritative" is a label, not a proof

A file named `*_authoritative.dsk` is only a prior merge's *output*.
Where two "authoritative" images of disks that should share a file
disagree, at least one is wrong — the name guarantees nothing. Treat
these as derived artifacts (never as sources), and re-verify before
promoting anything into `authoritative/`.

## 7. Legitimate difference mistaken for corruption (and vice-versa)

Two disks can share files on one side and carry entirely different
programs on the other. A raw binary diff of two full images then shows
huge "differences" that are *expected*, drowning the few real
corruption bytes. Always diff **per logical file on the shared set**,
not whole images. Conversely, within a shared file, distinguish a few
1–2-bit flips (decay) from a wholesale-different block (a real version
difference) via the Step 5 classifier — do not "fix" a real difference.
