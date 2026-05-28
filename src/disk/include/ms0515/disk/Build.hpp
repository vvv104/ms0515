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

/* Assemble `files` into an image written through `layout`.  Image size
 * is 409600 (SS) or 819200 (DS) per the layout.  Throws std::runtime_error
 * if the files do not fit or a name is not RAD50-encodable. */
[[nodiscard]] std::vector<uint8_t>
buildVolume(Layout layout, const std::vector<BuildFile> &files);

} /* namespace ms0515::disk */

#endif /* MS0515_DISK_BUILD_HPP */
