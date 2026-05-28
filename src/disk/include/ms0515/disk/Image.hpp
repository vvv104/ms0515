/*
 * Image.hpp — load an MS-0515 capture and auto-detect its physical layout.
 *
 * Wraps a raw disk image (the byte-for-byte sector dump) together with
 * the Layout its directory parses under.  Detection is structural: try
 * the candidate layouts appropriate to the image size and keep the one
 * whose RT-11 directory validates with the most permanent files.  This
 * is what lets the same code read SS canonical disks and the DS-spanning
 * vvv104 family without a hand-given layout.
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

/* One single-sided RT-11 volume.  An MS-0515 double-sided dump (819200
 * bytes) is two independent SS volumes — side 0 and side 1 — each its
 * own device (the sides are NOT one spanning filesystem; some disks put
 * a copy-protection magic on side 1 with no directory at all).  An
 * Image always holds exactly one side's 409600 bytes; pick the side at
 * load time. */
struct Image {
    std::vector<uint8_t> data;        /* this side's bytes (<=409600) */
    int                  side = 0;    /* 0 or 1 (1 only for DS dumps) */
    Layout               layout = Layout::SsCanonical;  /* detected */
    bool                 hasDirectory = false;
    Directory            directory;

    /* Read a file's raw bytes (length rounded up to whole blocks) through
     * the detected layout.  Empty vector if the file is not present. */
    [[nodiscard]] std::vector<uint8_t> readFile(std::string_view name) const;

    /* Read one logical block via the detected layout. */
    [[nodiscard]] std::span<const uint8_t> block(int lbn) const;
};

/* Pick the layout whose directory parses with the most permanent files.
 * Only single-sided mappings are tried — MS-0515 sides are separate
 * volumes, never a spanning filesystem.  nullopt if none validate. */
[[nodiscard]] std::optional<Layout> detectLayout(std::span<const uint8_t> data);

/* Detect + parse a single side already isolated in `bytes`. */
[[nodiscard]] std::optional<Image> openImage(std::vector<uint8_t> bytes,
                                             int side = 0);

/* Load `path`, select `side` (0/1; 1 valid only for an 819200 dump),
 * detect layout, parse directory.  nullopt on read error or bad side.
 * hasDirectory==false means the side was read but holds no RT-11
 * directory (e.g. a copy-protection side). */
[[nodiscard]] std::optional<Image> loadImage(const std::string &path,
                                             int side = 0);

} /* namespace ms0515::disk */

#endif /* MS0515_DISK_IMAGE_HPP */
