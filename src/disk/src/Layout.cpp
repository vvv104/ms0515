/*
 * Layout.cpp — LBN→byte geometry, replicating the emulator FDC + OS driver.
 * Truth: src/core/src/floppy.c (disk_offset) — see Layout.hpp.
 */

#include "ms0515/disk/Layout.hpp"

namespace ms0515::disk {

namespace {

/* 2:1 sector interleave table used by the OS driver. */
constexpr int kInterleave[kSectorsPerTrack] = {0, 2, 4, 6, 8, 1, 3, 5, 7, 9};

}  /* namespace */

std::size_t lbnToByte(int lbn, int side, bool ds) noexcept
{
    int n = lbn % kSsBlocks;
    if (n < 0) n += kSsBlocks;

    /* OS driver: LBN -> (track, sector). */
    const int track  = (n / kSectorsPerTrack + 1) % kTracks;          /* cyl-0-last */
    int sector = (kInterleave[n % kSectorsPerTrack] + 2 * track - 2) % kSectorsPerTrack;
    if (sector < 0) sector += kSectorsPerTrack;                        /* track 0 wrap */

    /* FDC: (track, sector, side) -> byte. */
    const int stride  = ds ? 2 * kTrackSize : kTrackSize;
    const int sideOff = ds ? side * kTrackSize : 0;
    return static_cast<std::size_t>(sideOff)
         + static_cast<std::size_t>(track) * stride
         + static_cast<std::size_t>(sector) * kBlock;
}

int lbnFromPhys(int track, int sector) noexcept
{
    /* Inverse interleave: position j with kInterleave[j] == s. */
    constexpr int kInverse[kSectorsPerTrack] = {0, 5, 1, 6, 2, 7, 3, 8, 4, 9};

    const int lt = (track + kTracks - 1) % kTracks;        /* undo cyl-0-last */
    int s = ((sector - 1) - (2 * track - 2)) % kSectorsPerTrack;
    if (s < 0) s += kSectorsPerTrack;                      /* undo skew       */
    return lt * kSectorsPerTrack + kInverse[s];
}

} /* namespace ms0515::disk */
