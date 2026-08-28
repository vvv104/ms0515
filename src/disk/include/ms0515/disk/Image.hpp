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
#include <utility>
#include <vector>

namespace ms0515::disk {

/* One RT-11 volume = one side of an image.  The struct keeps the WHOLE raw
 * image (a double-sided dump is track-interleaved, so a side is not a
 * contiguous slice) and addresses into it via `side` + `ds`. */
struct Image {
    std::vector<uint8_t> data;          /* full raw image (409600 or 819200) */
    int                  side = 0;      /* 0 lower/boot, 1 upper (DS only)    */
    bool                 ds   = false;  /* true for an 819200 double-sided    */
    Vol                  vol = Vol::floppy; /* addressing (Layout.hpp Vol)   */
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
 * means the side holds no RT-11 directory (e.g. a copy-protection side).
 * DZ-floppy addressing only — use openVolume for a DV/MZ/linear volume. */
[[nodiscard]] std::optional<Image> openImage(std::vector<uint8_t> bytes,
                                             int side = 0);

/* Open a linear LD/HD container (any positive multiple of 512 bytes,
 * addressed as byte = LBN*512) and parse its RT-11 directory.  Use this for
 * paravirtual HD images; `side`/`ds` do not apply. */
[[nodiscard]] std::optional<Image> openLinearImage(std::vector<uint8_t> bytes);

/* Load `path` (whole file), select `side` (1 valid only for an 819200 dump),
 * parse its directory.  nullopt on read error or invalid side. */
[[nodiscard]] std::optional<Image> loadImage(const std::string &path,
                                             int side = 0);

/* loadImage for a linear LD/HD container (see openLinearImage). */
[[nodiscard]] std::optional<Image> loadLinearImage(const std::string &path);

/* Wrap a raw image as a volume of an explicit kind: a floppy side (side
 * 0/1 of an SS/DS dump), a DV: or MZ: whole-diskette volume (819200 bytes,
 * side must be 0), or a linear container.  nullopt when the bytes do not
 * fit the kind. */
[[nodiscard]] std::optional<Image> openVolume(std::vector<uint8_t> bytes,
                                              Vol vol, int side = 0);

/* loadImage for an explicit volume kind (see openVolume). */
[[nodiscard]] std::optional<Image> loadVolume(const std::string &path,
                                              Vol vol, int side = 0);

/* One detected volume: its addressing and, for a floppy side, which side. */
struct VolumeSpec {
    Vol vol  = Vol::floppy;
    int side = 0;
};

/* Content-based format detection: every (vol, side) combination whose
 * RT-11 directory validates in `bytes`, tried by what the byte count
 * allows — 409600: the single side; 819200: DZ side 0, DZ side 1, DV, MZ;
 * any other multiple of 512: a linear container.  Empty when nothing
 * parses (a blank or damaged image). */
[[nodiscard]] std::vector<VolumeSpec> detectVolumes(std::span<const uint8_t> bytes);

/* Split an 819200-byte track-interleaved double-sided image into its two
 * 409600-byte single-sided images (lower side 0 first, upper side 1).  Pure
 * byte de-interleave per the FDC geometry — no RT-11 parsing.  nullopt if the
 * input is not 819200 bytes. */
[[nodiscard]] std::optional<std::pair<std::vector<uint8_t>, std::vector<uint8_t>>>
splitDoubleSided(std::span<const uint8_t> ds);

/* Merge two 409600-byte single-sided images into one 819200-byte
 * track-interleaved double-sided image (inverse of splitDoubleSided).
 * nullopt unless both inputs are 409600 bytes. */
[[nodiscard]] std::optional<std::vector<uint8_t>>
mergeSides(std::span<const uint8_t> side0, std::span<const uint8_t> side1);

} /* namespace ms0515::disk */

#endif /* MS0515_DISK_IMAGE_HPP */
