/*
 * ZeroRun.cpp - see ZeroRun.hpp for the format.
 */

#include "ZeroRun.hpp"

#include <algorithm>

namespace ms0515::run {

namespace {

/* A gap of zeros shorter than a stretch's own header is cheaper kept. */
constexpr std::size_t kHeader = 8;

void putU32(std::vector<uint8_t> &out, std::size_t v)
{
    for (int shift = 0; shift < 32; shift += 8)
        out.push_back(static_cast<uint8_t>(v >> shift));
}

std::size_t getU32(std::span<const uint8_t> in, std::size_t at)
{
    return static_cast<std::size_t>(in[at]) |
           static_cast<std::size_t>(in[at + 1]) << 8 |
           static_cast<std::size_t>(in[at + 2]) << 16 |
           static_cast<std::size_t>(in[at + 3]) << 24;
}

} /* namespace */

std::vector<uint8_t> packZeroRuns(std::span<const uint8_t> data)
{
    std::vector<uint8_t> out;
    putU32(out, data.size());

    const auto nonZero = [](uint8_t b) { return b != 0; };
    auto pos = std::find_if(data.begin(), data.end(), nonZero);
    while (pos != data.end()) {
        /* The stretch runs on until a gap of zeros worth leaving out. */
        auto end = pos;
        for (auto scan = pos; scan != data.end();) {
            end = std::find(scan, data.end(), uint8_t{0});
            scan = std::find_if(end, data.end(), nonZero);
            if (scan == data.end() ||
                static_cast<std::size_t>(scan - end) > kHeader)
                break;
        }
        putU32(out, static_cast<std::size_t>(pos - data.begin()));
        putU32(out, static_cast<std::size_t>(end - pos));
        out.insert(out.end(), pos, end);
        pos = std::find_if(end, data.end(), nonZero);
    }
    return out;
}

std::optional<std::vector<uint8_t>>
unpackZeroRuns(std::span<const uint8_t> packed)
{
    if (packed.size() < 4)
        return std::nullopt;
    std::vector<uint8_t> out(getU32(packed, 0), 0);

    std::size_t at = 4, floor = 0;
    while (at < packed.size()) {
        if (packed.size() - at < kHeader)
            return std::nullopt;
        const std::size_t offset = getU32(packed, at);
        const std::size_t length = getU32(packed, at + 4);
        at += kHeader;
        if (offset < floor || offset > out.size() ||
            length > out.size() - offset || length > packed.size() - at)
            return std::nullopt;
        std::copy_n(packed.begin() + static_cast<std::ptrdiff_t>(at), length,
                    out.begin() + static_cast<std::ptrdiff_t>(offset));
        at += length;
        floor = offset + length;
    }
    return out;
}

} /* namespace ms0515::run */
