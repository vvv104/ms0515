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
 * RAD50-encodable, the data does not fit, or the directory segment is full. */
void putFile(std::vector<uint8_t> &image, int side, bool ds,
             const std::string &name, std::span<const uint8_t> data);

} /* namespace ms0515::disk */

#endif /* MS0515_DISK_BUILD_HPP */
