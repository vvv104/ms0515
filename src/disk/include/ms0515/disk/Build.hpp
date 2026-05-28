/*
 * Build.hpp — assemble an RT-11 volume image from a set of files.
 *
 * Writes a minimal but valid RT-11 data volume (home block + one directory
 * segment + contiguous file data) addressed through the FDC geometry, so the
 * result is byte-compatible with what the emulator reads.  The inverse of
 * Image/Extract.
 */

#ifndef MS0515_DISK_BUILD_HPP
#define MS0515_DISK_BUILD_HPP

#include "ms0515/disk/Layout.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace ms0515::disk {

struct BuildFile {
    std::string          name;   /* "NAME.EXT", <=6+3, RAD50-encodable */
    std::vector<uint8_t> data;   /* raw bytes; padded to a block boundary */
};

/* Assemble `files` into a single-sided 409600-byte volume.  Throws
 * std::runtime_error if the files do not fit or a name is not RAD50. */
[[nodiscard]] std::vector<uint8_t>
buildVolume(const std::vector<BuildFile> &files);

/* Assemble an 819200-byte double-sided dump: two independent volumes
 * (side 0 then side 1), track-interleaved on disk per the FDC geometry.
 * Either side may be empty (e.g. a bare copy-protection side). */
[[nodiscard]] std::vector<uint8_t>
buildDoubleSided(const std::vector<BuildFile> &side0,
                 const std::vector<BuildFile> &side1);

} /* namespace ms0515::disk */

#endif /* MS0515_DISK_BUILD_HPP */
