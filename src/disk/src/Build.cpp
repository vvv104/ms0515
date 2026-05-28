/*
 * Build.cpp — create / init / put primitives for RT-11 volumes, mirroring the
 * OS's create + INIT + PIP.  Addresses through the FDC geometry (Layout.hpp)
 * so output is byte-compatible with what the emulator reads/writes.
 */

#include "ms0515/disk/Build.hpp"
#include "ms0515/disk/Directory.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

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

uint16_t getw(const uint8_t *p)
{
    return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}

void putEntry(uint8_t *seg, std::size_t p, uint16_t status, uint16_t n1,
              uint16_t n2, uint16_t ext, uint16_t length)
{
    putw(&seg[p + 0],  status);
    putw(&seg[p + 2],  n1);
    putw(&seg[p + 4],  n2);
    putw(&seg[p + 6],  ext);
    putw(&seg[p + 8],  length);
    putw(&seg[p + 10], 0);          /* job/channel */
    putw(&seg[p + 12], 0);          /* date        */
}

/* The boot block (LBN 0) RT-11 INIT writes on a non-system volume: a tiny
 * PDP-11 stub that prints "?BOOT-U-No boot on volume", then zeros to 512.
 * Captured from the emulator; identical across OSA/Omega/Mihin. */
constexpr uint8_t kBootStub[] = {
    0xA0,0x00,0x05,0x00,0x04,0x01,0x00,0x00,0x00,0x00,0x10,0x43,0x10,0x9C,0x00,0x01,
    0x37,0x08,0x24,0x00,0x0D,0x00,0x00,0x00,0x00,0x0A,0x3F,0x42,0x4F,0x4F,0x54,0x2D,
    0x55,0x2D,0x4E,0x6F,0x20,0x62,0x6F,0x6F,0x74,0x20,0x6F,0x6E,0x20,0x76,0x6F,0x6C,
    0x75,0x6D,0x65,0x0D,0x0A,0x0A,0x80,0x00,0xDF,0x8B,0x74,0xFF,0xFD,0x80,0x1F,0x94,
    0x76,0xFF,0xFA,0x80,0xFF,0x01,
};

constexpr int kDirLbn = 6;   /* first directory segment block */

/* Home block, matching what INIT writes on a B6 6D blank: a few leading
 * bytes, the FFFF marker at 0x1C0, the directory pointer, version, and the
 * volume/owner/system identity strings; bytes INIT does not touch keep the
 * blank 0xB6 0x6D pattern. */
std::vector<uint8_t> makeHomeBlock(const std::string &volumeId,
                                   const std::string &owner)
{
    if (volumeId.size() > 12) throw std::runtime_error("volume id exceeds 12 characters");
    if (owner.size()    > 12) throw std::runtime_error("owner name exceeds 12 characters");

    std::vector<uint8_t> home(kBlock);
    for (int i = 0; i < kBlock; ++i) home[i] = (i & 1) ? 0x6D : 0xB6;

    const uint8_t lead[6] = {0x00, 0x00, 0x00, 0xF0, 0xFF, 0x0F};
    std::memcpy(&home[0], lead, sizeof lead);

    std::memset(&home[0x84], 0, kBlock - 0x84);   /* INIT zeroes 0x84..0x1FF */
    home[0x1C0] = 0xFF; home[0x1C1] = 0xFF;
    putw(&home[0x1D2], 1);            /* pack cluster size            */
    putw(&home[0x1D4], kDirLbn);      /* first directory segment      */
    putw(&home[0x1D6], 0x8E53);       /* system version (RAD50 "V05") */
    std::memset(&home[0x1D8], ' ', 12);                       /* volume id  */
    std::memcpy(&home[0x1D8], volumeId.data(), volumeId.size());
    std::memset(&home[0x1E4], ' ', 12);                       /* owner name */
    std::memcpy(&home[0x1E4], owner.data(), owner.size());
    std::memcpy(&home[0x1F0], "DECRT11A    ", 12);            /* system id  */
    return home;
}

}  /* namespace */

std::vector<uint8_t> blankImage(bool ds)
{
    const std::size_t size = ds ? kDoubleSize : kSideSize;
    std::vector<uint8_t> img(size);
    for (std::size_t i = 0; i < size; ++i) img[i] = (i & 1) ? 0x6D : 0xB6;
    return img;
}

void initVolume(std::vector<uint8_t> &image, int side, bool ds,
                const InitOptions &opts)
{
    const std::size_t want = ds ? kDoubleSize : kSideSize;
    if (image.size() != want)
        throw std::runtime_error("image size does not match the requested ss/ds");
    if (ds ? (side != 0 && side != 1) : (side != 0))
        throw std::runtime_error("invalid side for this image");
    if (opts.segments < 1 || opts.segments > 31)
        throw std::runtime_error("directory segments must be 1..31");

    auto writeBlock = [&](int lbn, const uint8_t *src) {
        std::memcpy(image.data() + lbnToByte(lbn, side, ds), src, kBlock);
    };

    {   /* boot block (LBN 0): "no boot" stub + zeros */
        std::vector<uint8_t> boot(kBlock, 0);
        std::memcpy(boot.data(), kBootStub, sizeof kBootStub);
        writeBlock(0, boot.data());
    }
    {   /* home block (LBN 1) */
        auto home = makeHomeBlock(opts.volumeId, opts.owner);
        writeBlock(1, home.data());
    }

    /* Directory segment 1.  Reserve `segments` two-block segments at LBN
     * 6..; data starts after them.  Segments 2.. stay blank until used. */
    const int dataStart = kDirLbn + opts.segments * 2;
    if (dataStart >= kSsBlocks)
        throw std::runtime_error("too many directory segments for the volume");

    std::vector<uint8_t> seg(2 * kBlock);
    for (std::size_t i = 0; i < seg.size(); ++i) seg[i] = (i & 1) ? 0x6D : 0xB6;
    putw(&seg[0], static_cast<uint16_t>(opts.segments));  /* segments total  */
    putw(&seg[2], 0);                                     /* next segment    */
    putw(&seg[4], 1);                                     /* highest in use  */
    putw(&seg[6], 0);                                     /* extra bytes     */
    putw(&seg[8], static_cast<uint16_t>(dataStart));      /* first data block*/

    /* Free-space entry named " EMPTY.FIL" (as INIT writes it) + EOS marker. */
    putEntry(seg.data(), 10, kStatusEmpty, 0x00D5, 0x6739, 0x26F4,
             static_cast<uint16_t>(kSsBlocks - dataStart));
    putw(&seg[10 + 14], kStatusEndOfSeg);

    writeBlock(kDirLbn,     seg.data());
    writeBlock(kDirLbn + 1, seg.data() + kBlock);
}

void putFile(std::vector<uint8_t> &image, int side, bool ds,
             const std::string &name, std::span<const uint8_t> data)
{
    const std::size_t want = ds ? kDoubleSize : kSideSize;
    if (image.size() != want)
        throw std::runtime_error("image size does not match the requested ss/ds");

    auto off = [&](int lbn) { return lbnToByte(lbn, side, ds); };

    const int dirLbn = getw(image.data() + off(1) + 0x1D4);
    if (dirLbn < 1 || dirLbn > kSsBlocks)
        throw std::runtime_error("side is not initialised (run init first)");

    std::vector<uint8_t> seg(2 * kBlock);
    std::memcpy(seg.data(),          image.data() + off(dirLbn),     kBlock);
    std::memcpy(seg.data() + kBlock, image.data() + off(dirLbn + 1), kBlock);

    const uint16_t extra = getw(&seg[6]);
    if (extra & 1) throw std::runtime_error("unsupported directory (odd extra bytes)");
    const std::size_t entrySize = 14 + extra;

    /* Walk to the free-space (empty) entry, tracking its start block. */
    int cur = getw(&seg[8]);
    std::size_t p = 10, emptyP = 0;
    int emptyStart = 0, emptyLen = -1;
    while (p + entrySize <= seg.size()) {
        const uint16_t status = getw(&seg[p]);
        if (status == 0) break;
        const uint16_t len = getw(&seg[p + 8]);
        if (status & kStatusEmpty) { emptyP = p; emptyStart = cur; emptyLen = len; break; }
        if (status & kStatusEndOfSeg) break;
        cur += len;
        p   += entrySize;
    }
    if (emptyLen < 0)
        throw std::runtime_error("directory has no free area for " + name);

    const int nblk = static_cast<int>((data.size() + kBlock - 1) / kBlock);
    if (nblk > emptyLen)
        throw std::runtime_error("file " + name + " does not fit: needs " +
                                 std::to_string(nblk) + " blocks, " +
                                 std::to_string(emptyLen) + " free");
    if (emptyP + 2 * entrySize + 2 > seg.size())
        throw std::runtime_error("directory segment is full (cannot add " + name + ")");

    char nm[6], ex[3];
    splitName(name, nm, ex);   /* validates 6.3 + RAD50 before we touch data */

    for (int i = 0; i < nblk; ++i) {
        const std::size_t o = off(emptyStart + i);
        const std::size_t srcOff = static_cast<std::size_t>(i) * kBlock;
        const std::size_t n = (srcOff < data.size())
                            ? std::min<std::size_t>(kBlock, data.size() - srcOff) : 0;
        std::memset(image.data() + o, 0, kBlock);
        if (n) std::memcpy(image.data() + o, data.data() + srcOff, n);
    }

    /* The file takes the empty entry's slot; a smaller empty entry and the
     * EOS marker follow — the same shape PIP leaves. */
    putEntry(seg.data(), emptyP, kStatusPermanent, encodeRad50(nm),
             encodeRad50(nm + 3), encodeRad50(ex), static_cast<uint16_t>(nblk));
    putEntry(seg.data(), emptyP + entrySize, kStatusEmpty, 0x00D5, 0x6739, 0x26F4,
             static_cast<uint16_t>(emptyLen - nblk));
    putw(&seg[emptyP + 2 * entrySize], kStatusEndOfSeg);

    std::memcpy(image.data() + off(dirLbn),     seg.data(),          kBlock);
    std::memcpy(image.data() + off(dirLbn + 1), seg.data() + kBlock, kBlock);
}

} /* namespace ms0515::disk */
