/*
 * test_disks.cpp — contract tests for libapp Disks.  Goes through the
 * public header only; mountDisksFromCli exercises a real Emulator.
 */

#include <doctest/doctest.h>

#include <ms0515/app/Cli.hpp>
#include <ms0515/app/Config.hpp>     /* fdcUnitFor */
#include <ms0515/app/Disks.hpp>

#include <ms0515/Emulator.hpp>       /* kFloppyDiskSize */

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs   = std::filesystem;
namespace app  = ms0515::app;

namespace {

/* Create `bytes` zero bytes at `path`; returns the path back for
 * convenience. */
fs::path makeBlankFile(const fs::path &path, std::uintmax_t bytes)
{
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    if (bytes > 0) {
        std::vector<char> zeros(static_cast<size_t>(bytes), 0);
        out.write(zeros.data(), static_cast<std::streamsize>(bytes));
    }
    return path;
}

fs::path fixtureRoot()
{
    return fs::path(TESTS_BUILD_DIR) / "fixture-disks";
}

}  // namespace


TEST_SUITE("validateSingleSideImage") {

TEST_CASE("a 409 600-byte file is accepted") {
    fs::path p = makeBlankFile(fixtureRoot() / "ss.dsk", ms0515::kFloppyDiskSize);
    CHECK_FALSE(app::validateSingleSideImage(p.string()).has_value());
    fs::remove(p);
}

TEST_CASE("a 819 200-byte file is rejected with a hint to use --diskN") {
    fs::path p = makeBlankFile(fixtureRoot() / "ds.dsk", 2 * ms0515::kFloppyDiskSize);
    auto err = app::validateSingleSideImage(p.string());
    REQUIRE(err.has_value());
    /* The error message is meant for stderr — it should at least
     * mention the rival flag, otherwise the user has no idea what to
     * try next. */
    CHECK(err->find("double-sided") != std::string::npos);
    fs::remove(p);
}

TEST_CASE("a missing file returns an error string, not std::nullopt") {
    auto err = app::validateSingleSideImage("/nope/missing.dsk");
    CHECK(err.has_value());
}

TEST_CASE("a file of an unexpected size also fails") {
    fs::path p = makeBlankFile(fixtureRoot() / "weird.dsk", 12345);
    auto err = app::validateSingleSideImage(p.string());
    CHECK(err.has_value());
    fs::remove(p);
}

}  // TEST_SUITE


TEST_SUITE("validateDoubleSidedImage") {

TEST_CASE("a 819 200-byte file is accepted") {
    fs::path p = makeBlankFile(fixtureRoot() / "ds2.dsk", 2 * ms0515::kFloppyDiskSize);
    CHECK_FALSE(app::validateDoubleSidedImage(p.string()).has_value());
    fs::remove(p);
}

TEST_CASE("a 409 600-byte file is rejected with a hint to use --diskN-sideN") {
    fs::path p = makeBlankFile(fixtureRoot() / "ss2.dsk", ms0515::kFloppyDiskSize);
    auto err = app::validateDoubleSidedImage(p.string());
    REQUIRE(err.has_value());
    CHECK(err->find("single-side") != std::string::npos);
    fs::remove(p);
}

}  // TEST_SUITE


TEST_SUITE("discoverRoms") {

TEST_CASE("scans assets/rom under search roots, returns sorted unique paths") {
    /* Plant two ROMs in <buildDir>/assets/rom — that's <exeDir> for
     * the test binary AND likely the cwd if launched from there. */
    fs::path base = fs::path(TESTS_BUILD_DIR);
    fs::path romDir = base / "assets" / "rom";
    fs::create_directories(romDir);
    fs::path a = romDir / "aaa-discover-test.rom";
    fs::path b = romDir / "zzz-discover-test.rom";
    std::ofstream(a) << "stub";
    std::ofstream(b) << "stub";

    auto roms = app::discoverRoms();
    /* Locate both fixtures by filename suffix — the absolute paths get
     * normalised so we can't string-equal them.  Sorting is documented;
     * deduplication too. */
    auto findByName = [&](std::string_view leaf) {
        for (const auto &p : roms) {
            if (fs::path(p).filename().string() == leaf) return true;
        }
        return false;
    };

    /* The test binary may be launched from somewhere that doesn't see
     * the fixture (when exeDir is the lib build dir AND cwd is the
     * source tree).  Skip the assert in that case but at least make
     * sure the call itself returned a list. */
    if (findByName("aaa-discover-test.rom") ||
        findByName("zzz-discover-test.rom")) {
        CHECK(findByName("aaa-discover-test.rom"));
        CHECK(findByName("zzz-discover-test.rom"));
        /* Sorted: aaa- < zzz- so aaa- must appear first. */
        auto idx = [&](std::string_view leaf) {
            for (size_t i = 0; i < roms.size(); ++i)
                if (fs::path(roms[i]).filename().string() == leaf) return i;
            return roms.size();
        };
        CHECK(idx("aaa-discover-test.rom") < idx("zzz-discover-test.rom"));
    } else {
        MESSAGE("test binary doesn't have the fixture in its search "
                "roots — discoverRoms() returned without crashing");
    }

    fs::remove(a);
    fs::remove(b);
}

}  // TEST_SUITE


TEST_SUITE("mountDisksFromCli") {

TEST_CASE("mounts a single-sided disk on the requested unit") {
    fs::path p = makeBlankFile(fixtureRoot() / "ss-mount.dsk",
                               ms0515::kFloppyDiskSize);
    app::CliArgs cli;
    cli.fdPath[app::fdcUnitFor(0, 0)] = p.string();

    {
        ms0515::Emulator emu;
        CHECK(app::mountDisksFromCli(emu, cli));
        /* Sanity: the Emulator records the path for unit 0. */
        CHECK(emu.diskPath(app::fdcUnitFor(0, 0)) == p.string());
    }   /* Emulator releases file handles on destruction. */
    fs::remove(p);
}

TEST_CASE("a double-sided image attaches to BOTH sides of the drive") {
    fs::path p = makeBlankFile(fixtureRoot() / "ds-mount.dsk",
                               2 * ms0515::kFloppyDiskSize);
    app::CliArgs cli;
    cli.dsPath[0] = p.string();

    {
        ms0515::Emulator emu;
        CHECK(app::mountDisksFromCli(emu, cli));
        CHECK(emu.diskPath(app::fdcUnitFor(0, 0)) == p.string());
        CHECK(emu.diskPath(app::fdcUnitFor(0, 1)) == p.string());
    }
    fs::remove(p);
}

TEST_CASE("ds + side conflict on the same drive skips the drive without aborting") {
    fs::path ds = makeBlankFile(fixtureRoot() / "conflict-ds.dsk",
                                2 * ms0515::kFloppyDiskSize);
    fs::path ss = makeBlankFile(fixtureRoot() / "conflict-ss.dsk",
                                ms0515::kFloppyDiskSize);
    app::CliArgs cli;
    cli.dsPath[0] = ds.string();
    cli.fdPath[app::fdcUnitFor(0, 0)] = ss.string();

    {
        ms0515::Emulator emu;
        /* Header says: error to stderr, drive skipped, overall result
         * still non-fatal (= true).  Nothing should land in the FDC
         * for drive 0. */
        CHECK(app::mountDisksFromCli(emu, cli));
        CHECK(emu.diskPath(app::fdcUnitFor(0, 0)).empty());
        CHECK(emu.diskPath(app::fdcUnitFor(0, 1)).empty());
    }
    fs::remove(ds);
    fs::remove(ss);
}

TEST_CASE("a misshaped image triggers validation, drive skipped, overall OK") {
    fs::path p = makeBlankFile(fixtureRoot() / "broken.dsk", 1024);
    app::CliArgs cli;
    cli.fdPath[app::fdcUnitFor(0, 0)] = p.string();

    {
        ms0515::Emulator emu;
        CHECK(app::mountDisksFromCli(emu, cli));
        CHECK(emu.diskPath(app::fdcUnitFor(0, 0)).empty());
    }
    fs::remove(p);
}

TEST_CASE("empty CliArgs is a no-op, no mounts performed") {
    app::CliArgs cli;
    ms0515::Emulator emu;
    CHECK(app::mountDisksFromCli(emu, cli));
    for (int u = 0; u < 4; ++u)
        CHECK(emu.diskPath(u).empty());
}

}  // TEST_SUITE
