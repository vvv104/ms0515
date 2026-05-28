/*
 * Directory.cpp — RT-11 directory parser.  Format: docs/hardware/filesystem.md.
 */

#include "ms0515/disk/Directory.hpp"

#include <array>
#include <cctype>
#include <cstring>

namespace ms0515::disk {

namespace {

constexpr char kRad50[] =
    " ABCDEFGHIJKLMNOPQRSTUVWXYZ$.?0123456789";

/* Candidate LBNs for the first directory segment (home block usually
 * points at 6; some volumes — including the vvv104 DS family — sit the
 * directory at 13). */
constexpr std::array<int, 5> kDirCandidates = {6, 13, 8, 10, 12};

uint16_t rd16(std::span<const uint8_t> b, size_t off) noexcept
{
    return static_cast<uint16_t>(b[off]) |
           static_cast<uint16_t>(static_cast<uint16_t>(b[off + 1]) << 8);
}

std::string rad50Word(uint16_t w)
{
    if (w >= 64000) return "???";
    std::string s;
    s += kRad50[w / 1600];
    s += kRad50[(w / 40) % 40];
    s += kRad50[w % 40];
    return s;
}

void rstrip(std::string &s)
{
    while (!s.empty() && s.back() == ' ') s.pop_back();
}

}  /* namespace */

std::string decodeRad50Name(uint16_t n1, uint16_t n2, uint16_t ext)
{
    std::string name = rad50Word(n1) + rad50Word(n2);
    rstrip(name);
    std::string e = rad50Word(ext);
    rstrip(e);
    return e.empty() ? name : name + "." + e;
}

std::vector<DirEntry> Directory::permanentFiles() const
{
    std::vector<DirEntry> out;
    for (const auto &e : entries)
        if (e.isPermanent()) out.push_back(e);
    return out;
}

const DirEntry *Directory::find(std::string_view name) const
{
    for (const auto &e : entries)
        if (e.isPermanent() && e.name.size() == name.size()) {
            bool eq = true;
            for (size_t i = 0; i < name.size(); ++i)
                if (std::toupper(static_cast<unsigned char>(e.name[i])) !=
                    std::toupper(static_cast<unsigned char>(name[i]))) {
                    eq = false; break;
                }
            if (eq) return &e;
        }
    return nullptr;
}

std::optional<Directory> parseSegment(std::span<const uint8_t> seg)
{
    if (seg.size() < 1024) return std::nullopt;

    const uint16_t segTotal = rd16(seg, 0);
    /* word at offset 2 is the next-segment link, followed by the caller */
    const uint16_t segHigh  = rd16(seg, 4);
    const uint16_t extra    = rd16(seg, 6);
    const uint16_t dataBlk  = rd16(seg, 8);

    if (segTotal == 0 || segTotal > 31)        return std::nullopt;
    if (segHigh == 0 || segHigh > segTotal)    return std::nullopt;
    if ((extra & 1) || extra > 64)             return std::nullopt;
    if (dataBlk < 1 || dataBlk > kDsBlocks)    return std::nullopt;

    Directory dir;
    dir.segsTotal  = segTotal;
    dir.highSeg    = segHigh;
    dir.extraBytes = extra;
    dir.dataStart  = dataBlk;

    const size_t entrySize = 14 + extra;
    int curBlock = dataBlk;
    size_t p = 10;
    while (p + entrySize <= 1024) {
        const uint16_t status = rd16(seg, p);
        if (status == 0) return std::nullopt;  /* malformed */

        DirEntry e;
        e.status     = status;
        e.name       = decodeRad50Name(rd16(seg, p + 2), rd16(seg, p + 4),
                                       rd16(seg, p + 6));
        e.length     = rd16(seg, p + 8);
        e.date       = rd16(seg, p + 12);
        e.startBlock = curBlock;
        dir.entries.push_back(e);

        curBlock += e.length;
        p += entrySize;
        if (status & kStatusEndOfSeg) break;
    }

    bool anyPerm = false;
    for (const auto &e : dir.entries)
        if (e.isPermanent()) { anyPerm = true; break; }
    if (!anyPerm) return std::nullopt;

    return dir;
}

std::optional<Directory>
parseDirectory(std::span<const uint8_t> data, Layout layout)
{
    auto readSegmentBytes = [&](int lbn, std::array<uint8_t, 1024> &buf) {
        for (int half = 0; half < 2; ++half) {
            const std::size_t off = lbnToByte(layout, lbn + half);
            if (off + kBlock <= data.size())
                std::memcpy(buf.data() + half * kBlock, data.data() + off, kBlock);
            else
                std::memset(buf.data() + half * kBlock, 0, kBlock);
        }
    };

    for (int start : kDirCandidates) {
        std::array<uint8_t, 1024> buf{};
        readSegmentBytes(start, buf);
        auto first = parseSegment(buf);
        if (!first) continue;

        Directory dir = *first;
        dir.dirStartLbn = start;

        /* Follow the segment chain.  Segment k (1-based) sits at
         * start + (k-1)*2.  The link word lives at offset 2. */
        uint16_t next = rd16(buf, 2);
        int guard = 0;
        while (next != 0 && guard++ < 31) {
            const int segLbn = start + (next - 1) * 2;
            readSegmentBytes(segLbn, buf);
            auto more = parseSegment(buf);
            if (!more) break;
            for (const auto &e : more->entries)
                dir.entries.push_back(e);
            next = rd16(buf, 2);
        }
        return dir;
    }
    return std::nullopt;
}

} /* namespace ms0515::disk */
