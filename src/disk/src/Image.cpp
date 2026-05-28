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
    const std::size_t off = lbnToByte(lbn, side, ds);
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

std::optional<Image> loadImage(const std::string &path, int side)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return std::nullopt;
    std::vector<uint8_t> raw;
    raw.assign(std::istreambuf_iterator<char>(f),
               std::istreambuf_iterator<char>());
    if (raw.empty()) return std::nullopt;
    return openImage(std::move(raw), side);
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
