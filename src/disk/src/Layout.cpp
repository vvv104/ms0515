/*
 * Layout.cpp — LBN→byte mappings.  Formulas: docs/hardware/filesystem.md.
 */

#include "ms0515/disk/Layout.hpp"

namespace ms0515::disk {

namespace {

/* 2:1 sector interleave table. */
constexpr int kInterleave[kSectorsPerTrack] = {0, 2, 4, 6, 8, 1, 3, 5, 7, 9};

std::size_t ssCanonical(int n) noexcept
{
    const int track  = (n / 10 + 1) % kTracks;     /* cyl-0-last */
    const int sector = kInterleave[n % 10];        /* 2:1 interleave */
    return static_cast<std::size_t>(track) * kTrackSize + sector * kBlock;
}

std::size_t ssCyl0LastNoIl(int n) noexcept
{
    const int track = (n / 10 + 1) % kTracks;
    return static_cast<std::size_t>(track) * kTrackSize + (n % 10) * kBlock;
}

std::size_t ssCyl0FirstNoIl(int n) noexcept
{
    return static_cast<std::size_t>(n / 10) * kTrackSize + (n % 10) * kBlock;
}

std::size_t ssLbnLinear(int n) noexcept
{
    return static_cast<std::size_t>(n) * kBlock;
}

std::size_t ssOsaSkew(int n) noexcept
{
    const int track  = (n / 10 + 1) % kTracks;
    const int sector = (kInterleave[n % 10] + 2 * track - 2) % 10;
    return static_cast<std::size_t>(track) * kTrackSize + sector * kBlock;
}

std::size_t dsCyl0LastNoIl(int n) noexcept
{
    const int cyl  = (n / 20 + 1) % kTracks;       /* cyl-0-last */
    const int head = (n / 10) % 2;                 /* side alternates per track */
    const int sec  = n % 10;                       /* no interleave */
    return static_cast<std::size_t>(cyl * 2 + head) * kTrackSize + sec * kBlock;
}

}  /* namespace */

std::size_t lbnToByte(Layout layout, int lbn) noexcept
{
    const int blocks = volumeBlocks(layout);
    int n = lbn % blocks;
    if (n < 0) n += blocks;

    switch (layout) {
    case Layout::SsCanonical:     return ssCanonical(n);
    case Layout::SsOsaSkew:       return ssOsaSkew(n);
    case Layout::SsCyl0LastNoIl:  return ssCyl0LastNoIl(n);
    case Layout::SsCyl0FirstNoIl: return ssCyl0FirstNoIl(n);
    case Layout::SsLbnLinear:     return ssLbnLinear(n);
    case Layout::DsCyl0LastNoIl:  return dsCyl0LastNoIl(n);
    }
    return 0;
}

bool isDoubleSided(Layout layout) noexcept
{
    return layout == Layout::DsCyl0LastNoIl;
}

int volumeBlocks(Layout layout) noexcept
{
    return isDoubleSided(layout) ? kDsBlocks : kSsBlocks;
}

std::string_view layoutTag(Layout layout) noexcept
{
    switch (layout) {
    case Layout::SsCanonical:     return "ss-canonical";
    case Layout::SsOsaSkew:       return "ss-osa-skew";
    case Layout::SsCyl0LastNoIl:  return "ss-cyl0last-noil";
    case Layout::SsCyl0FirstNoIl: return "ss-cyl0first-noil";
    case Layout::SsLbnLinear:     return "ss-lbn-linear";
    case Layout::DsCyl0LastNoIl:  return "ds-cyl0last-noil";
    }
    return "?";
}

} /* namespace ms0515::disk */
