/*
 * Build.cpp — RT-11 volume writer.  Inverse of Directory/Extract.
 */

#include "ms0515/disk/Build.hpp"
#include "ms0515/disk/Directory.hpp"

#include <cstring>
#include <stdexcept>
#include <string>

namespace ms0515::disk {

namespace {

constexpr char kRad50[] =
    " ABCDEFGHIJKLMNOPQRSTUVWXYZ$.?0123456789";

int rad50Index(char c)
{
    for (int i = 0; i < 40; ++i)
        if (kRad50[i] == c) return i;
    throw std::runtime_error(std::string("character not RAD50-encodable: ") + c);
}

uint16_t encodeRad50(const char *p)  /* exactly 3 chars */
{
    return static_cast<uint16_t>(rad50Index(p[0]) * 1600 +
                                 rad50Index(p[1]) * 40 +
                                 rad50Index(p[2]));
}

/* Split "NAME.EXT" into space-padded name[6] + ext[3], uppercased. */
void splitName(const std::string &filename, char name[6], char ext[3])
{
    std::memset(name, ' ', 6);
    std::memset(ext, ' ', 3);
    const auto dot = filename.find('.');
    const std::string base = filename.substr(0, dot);
    const std::string e = (dot == std::string::npos) ? "" : filename.substr(dot + 1);
    if (base.size() > 6 || e.size() > 3)
        throw std::runtime_error("filename exceeds 6.3: " + filename);
    auto up = [](char c) {
        return static_cast<char>((c >= 'a' && c <= 'z') ? c - 'a' + 'A' : c);
    };
    for (size_t i = 0; i < base.size(); ++i) name[i] = up(base[i]);
    for (size_t i = 0; i < e.size();    ++i) ext[i]  = up(e[i]);
}

void putw(uint8_t *p, uint16_t v)
{
    p[0] = static_cast<uint8_t>(v & 0xFF);
    p[1] = static_cast<uint8_t>(v >> 8);
}

}  /* namespace */

std::vector<uint8_t> buildVolume(Layout layout, const std::vector<BuildFile> &files)
{
    const int totalBlocks = volumeBlocks(layout);
    const std::size_t imageSize =
        isDoubleSided(layout) ? static_cast<std::size_t>(kDoubleSize)
                              : static_cast<std::size_t>(kSideSize);
    std::vector<uint8_t> image(imageSize, 0);

    auto writeBlock = [&](int lbn, const uint8_t *src, std::size_t n) {
        const std::size_t off = lbnToByte(layout, lbn);
        if (off + kBlock > image.size()) return;
        std::memcpy(image.data() + off, src, n > kBlock ? kBlock : n);
    };

    constexpr int kDirLbn    = 6;
    constexpr int kDataStart = 8;   /* one directory segment at LBN 6-7 */

    /* ── Home block (LBN 1): just enough for an OS to mount ── */
    {
        std::vector<uint8_t> home(kBlock, 0);
        putw(&home[0x1D2], 1);          /* cluster size */
        putw(&home[0x1D4], kDirLbn);    /* first directory segment block */
        writeBlock(1, home.data(), home.size());
    }

    /* ── File data + directory entries ── */
    std::vector<uint8_t> seg(1024, 0);
    putw(&seg[0], 1);                   /* segments total */
    putw(&seg[2], 0);                   /* next segment (none) */
    putw(&seg[4], 1);                   /* highest segment in use */
    putw(&seg[6], 0);                   /* extra bytes per entry */
    putw(&seg[8], kDataStart);          /* first data block */

    std::size_t p = 10;
    int cur = kDataStart;
    for (const auto &f : files) {
        const int nblk = static_cast<int>((f.data.size() + kBlock - 1) / kBlock);
        if (cur + nblk > totalBlocks)
            throw std::runtime_error("files do not fit in the volume");

        for (int i = 0; i < nblk; ++i) {
            const std::size_t srcOff = static_cast<std::size_t>(i) * kBlock;
            const std::size_t n = (srcOff < f.data.size())
                                ? std::min<std::size_t>(kBlock, f.data.size() - srcOff)
                                : 0;
            uint8_t blk[kBlock] = {0};
            if (n) std::memcpy(blk, f.data.data() + srcOff, n);
            writeBlock(cur + i, blk, kBlock);
        }

        char name[6], ext[3];
        splitName(f.name, name, ext);
        putw(&seg[p + 0],  kStatusPermanent);
        putw(&seg[p + 2],  encodeRad50(name));
        putw(&seg[p + 4],  encodeRad50(name + 3));
        putw(&seg[p + 6],  encodeRad50(ext));
        putw(&seg[p + 8],  static_cast<uint16_t>(nblk));
        putw(&seg[p + 10], 0);
        putw(&seg[p + 12], 0);
        p += 14;
        cur += nblk;
    }

    /* Trailing free-space marker: one EMPTY entry covering the rest,
     * flagged end-of-segment. */
    const int free = totalBlocks - cur;
    putw(&seg[p + 0], static_cast<uint16_t>(kStatusEmpty | kStatusEndOfSeg));
    putw(&seg[p + 2], 0);
    putw(&seg[p + 4], 0);
    putw(&seg[p + 6], 0);
    putw(&seg[p + 8], static_cast<uint16_t>(free < 0 ? 0 : free));

    writeBlock(kDirLbn,     seg.data(),         kBlock);
    writeBlock(kDirLbn + 1, seg.data() + kBlock, kBlock);

    return image;
}

} /* namespace ms0515::disk */
