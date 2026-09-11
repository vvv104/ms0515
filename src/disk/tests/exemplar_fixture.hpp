/*
 * exemplar_fixture.hpp - a make-believe bootable system diskette for the
 * composition tests: a kit the size of a few blocks, a monitor naming START
 * as its startup file, DZ.SYS and DV.SYS with a primary driver each.
 */

#ifndef MS0515_DISK_TESTS_EXEMPLAR_FIXTURE_HPP
#define MS0515_DISK_TESTS_EXEMPLAR_FIXTURE_HPP

#include <ms0515/disk/Build.hpp>
#include <ms0515/disk/Compose.hpp>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace ms0515::disk::fixture {

/* A handler with a primary driver at block 1 (word 062 = 01000) and its
 * read routine at 0140, as writeBoot wants one. */
inline std::vector<uint8_t> handler(uint8_t fill)
{
    std::vector<uint8_t> h(3 * kBlock, 0);
    h[062] = 0x00; h[063] = 0x02;
    h[064] = 0x00; h[065] = 0x02;
    h[066] = 0x60; h[067] = 0x00;
    for (std::size_t i = 0; i < kBlock; ++i) h[kBlock + i] = static_cast<uint8_t>(fill + i % 32);
    return h;
}

/* A six-block monitor naming START as its startup file. */
inline std::vector<uint8_t> monitor()
{
    std::vector<uint8_t> m(6 * kBlock);
    for (std::size_t i = 0; i < m.size(); ++i) m[i] = static_cast<uint8_t>(i / kBlock * 16 + i % 16 + 1);
    for (std::size_t i = 4 * kBlock; i < 5 * kBlock; ++i) m[i] = 0;
    const char line[] = "\0@START \0";
    std::copy(line, line + 9, m.begin() + 5 * kBlock + 100);
    return m;
}

inline std::vector<uint8_t> text(const std::string &s) { return {s.begin(), s.end()}; }

/* The kit on the boot volume of the media asked, bootable. */
inline std::vector<uint8_t> exemplar(Media media)
{
    const bool ds = media != Media::ss;
    const Vol vol = media == Media::dv ? Vol::dv : Vol::floppy;
    auto img = blankImage(ds);
    initVolume(img, 0, ds, {}, vol);
    if (media == Media::dz) initVolume(img, 1, ds);
    auto put = [&](const std::string &name, const std::vector<uint8_t> &data, uint16_t date, bool p) {
        putFile(img, 0, ds, name, data, PutOptions{date, p}, vol);
    };
    put("SWAP.SYS", std::vector<uint8_t>(2 * kBlock, 3), encodeDate(1990, 12, 27), true);
    put("RT11SJ.SYS", monitor(), encodeDate(1991, 11, 12), true);
    put("DV.SYS", handler(0xC0), encodeDate(1995, 1, 1), true);
    put("DZ.SYS", handler(0xA0), encodeDate(1991, 11, 4), true);
    put("TT.SYS", std::vector<uint8_t>(kBlock, 7), encodeDate(1991, 1, 31), true);
    put("PIP.SAV", std::vector<uint8_t>(3 * kBlock, 4), encodeDate(1991, 1, 31), true);
    put("START.COM", text("SET TT QUIET\r\nSET SL ON\r\n"), encodeDate(1995, 4, 1), false);
    writeBoot(img, 0, ds, "RT11SJ", vol);
    return img;
}

inline const std::vector<std::string> kKit{"SWAP.SYS", "RT11SJ.SYS", "DV.SYS", "DZ.SYS", "TT.SYS", "PIP.SAV", "START.COM"};

}  /* namespace ms0515::disk::fixture */

#endif
