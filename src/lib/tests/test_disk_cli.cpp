/*
 * test_disk_cli.cpp — integration tests for the ms0515-disk CLI binary.
 *
 * The lib-level unit tests in src/disk/tests cover the C++ API directly;
 * these tests sit on top of the BINARY and exercise the round-trip
 * behaviour that's exclusive to the CLI (host file mtime <-> directory
 * date, argument parsing, exit codes).  They subprocess `ms0515-disk`
 * the way a user would.
 */

#include <doctest/doctest.h>

#include <ms0515/disk/Build.hpp>
#include <ms0515/disk/Image.hpp>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <regex>
#include <string>
#include <vector>

#ifndef MS0515_DISK_BIN
#error "MS0515_DISK_BIN must be defined by the build system"
#endif
#ifndef TESTS_BUILD_DIR
#error "TESTS_BUILD_DIR must be defined by the build system"
#endif

namespace fs = std::filesystem;

namespace {

const fs::path kDiskBin = MS0515_DISK_BIN;

/* Build the per-test scratch dir and wipe whatever was there from a previous
 * run, so each test starts with predictable filenames. */
fs::path scratch(const std::string &tag)
{
    fs::path p = fs::path(TESTS_BUILD_DIR) / ("cli_" + tag);
    std::error_code ec;
    fs::remove_all(p, ec);
    fs::create_directories(p, ec);
    return p;
}

/* Run ms0515-disk with the given args; return exit code and captured stdout.
 * We surface stderr to the test log so an unexpected failure isn't silent. */
struct Run { int code; std::string out; };

Run runDisk(const std::vector<std::string> &args)
{
    std::string cmd = "\"" + kDiskBin.string() + "\"";
    for (const auto &a : args) cmd += " \"" + a + "\"";
    const fs::path outFile = fs::path(TESTS_BUILD_DIR) / "cli_run.out";
    /* cmd.exe strips the outermost pair of quotes around the whole command,
     * so when the binary path itself is quoted we need an extra wrapping
     * layer; see CRT docs for system().  On non-Windows the extra "" pair
     * around the command is harmless. */
    const std::string inner = cmd + " > \"" + outFile.string() + "\" 2>&1";
    const std::string fullCmd =
#ifdef _WIN32
        "\"" + inner + "\"";
#else
        inner;
#endif
    const int code = std::system(fullCmd.c_str());
    std::ifstream f(outFile);
    return {code, std::string(std::istreambuf_iterator<char>(f),
                              std::istreambuf_iterator<char>())};
}

/* Set the host mtime on `p` to (year, month, day) at noon local time, so
 * tests don't fight DST or timezone-day-roll edge cases. */
void setMtime(const fs::path &p, int year, int month, int day)
{
    namespace ch = std::chrono;
    const ch::year_month_day ymd{ch::year{year},
                                 ch::month{static_cast<unsigned>(month)},
                                 ch::day  {static_cast<unsigned>(day)}};
    REQUIRE(ymd.ok());
    const auto sys = ch::time_point_cast<ch::system_clock::duration>(
        ch::sys_days{ymd} + ch::hours{12});
    /* system_clock -> file_clock without clock_cast (apple-clang's libc++
     * lacks it): the now()-offset bridge, matching ms0515-disk. */
    const auto ft = ch::time_point_cast<fs::file_time_type::duration>(
        sys - ch::system_clock::now() + fs::file_time_type::clock::now());
    fs::last_write_time(p, ft);
}

/* Decode the host mtime of `p` back to (year, month, day). */
ms0515::disk::DateParts mtimeOf(const fs::path &p)
{
    namespace ch = std::chrono;
    const auto ft  = fs::last_write_time(p);
    /* file_clock -> system_clock without clock_cast (see setMtime). */
    const auto sys = ch::time_point_cast<ch::system_clock::duration>(
        ft - fs::file_time_type::clock::now() + ch::system_clock::now());
    const auto dp  = ch::floor<ch::days>(sys);
    const ch::year_month_day ymd{dp};
    return {int(ymd.year()),
            static_cast<int>(unsigned(ymd.month())),
            static_cast<int>(unsigned(ymd.day()))};
}

/* Read the directory date of `name` (side 0) from the image, decoded as
 * (year, month, day). */
ms0515::disk::DateParts diskDate(const fs::path &image, const std::string &name)
{
    auto img = ms0515::disk::loadImage(image.string());
    REQUIRE(img.has_value());
    const auto *e = img->directory.find(name);
    REQUIRE(e != nullptr);
    return ms0515::disk::decodeDate(e->date);
}

/* Tiny single-sided image with one file already on it.  Used by tests that
 * want to start from a populated volume. */
fs::path makePopulatedDisk(const fs::path &dir)
{
    auto img = ms0515::disk::blankImage(false);
    ms0515::disk::initVolume(img, 0, false);
    ms0515::disk::putFile(img, 0, false, "F.X",
                          std::vector<uint8_t>(ms0515::disk::kBlock, 0xAA));
    const fs::path p = dir / "vol.dsk";
    std::ofstream(p, std::ios::binary)
        .write(reinterpret_cast<const char *>(img.data()),
               static_cast<std::streamsize>(img.size()));
    return p;
}

}  /* namespace */

TEST_SUITE("DiskCli") {

TEST_CASE("put without --date stamps the host file's mtime onto the entry") {
    using namespace ms0515::disk;
    const auto dir = scratch("put_mtime");
    const fs::path host = dir / "F.X";
    std::ofstream(host, std::ios::binary)
        .write(std::string(ms0515::disk::kBlock, 'A').data(), ms0515::disk::kBlock);
    setMtime(host, 1995, 4, 1);

    auto img = blankImage(false);
    initVolume(img, 0, false);
    const fs::path dsk = dir / "out.dsk";
    std::ofstream(dsk, std::ios::binary)
        .write(reinterpret_cast<const char *>(img.data()),
               static_cast<std::streamsize>(img.size()));

    REQUIRE(runDisk({"put", dsk.string(), host.string()}).code == 0);
    const auto dp = diskDate(dsk, "F.X");
    CHECK(dp.year  == 1995);
    CHECK(dp.month == 4);
    CHECK(dp.day   == 1);
}

TEST_CASE("put --date overrides the host mtime") {
    using namespace ms0515::disk;
    const auto dir = scratch("put_explicit");
    const fs::path host = dir / "F.X";
    std::ofstream(host, std::ios::binary)
        .write(std::string(ms0515::disk::kBlock, 'A').data(), ms0515::disk::kBlock);
    setMtime(host, 2020, 6, 15);

    auto img = blankImage(false);
    initVolume(img, 0, false);
    const fs::path dsk = dir / "out.dsk";
    std::ofstream(dsk, std::ios::binary)
        .write(reinterpret_cast<const char *>(img.data()),
               static_cast<std::streamsize>(img.size()));

    REQUIRE(runDisk({"put", dsk.string(),
                     "--date", "1994-02-18", host.string()}).code == 0);
    const auto dp = diskDate(dsk, "F.X");
    CHECK(dp.year  == 1994);
    CHECK(dp.month == 2);
    CHECK(dp.day   == 18);
}

TEST_CASE("get stamps the disk's directory date onto the extracted file") {
    using namespace ms0515::disk;
    const auto dir = scratch("get_mtime");
    auto img = blankImage(false);
    initVolume(img, 0, false);
    PutOptions opts;
    opts.date = encodeDate(1995, 4, 1);
    putFile(img, 0, false, "F.X", std::vector<uint8_t>(ms0515::disk::kBlock, 0xAA), opts);
    const fs::path dsk = dir / "in.dsk";
    std::ofstream(dsk, std::ios::binary)
        .write(reinterpret_cast<const char *>(img.data()),
               static_cast<std::streamsize>(img.size()));

    const fs::path outdir = dir / "extracted";
    fs::create_directories(outdir);
    REQUIRE(runDisk({"get", dsk.string(),
                     "--out", outdir.string(), "F.X"}).code == 0);

    const auto dp = mtimeOf(outdir / "F.X");
    CHECK(dp.year  == 1995);
    CHECK(dp.month == 4);
    CHECK(dp.day   == 1);
}

TEST_CASE("get + put round-trips the date without an explicit --date") {
    using namespace ms0515::disk;
    const auto dir = scratch("roundtrip");
    auto img = blankImage(false);
    initVolume(img, 0, false);
    PutOptions opts;
    opts.date = encodeDate(1994, 2, 18);
    putFile(img, 0, false, "F.X", std::vector<uint8_t>(ms0515::disk::kBlock, 0xAA), opts);
    const fs::path src = dir / "src.dsk";
    std::ofstream(src, std::ios::binary)
        .write(reinterpret_cast<const char *>(img.data()),
               static_cast<std::streamsize>(img.size()));

    const fs::path outdir = dir / "extract";
    fs::create_directories(outdir);
    REQUIRE(runDisk({"get", src.string(),
                     "--out", outdir.string(), "F.X"}).code == 0);

    auto fresh = blankImage(false);
    initVolume(fresh, 0, false);
    const fs::path dst = dir / "dst.dsk";
    std::ofstream(dst, std::ios::binary)
        .write(reinterpret_cast<const char *>(fresh.data()),
               static_cast<std::streamsize>(fresh.size()));
    REQUIRE(runDisk({"put", dst.string(), (outdir / "F.X").string()}).code == 0);

    const auto dp = diskDate(dst, "F.X");
    CHECK(dp.year  == 1994);
    CHECK(dp.month == 2);
    CHECK(dp.day   == 18);
}

TEST_CASE("setdate rewrites the entry's date in place") {
    using namespace ms0515::disk;
    const auto dir = scratch("setdate");
    const fs::path dsk = makePopulatedDisk(dir);

    REQUIRE(runDisk({"setdate", dsk.string(),
                     "--date", "1999-12-31", "F.X"}).code == 0);
    const auto dp = diskDate(dsk, "F.X");
    CHECK(dp.year  == 1999);
    CHECK(dp.month == 12);
    CHECK(dp.day   == 31);
}

TEST_CASE("protect / unprotect toggle the /PROTECT flag in place") {
    using namespace ms0515::disk;
    const auto dir = scratch("protect");
    const fs::path dsk = makePopulatedDisk(dir);

    REQUIRE(runDisk({"protect", dsk.string(), "F.X"}).code == 0);
    {
        auto im = loadImage(dsk.string());
        REQUIRE(im.has_value());
        const auto *e = im->directory.find("F.X");
        REQUIRE(e != nullptr);
        CHECK((e->status & kStatusProtected) != 0);
    }

    REQUIRE(runDisk({"unprotect", dsk.string(), "F.X"}).code == 0);
    {
        auto im = loadImage(dsk.string());
        REQUIRE(im.has_value());
        const auto *e = im->directory.find("F.X");
        REQUIRE(e != nullptr);
        CHECK((e->status & kStatusProtected) == 0);
    }
}

TEST_CASE("rm + put again from the freed slot keeps the volume intact") {
    using namespace ms0515::disk;
    const auto dir = scratch("rm_put");
    auto img = blankImage(false);
    initVolume(img, 0, false);
    PutOptions opts; opts.date = encodeDate(1994, 2, 18);
    for (const char *n : {"A.X", "B.X", "C.X"})
        putFile(img, 0, false, n, std::vector<uint8_t>(ms0515::disk::kBlock, 0x11), opts);
    const fs::path dsk = dir / "vol.dsk";
    std::ofstream(dsk, std::ios::binary)
        .write(reinterpret_cast<const char *>(img.data()),
               static_cast<std::streamsize>(img.size()));

    REQUIRE(runDisk({"rm", dsk.string(), "B.X"}).code == 0);

    const fs::path replacement = dir / "B.X";
    std::ofstream(replacement, std::ios::binary)
        .write(std::string(ms0515::disk::kBlock, 'Q').data(), ms0515::disk::kBlock);
    setMtime(replacement, 1995, 4, 1);
    REQUIRE(runDisk({"put", dsk.string(), replacement.string()}).code == 0);

    auto im = loadImage(dsk.string());
    REQUIRE(im.has_value());
    CHECK(im->directory.permanentFiles().size() == 3);
    const auto *e = im->directory.find("B.X");
    REQUIRE(e != nullptr);
    const auto dp = decodeDate(e->date);
    CHECK(dp.year == 1995);   /* mtime carried through */
}

TEST_CASE("dir output exposes the date and /PROTECT marker") {
    using namespace ms0515::disk;
    const auto dir = scratch("dir_out");
    auto img = blankImage(false);
    initVolume(img, 0, false);
    PutOptions opts;
    opts.date     = encodeDate(1994, 2, 18);
    opts.readOnly = true;
    putFile(img, 0, false, "P.X", std::vector<uint8_t>(ms0515::disk::kBlock, 0xCC), opts);
    const fs::path dsk = dir / "dir.dsk";
    std::ofstream(dsk, std::ios::binary)
        .write(reinterpret_cast<const char *>(img.data()),
               static_cast<std::streamsize>(img.size()));

    const auto run = runDisk({"dir", dsk.string()});
    REQUIRE(run.code == 0);
    CHECK_MESSAGE(run.out.find("date=1994-02-18") != std::string::npos,
                  "dir output does not include the date: " << run.out);
    CHECK_MESSAGE(run.out.find("[P]") != std::string::npos,
                  "dir output does not include the /PROTECT marker: " << run.out);
}

}  /* TEST_SUITE */
