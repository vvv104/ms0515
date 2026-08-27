/*
 * test_system_disk.cpp — a system volume made by the disk library alone.
 *
 * Two oracles, both the OS's own:
 *   1. The shipped disks' boot blocks were written by RT-11's COPY/BOOT.
 *      writeBoot() over their own files must reproduce them byte for byte
 *      (osa MON8SJ, vvv / omega RT11SJ, mihin RT11SJ of another kit,
 *      rodionov RT15SJ - three DUP variants, three monitor families).
 *   2. A blank floppy given the kit of a shipped system (systemKit) and
 *      writeBoot() must boot in the emulator to the command prompt - and
 *      a second one made from that copy must boot too: the copy can do
 *      the same again.
 */
#include <doctest/doctest.h>

#include <ms0515/Emulator.hpp>
#include <ms0515/Terminal.hpp>
#include <ms0515/disk/Build.hpp>
#include <ms0515/disk/Image.hpp>
#include <ms0515/disk/Layout.hpp>

#include "test_disk.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#ifndef ASSETS_DIR
#error "ASSETS_DIR must be defined by the build system"
#endif

using namespace ms0515::disk;

namespace {

const std::string kDisks = std::string{ASSETS_DIR} + "/disks";
const std::string kRomA  = std::string{ASSETS_DIR} + "/rom/ms0515-roma.rom";
const std::string kRomB  = std::string{ASSETS_DIR} + "/rom/ms0515-romb.rom";

std::vector<uint8_t> readAll(const std::string &path)
{
    std::ifstream f(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

/* The five boot blocks of a side, LBN 0 and 2..5, as one vector. */
std::vector<uint8_t> bootBlocks(const std::vector<uint8_t> &image, int side, bool ds)
{
    std::vector<uint8_t> out;
    for (int lbn : {0, 2, 3, 4, 5}) {
        const auto at = lbnToByte(lbn, side, ds);
        out.insert(out.end(), image.begin() + static_cast<std::ptrdiff_t>(at), image.begin() + static_cast<std::ptrdiff_t>(at + kBlock));
    }
    return out;
}

/* A fresh single-sided volume holding the kit of `system` (side 0), made
 * bootable by the library. */
std::vector<uint8_t> systemCopy(const std::vector<uint8_t> &system, bool systemDs)
{
    auto target = blankImage(false);
    initVolume(target, 0, false);
    auto src = openImage(system, 0);
    REQUIRE(src.has_value());
    for (const auto &name : systemKit(system, 0, systemDs)) {
        const auto *e = src->directory.find(name);
        REQUIRE(e != nullptr);
        PutOptions opts;
        opts.date = e->date;
        opts.readOnly = (e->status & kStatusProtected) != 0;
        putFile(target, 0, false, name, src->readFile(name), opts);
    }
    writeBoot(target, 0, false, bootedMonitor(system, 0, systemDs));
    return target;
}

/* Boots the machine from a temporary copy of `image` in drive 0 and tells
 * whether a row of the screen starts with the OS prompt "." after
 * `frames` frames (the way test_boot.cpp tells a boot from a hang). */
bool bootsToPrompt(const std::string &rom, const std::vector<uint8_t> &image, int frames)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(TESTS_BUILD_DIR "/temp", ec);
    const fs::path path = fs::path{TESTS_BUILD_DIR "/temp"} / "ms0515_system_copy.dsk";
    { std::ofstream f(path, std::ios::binary); f.write(reinterpret_cast<const char *>(image.data()), static_cast<std::streamsize>(image.size())); }
    bool prompt = false;
    {
        ms0515::Emulator emu;
        REQUIRE(emu.loadRomFile(rom));
        REQUIRE(emu.mountDisk(0, path.string()));
        emu.reset();
        for (int i = 0; i < frames; ++i) (void)emu.stepFrame();
        ms0515::Terminal term;
        const auto snap = term.decode(emu);
        for (int row = 0; row < ms0515::Terminal::kRows && !prompt; ++row) {
            const auto rs = snap.row(row);
            prompt = !rs.empty() && rs[0] == '.';
        }
    }
    fs::remove(path, ec);
    return prompt;
}

}  /* namespace */

TEST_CASE("writeBoot reproduces the boot blocks RT-11's COPY/BOOT wrote on the shipped disks") {
    struct Disk { const char *file; bool ds; const char *monitor; };
    const Disk disks[] = {
        {"osa.dsk", false, "MON8SJ"}, {"vvv.dsk", false, "RT11SJ"}, {"omega-games.dsk", false, "RT11SJ"},
        {"omega-lang.dsk", false, "RT11SJ"}, {"mihin.dsk", false, "RT11SJ"}, {"rodionov.dsk", true, "RT15SJ"},
    };
    for (const auto &d : disks) {
        SUBCASE(d.file) {
            const auto original = readAll(kDisks + "/" + d.file);
            REQUIRE(original.size() == (d.ds ? 2u * kSideSize : kSideSize));
            CHECK(bootedMonitor(original, 0, d.ds) == d.monitor);

            auto image = original;
            for (int lbn : {0, 2, 3, 4, 5})            /* the boot blocks wiped, then written anew */
                std::fill_n(image.begin() + static_cast<std::ptrdiff_t>(lbnToByte(lbn, 0, d.ds)), kBlock, uint8_t{0xEE});
            CHECK(bootedMonitor(image, 0, d.ds) == "");
            writeBoot(image, 0, d.ds, d.monitor);
            CHECK(bootBlocks(image, 0, d.ds) == bootBlocks(original, 0, d.ds));
            CHECK(image == original);                   /* nothing else touched */
        }
    }
}

TEST_CASE("a system volume the library makes boots, and makes another that boots") {
    struct System { const char *file; const char *rom; const char *monitor; };
    const System systems[] = {{"vvv.dsk", "a", "RT11SJ"}, {"osa.dsk", "a", "MON8SJ"}, {"mihin.dsk", "b", "RT11SJ"}};
    for (const auto &s : systems) {
        SUBCASE(s.file) {
            const auto system = readAll(kDisks + "/" + s.file);
            const auto kit = systemKit(system, 0, false);
            CHECK(std::find(kit.begin(), kit.end(), std::string(s.monitor) + ".SYS") != kit.end());
            CHECK(std::find(kit.begin(), kit.end(), "SWAP.SYS") != kit.end());
            CHECK(std::find(kit.begin(), kit.end(), "DZ.SYS") != kit.end());

            const auto first = systemCopy(system, false);
            CHECK(bootedMonitor(first, 0, false) == s.monitor);
            CHECK(bootsToPrompt(s.rom[0] == 'a' ? kRomA : kRomB, first, 600));

            const auto second = systemCopy(first, false);   /* the copy can do the same again */
            CHECK(bootedMonitor(second, 0, false) == s.monitor);
            CHECK(bootsToPrompt(s.rom[0] == 'a' ? kRomA : kRomB, second, 600));
        }
    }
}
