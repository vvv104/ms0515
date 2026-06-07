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

/* Raw, unformatted media: 409600 bytes (single-sided) or 819200 (double-
 * sided), filled with the 0xB6 0x6D blank pattern.  No RT-11 structure. */
[[nodiscard]] std::vector<uint8_t> blankImage(bool ds);

/* Format side `side` (0 lower/boot, 1 upper) of `image` as an empty RT-11
 * volume.  `image` must already be a blank of the right size for `ds`.
 * Throws std::runtime_error on a size/side mismatch, a too-long id/owner, or
 * a bad segment count. */
void initVolume(std::vector<uint8_t> &image, int side, bool ds,
                const InitOptions &opts = {});

/* Add one file to the initialised volume on `side` (the equivalent of PIP).
 * Throws std::runtime_error if the side is not initialised, the name is not
 * RAD50-encodable, the data does not fit, or the directory segment is full.
 * `opts` lets the caller set the entry's date and protected flag — useful
 * when replacing a system file that the OS marked /PROTECT or when the user
 * wants the original write date preserved. */
void putFile(std::vector<uint8_t> &image, int side, bool ds,
             const std::string &name, std::span<const uint8_t> data,
             const PutOptions &opts = {});

/* Flip the kStatusProtected bit on the directory entry of `name` on `side`.
 * `on=true` sets it (equivalent to PIP /PROTECT), `on=false` clears it
 * (PIP /NOPROTECT).  Throws std::runtime_error if the file is not found or
 * the side is not initialised. */
void setProtected(std::vector<uint8_t> &image, int side, bool ds,
                  const std::string &name, bool on);

/* Overwrite the date word on the directory entry of `name`.  Pass an already-
 * encoded value from encodeDate(); zero means "no date" (clears the field).
 * Throws std::runtime_error if the file is not found or the side is not
 * initialised. */
void setEntryDate(std::vector<uint8_t> &image, int side, bool ds,
                  const std::string &name, uint16_t date);

/* Delete one file from `side` (the equivalent of PIP /DELETE): looks up the
 * directory entry by name and flips it to an empty slot of the same length,
 * so a subsequent putFile() that fits can reuse the blocks.  Throws
 * std::runtime_error if the side is not initialised, the name is not
 * RAD50-encodable, or no permanent file with that name exists. */
void removeFile(std::vector<uint8_t> &image, int side, bool ds,
                const std::string &name);

} /* namespace ms0515::disk */

#endif /* MS0515_DISK_BUILD_HPP */
