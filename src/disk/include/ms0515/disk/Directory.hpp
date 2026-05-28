/*
 * Directory.hpp — RT-11 directory parsing over a chosen physical layout.
 *
 * An RT-11 volume keeps its files in a chain of directory *segments*
 * (2 blocks / 1024 bytes each).  Each segment has a 5-word header and a
 * run of fixed-size entries terminated by an end-of-segment marker.
 * Filenames are RAD50.  See docs/hardware/filesystem.md for the wire
 * format; this reads it through a ms0515::disk::Layout so it works on
 * any of the observed physical mappings (including DS-spanning).
 */

#ifndef MS0515_DISK_DIRECTORY_HPP
#define MS0515_DISK_DIRECTORY_HPP

#include "ms0515/disk/Layout.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace ms0515::disk {

/* RT-11 directory entry status bits. */
inline constexpr uint16_t kStatusTentative = 0000400;
inline constexpr uint16_t kStatusEmpty     = 0001000;
inline constexpr uint16_t kStatusPermanent = 0002000;
inline constexpr uint16_t kStatusEndOfSeg  = 0004000;
inline constexpr uint16_t kStatusReadOnly  = 0000040;
inline constexpr uint16_t kStatusProtected = 0100000;

struct DirEntry {
    uint16_t    status   = 0;
    std::string name;          /* "FOO.BAR", RAD50-decoded; empty for <empty> */
    int         startBlock = 0; /* LBN of the file's first data block */
    int         length     = 0; /* file length in blocks */
    uint16_t    date       = 0;

    [[nodiscard]] bool isPermanent() const noexcept
    { return (status & kStatusPermanent) != 0; }
    [[nodiscard]] bool isEmpty() const noexcept
    { return (status & kStatusEmpty) != 0; }
};

struct Directory {
    int dirStartLbn = 0;   /* LBN where segment 1 was found */
    int segsTotal   = 0;
    int highSeg     = 0;
    int extraBytes  = 0;
    int dataStart   = 0;   /* first data block of segment 1 */
    std::vector<DirEntry> entries;

    [[nodiscard]] std::vector<DirEntry> permanentFiles() const;
    [[nodiscard]] const DirEntry *find(std::string_view name) const;
};

/* Decode one 1024-byte segment buffer.  Returns nullopt if it does not
 * structurally validate as an RT-11 directory segment.  Exposed for
 * unit-testing against synthetic segments. */
[[nodiscard]] std::optional<Directory>
parseSegment(std::span<const uint8_t> segment);

/* Locate and parse the directory in `data` read through `layout`,
 * following the segment chain.  Searches the usual candidate start LBNs
 * (6, 13, 8, 10, 12).  Returns nullopt if none parse. */
[[nodiscard]] std::optional<Directory>
parseDirectory(std::span<const uint8_t> data, Layout layout);

/* Decode a 3-word RAD50 filename triple to "NAME.EXT". */
[[nodiscard]] std::string decodeRad50Name(uint16_t n1, uint16_t n2, uint16_t ext);

} /* namespace ms0515::disk */

#endif /* MS0515_DISK_DIRECTORY_HPP */
