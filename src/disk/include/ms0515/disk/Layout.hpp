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
 * So a DZ: disk is fully described by its size (single- vs double-sided)
 * and, for a double-sided image, which side.  The DV: and MZ: handlers
 * instead treat the whole double-sided diskette as one 1600-block volume
 * (their own translate code: track = LBN/10, +2 for DV with wrap at 160,
 * natural sides, no interleave) — see the Vol enum below.
 */

#ifndef MS0515_DISK_LAYOUT_HPP
#define MS0515_DISK_LAYOUT_HPP

#include <cstddef>
#include <cstdint>

namespace ms0515::disk {

/* Geometry of an MS-0515 5.25" floppy (mirrors core/floppy.h). */
inline constexpr int         kBlock           = 512;
inline constexpr int         kSectorsPerTrack = 10;
inline constexpr int         kTracks          = 80;
inline constexpr int         kTrackSize       = kSectorsPerTrack * kBlock;  /* 5120   */
inline constexpr std::size_t kSideSize        = static_cast<std::size_t>(kTracks) * kTrackSize; /* 409600 */
inline constexpr std::size_t kDoubleSize      = 2 * kSideSize;             /* 819200 */
inline constexpr int         kSsBlocks        = kTracks * kSectorsPerTrack;/* 800    */
inline constexpr int         kDsBlocks        = 2 * kSsBlocks;             /* 1600   */
inline constexpr int         kDvRotate        = 2 * kSectorsPerTrack;      /* 20     */

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

/* Which addressing a volume uses — the OS handler that made it:
 *   floppy — one side of a diskette through the DZ: driver's 2:1
 *            interleave + per-track skew (`side`/`ds` pick the slice);
 *   linear — LD/HD container: byte = LBN*512, any multiple of 512 blocks;
 *   mz     — the whole double-sided diskette as one 1600-block volume,
 *            the MZ: way (track = LBN/10, natural sides, no interleave),
 *            which over a track-interleaved dump is byte-linear;
 *   dv     — the same through the DV: driver (track = LBN/10 + 2, wrap
 *            at 160): cylinder 0 holds LBN 1580..1599, i.e. linear
 *            rotated 20 blocks — the OSA-canonical rotation.
 * Truth for dv/mz: the handlers' own translate code (DV.SYS / MZ.SYS
 * disassembled), verified against the OS in the emulator and against
 * raw hardware dumps (vvv104 disk4/disk5). */
enum class Vol : uint8_t { floppy, linear, dv, mz };

/* Geometry-aware offset for any volume kind.  `side`/`ds` matter only for
 * Vol::floppy; dv/mz wrap `lbn` modulo the 1600-block diskette. */
[[nodiscard]] inline std::size_t lbnToByte(int lbn, int side, bool ds,
                                           Vol vol) noexcept
{
    switch (vol) {
    case Vol::linear: return lbnToByteLinear(lbn);
    case Vol::mz:
    case Vol::dv: {
        int n = (lbn + (vol == Vol::dv ? kDvRotate : 0)) % kDsBlocks;
        if (n < 0) n += kDsBlocks;
        return lbnToByteLinear(n);
    }
    default:          return lbnToByte(lbn, side, ds);
    }
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
