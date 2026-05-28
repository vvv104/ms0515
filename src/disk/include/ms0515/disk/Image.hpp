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

struct Image {
    std::vector<uint8_t> data;
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
 * Candidate set depends on the image size (SS vs DS).  nullopt if none
 * yield a valid directory. */
[[nodiscard]] std::optional<Layout> detectLayout(std::span<const uint8_t> data);

/* Load `path`, detect layout, parse directory.  nullopt on read error.
 * A loaded Image with hasDirectory==false means the bytes were read but
 * no layout produced a valid directory. */
[[nodiscard]] std::optional<Image> loadImage(const std::string &path);

} /* namespace ms0515::disk */

#endif /* MS0515_DISK_IMAGE_HPP */
