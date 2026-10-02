/*
 * SparseVolume.cpp — see SparseVolume.hpp for the idea and the serial form.
 */

#include "ms0515/disk/SparseVolume.hpp"

#include <algorithm>
#include <cstring>

namespace ms0515::disk {

namespace {

constexpr char        kMagic[8]  = {'M', 'S', '0', '5', '1', '5', 'S', 'V'};
constexpr uint16_t    kVersion   = 1;
constexpr std::size_t kBlockSize = 512;
constexpr std::size_t kHeader    = 20;
constexpr std::size_t kEntry     = 4 + kBlockSize;

void put16(std::vector<uint8_t> &out, std::size_t v)
{
    out.push_back(static_cast<uint8_t>(v));
    out.push_back(static_cast<uint8_t>(v >> 8));
}

void put32(std::vector<uint8_t> &out, std::size_t v)
{
    put16(out, v & 0xFFFF);
    put16(out, v >> 16);
}

std::size_t get16(std::span<const uint8_t> in, std::size_t at)
{
    return static_cast<std::size_t>(in[at]) | static_cast<std::size_t>(in[at + 1]) << 8;
}

std::size_t get32(std::span<const uint8_t> in, std::size_t at)
{
    return get16(in, at) | get16(in, at + 2) << 16;
}

bool allZero(const uint8_t *block)
{
    return std::all_of(block, block + kBlockSize, [](uint8_t b) { return b == 0; });
}

} /* namespace */

SparseVolume::SparseVolume(int blocks) : blocks_(std::max(blocks, 0)) {}

std::optional<SparseVolume>
SparseVolume::parse(std::span<const uint8_t> bytes, std::string *error)
{
    const auto refuse = [&](const char *why) {
        if (error) *error = why;
        return std::nullopt;
    };
    if (bytes.size() < kHeader || std::memcmp(bytes.data(), kMagic, sizeof kMagic) != 0)
        return refuse("not a sparse volume");
    if (get16(bytes, 8) != kVersion)
        return refuse("a sparse volume of an unknown version");
    if (get16(bytes, 10) != kBlockSize)
        return refuse("a sparse volume of another block size");
    const std::size_t blocks = get32(bytes, 12);
    const std::size_t count  = get32(bytes, 16);
    if (blocks > 0x7FFFFFFF || (bytes.size() - kHeader) % kEntry != 0 ||
        (bytes.size() - kHeader) / kEntry != count)
        return refuse("a sparse volume cut short or with a wrong count");

    SparseVolume vol(static_cast<int>(blocks));
    std::size_t at = kHeader;
    std::size_t floor = 0;
    for (std::size_t i = 0; i < count; ++i, at += kEntry) {
        const std::size_t lbn = get32(bytes, at);
        if (lbn >= blocks || lbn < floor)
            return refuse("a sparse volume with its blocks out of order or range");
        floor = lbn + 1;
        Block block;
        std::memcpy(block.data(), bytes.data() + at + 4, kBlockSize);
        vol.held_.emplace(static_cast<int>(lbn), block);
    }
    return vol;
}

std::vector<uint8_t> SparseVolume::serialize() const
{
    std::vector<uint8_t> out;
    out.reserve(kHeader + held_.size() * kEntry);
    out.insert(out.end(), std::begin(kMagic), std::end(kMagic));
    put16(out, kVersion);
    put16(out, kBlockSize);
    put32(out, static_cast<std::size_t>(blocks_));
    put32(out, held_.size());
    for (const auto &[lbn, block] : held_) {
        put32(out, static_cast<std::size_t>(lbn));
        out.insert(out.end(), block.begin(), block.end());
    }
    return out;
}

SparseVolume SparseVolume::fromLinear(std::span<const uint8_t> linear)
{
    SparseVolume vol(static_cast<int>(linear.size() / kBlockSize));
    for (int lbn = 0; lbn < vol.blocks_; ++lbn)
        vol.writeBlock(lbn, linear.data() + static_cast<std::size_t>(lbn) * kBlockSize);
    return vol;
}

std::vector<uint8_t> SparseVolume::toLinear() const
{
    std::vector<uint8_t> linear(static_cast<std::size_t>(blocks_) * kBlockSize, 0);
    for (const auto &[lbn, block] : held_)
        std::memcpy(linear.data() + static_cast<std::size_t>(lbn) * kBlockSize,
                    block.data(), kBlockSize);
    return linear;
}

void SparseVolume::resize(int blocks)
{
    blocks_ = std::max(blocks, 0);
    held_.erase(held_.lower_bound(blocks_), held_.end());
}

void SparseVolume::readBlock(int lbn, uint8_t *out) const
{
    const auto it = held_.find(lbn);
    if (it == held_.end())
        std::memset(out, 0, kBlockSize);
    else
        std::memcpy(out, it->second.data(), kBlockSize);
}

void SparseVolume::writeBlock(int lbn, const uint8_t *in)
{
    if (lbn < 0 || lbn >= blocks_)
        return;
    if (allZero(in)) {
        held_.erase(lbn);
        return;
    }
    std::memcpy(held_[lbn].data(), in, kBlockSize);
}

void SparseVolume::clearBlock(int lbn)
{
    held_.erase(lbn);
}

} /* namespace ms0515::disk */
