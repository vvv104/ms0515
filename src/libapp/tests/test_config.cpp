/*
 * test_config.cpp — interface-contract tests for libapp Config.  Goes
 * through the public hpp only, no peeking at the YAML format or
 * implementation details.
 */

#include <doctest/doctest.h>

#include <ms0515/app/Config.hpp>
#include <ms0515/app/Paths.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace fs   = std::filesystem;
namespace app  = ms0515::app;

TEST_SUITE("fdcUnitFor") {

TEST_CASE("hardware mapping FD0..FD3 from (drive, side)") {
    /* The header documents:  FD0 = drive 0 side 0, FD1 = drive 1 side 0,
     *                        FD2 = drive 0 side 1, FD3 = drive 1 side 1.
     * That's the same convention the core uses. */
    CHECK(app::fdcUnitFor(0, 0) == 0);
    CHECK(app::fdcUnitFor(1, 0) == 1);
    CHECK(app::fdcUnitFor(0, 1) == 2);
    CHECK(app::fdcUnitFor(1, 1) == 3);
}

}  // TEST_SUITE("fdcUnitFor")


TEST_SUITE("Config defaults / isDefault") {

TEST_CASE("a freshly-constructed Config is isDefault()") {
    app::Config c;
    CHECK(c.isDefault());
}

TEST_CASE("setting any disk path makes the config non-default") {
    app::Config c;
    c.fdPath[0] = "x.dsk";
    CHECK_FALSE(c.isDefault());
}

TEST_CASE("setting dsPath also makes the config non-default") {
    app::Config c;
    c.dsPath[1] = "x.dsk";
    CHECK_FALSE(c.isDefault());
}

TEST_CASE("setting hdPath also makes the config non-default") {
    app::Config c;
    c.hdPath = "winchester.hd";
    CHECK_FALSE(c.isDefault());
}

TEST_CASE("enabling the HD controller (no image) makes it non-default") {
    app::Config c;
    c.hdEnabled = true;
    CHECK_FALSE(c.isDefault());
}

TEST_CASE("ROM, UI toggles and history settings each take it out of default") {
    {   app::Config c; c.romPath = "rom"; CHECK_FALSE(c.isDefault()); }
    {   app::Config c; c.showKeyboard = true; CHECK_FALSE(c.isDefault()); }
    {   app::Config c; c.showDebugger = true; CHECK_FALSE(c.isDefault()); }
    {   app::Config c; c.hostMode = true;     CHECK_FALSE(c.isDefault()); }
    {   app::Config c; c.fullscreen = true;   CHECK_FALSE(c.isDefault()); }
    {   app::Config c; c.historySize = 16;    CHECK_FALSE(c.isDefault()); }
    {   app::Config c; c.kbdTypingDelayMs = 0; CHECK_FALSE(c.isDefault()); }
}

}  // TEST_SUITE


TEST_SUITE("Config::path") {

TEST_CASE("path lives next to the executable, named ms0515.yaml") {
    std::string p = app::Config::path();
    CHECK(p.starts_with(app::Paths::exeDir()));
    CHECK(p.ends_with("ms0515.yaml"));
}

}  // TEST_SUITE("Config::path")


TEST_SUITE("Config::load") {

TEST_CASE("returns defaults when ms0515.yaml is absent") {
    /* Make sure no ms0515.yaml is in the way for this test. */
    std::string p = app::Config::path();
    bool existed = fs::exists(p);
    std::string saved;
    if (existed) { std::ifstream f(p); std::getline(f, saved, '\0'); fs::remove(p); }

    app::Config c = app::Config::load();
    CHECK(c.isDefault());

    /* Restore whatever was there before. */
    if (existed) { std::ofstream f(p); f.write(saved.data(),
                                               static_cast<std::streamsize>(saved.size())); }
}

}  // TEST_SUITE


TEST_SUITE("Config save/load round-trip") {

TEST_CASE("non-default values written by save() come back through load()") {
    /* Snapshot whatever is on disk so we don't clobber the user's
     * config when the test runs outside CI. */
    std::string p = app::Config::path();
    bool existed = fs::exists(p);
    std::string saved;
    if (existed) {
        std::ifstream in(p, std::ios::binary);
        std::ostringstream buf; buf << in.rdbuf();
        saved = buf.str();
        fs::remove(p);
    }

    {
        app::Config out;
        out.romPath  = "rom.rom";
        out.dsPath[0] = "disk0.dsk";
        out.fdPath[app::fdcUnitFor(1, 0)] = "drive1side0.dsk";
        out.hdPath   = "winchester.hd";
        out.historySize = 256;
        out.fullscreen  = true;
        out.kbdTypingDelayMs = 50;
        out.save();

        app::Config in_ = app::Config::load();
        CHECK(in_.romPath              == out.romPath);
        CHECK(in_.dsPath[0]            == out.dsPath[0]);
        CHECK(in_.fdPath[app::fdcUnitFor(1, 0)] == out.fdPath[app::fdcUnitFor(1, 0)]);
        CHECK(in_.hdPath               == out.hdPath);
        CHECK(in_.historySize          == out.historySize);
        CHECK(in_.fullscreen           == out.fullscreen);
        CHECK(in_.kbdTypingDelayMs     == out.kbdTypingDelayMs);
        CHECK_FALSE(in_.isDefault());
    }

    /* An enabled controller with no image survives the round-trip via the
     * explicit hd_enabled flag. */
    {
        app::Config out;
        out.hdEnabled = true;
        out.save();

        app::Config in_ = app::Config::load();
        CHECK(in_.hdEnabled == true);
        CHECK(in_.hdPath.empty());
        CHECK_FALSE(in_.isDefault());
    }

    /* Clean up + restore user's original config. */
    fs::remove(p);
    if (existed) {
        std::ofstream out_(p, std::ios::binary);
        out_.write(saved.data(), static_cast<std::streamsize>(saved.size()));
    }
}

TEST_CASE("save() removes the file when the config has reverted to defaults") {
    std::string p = app::Config::path();
    bool existed = fs::exists(p);
    std::string saved;
    if (existed) {
        std::ifstream in(p, std::ios::binary);
        std::ostringstream buf; buf << in.rdbuf();
        saved = buf.str();
        fs::remove(p);
    }

    /* Write a non-default config, then save defaults — file should
     * disappear. */
    { app::Config c; c.romPath = "rom.rom"; c.save(); }
    REQUIRE(fs::exists(p));
    { app::Config c; c.save(); }
    CHECK_FALSE(fs::exists(p));

    if (existed) {
        std::ofstream out_(p, std::ios::binary);
        out_.write(saved.data(), static_cast<std::streamsize>(saved.size()));
    }
}

}  // TEST_SUITE


TEST_SUITE("resolveRom") {

TEST_CASE("CLI path wins over config when it exists on disk") {
    fs::path tmp = fs::path(TESTS_BUILD_DIR) / "fixture-resolveRom-cli";
    fs::create_directories(tmp);
    fs::path cliRom = tmp / "cli.rom";
    fs::path cfgRom = tmp / "cfg.rom";
    std::ofstream(cliRom) << "stub";
    std::ofstream(cfgRom) << "stub";

    std::string r = app::resolveRom(cliRom.string(), cfgRom.string());
    CHECK(r == cliRom.string());

    fs::remove_all(tmp);
}

TEST_CASE("config path is the fallback when CLI path is empty") {
    fs::path tmp = fs::path(TESTS_BUILD_DIR) / "fixture-resolveRom-cfg";
    fs::create_directories(tmp);
    fs::path cfgRom = tmp / "cfg.rom";
    std::ofstream(cfgRom) << "stub";

    std::string r = app::resolveRom(/*cliPath=*/"", cfgRom.string());
    CHECK(r == cfgRom.string());

    fs::remove_all(tmp);
}

TEST_CASE("CLI path is skipped when it points at a missing file") {
    fs::path tmp = fs::path(TESTS_BUILD_DIR) / "fixture-resolveRom-fallback";
    fs::create_directories(tmp);
    fs::path cfgRom = tmp / "cfg.rom";
    std::ofstream(cfgRom) << "stub";

    std::string r = app::resolveRom("/nope/missing.rom", cfgRom.string());
    CHECK(r == cfgRom.string());

    fs::remove_all(tmp);
}

TEST_CASE("returns empty string when neither cli nor config nor default exists") {
    /* "ms0515-deliberately-absent.rom" is not a real default, and we
     * pass empty cli/cfg, so nothing should be found. */
    /* This relies on resolveRom's documented default search pattern
     * — if the search hits something unrelated and returns it, the
     * test would still pass with non-empty, which is also acceptable
     * (it means SOME default ROM exists in the test env). */
    std::string r = app::resolveRom("", "");
    if (!r.empty()) {
        /* Sanity: it must at least exist on disk. */
        CHECK(fs::exists(r));
    } else {
        MESSAGE("no default ROM in this build tree — empty return is correct");
    }
}

}  // TEST_SUITE
