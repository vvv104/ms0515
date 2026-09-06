/*
 * test_mount_sync.cpp — the commander's slots applied to the running
 * machine: units FD0..FD3 and the HD follow the Mounts, side by side.
 */
#include "MountSync.hpp"

#include <ms0515/Emulator.hpp>

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;
using namespace ms0515;

namespace {

fs::path scratch(const std::string &name)
{
    const fs::path dir = fs::path(TESTS_BUILD_DIR) / "scratch";
    fs::create_directories(dir);
    return dir / name;
}

fs::path fixture(const std::string &fixtureName, const std::string &name)
{
    const fs::path dst = scratch(name);
    fs::copy_file(fs::path(FIXTURE_DISKS_DIR) / fixtureName, dst, fs::copy_options::overwrite_existing);
    return dst;
}

fs::path blankHd(const std::string &name, int blocks)
{
    const fs::path p = scratch(name);
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    const std::vector<char> zeros(512, 0);
    for (int i = 0; i < blocks; ++i) out.write(zeros.data(), 512);
    return p;
}

} // namespace

TEST_CASE("a two-sided image takes both units of its drive, a single-sided one its side only; ejecting empties them")
{
    const auto rod = fixture("test_rod.dsk", "sync_rod.dsk");
    const auto osa = fixture("test_osa.dsk", "sync_osa.dsk");
    Emulator emu;
    files::Mounts m;
    REQUIRE(m.mount(files::Slot::driveA, rod, 0, true).empty());
    REQUIRE(m.mount(files::Slot::driveB, osa, 1, true).empty());
    CHECK(cli::applyMounts(emu, m).empty());
    CHECK(emu.diskPath(0) == rod.string());
    CHECK(emu.diskPath(2) == rod.string());
    CHECK(emu.diskPath(1).empty());
    CHECK(emu.diskPath(3) == osa.string());

    /* the same again changes nothing and errs nowhere */
    CHECK(cli::applyMounts(emu, m).empty());
    CHECK(emu.diskPath(0) == rod.string());

    /* drive A ejected; B gets the same image on side 0 too (a drive holds
     * a single-sided image per side) */
    m.unmount(files::Slot::driveA);
    REQUIRE(m.mount(files::Slot::driveB, osa, 0, true).empty());
    CHECK(cli::applyMounts(emu, m).empty());
    CHECK(emu.diskPath(0).empty());
    CHECK(emu.diskPath(2).empty());
    CHECK(emu.diskPath(1) == osa.string());
    CHECK(emu.diskPath(3) == osa.string());

    /* B ejected altogether */
    m.unmount(files::Slot::driveB);
    CHECK(cli::applyMounts(emu, m).empty());
    CHECK(emu.diskPath(1).empty());
    CHECK(emu.diskPath(3).empty());
}

TEST_CASE("the HD slot mounts the image and turns the controller on; an image that cannot be mounted is reported")
{
    const auto hd = blankHd("sync_hd.img", 64);
    Emulator emu;
    files::Mounts m;
    REQUIRE(m.mount(files::Slot::hd, hd, 0, true).empty());
    CHECK(cli::applyMounts(emu, m).empty());
    CHECK(emu.hdMounted());
    CHECK(emu.hdEnabled());
    CHECK(emu.hdPath() == hd.string());

    m.unmount(files::Slot::hd);
    CHECK(cli::applyMounts(emu, m).empty());
    CHECK_FALSE(emu.hdMounted());

    /* gone from the disk between the mount and the apply */
    const auto gone = blankHd("sync_gone.img", 4);
    REQUIRE(m.mount(files::Slot::hd, gone, 0, true).empty());
    fs::remove(gone);
    const auto errors = cli::applyMounts(emu, m);
    REQUIRE(errors.size() == 1);
    CHECK(errors[0].find("sync_gone.img") != std::string::npos);
    CHECK_FALSE(emu.hdMounted());
}
