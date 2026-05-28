/*
 * Build.hpp — assemble an RT-11 volume image from a set of files.
 *
 * Writes a minimal but valid RT-11 data volume (home block + one
 * directory segment + contiguous file data) through a chosen physical
 * Layout, so the resulting image is mountable by an OS that uses that
 * layout.  The inverse of Image/Extract.
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

/* Assemble `files` into a single-sided 409600-byte volume written
 * through `layout` (must be an SS layout).  Throws std::runtime_error
 * if the files do not fit or a name is not RAD50-encodable. */
[[nodiscard]] std::vector<uint8_t>
buildVolume(Layout layout, const std::vector<BuildFile> &files);

/* Assemble an MS-0515 double-sided 819200-byte dump: two independent SS
 * volumes (side 0 then side 1) back to back, each built via `layout`.
 * Either side may be empty (e.g. a bare copy-protection side). */
[[nodiscard]] std::vector<uint8_t>
buildDoubleSided(Layout layout,
                 const std::vector<BuildFile> &side0,
                 const std::vector<BuildFile> &side1);

} /* namespace ms0515::disk */

#endif /* MS0515_DISK_BUILD_HPP */
