/*
 * Build.hpp — create and populate MS-0515 RT-11 volumes, mirroring the OS's
 * own create / INIT / PIP steps:
 *
 *   blankImage  — raw unformatted media (the 0xB6 0x6D power-up pattern)
 *   initVolume  — format one side (boot block + home block + empty directory),
 *                 byte-identical to what the OS's INIT writes
 *   putFile     — add one file to an already-initialised side (like PIP)
 *
 * All addressing goes through the FDC geometry (Layout.hpp), so the results
 * are byte-compatible with what the emulator reads and writes.
 */

#ifndef MS0515_DISK_BUILD_HPP
#define MS0515_DISK_BUILD_HPP

#include "ms0515/disk/Layout.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace ms0515::disk {

/* Options mirroring the OS INITIALIZE switches that shape the on-disk format.
 * Defaults reproduce a plain `INIT` byte-for-byte (volume id "RT11A", blank
 * owner, 4 directory segments). */
struct InitOptions {
    std::string volumeId = "RT11A";  /* /VOLUMEID — up to 12 chars */
    std::string owner    = "";       /* owner name — up to 12 chars */
    int         segments = 4;        /* /SEGMENTS  — directory segments (1..31) */
};

/* Per-file metadata the directory entry carries alongside the bytes.  Both
 * fields default to "leave blank" (date 0 = no date, protected off), matching
 * what PIP writes when the operator hasn't set a system date. */
struct PutOptions {
    uint16_t date = 0;       /* Encoded RT-11 directory date — see encodeDate() */
    bool   readOnly = false; /* kStatusProtected on the entry (RT-11 "/PROTECT") */
};

/* Pack a (year, month, day) into the RT-11 directory date word.  Bit layout:
 *   bits 0-4   : year - 1972 mod 32         (5 bits, "year_bits")
 *   bits 5-9   : day (1..31)                (5 bits)
 *   bits 10-13 : month (1..12)              (4 bits)
 *   bits 14-15 : age = (year - 1972) >> 5   (2 bits)  -> extends range to 2099
 *
 * Throws std::runtime_error on out-of-range inputs (year not in 1972..2099,
 * month not in 1..12, day not in 1..31).  year/month/day == 0 means "no
 * date", and encodes to 0. */
[[nodiscard]] uint16_t encodeDate(int year, int month, int day);

/* Inverse of encodeDate: unpack a directory date word into (year, month, day).
 * Returns the all-zero tuple for a zero-valued date (the "no date" sentinel
 * the OS writes when no system date has been set).  Never throws; out-of-band
 * inputs come out of the bit fields (e.g. month bits = 0 stay 0). */
struct DateParts { int year, month, day; };
[[nodiscard]] DateParts decodeDate(uint16_t encoded);

/* Raw, unformatted media: 409600 bytes (single-sided) or 819200 (double-
 * sided), filled with the 0xB6 0x6D blank pattern.  No RT-11 structure. */
[[nodiscard]] std::vector<uint8_t> blankImage(bool ds);

/* Raw, unformatted linear HD/LD container of `blocks` * 512 bytes,
 * zero-filled (the paravirtual HD: device powers up zeroed).  No RT-11
 * structure — run initVolume(..., linear=true) to format it. */
[[nodiscard]] std::vector<uint8_t> blankLinear(int blocks);

/* Format side `side` (0 lower/boot, 1 upper) of `image` as an empty RT-11
 * volume.  `image` must already be a blank of the right size for `ds`.
 * Throws std::runtime_error on a size/side mismatch, a too-long id/owner, or
 * a bad segment count. */
void initVolume(std::vector<uint8_t> &image, int side, bool ds,
                const InitOptions &opts = {}, bool linear = false);

/* Write the volume id and the owner (12 characters each at most) into the
 * home block of an initialised side, the directory and the files left as
 * they are - INITIALIZE/VOLUMEID:ONLY.  Throws std::runtime_error when
 * the side holds no directory or a name is too long. */
void setVolumeId(std::vector<uint8_t> &image, int side, bool ds,
                 const std::string &volumeId, const std::string &owner,
                 bool linear = false);

/* Add one file to the initialised volume on `side` (the equivalent of PIP).
 * Throws std::runtime_error if the side is not initialised, the name is not
 * RAD50-encodable, the data does not fit, or the directory segment is full.
 * `opts` lets the caller set the entry's date and protected flag — useful
 * when replacing a system file that the OS marked /PROTECT or when the user
 * wants the original write date preserved. */
void putFile(std::vector<uint8_t> &image, int side, bool ds,
             const std::string &name, std::span<const uint8_t> data,
             const PutOptions &opts = {}, bool linear = false);

/* Flip the kStatusProtected bit on the directory entry of `name` on `side`.
 * `on=true` sets it (equivalent to PIP /PROTECT), `on=false` clears it
 * (PIP /NOPROTECT).  Throws std::runtime_error if the file is not found or
 * the side is not initialised. */
void setProtected(std::vector<uint8_t> &image, int side, bool ds,
                  const std::string &name, bool on, bool linear = false);

/* Overwrite the date word on the directory entry of `name`.  Pass an already-
 * encoded value from encodeDate(); zero means "no date" (clears the field).
 * Throws std::runtime_error if the file is not found or the side is not
 * initialised. */
void setEntryDate(std::vector<uint8_t> &image, int side, bool ds,
                  const std::string &name, uint16_t date, bool linear = false);

/* Defragment a side (the equivalent of RT-11 SQUEEZE): walk every
 * permanent entry in directory order, move its data blocks left to be
 * contiguous starting at data_start, and rewrite the directory so the
 * permanents stay in order followed by a single empty entry covering all
 * remaining free space.  Throws if the side isn't initialised or the
 * directory has multiple segments (multi-segment squeeze isn't supported
 * yet — INIT defaults still produce a single segment under our usage). */
void squeeze(std::vector<uint8_t> &image, int side, bool ds, bool linear = false);

/* Delete one file from `side` (the equivalent of PIP /DELETE): looks up the
 * directory entry by name and flips its status to empty, the length kept
 * so a subsequent putFile() that fits can reuse the blocks - and the name
 * and the date kept too, as the OS's DELETE leaves them (OS-verified), so
 * undeleteEntry() can bring the file back.  Empty entries next to it are
 * merged into one, as the OS does (seen: two, then three neighbours
 * became one entry with the earliest one's name, length summed) - so a
 * file put back larger, with room behind it, lands where it was.  Throws
 * std::runtime_error if the side is not initialised, the name is not
 * RAD50-encodable, or no permanent file with that name exists. */
void removeFile(std::vector<uint8_t> &image, int side, bool ds,
                const std::string &name, bool linear = false);

/* Rename a permanent file in place (the entry's RAD50 words); the new name
 * must be a valid 6.3 name not already on the volume.  Throws on either. */
void renameFile(std::vector<uint8_t> &image, int side, bool ds,
                const std::string &name, const std::string &newName,
                bool linear = false);

/* The monitor a floppy side boots: the .SYS file whose blocks 1..3 are the
 * side's LBN 2..4 (the secondary bootstrap COPY/BOOT put there), named
 * without the extension ("RT11SJ", "MON8SJ", "RT15SJ").  "" when none -
 * the side does not boot, or holds no directory. */
[[nodiscard]] std::string bootedMonitor(const std::vector<uint8_t> &image, int side, bool ds);

/* The files a system volume needs, of those the side holds, in directory
 * order: the monitor, SWAP.SYS, DZ.SYS and TT.SYS - the boot refuses to go
 * on without any of them (seen: "?BOOT-U-No SWAP.SYS", "?BOOT-W-TT.SYS
 * missing") - and PIP, DUP, DIR, RESORC, the utilities that let the copy
 * do the same again.  Other handlers (VM, LD, HD...) are extras. */
[[nodiscard]] std::vector<std::string> systemKit(const std::vector<uint8_t> &image, int side, bool ds,
                                                 const std::string &monitor);

/* The command file a monitor runs at boot ("STARTS.COM" for the Omega
 * kits, "ST.COM" for OSA, "START.COM" for RT15SJ): the monitor file holds
 * it as the KMON line "@NAME" - a NUL, "@", the name in six characters,
 * a NUL.  "" when the file holds none. */
[[nodiscard]] std::string startupFile(const std::vector<uint8_t> &monitorFile);

/* A system volume: the kit of `source`'s side put on `target`'s side (the
 * files there of the same names replaced), every one protected as the
 * system's own volumes have them; the extras named copied as they are;
 * the monitor's startup command file made anew, unprotected, holding
 * "SET TT QUIET" alone, unless the target holds one; then the bootstrap.
 * Returns the monitor's name.  Throws when the source does not boot, the
 * target holds no directory, or a file does not fit. */
std::string makeSystemVolume(std::vector<uint8_t> &target, int side, bool ds,
                             const std::vector<uint8_t> &source, int sourceSide, bool sourceDs,
                             const std::vector<std::string> &extras = {});

/* The bootstrap of a floppy side - what COPY/BOOT (DUP) writes, verified
 * byte for byte against every DUP and monitor in the collection:
 *   LBN 0    = 512 bytes of the side's own DZ.SYS from the offset in word
 *              0o62 of the handler's block 0 (the .DRBOT macro's primary
 *              driver; word 0o66 the offset of its read routine);
 *   LBN 2..5 = blocks 1..4 of the monitor file, the last one with word
 *              0o716 = RAD50 "DZ ", words 0o724 and 0o726 = RAD50 of the
 *              monitor's name, word 0o730 = the handler's word 0o66.
 * No block numbers: the secondary bootstrap finds the monitor by name.
 * `monitor` is the file's name with or without ".SYS".  Throws
 * std::runtime_error when the side holds no directory, DZ.SYS or the
 * monitor is missing, DZ.SYS has no primary driver (word 0o62 zero), or
 * the monitor's block 4 is not zero where the words go (a boot block of
 * another layout: left alone). */
void writeBoot(std::vector<uint8_t> &image, int side, bool ds, const std::string &monitor);

/* Enlarge a linear (HD / logical-disk) volume by `blocks`: zero blocks
 * appended and the directory told - the last entry of the last segment
 * lengthened when it is an empty one, else a new empty entry added before
 * the end marker.  A logical disk is a file, and the LD handler takes the
 * volume's size from the file's length, so a grown one mounts as is.
 * Throws std::runtime_error on a non-positive count, an uninitialised
 * image, or a directory segment with no room for the new entry. */
void growLinear(std::vector<uint8_t> &image, int blocks);

/* Bring a deleted file back: the empty entry at `ordinal` (its place in
 * the first directory segment, counting every entry from the first) made
 * permanent again with the name, length and date it kept.  The data is
 * whole unless something was put over the area since - which leaves the
 * sentinel name on what remains of it, so such an entry is refused.
 * `newName`, when given, is the name the file comes back under (the old
 * one taken meanwhile, say) - and with it any unused area, even one that
 * holds no deleted file's name, is made a file of that name and length:
 * a recovery of whatever lies in it.  Throws std::runtime_error when the
 * entry is not an unused area, no proper 6.3 name is there or given, or a
 * file of the name is on the volume already. */
void undeleteEntry(std::vector<uint8_t> &image, int side, bool ds, int ordinal,
                   const std::string &newName = "", bool linear = false);

} /* namespace ms0515::disk */

#endif /* MS0515_DISK_BUILD_HPP */
