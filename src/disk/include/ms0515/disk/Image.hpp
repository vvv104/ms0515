/*
 * Image.hpp — load an MS-0515 capture and read its files.
 *
 * Wraps a raw disk image (the byte-for-byte sector dump).  There is no
 * layout to detect: the geometry follows from the file size — 409600 bytes
 * is single-sided, 819200 bytes is double-sided (two independent sides,
 * track-interleaved on disk).  All addressing goes through lbnToByte, which
 * replicates the emulator FDC + OS driver (see Layout.hpp), so reads match
 * the emulator byte-for-byte.
 */

#ifndef MS0515_DISK_IMAGE_HPP
#define MS0515_DISK_IMAGE_HPP

#include "ms0515/disk/Directory.hpp"
#include "ms0515/disk/Layout.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ms0515::disk {

/* One RT-11 volume = one side of an image.  The struct keeps the WHOLE raw
 * image (a double-sided dump is track-interleaved, so a side is not a
 * contiguous slice) and addresses into it via `side` + `ds`. */
struct Image {
    std::vector<uint8_t> data;          /* full raw image (409600 or 819200) */
    int                  side = 0;      /* 0 lower/boot, 1 upper (DS only)    */
    bool                 ds   = false;  /* true for an 819200 double-sided    */
    bool                 hasDirectory = false;
    Directory            directory;

    /* Read a file's raw bytes (length rounded up to whole blocks).  Empty
     * vector if the file is not present. */
    [[nodiscard]] std::vector<uint8_t> readFile(std::string_view name) const;

    /* Read one logical block. */
    [[nodiscard]] std::span<const uint8_t> block(int lbn) const;
};

/* Wrap a full raw image and parse the directory of `side`.  `bytes` is the
 * whole file; double-sided is inferred from its size.  hasDirectory==false
 * means the side holds no RT-11 directory (e.g. a copy-protection side). */
[[nodiscard]] std::optional<Image> openImage(std::vector<uint8_t> bytes,
                                             int side = 0);

/* Load `path` (whole file), select `side` (1 valid only for an 819200 dump),
 * parse its directory.  nullopt on read error or invalid side. */
[[nodiscard]] std::optional<Image> loadImage(const std::string &path,
                                             int side = 0);

} /* namespace ms0515::disk */

#endif /* MS0515_DISK_IMAGE_HPP */
