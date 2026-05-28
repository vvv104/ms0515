/*
 * Layout.hpp — MS-0515 floppy LBN→byte mappings.
 *
 * A logical block number (LBN) lands at different byte offsets inside a
 * raw image depending on which OS driver wrote the disk.  These are the
 * physical layouts observed across surviving MS-0515 disks; the formulas
 * are specified in docs/hardware/filesystem.md and validated against the
 * vvv104 disk family.
 *
 * Pure functions, no I/O — the building block the directory parser and
 * file extractor read through.
 */

#ifndef MS0515_DISK_LAYOUT_HPP
#define MS0515_DISK_LAYOUT_HPP

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace ms0515::disk {

/* Geometry of an MS-0515 5.25" floppy. */
inline constexpr int kBlock          = 512;
inline constexpr int kSectorsPerTrack = 10;
inline constexpr int kTracks         = 80;
inline constexpr int kTrackSize      = kSectorsPerTrack * kBlock;  /* 5120   */
inline constexpr int kSideSize       = kTracks * kTrackSize;       /* 409600 */
inline constexpr int kDoubleSize     = 2 * kSideSize;              /* 819200 */
inline constexpr int kSsBlocks       = kTracks * kSectorsPerTrack; /* 800    */
inline constexpr int kDsBlocks       = 2 * kSsBlocks;              /* 1600   */

enum class Layout {
    SsCanonical,      /* 2:1 interleave, cyl-0-last — metadata of every OS,
                         and rodionov file data */
    SsOsaSkew,        /* canonical + +2-sectors-per-track skew — OSA/Omega/
                         Mihin file data */
    SsCyl0LastNoIl,   /* cyl-0-last, no interleave */
    SsCyl0FirstNoIl,  /* cyl-0-first, no interleave */
    SsLbnLinear,      /* block N at byte N*512 */
    DsCyl0LastNoIl,   /* double-sided spanning FS (~1600-block volume):
                         per-track side interleave, cyl-0-last, no interleave */
};

/* Map a logical block number to its byte offset within the image.
 * `lbn` is taken modulo the volume's block count, so callers may pass
 * raw block numbers without bounds-checking the wrap. */
[[nodiscard]] std::size_t lbnToByte(Layout layout, int lbn) noexcept;

/* True for layouts that address the double-sided spanning volume
 * (1600 blocks across both physical sides). */
[[nodiscard]] bool isDoubleSided(Layout layout) noexcept;

/* Number of logical blocks the layout's volume holds (800 or 1600). */
[[nodiscard]] int volumeBlocks(Layout layout) noexcept;

/* Lower-case tag matching the names used in filesystem.md / the Python
 * reference (e.g. "ss-canonical", "ds-cyl0last-noil"). */
[[nodiscard]] std::string_view layoutTag(Layout layout) noexcept;

} /* namespace ms0515::disk */

#endif /* MS0515_DISK_LAYOUT_HPP */
