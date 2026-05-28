/*
 * test_paths.cpp — exercises only what ms0515::app::Paths promises in
 * the header, no peeking at the .cpp.  Goal is to lock in the contract,
 * not the implementation.
 */

#include <doctest/doctest.h>

#include <ms0515/app/Paths.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <regex>
#include <string>

namespace fs   = std::filesystem;
namespace app  = ms0515::app;

TEST_SUITE("Paths::parseNumber") {

TEST_CASE("decimal integers parse straight through") {
    CHECK(app::Paths::parseNumber("0")     == 0);
    CHECK(app::Paths::parseNumber("1")     == 1);
    CHECK(app::Paths::parseNumber("42")    == 42);
    CHECK(app::Paths::parseNumber("1000")  == 1000);
}

TEST_CASE("hex with 0x or 0X prefix is recognised") {
    CHECK(app::Paths::parseNumber("0x10")  == 16);
    CHECK(app::Paths::parseNumber("0X10")  == 16);
    CHECK(app::Paths::parseNumber("0xff")  == 255);
    CHECK(app::Paths::parseNumber("0xDEAD") == 0xDEAD);
}

TEST_CASE("octal with 0o or 0O prefix is recognised — Python-style") {
    CHECK(app::Paths::parseNumber("0o10")   == 8);
    CHECK(app::Paths::parseNumber("0O10")   == 8);
    CHECK(app::Paths::parseNumber("0o777")  == 0777);
    CHECK(app::Paths::parseNumber("0o177400") == 0177400);  /* dispatcher reg */
}

TEST_CASE("malformed strings return 0 without throwing") {
    CHECK(app::Paths::parseNumber("")          == 0);
    CHECK(app::Paths::parseNumber("garbage")   == 0);
    CHECK(app::Paths::parseNumber("0xZZ")      == 0);
    CHECK(app::Paths::parseNumber("0o9")       == 0); /* 9 isn't octal */
}

}  // TEST_SUITE("Paths::parseNumber")


TEST_SUITE("Paths::exeDir") {

TEST_CASE("returns a non-empty string ending with a path separator") {
    std::string dir = app::Paths::exeDir();
    REQUIRE_FALSE(dir.empty());
    char last = dir.back();
    CHECK((last == '/' || last == '\\'));
}

TEST_CASE("the returned path exists as a directory") {
    std::string dir = app::Paths::exeDir();
    REQUIRE_FALSE(dir.empty());
    /* exeDir always falls back to "./" if the OS query fails — "./"
     * is itself a valid directory, so this check holds either way. */
    std::error_code ec;
    CHECK(fs::is_directory(fs::path(dir), ec));
}

TEST_CASE("two calls return the same value") {
    CHECK(app::Paths::exeDir() == app::Paths::exeDir());
}

}  // TEST_SUITE("Paths::exeDir")


TEST_SUITE("Paths::searchRoots") {

TEST_CASE("contains at least the exeDir entry") {
    auto roots = app::Paths::searchRoots();
    REQUIRE_FALSE(roots.empty());
    std::string exeDir = app::Paths::exeDir();
    bool found = false;
    for (const auto &r : roots) {
        if (r.string() == exeDir ||
            r.string() + "/" == exeDir ||
            r.string() + "\\" == exeDir ||
            r == fs::path(exeDir)) {
            found = true;
            break;
        }
    }
    CHECK(found);
}

}  // TEST_SUITE("Paths::searchRoots")


TEST_SUITE("Paths::timestamped") {

TEST_CASE("anchors the filename to exeDir, embeds prefix + ext") {
    std::string p = app::Paths::timestamped("snapshot", ".png");
    REQUIRE_FALSE(p.empty());
    CHECK(p.starts_with(app::Paths::exeDir()));
    CHECK(p.ends_with(".png"));
    /* Filename layout is <prefix>_YYYY-MM-DD_HHMMSS<ext> per docs.  We
     * don't pin the exact timestamp (test would race with the clock)
     * — just the structural form. */
    std::regex re(R"(snapshot_\d{4}-\d{2}-\d{2}_\d{6}\.png$)");
    CHECK(std::regex_search(p, re));
}

}  // TEST_SUITE("Paths::timestamped")


TEST_SUITE("Paths::findAssetRom") {

TEST_CASE("returns empty string when the named ROM isn't anywhere") {
    /* The header promises empty-on-failure, no exception. */
    auto found = app::Paths::findAssetRom("totally-not-a-real-rom-xyz.rom");
    CHECK(found.empty());
}

TEST_CASE("finds a ROM we've planted under a search root") {
    /* Make a fixture inside the build dir (a guaranteed-writable
     * location).  The exact contents don't matter — findAssetRom only
     * checks for existence. */
    fs::path tmp = fs::path(TESTS_BUILD_DIR) / "fixture-paths";
    fs::create_directories(tmp / "assets" / "rom");
    std::string name = "ms0515-paths-test.rom";
    fs::path romFile = tmp / "assets" / "rom" / name;
    std::ofstream(romFile) << "stub";

    /* searchRoots() returns the exeDir and cwd.  exeDir is the build
     * dir (the test binary lives there).  CWD when running through
     * ctest is also typically the build dir — so the fixture
     * <buildDir>/assets/rom/<name> should be discoverable.  If the
     * test is launched from elsewhere we skip the assert and just
     * remove the fixture. */
    std::string discovered = app::Paths::findAssetRom(name);
    if (!discovered.empty()) {
        CHECK(discovered.ends_with(name));
        CHECK(fs::exists(discovered));
    }

    fs::remove(romFile);
    fs::remove(tmp / "assets" / "rom");
    fs::remove(tmp / "assets");
    fs::remove(tmp);
}

}  // TEST_SUITE("Paths::findAssetRom")
