/*
 * Layout.hpp — MS-0515 floppy LBN→byte geometry.
 *
 * Single source of truth: the emulator FDC (src/core/src/floppy.c).  The FDC
 * itself does no interleave/skew — it is pure
 *     byte = side*kTrackSize + track*track_stride + (sector-1)*kBlock,
 * with track_stride = kTrackSize for a 400 KB single-sided image and
 * 2*kTrackSize for an 800 KB double-sided image (track-interleaved: each
 * cylinder stores side 0 then side 1 back to back).  The interleave + skew
 * live in the OS driver, which is the same for every disk:
 *     track  = (LBN/10 + 1) % 80        (cyl-0-last)
 *     sector = (IL[LBN%10] + 2*track-2) % 10   (2:1 interleave + per-track skew)
 *
 * So a disk is fully described by its size (single- vs double-sided) and,
 * for a double-sided image, which side — there is no per-OS "layout" choice.
 */

#ifndef MS0515_DISK_LAYOUT_HPP
#define MS0515_DISK_LAYOUT_HPP

#include <cstddef>

namespace ms0515::disk {

/* Geometry of an MS-0515 5.25" floppy (mirrors core/floppy.h). */
inline constexpr int         kBlock           = 512;
inline constexpr int         kSectorsPerTrack = 10;
inline constexpr int         kTracks          = 80;
inline constexpr int         kTrackSize       = kSectorsPerTrack * kBlock;  /* 5120   */
inline constexpr std::size_t kSideSize        = static_cast<std::size_t>(kTracks) * kTrackSize; /* 409600 */
inline constexpr std::size_t kDoubleSize      = 2 * kSideSize;             /* 819200 */
inline constexpr int         kSsBlocks        = kTracks * kSectorsPerTrack;/* 800    */

/* Map a logical block number to its byte offset within a raw image, exactly
 * as the emulator (FDC + OS driver) would address it.  `side` selects a side
 * of a double-sided (800 KB) image (0 = lower/boot, 1 = upper); it is ignored
 * when `ds` is false.  `lbn` is taken modulo the 800-block side, so callers
 * may pass raw block numbers without bounds-checking the wrap. */
[[nodiscard]] std::size_t lbnToByte(int lbn, int side, bool ds) noexcept;

/* Linear (LD/HD container) addressing: block N is simply at byte N*512, with
 * no interleave/skew and no side concept.  This is how the paravirtual HD:
 * device and RT-11 logical-disk containers are laid out, as opposed to the
 * skewed floppy geometry of lbnToByte().  `linear` selectors thread through
 * Directory/Image/Build so the same RT-11 parsing serves both. */
[[nodiscard]] inline std::size_t lbnToByteLinear(int lbn) noexcept
{
    return static_cast<std::size_t>(lbn) * kBlock;
}

/* Geometry-aware offset: linear when `linear` is set, else the floppy skew. */
[[nodiscard]] inline std::size_t lbnToByte(int lbn, int side, bool ds,
                                           bool linear) noexcept
{
    return linear ? lbnToByteLinear(lbn) : lbnToByte(lbn, side, ds);
}

/* Inverse of the OS-driver mapping for a single-sided diskette: which LBN
 * lives at physical (track, sector)?  `sector` is the FDC's 1-based sector
 * register (1..10), `track` 0..79.  This is what a folder-backed floppy
 * uses to answer raw FDC sector requests with RT-11 logical blocks. */
[[nodiscard]] int lbnFromPhys(int track, int sector) noexcept;

/* True when a file of this byte size is a double-sided (800 KB) dump. */
[[nodiscard]] inline bool isDoubleSidedSize(std::size_t fileSize) noexcept
{
    return fileSize == kDoubleSize;
}

} /* namespace ms0515::disk */

#endif /* MS0515_DISK_LAYOUT_HPP */
