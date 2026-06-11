/*
 * Build.cpp — create / init / put primitives for RT-11 volumes, mirroring the
 * OS's create + INIT + PIP.  Addresses through the FDC geometry (Layout.hpp)
 * so output is byte-compatible with what the emulator reads/writes.
 */

#include "ms0515/disk/Build.hpp"
#include "ms0515/disk/Directory.hpp"

#include "Internal.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace ms0515::disk {

using namespace internal;     /* encodeRad50, splitName, putw, putEntry, ... */

namespace {

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

}  /* namespace */

namespace {

/* Total logical block count of the volume: the actual file size for a linear
 * HD/LD container, or the fixed 800-block side for a floppy. */
std::size_t volumeBlocks(const std::vector<uint8_t> &image, bool linear)
{
    return linear ? image.size() / kBlock : static_cast<std::size_t>(kSsBlocks);
}

/* Validate that `image` has an acceptable size for the chosen geometry. */
void requireValidSize(const std::vector<uint8_t> &image, bool ds, bool linear)
{
    if (linear) {
        if (image.empty() || (image.size() % kBlock) != 0)
            throw std::runtime_error(
                "linear HD image size must be a positive multiple of 512");
    } else if (image.size() != (ds ? kDoubleSize : kSideSize)) {
        throw std::runtime_error("image size does not match the requested ss/ds");
    }
}

}  /* namespace */

std::vector<uint8_t> blankImage(bool ds)
{
    const std::size_t size = ds ? kDoubleSize : kSideSize;
    std::vector<uint8_t> img(size);
    for (std::size_t i = 0; i < size; ++i) img[i] = (i & 1) ? 0x6D : 0xB6;
    return img;
}

std::vector<uint8_t> blankLinear(int blocks)
{
    if (blocks <= 0)
        throw std::runtime_error("blankLinear: block count must be positive");
    return std::vector<uint8_t>(static_cast<std::size_t>(blocks) * kBlock, 0);
}

void initVolume(std::vector<uint8_t> &image, int side, bool ds,
                const InitOptions &opts, bool linear)
{
    requireValidSize(image, ds, linear);
    if (linear ? (side != 0) : (ds ? (side != 0 && side != 1) : (side != 0)))
        throw std::runtime_error("invalid side for this image");
    if (opts.segments < 1 || opts.segments > 31)
        throw std::runtime_error("directory segments must be 1..31");

    const int volBlocks = static_cast<int>(volumeBlocks(image, linear));

    auto writeBlock = [&](int lbn, const uint8_t *src) {
        std::memcpy(image.data() + lbnToByte(lbn, side, ds, linear), src, kBlock);
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
    if (dataStart >= volBlocks)
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
             static_cast<uint16_t>(volBlocks - dataStart));
    putw(&seg[10 + 14], kStatusEndOfSeg);

    writeBlock(kDirLbn,     seg.data());
    writeBlock(kDirLbn + 1, seg.data() + kBlock);
}

DateParts decodeDate(uint16_t encoded)
{
    if (encoded == 0) return {0, 0, 0};
    const int age = (encoded >> 14) & 0x3;
    const int month = (encoded >> 10) & 0x0F;
    const int day = (encoded >> 5) & 0x1F;
    const int yr = encoded & 0x1F;
    return {1972 + (age << 5) + yr, month, day};
}

uint16_t encodeDate(int year, int month, int day)
{
    if (year == 0 && month == 0 && day == 0) return 0;
    if (year < 1972 || year > 2099)
        throw std::runtime_error("date year out of range (1972..2099): " +
                                 std::to_string(year));
    if (month < 1 || month > 12)
        throw std::runtime_error("date month out of range (1..12): " +
                                 std::to_string(month));
    if (day < 1 || day > 31)
        throw std::runtime_error("date day out of range (1..31): " +
                                 std::to_string(day));
    const int n   = year - 1972;
    const int age = (n >> 5) & 0x3;
    const int yr  = n & 0x1F;
    return static_cast<uint16_t>((age << 14) | ((month & 0x0F) << 10)
                               | ((day & 0x1F) << 5) | (yr & 0x1F));
}

void putFile(std::vector<uint8_t> &image, int side, bool ds,
             const std::string &name, std::span<const uint8_t> data,
             const PutOptions &opts, bool linear)
{
    requireValidSize(image, ds, linear);

    auto off = [&](int lbn) { return lbnToByte(lbn, side, ds, linear); };
    const int volBlocks = static_cast<int>(volumeBlocks(image, linear));

    const int dirLbn = getw(image.data() + off(1) + 0x1D4);
    if (dirLbn < 1 || dirLbn > volBlocks)
        throw std::runtime_error("side is not initialised (run init first)");

    std::vector<uint8_t> seg(2 * kBlock);
    std::memcpy(seg.data(),          image.data() + off(dirLbn),     kBlock);
    std::memcpy(seg.data() + kBlock, image.data() + off(dirLbn + 1), kBlock);

    const uint16_t extra = getw(&seg[6]);
    if (extra & 1) throw std::runtime_error("unsupported directory (odd extra bytes)");
    const std::size_t entrySize = 14 + extra;

    const int nblk = static_cast<int>((data.size() + kBlock - 1) / kBlock);

    /* Walk every entry until EOS, picking the first empty slot that fits
     * (first-fit).  Greedy "use whatever's first" is what the original
     * append-only put did, but the moment removeFile starts leaving holes
     * we must scan past undersized empties to find a usable one, AND prefer
     * a freed mid-disk slot over the tail empty whenever both fit (so the
     * tool reuses the hole the OS left behind, the same as PIP). */
    int cur = getw(&seg[8]);
    std::size_t p = 10, emptyP = 0;
    int emptyStart = 0, emptyLen = -1, biggest = 0;
    while (p + entrySize <= seg.size()) {
        const uint16_t status = getw(&seg[p]);
        if (status == 0 || (status & kStatusEndOfSeg)) break;
        const uint16_t len = getw(&seg[p + 8]);
        if ((status & kStatusEmpty) && static_cast<int>(len) > biggest)
            biggest = len;
        if ((status & kStatusEmpty) && emptyLen < 0 && static_cast<int>(len) >= nblk) {
            emptyP = p; emptyStart = cur; emptyLen = len;
        }
        cur += len;
        p   += entrySize;
    }
    if (emptyLen < 0) {
        if (biggest == 0)
            throw std::runtime_error("directory has no free area for " + name);
        throw std::runtime_error("file " + name + " does not fit: needs " +
                                 std::to_string(nblk) + " blocks, biggest free is " +
                                 std::to_string(biggest));
    }

    /* Is the slot we're filling at the end of the directory (the canonical
     * shape PIP leaves after a freshly-INIT'd volume, and after any
     * append-only add) or in the middle (e.g. just freed by removeFile)?
     * The shape after the write must preserve any tail entries unchanged. */
    const std::size_t afterEmpty = emptyP + entrySize;
    const uint16_t nextStatus = (afterEmpty + 2 <= seg.size())
                              ? getw(&seg[afterEmpty]) : 0;
    const bool hasTail = nextStatus != 0 && !(nextStatus & kStatusEndOfSeg);

    if (hasTail && nblk < emptyLen) {
        /* Need to insert a residual empty entry between the new file and the
         * tail.  Shift the tail right by entrySize to make room.  Find the
         * tail's end first (EOS marker or null status), then enforce that
         * the shifted tail still fits in the segment. */
        std::size_t tailEnd = afterEmpty;
        while (tailEnd + entrySize <= seg.size()) {
            const uint16_t st = getw(&seg[tailEnd]);
            if (st == 0 || (st & kStatusEndOfSeg)) { tailEnd += 2; break; }
            tailEnd += entrySize;
        }
        const std::size_t tailSize = tailEnd - afterEmpty;
        if (afterEmpty + entrySize + tailSize > seg.size())
            throw std::runtime_error("directory segment is full (cannot add " + name + ")");
        std::memmove(seg.data() + afterEmpty + entrySize,
                     seg.data() + afterEmpty, tailSize);
    } else if (!hasTail && emptyP + 2 * entrySize + 2 > seg.size()) {
        throw std::runtime_error("directory segment is full (cannot add " + name + ")");
    }

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

    const uint16_t newStatus = static_cast<uint16_t>(
        kStatusPermanent | (opts.readOnly ? kStatusProtected : 0));
    putEntry(seg.data(), emptyP, newStatus, encodeRad50(nm),
             encodeRad50(nm + 3), encodeRad50(ex), static_cast<uint16_t>(nblk));
    putw(&seg[emptyP + 12], opts.date);
    if (nblk < emptyLen) {
        putEntry(seg.data(), afterEmpty, kStatusEmpty, 0x00D5, 0x6739, 0x26F4,
                 static_cast<uint16_t>(emptyLen - nblk));
    }
    if (!hasTail) {
        /* Append-only case: rewrite the EOS marker after our entries.  When
         * there IS a tail we leave it as-is — its own EOS is preserved. */
        const std::size_t eosAt = (nblk < emptyLen) ? (afterEmpty + entrySize)
                                                    : afterEmpty;
        putw(&seg[eosAt], kStatusEndOfSeg);
    }

    std::memcpy(image.data() + off(dirLbn),     seg.data(),          kBlock);
    std::memcpy(image.data() + off(dirLbn + 1), seg.data() + kBlock, kBlock);
}

namespace {

/* Load the directory segment, find the permanent entry that matches `name`,
 * invoke `mutator(seg, p, entrySize)` to change something in-place, and write
 * the segment back.  The mutator MUST NOT change the entry's RAD50 name
 * fields or the segment shape — only the status / length / job / date or
 * the file's data blocks (via the image, captured separately).  Throws if
 * the side isn't initialised or the file doesn't exist. */
template <typename Mutator>
void mutatePermanentEntry(std::vector<uint8_t> &image, int side, bool ds,
                          const std::string &name, Mutator mutator, bool linear)
{
    requireValidSize(image, ds, linear);

    const std::size_t homeOff = lbnToByte(1, side, ds, linear);
    const int volBlocks = static_cast<int>(volumeBlocks(image, linear));
    const int dirLbn = getw(image.data() + homeOff + 0x1D4);
    if (dirLbn < 1 || dirLbn > volBlocks)
        throw std::runtime_error("side is not initialised (run init first)");

    const std::size_t segOff0 = lbnToByte(dirLbn,     side, ds, linear);
    const std::size_t segOff1 = lbnToByte(dirLbn + 1, side, ds, linear);

    std::vector<uint8_t> seg(2 * kBlock);
    std::memcpy(seg.data(),          image.data() + segOff0, kBlock);
    std::memcpy(seg.data() + kBlock, image.data() + segOff1, kBlock);

    const uint16_t extra = getw(&seg[6]);
    if (extra & 1) throw std::runtime_error("unsupported directory (odd extra bytes)");
    const std::size_t entrySize = 14 + extra;

    char nm[6], ex[3];
    splitName(name, nm, ex);
    const uint16_t want1 = encodeRad50(nm);
    const uint16_t want2 = encodeRad50(nm + 3);
    const uint16_t wantE = encodeRad50(ex);

    std::size_t p = 10;
    while (p + entrySize <= seg.size()) {
        const uint16_t status = getw(&seg[p]);
        if (status == 0 || (status & kStatusEndOfSeg)) break;
        if ((status & kStatusPermanent) &&
            getw(&seg[p + 2]) == want1 &&
            getw(&seg[p + 4]) == want2 &&
            getw(&seg[p + 6]) == wantE)
        {
            mutator(seg.data(), p, entrySize);
            std::memcpy(image.data() + segOff0, seg.data(),          kBlock);
            std::memcpy(image.data() + segOff1, seg.data() + kBlock, kBlock);
            return;
        }
        p += entrySize;
    }
    throw std::runtime_error("no permanent file named " + name + " on this side");
}

}  /* anonymous namespace */

void removeFile(std::vector<uint8_t> &image, int side, bool ds,
                const std::string &name, bool linear)
{
    mutatePermanentEntry(image, side, ds, name,
        [](uint8_t *seg, std::size_t p, std::size_t /*entrySize*/) {
            /* Flip to an empty slot, preserving the length so the freed
             * blocks remain accounted for.  Match the sentinel name PIP
             * leaves on empty entries so dir tools that scan for them
             * still see a consistent shape. */
            const uint16_t len = getw(&seg[p + 8]);
            putEntry(seg, p, kStatusEmpty, 0x00D5, 0x6739, 0x26F4, len);
        }, linear);
}

void setProtected(std::vector<uint8_t> &image, int side, bool ds,
                  const std::string &name, bool on, bool linear)
{
    mutatePermanentEntry(image, side, ds, name,
        [on](uint8_t *seg, std::size_t p, std::size_t /*entrySize*/) {
            uint16_t status = getw(&seg[p]);
            if (on) status |=  kStatusProtected;
            else    status &= ~kStatusProtected;
            putw(&seg[p], status);
        }, linear);
}

void setEntryDate(std::vector<uint8_t> &image, int side, bool ds,
                  const std::string &name, uint16_t date, bool linear)
{
    mutatePermanentEntry(image, side, ds, name,
        [date](uint8_t *seg, std::size_t p, std::size_t /*entrySize*/) {
            putw(&seg[p + 12], date);
        }, linear);
}

void squeeze(std::vector<uint8_t> &image, int side, bool ds, bool linear)
{
    requireValidSize(image, ds, linear);

    auto off = [&](int lbn) { return lbnToByte(lbn, side, ds, linear); };
    const int volBlocks = static_cast<int>(volumeBlocks(image, linear));

    const int dirLbn = getw(image.data() + off(1) + 0x1D4);
    if (dirLbn < 1 || dirLbn > volBlocks)
        throw std::runtime_error("side is not initialised (run init first)");

    std::vector<uint8_t> seg(2 * kBlock);
    std::memcpy(seg.data(),          image.data() + off(dirLbn),     kBlock);
    std::memcpy(seg.data() + kBlock, image.data() + off(dirLbn + 1), kBlock);

    const uint16_t segTotal     = getw(&seg[0]);
    const uint16_t highestInUse = getw(&seg[4]);
    if (segTotal > 1 && highestInUse > 1)
        throw std::runtime_error("squeeze: multi-segment directories not supported");

    const uint16_t extra = getw(&seg[6]);
    if (extra & 1) throw std::runtime_error("unsupported directory (odd extra bytes)");
    const std::size_t entrySize = 14 + extra;
    const int dataStart = getw(&seg[8]);

    /* Snapshot every permanent entry's mutable fields and old start block. */
    struct Perm {
        uint16_t status, n1, n2, ex, length, job, date;
        int      oldStart;
    };
    std::vector<Perm> perms;
    int cur = dataStart;
    std::size_t p = 10;
    while (p + entrySize <= seg.size()) {
        const uint16_t status = getw(&seg[p]);
        if (status == 0 || (status & kStatusEndOfSeg)) break;
        const uint16_t len = getw(&seg[p + 8]);
        if (status & kStatusPermanent)
            perms.push_back({status, getw(&seg[p + 2]), getw(&seg[p + 4]),
                             getw(&seg[p + 6]), len, getw(&seg[p + 10]),
                             getw(&seg[p + 12]), cur});
        cur += len;
        p   += entrySize;
    }
    const int totalSpan = cur - dataStart;        /* blocks under directory control */

    /* Move file data blocks LEFT in directory order so they end up contiguous
     * starting at dataStart.  Walking left-to-right is safe because every
     * file's new start is no greater than its old start AND the next file's
     * old start is strictly greater than this file's new end (no overlap). */
    int newCursor = dataStart;
    for (auto &f : perms) {
        if (f.oldStart != newCursor) {
            const std::size_t srcOff = off(f.oldStart);
            const std::size_t dstOff = off(newCursor);
            const std::size_t n      = static_cast<std::size_t>(f.length) * kBlock;
            std::memmove(image.data() + dstOff, image.data() + srcOff, n);
        }
        f.oldStart = newCursor;
        newCursor += f.length;
    }

    /* Rebuild the segment buffer in place: header preserved, permanents in
     * order with metadata intact, single trailing empty, EOS marker. */
    std::fill(seg.begin() + 10, seg.end(), uint8_t{0});
    p = 10;
    for (const auto &f : perms) {
        putEntry(seg.data(), p, f.status, f.n1, f.n2, f.ex, f.length);
        putw(&seg[p + 10], f.job);
        putw(&seg[p + 12], f.date);
        p += entrySize;
    }
    if (p + entrySize + 2 > seg.size())
        throw std::runtime_error("squeeze: directory does not fit the compacted layout");
    const int freed = totalSpan - (newCursor - dataStart);
    putEntry(seg.data(), p, kStatusEmpty, 0x00D5, 0x6739, 0x26F4,
             static_cast<uint16_t>(freed));
    putw(&seg[p + entrySize], kStatusEndOfSeg);

    std::memcpy(image.data() + off(dirLbn),     seg.data(),          kBlock);
    std::memcpy(image.data() + off(dirLbn + 1), seg.data() + kBlock, kBlock);
}

} /* namespace ms0515::disk */
