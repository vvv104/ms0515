/*
 * Internal.hpp — RT-11 on-disk format helpers shared by Build.cpp and
 * FolderVolume.cpp.  Lib-internal: never installed, never included from
 * public headers.
 */

#ifndef MS0515_DISK_INTERNAL_HPP
#define MS0515_DISK_INTERNAL_HPP

#include "ms0515/disk/Layout.hpp"

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace ms0515::disk::internal {

inline constexpr char kRad50[] =
    " ABCDEFGHIJKLMNOPQRSTUVWXYZ$.?0123456789";

inline int rad50Index(char c)
{
    for (int i = 0; i < 40; ++i)
        if (kRad50[i] == c) return i;
    throw std::runtime_error(std::string("character not RAD50-encodable: ") + c);
}

inline uint16_t encodeRad50(const char *p)  /* exactly 3 chars */
{
    return static_cast<uint16_t>(rad50Index(p[0]) * 1600 +
                                 rad50Index(p[1]) * 40 +
                                 rad50Index(p[2]));
}

/* Split "NAME.EXT" into space-padded name[6] + ext[3], uppercased. */
inline void splitName(const std::string &filename, char name[6], char ext[3])
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

inline void putw(uint8_t *p, uint16_t v)
{
    p[0] = static_cast<uint8_t>(v & 0xFF);
    p[1] = static_cast<uint8_t>(v >> 8);
}

inline uint16_t getw(const uint8_t *p)
{
    return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}

inline void putEntry(uint8_t *seg, std::size_t p, uint16_t status, uint16_t n1,
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

inline constexpr int kDirLbn = 6;   /* first directory segment block */

/* Home block, matching what INIT writes on a B6 6D blank: a few leading
 * bytes, the FFFF marker at 0x1C0, the directory pointer, version, and the
 * volume/owner/system identity strings; bytes INIT does not touch keep the
 * blank 0xB6 0x6D pattern. */
inline std::vector<uint8_t> makeHomeBlock(const std::string &volumeId,
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

} /* namespace ms0515::disk::internal */

#endif /* MS0515_DISK_INTERNAL_HPP */
