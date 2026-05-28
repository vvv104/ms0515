/*
 * Image.cpp — capture loading + structural layout detection.
 */

#include "ms0515/disk/Image.hpp"

#include <array>
#include <fstream>

namespace ms0515::disk {

namespace {

/* Single-sided candidate mappings, in detection order.  MS-0515 sides
 * are always separate SS volumes — there is no spanning layout to try. */
constexpr Layout kCandidates[] = {
    Layout::SsCanonical, Layout::SsCyl0LastNoIl, Layout::SsCyl0FirstNoIl,
    Layout::SsOsaSkew,   Layout::SsLbnLinear,
};

}  /* namespace */

std::optional<Layout> detectLayout(std::span<const uint8_t> data)
{
    std::optional<Layout> best;
    std::size_t bestPerm = 0;
    for (Layout l : kCandidates) {
        auto dir = parseDirectory(data, l);
        if (!dir) continue;
        const std::size_t perm = dir->permanentFiles().size();
        if (perm > bestPerm) { bestPerm = perm; best = l; }
    }
    return best;
}

std::span<const uint8_t> Image::block(int lbn) const
{
    const std::size_t off = lbnToByte(layout, lbn);
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
    Image img;
    img.data = std::move(bytes);
    img.side = side;
    if (img.data.empty()) return std::nullopt;

    if (auto l = detectLayout(img.data)) {
        img.layout = *l;
        if (auto dir = parseDirectory(img.data, *l)) {
            img.directory = *dir;
            img.hasDirectory = true;
        }
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

    /* Isolate the requested side.  A 819200 dump is two SS volumes back
     * to back; anything else is single-sided (side 1 invalid). */
    if (raw.size() == static_cast<std::size_t>(kDoubleSize)) {
        if (side != 0 && side != 1) return std::nullopt;
        const auto begin = raw.begin() + static_cast<std::ptrdiff_t>(side) * kSideSize;
        return openImage(std::vector<uint8_t>(begin, begin + kSideSize), side);
    }
    if (side != 0) return std::nullopt;
    return openImage(std::move(raw), 0);
}

} /* namespace ms0515::disk */
