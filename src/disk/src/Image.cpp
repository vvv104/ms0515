/*
 * Image.cpp — capture loading + file reads, addressed via the FDC geometry.
 */

#include "ms0515/disk/Image.hpp"

#include <fstream>

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

} /* namespace ms0515::disk */
