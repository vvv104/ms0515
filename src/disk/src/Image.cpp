/*
 * Image.cpp — capture loading + structural layout detection.
 */

#include "ms0515/disk/Image.hpp"

#include <array>
#include <fstream>

namespace ms0515::disk {

namespace {

/* Layouts worth trying for a given image size.  DS-spanning is tried
 * first on double-sided images; the SS canonical fallback also reads a
 * DS-two-volume disk's side-0 directory. */
std::vector<Layout> candidates(std::size_t size)
{
    if (size == static_cast<std::size_t>(kDoubleSize))
        return {Layout::DsCyl0LastNoIl, Layout::SsCanonical,
                Layout::SsCyl0LastNoIl, Layout::SsLbnLinear};
    /* Treat any other size as single-sided / linear and try the SS set. */
    return {Layout::SsCanonical, Layout::SsCyl0LastNoIl,
            Layout::SsCyl0FirstNoIl, Layout::SsOsaSkew, Layout::SsLbnLinear};
}

}  /* namespace */

std::optional<Layout> detectLayout(std::span<const uint8_t> data)
{
    std::optional<Layout> best;
    std::size_t bestPerm = 0;
    for (Layout l : candidates(data.size())) {
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

std::optional<Image> loadImage(const std::string &path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return std::nullopt;

    Image img;
    img.data.assign(std::istreambuf_iterator<char>(f),
                    std::istreambuf_iterator<char>());
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

} /* namespace ms0515::disk */
