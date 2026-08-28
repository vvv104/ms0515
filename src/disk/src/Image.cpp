/*
 * Image.cpp — capture loading + file reads, addressed via the FDC geometry.
 */

#include "ms0515/disk/Image.hpp"

#include <algorithm>
#include <fstream>
#include <utility>

namespace ms0515::disk {

std::span<const uint8_t> Image::block(int lbn) const
{
    const std::size_t off = lbnToByte(lbn, side, ds, vol);
    if (off + kBlock > data.size()) return {};
    return std::span<const uint8_t>(data.data() + off, kBlock);
}

std::vector<uint8_t> Image::readFile(std::string_view name) const
{
    if (!hasDirectory) return {};
    const DirEntry *e = directory.find(name);
    if (!e) return {};
    std::vector<uint8_t> out;
    out.reserve(static_cast<std::size_t>(e->length) * kBlock);
    for (int i = 0; i < e->length; ++i) {
        auto b = block(e->startBlock + i);
        if (b.size() == kBlock) out.insert(out.end(), b.begin(), b.end());
        else                    out.insert(out.end(), kBlock, 0);
    }
    return out;
}

std::optional<Image> openImage(std::vector<uint8_t> bytes, int side)
{
    if (bytes.empty()) return std::nullopt;
    const bool ds = isDoubleSidedSize(bytes.size());
    if (side != 0 && (!ds || side != 1)) return std::nullopt;

    Image img;
    img.data = std::move(bytes);
    img.side = side;
    img.ds   = ds;
    if (auto dir = parseDirectory(img.data, side, ds)) {
        img.directory = *dir;
        img.hasDirectory = true;
    }
    return img;
}

std::optional<Image> openLinearImage(std::vector<uint8_t> bytes)
{
    if (bytes.empty() || (bytes.size() % kBlock) != 0) return std::nullopt;

    Image img;
    img.data   = std::move(bytes);
    img.side   = 0;
    img.ds     = false;
    img.vol    = Vol::linear;
    if (auto dir = parseDirectory(img.data, 0, false, Vol::linear)) {
        img.directory = *dir;
        img.hasDirectory = true;
    }
    return img;
}

namespace {
std::optional<std::vector<uint8_t>> readWholeFile(const std::string &path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return std::nullopt;
    std::vector<uint8_t> raw;
    raw.assign(std::istreambuf_iterator<char>(f),
               std::istreambuf_iterator<char>());
    if (raw.empty()) return std::nullopt;
    return raw;
}
}  /* namespace */

std::optional<Image> loadImage(const std::string &path, int side)
{
    auto raw = readWholeFile(path);
    if (!raw) return std::nullopt;
    return openImage(std::move(*raw), side);
}

std::optional<Image> loadLinearImage(const std::string &path)
{
    auto raw = readWholeFile(path);
    if (!raw) return std::nullopt;
    return openLinearImage(std::move(*raw));
}

std::optional<Image> openVolume(std::vector<uint8_t> bytes, Vol vol, int side)
{
    switch (vol) {
    case Vol::floppy: return openImage(std::move(bytes), side);
    case Vol::linear: return openLinearImage(std::move(bytes));
    case Vol::dv:
    case Vol::mz:     break;
    }
    if (bytes.size() != kDoubleSize || side != 0) return std::nullopt;

    Image img;
    img.data = std::move(bytes);
    img.side = 0;
    img.ds   = true;
    img.vol  = vol;
    if (auto dir = parseDirectory(img.data, 0, true, vol)) {
        img.directory = *dir;
        img.hasDirectory = true;
    }
    return img;
}

std::optional<Image> loadVolume(const std::string &path, Vol vol, int side)
{
    auto raw = readWholeFile(path);
    if (!raw) return std::nullopt;
    return openVolume(std::move(*raw), vol, side);
}

std::vector<VolumeSpec> detectVolumes(std::span<const uint8_t> bytes)
{
    std::vector<VolumeSpec> found;
    auto tryOne = [&](Vol vol, int side, bool ds) {
        /* Strict probe: the home block (LBN 1 through this lens) must hold
         * a plausible directory pointer, and the directory must be there.
         * parseDirectory alone is too lax for detection — its candidate
         * scan can land on a *second* directory segment of a DZ volume
         * seen through the DV/MZ lens (block aliasing). */
        const std::size_t homeOff = lbnToByte(1, side, ds, vol);
        if (homeOff + kBlock > bytes.size()) return;
        const int dirLbn = bytes[homeOff + 0x1D4]
                         | (bytes[homeOff + 0x1D5] << 8);
        if (dirLbn < 2 || dirLbn > 64) return;
        if (auto dir = parseDirectory(bytes, side, ds, vol);
            dir && dir->dirStartLbn == dirLbn)
            found.push_back({vol, side});
    };
    if (bytes.size() == kSideSize) {
        tryOne(Vol::floppy, 0, false);
    } else if (bytes.size() == kDoubleSize) {
        tryOne(Vol::floppy, 0, true);
        tryOne(Vol::floppy, 1, true);
        tryOne(Vol::dv, 0, true);
        tryOne(Vol::mz, 0, true);
    } else if (!bytes.empty() && bytes.size() % kBlock == 0) {
        tryOne(Vol::linear, 0, false);
    }
    return found;
}

std::optional<std::pair<std::vector<uint8_t>, std::vector<uint8_t>>>
splitDoubleSided(std::span<const uint8_t> ds)
{
    if (ds.size() != kDoubleSize) return std::nullopt;
    std::vector<uint8_t> s0(kSideSize), s1(kSideSize);
    for (int t = 0; t < kTracks; ++t) {
        const std::size_t src = static_cast<std::size_t>(t) * 2 * kTrackSize;
        const std::size_t dst = static_cast<std::size_t>(t) * kTrackSize;
        std::copy_n(ds.begin() + src,             kTrackSize, s0.begin() + dst);
        std::copy_n(ds.begin() + src + kTrackSize, kTrackSize, s1.begin() + dst);
    }
    return std::make_pair(std::move(s0), std::move(s1));
}

std::optional<std::vector<uint8_t>>
mergeSides(std::span<const uint8_t> side0, std::span<const uint8_t> side1)
{
    if (side0.size() != kSideSize || side1.size() != kSideSize) return std::nullopt;
    std::vector<uint8_t> ds(kDoubleSize);
    for (int t = 0; t < kTracks; ++t) {
        const std::size_t src = static_cast<std::size_t>(t) * kTrackSize;
        const std::size_t dst = static_cast<std::size_t>(t) * 2 * kTrackSize;
        std::copy_n(side0.begin() + src, kTrackSize, ds.begin() + dst);
        std::copy_n(side1.begin() + src, kTrackSize, ds.begin() + dst + kTrackSize);
    }
    return ds;
}

} /* namespace ms0515::disk */
