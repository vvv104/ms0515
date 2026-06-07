/*
 * test_dir_vs_os.cpp — cross-check the ms0515_disk tool against the real OS
 * running in the emulator (the authoritative oracle).
 *
 *  1. DIR vs OS: boot each reference disk, run the OS's own `DIR`, and compare
 *     the file list + block sizes to what ms0515::disk parses from the same
 *     image.  Agreement proves our directory geometry matches the OS — for
 *     single- AND double-sided (track-interleaved) images.
 *  2. Content oracle: have the OS INIT a blank and PIP a real multi-block file
 *     onto it, then assert the file the tool extracts from the fresh copy is
 *     byte-for-byte identical to the one it extracts from the original disk.
 *     Only a correct LBN→byte geometry makes them match.
 */

#include <doctest/doctest.h>

#include <ms0515/Emulator.hpp>
#include <ms0515/Terminal.hpp>
#include <ms0515/VramMirror.hpp>
#include <ms0515/disk/Build.hpp>
#include <ms0515/disk/Image.hpp>

#include "test_disk.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <string>
#include <system_error>
#include <vector>

#ifndef ASSETS_DIR
#error "ASSETS_DIR must be defined by the build system"
#endif
#ifndef TESTS_DIR
#error "TESTS_DIR must be defined by the build system"
#endif

namespace {

constexpr const char *kRomA = ASSETS_DIR "/rom/ms0515-roma.rom";
constexpr const char *kRomB = ASSETS_DIR "/rom/ms0515-romb.rom";

void stepFrames(ms0515::Emulator &emu, ms0515::VramMirror &mirror, int n)
{
    for (int i = 0; i < n; ++i) { (void)emu.stepFrame(); mirror.flushFrame(); }
}

void tap(ms0515::Emulator &emu, ms0515::VramMirror &mirror, ms0515::Key key)
{
    emu.keyPress(key, true);
    stepFrames(emu, mirror, 2);
    emu.keyPress(key, false);
    stepFrames(emu, mirror, 8);
}

void shiftTap(ms0515::Emulator &emu, ms0515::VramMirror &mirror, ms0515::Key key)
{
    using K = ms0515::Key;
    emu.keyPress(K::ShiftL, true);
    stepFrames(emu, mirror, 2);
    emu.keyPress(key, true);
    stepFrames(emu, mirror, 2);
    emu.keyPress(key, false);
    stepFrames(emu, mirror, 2);
    emu.keyPress(K::ShiftL, false);
    stepFrames(emu, mirror, 8);
}

/* Idle on terminal output (VRAM history). */
void waitForIdle(ms0515::Emulator &emu, ms0515::VramMirror &mirror,
                 int quiet, int cap)
{
    int q = 0;
    for (int i = 0; i < cap; ++i) {
        const size_t before = mirror.history().size();
        (void)emu.stepFrame(); mirror.flushFrame();
        if (mirror.history().size() == before) ++q; else q = 0;
        if (q >= quiet) return;
    }
}

/* Idle on floppy I/O.  Disk-heavy commands (PIP, INIT) do long FDC stretches
 * with NO terminal output, so a VRAM-idle check reports "done" mid-operation
 * and the image is read half-written (entry still tentative). */
void waitForDiskIdle(ms0515::Emulator &emu, ms0515::VramMirror &mirror,
                     int quiet, int cap)
{
    int q = 0;
    for (int i = 0; i < cap; ++i) {
        bool active = false;
        for (int u = 0; u < 4; ++u) if (emu.diskActive(u)) active = true;
        (void)emu.stepFrame(); mirror.flushFrame();
        if (active) q = 0; else ++q;
        if (q >= quiet) return;
    }
}

ms0515::Key letterKey(char c)
{
    using K = ms0515::Key;
    static constexpr K letters[26] = {
        K::A, K::B, K::C, K::D, K::E, K::F, K::G, K::H, K::I, K::J, K::K,
        K::L, K::M, K::N, K::O, K::P, K::Q, K::R, K::S, K::T, K::U, K::V,
        K::W, K::X, K::Y, K::Z,
    };
    return letters[c - 'A'];
}

void typeLine(ms0515::Emulator &emu, ms0515::VramMirror &mirror, const char *s)
{
    using K = ms0515::Key;
    static constexpr K digits[10] = {
        K::Digit0, K::Digit1, K::Digit2, K::Digit3, K::Digit4,
        K::Digit5, K::Digit6, K::Digit7, K::Digit8, K::Digit9,
    };
    for (const char *p = s; *p; ++p) {
        char c = *p;
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
        if (c >= 'A' && c <= 'Z') tap(emu, mirror, letterKey(c));
        else if (c >= '0' && c <= '9') tap(emu, mirror, digits[c - '0']);
        else if (c == ' ')        tap(emu, mirror, K::Space);
        else if (c == ':')        tap(emu, mirror, K::ColonStar);
        else if (c == '.')        tap(emu, mirror, K::Period);
        else if (c == '/')        tap(emu, mirror, K::Slash);
        else if (c == '=')        shiftTap(emu, mirror, K::MinusEq);
    }
    tap(emu, mirror, K::Return);
}

std::vector<std::string> screenRows(const ms0515::Emulator &emu)
{
    ms0515::Terminal term;
    auto snap = term.decode(emu);
    std::vector<std::string> rows;
    for (int r = 0; r < ms0515::Terminal::kRows; ++r) {
        std::string row = snap.row(r);
        while (!row.empty() && row.back() == ' ') row.pop_back();
        rows.push_back(row);
    }
    return rows;
}

/* Pull (FILENAME.EXT -> blocks) pairs out of RT-11 DIR text. */
std::map<std::string, int> parseOsDir(const std::vector<std::string> &rows)
{
    std::map<std::string, int> out;
    const std::regex re(R"(([A-Z0-9$]{1,6}) *\.([A-Z0-9$ ]{1,3}) +([0-9]+))");
    for (const auto &row : rows) {
        for (std::sregex_iterator it(row.begin(), row.end(), re), end;
             it != end; ++it) {
            std::string name = (*it)[1].str();
            std::string ext  = (*it)[2].str();
            ext.erase(ext.find_last_not_of(' ') + 1);
            const int blocks = std::stoi((*it)[3].str());
            out[ext.empty() ? name : name + "." + ext] = blocks;
        }
    }
    return out;
}

std::map<std::string, int> parseToolDir(const std::string &imagePath)
{
    std::map<std::string, int> out;
    auto img = ms0515::disk::loadImage(imagePath);
    if (img && img->hasDirectory)
        for (const auto &e : img->directory.permanentFiles())
            out[e.name] = e.length;
    return out;
}

void dumpRows(const char *label, const std::vector<std::string> &rows)
{
    std::fprintf(stderr, "--- %s ---\n", label);
    for (size_t r = 0; r < rows.size(); ++r)
        if (!rows[r].empty())
            std::fprintf(stderr, "  %2zu| %s\n", r, rows[r].c_str());
    std::fprintf(stderr, "----------------\n");
}

std::vector<uint8_t> readFileBytes(const std::string &path)
{
    std::ifstream f(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),
                                std::istreambuf_iterator<char>());
}

void writeImage(const std::string &path, const std::vector<uint8_t> &image)
{
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char *>(image.data()),
            static_cast<std::streamsize>(image.size()));
}

struct DiskConfig {
    const char *disk;   /* file under TESTS_DIR/disks/ */
    const char *rom;    /* ROM image */
    const char *name;   /* subcase label */
};

constexpr DiskConfig kConfigs[] = {
    {"test_osa.dsk",   kRomA, "OSA"},
    {"test_omega.dsk", kRomB, "Omega"},
    {"test_mihin.dsk", kRomB, "Mihin"},
    {"test_rod.dsk",   kRomA, "RT-15SJ"},
};

void runDirCheck(const DiskConfig &cfg,
                 std::map<std::string, int> &osDir,
                 std::map<std::string, int> &toolDir)
{
    const std::string disk = std::string(TESTS_DIR) + "/disks/" + cfg.disk;
    ms0515_test::TempDisk td{disk};
    ms0515::Emulator emu;
    REQUIRE(emu.loadRomFile(cfg.rom));
    REQUIRE(emu.mountDisk(0, td.path().string()));
    /* A double-sided dump needs the upper-side unit mounted too. */
    std::error_code ec;
    if (std::filesystem::file_size(td.path(), ec) == ms0515::disk::kDoubleSize)
        REQUIRE(emu.mountDisk(2, td.path().string()));

    ms0515::VramMirror mirror;
    mirror.attach(emu);
    mirror.setOutput(nullptr);
    emu.reset();

    waitForIdle(emu, mirror, /*quiet=*/120, /*cap=*/3500);
    typeLine(emu, mirror, "DIR");
    waitForIdle(emu, mirror, /*quiet=*/120, /*cap=*/4000);

    auto rows = screenRows(emu);
    osDir   = parseOsDir(rows);
    toolDir = parseToolDir(td.path().string());
    if (osDir != toolDir) dumpRows(cfg.name, rows);
}

}  /* namespace */

TEST_SUITE("DirVsOs") {

TEST_CASE("OS DIR matches ms0515_disk parse across reference disks") {
    for (const auto &cfg : kConfigs) {
        SUBCASE(cfg.name) {
            const std::string label = cfg.name;
            std::map<std::string, int> osDir, toolDir;
            runDirCheck(cfg, osDir, toolDir);

            CHECK_MESSAGE(!toolDir.empty(), label << ": tool found no files");
            CHECK_MESSAGE(!osDir.empty(),   label << ": OS DIR produced nothing");
            CHECK_MESSAGE(osDir == toolDir,
                          label << ": OS DIR vs tool file-list/size mismatch");
        }
    }
}

/* buildVolume must produce exactly what the OS's INIT writes, so a built disk
 * is mountable AND writable by the OS (PIP into our blank used to HALT because
 * our format diverged).  All RT-11 variants here INIT identically except the
 * volume/system-ID strings (OSA/Omega/Mihin use "RT11A"/"DECRT11A", which is
 * what buildVolume writes; rod/FODOS uses "ФОДОС"), so we compare against OSA. */
TEST_CASE("buildVolume is byte-identical to the OS's INIT (OSA)") {
    namespace disk = ms0515::disk;
    const std::string sysPath = std::string(TESTS_DIR) + "/disks/test_osa.dsk";
    const std::string dstPath = std::string(TESTS_BUILD_DIR) + "/init_blank.dsk";

    ms0515_test::TempDisk sys{sysPath};
    std::vector<uint8_t> raw(disk::kSideSize);
    for (std::size_t i = 0; i < raw.size(); ++i) raw[i] = (i & 1) ? 0x6D : 0xB6;
    writeImage(dstPath, raw);

    ms0515::Emulator emu; REQUIRE(emu.loadRomFile(kRomA));
    REQUIRE(emu.mountDisk(0, sys.path().string()));
    REQUIRE(emu.mountDisk(3, dstPath));
    ms0515::VramMirror mirror; mirror.attach(emu); mirror.setOutput(nullptr);
    emu.reset(); waitForIdle(emu, mirror, 120, 3500);
    typeLine(emu, mirror, "INIT DZ3:"); waitForIdle(emu, mirror, 80, 2500);
    typeLine(emu, mirror, "Y");
    waitForDiskIdle(emu, mirror, 200, 60000); waitForIdle(emu, mirror, 150, 6000);

    auto osInit = readFileBytes(dstPath);
    auto built  = disk::blankImage(false);
    disk::initVolume(built, 0, false);
    REQUIRE(osInit.size() == built.size());
    if (osInit != built) {
        for (std::size_t i = 0; i < built.size(); ++i)
            if (osInit[i] != built[i]) {
                std::fprintf(stderr, "first diff @0x%zX: OS=%02X built=%02X\n",
                             i, osInit[i], built[i]);
                break;
            }
    }
    CHECK(osInit == built);
}

/* Same check for a double-sided dump: the OS INITs each side independently
 * (DZ1: lower, DZ3: upper of the second drive), and buildDoubleSided must
 * reproduce both sides at their track-interleaved offsets byte-for-byte. */
TEST_CASE("buildDoubleSided is byte-identical to the OS's INIT of both sides (OSA)") {
    namespace disk = ms0515::disk;
    const std::string sysPath = std::string(TESTS_DIR) + "/disks/test_osa.dsk";
    const std::string dstPath = std::string(TESTS_BUILD_DIR) + "/init_ds.dsk";

    ms0515_test::TempDisk sys{sysPath};
    std::vector<uint8_t> raw(disk::kDoubleSize);
    for (std::size_t i = 0; i < raw.size(); ++i) raw[i] = (i & 1) ? 0x6D : 0xB6;
    writeImage(dstPath, raw);

    ms0515::Emulator emu; REQUIRE(emu.loadRomFile(kRomA));
    REQUIRE(emu.mountDisk(0, sys.path().string()));
    REQUIRE(emu.mountDisk(1, dstPath));        /* DZ1: side 0 */
    REQUIRE(emu.mountDisk(3, dstPath));        /* DZ3: side 1 */
    ms0515::VramMirror mirror; mirror.attach(emu); mirror.setOutput(nullptr);
    emu.reset(); waitForIdle(emu, mirror, 120, 3500);

    for (const char *dev : {"DZ1:", "DZ3:"}) {
        typeLine(emu, mirror, (std::string("INIT ") + dev).c_str());
        waitForIdle(emu, mirror, 80, 2500);
        typeLine(emu, mirror, "Y");
        waitForDiskIdle(emu, mirror, 200, 60000);
        waitForIdle(emu, mirror, 150, 6000);
    }

    auto osInit = readFileBytes(dstPath);
    auto built  = disk::blankImage(true);
    disk::initVolume(built, 0, true);
    disk::initVolume(built, 1, true);
    REQUIRE(osInit.size() == built.size());
    if (osInit != built) {
        for (std::size_t i = 0; i < built.size(); ++i)
            if (osInit[i] != built[i]) {
                std::fprintf(stderr, "first diff @0x%zX: OS=%02X built=%02X\n",
                             i, osInit[i], built[i]);
                break;
            }
    }
    CHECK(osInit == built);
}

/* The authoritative content oracle: the OS itself lays out the bytes, and we
 * prove the tool reads the same bytes back.  PIP copies disk-to-disk, which
 * avoids the broken TT: output path; we wait on diskActive so the copy has
 * been finalised (entry permanent) before reading the image. */
TEST_CASE("OS-oracle: extracted content is byte-exact (OSA single-sided)") {
    namespace disk = ms0515::disk;
    const std::string sysPath = std::string(TESTS_DIR) + "/disks/test_osa.dsk";
    const std::string dstPath = std::string(TESTS_BUILD_DIR) + "/oracle_blank.dsk";

    ms0515_test::TempDisk sys{sysPath};
    std::vector<uint8_t> raw(disk::kSideSize);
    for (std::size_t i = 0; i < raw.size(); ++i) raw[i] = (i & 1) ? 0x6D : 0xB6;
    writeImage(dstPath, raw);

    ms0515::Emulator emu;
    REQUIRE(emu.loadRomFile(kRomA));
    REQUIRE(emu.mountDisk(0, sys.path().string()));
    REQUIRE(emu.mountDisk(3, dstPath));        /* raw blank -> OS will INIT */
    ms0515::VramMirror mirror; mirror.attach(emu); mirror.setOutput(nullptr);
    emu.reset();
    waitForIdle(emu, mirror, 120, 3500);

    typeLine(emu, mirror, "INIT DZ3:");
    waitForIdle(emu, mirror, 80, 2500);
    typeLine(emu, mirror, "Y");                 /* "Are you sure?" */
    waitForDiskIdle(emu, mirror, 200, 60000);
    waitForIdle(emu, mirror, 150, 6000);

    typeLine(emu, mirror, "PIP DZ3:PIP.SAV=DZ0:PIP.SAV");
    waitForDiskIdle(emu, mirror, 300, 120000);
    waitForIdle(emu, mirror, 200, 6000);

    auto orig = disk::openImage(readFileBytes(sys.path().string()), 0);
    auto copy = disk::openImage(readFileBytes(dstPath), 0);
    REQUIRE(orig.has_value());
    REQUIRE(copy.has_value());
    auto a = orig->readFile("PIP.SAV");
    auto b = copy->readFile("PIP.SAV");
    REQUIRE_MESSAGE(a.size() == 15360, "original PIP.SAV not read (size " << a.size() << ")");
    REQUIRE_MESSAGE(b.size() == a.size(),
                    "copy PIP.SAV not read — INIT/PIP failed? (size " << b.size() << ")");
    CHECK_MESSAGE(a == b, "tool extract differs from the OS's own copy");
}

TEST_CASE("OS-oracle: removeFile is the same as the OS's own DELETE (OSA)") {
    /* Build a volume from scratch with several files of varying sizes via the
     * disk library's primitives, then remove one from the MIDDLE.  Boot the
     * OS, run `DIR DZ3:`, and confirm the OS sees exactly the files the tool
     * still lists — same names, same lengths, no tail loss.  This is the
     * regression guard for the disk1.dsk bug (rm+put of LINK.SAV ate the five
     * directory entries that followed). */
    namespace disk = ms0515::disk;
    const std::string sysPath = std::string(TESTS_DIR) + "/disks/test_osa.dsk";
    const std::string dstPath = std::string(TESTS_BUILD_DIR) + "/oracle_rm.dsk";

    ms0515_test::TempDisk sys{sysPath};

    /* Build a fresh single-sided volume with five distinguishable files. */
    auto img = disk::blankImage(false);
    disk::initVolume(img, 0, false);
    struct F { std::string name; std::vector<uint8_t> data; };
    const std::vector<F> files = {
        {"A.DAT", std::vector<uint8_t>(  1 * disk::kBlock, 0xA1)},   /* 1 block */
        {"B.DAT", std::vector<uint8_t>(  3 * disk::kBlock, 0xB2)},   /* 3 blocks */
        {"VICTIM.X", std::vector<uint8_t>(2 * disk::kBlock, 0xCC)},  /* 2 blocks — to remove */
        {"D.DAT", std::vector<uint8_t>(  4 * disk::kBlock, 0xD4)},   /* 4 blocks */
        {"E.DAT", std::vector<uint8_t>(  1 * disk::kBlock, 0xE5)},   /* 1 block */
    };
    for (const auto &f : files) disk::putFile(img, 0, false, f.name, f.data);

    /* Remove the middle entry; expect the tail (D, E) to survive untouched. */
    disk::removeFile(img, 0, false, "VICTIM.X");
    writeImage(dstPath, img);

    /* Tool's view of the post-rm directory. */
    std::map<std::string, int> toolDir;
    {
        auto im = disk::openImage(img, 0);
        REQUIRE(im.has_value());
        for (const auto &e : im->directory.permanentFiles())
            toolDir[e.name] = e.length;
    }
    REQUIRE(toolDir.count("VICTIM.X") == 0);
    REQUIRE(toolDir.size() == 4);

    /* Boot the OS, scrape DIR DZ3:, compare to what the tool reports. */
    ms0515::Emulator emu;
    REQUIRE(emu.loadRomFile(kRomA));
    REQUIRE(emu.mountDisk(0, sys.path().string()));
    REQUIRE(emu.mountDisk(3, dstPath));
    ms0515::VramMirror mirror; mirror.attach(emu); mirror.setOutput(nullptr);
    emu.reset();
    waitForIdle(emu, mirror, 120, 3500);

    typeLine(emu, mirror, "DIR DZ3:");
    waitForIdle(emu, mirror, 150, 6000);
    auto rows = screenRows(emu);
    auto osDir = parseOsDir(rows);

    if (osDir != toolDir) dumpRows("after removeFile", rows);
    CHECK_MESSAGE(osDir == toolDir,
                  "OS DIR vs tool dir mismatch after removeFile");
    CHECK_MESSAGE(osDir.count("VICTIM.X") == 0,
                  "OS still sees the removed file");

    /* The freed slot must be REUSABLE by the OS itself.  Have PIP copy a
     * fresh file there; we'd expect it to land in VICTIM.X's old data
     * blocks (the slot empties out at 2 blocks free, PIP.SAV is 30 — so
     * PIP would refuse with no space if the slot survived as empty.  Use
     * a small substitute and check the tool then sees it). */
    typeLine(emu, mirror, "PIP DZ3:NEW.X=DZ0:TT.SYS");
    waitForDiskIdle(emu, mirror, 200, 60000);
    waitForIdle(emu, mirror, 150, 6000);

    auto after = disk::openImage(readFileBytes(dstPath), 0);
    REQUIRE(after.has_value());
    const disk::DirEntry *ne = after->directory.find("NEW.X");
    REQUIRE_MESSAGE(ne != nullptr, "OS could not PIP into the freed slot");
    CHECK(ne->length > 0);
}

TEST_CASE("OS-oracle: squeeze leaves a volume the OS reads correctly (OSA)") {
    /* Build a disk with files + a few holes, run our squeeze, then boot the
     * OS and confirm DIR DZ3 lists exactly the same files our tool does.
     * Tail-empty consolidation is then probed by having the OS PIP a large
     * file that wouldn't have fit any single old hole. */
    namespace disk = ms0515::disk;
    const std::string sysPath = std::string(TESTS_DIR) + "/disks/test_osa.dsk";
    const std::string dstPath = std::string(TESTS_BUILD_DIR) + "/oracle_squeeze.dsk";

    ms0515_test::TempDisk sys{sysPath};

    auto img = disk::blankImage(false);
    disk::initVolume(img, 0, false);
    const std::vector<std::pair<std::string, int>> files = {
        {"A.X", 3}, {"B.X", 2}, {"C.X", 5}, {"D.X", 1}, {"E.X", 4},
    };
    for (const auto &[n, len] : files)
        disk::putFile(img, 0, false, n,
                      std::vector<uint8_t>(len * disk::kBlock,
                                           static_cast<uint8_t>(n[0])));
    disk::removeFile(img, 0, false, "B.X");
    disk::removeFile(img, 0, false, "D.X");                /* two holes */

    disk::squeeze(img, 0, false);
    writeImage(dstPath, img);

    std::map<std::string, int> toolDir;
    {
        auto im = disk::openImage(img, 0);
        REQUIRE(im.has_value());
        for (const auto &e : im->directory.permanentFiles())
            toolDir[e.name] = e.length;
    }
    REQUIRE(toolDir.size() == 3);                          /* A, C, E */

    ms0515::Emulator emu;
    REQUIRE(emu.loadRomFile(kRomA));
    REQUIRE(emu.mountDisk(0, sys.path().string()));
    REQUIRE(emu.mountDisk(3, dstPath));
    ms0515::VramMirror mirror; mirror.attach(emu); mirror.setOutput(nullptr);
    emu.reset();
    waitForIdle(emu, mirror, 120, 3500);

    typeLine(emu, mirror, "DIR DZ3:");
    waitForIdle(emu, mirror, 150, 6000);
    auto osDir = parseOsDir(screenRows(emu));
    if (osDir != toolDir) dumpRows("after squeeze", screenRows(emu));
    CHECK_MESSAGE(osDir == toolDir, "OS DIR vs tool dir mismatch after squeeze");

    /* The consolidated free area must accept a file no single old hole would
     * have held — proof the trailing empty really covers all freed space. */
    typeLine(emu, mirror, "PIP DZ3:WIDE.X=DZ0:PIP.SAV");   /* PIP.SAV is 30 blocks */
    waitForDiskIdle(emu, mirror, 200, 60000);
    waitForIdle(emu, mirror, 150, 6000);
    auto after = disk::openImage(readFileBytes(dstPath), 0);
    REQUIRE(after.has_value());
    REQUIRE_MESSAGE(after->directory.find("WIDE.X") != nullptr,
                    "OS could not write into the consolidated free area");
}

TEST_CASE("OS-oracle: rm of the only file leaves a volume the OS can refill (OSA)") {
    /* Edge case: remove the SOLE permanent file.  The directory now holds two
     * adjacent empty entries (the freed slot + the original residual free
     * area).  The OS must still see the volume as initialised, list zero
     * files in DIR DZ3:, and accept a fresh PIP into it. */
    namespace disk = ms0515::disk;
    const std::string sysPath = std::string(TESTS_DIR) + "/disks/test_osa.dsk";
    const std::string dstPath = std::string(TESTS_BUILD_DIR) + "/oracle_rm_solo.dsk";

    ms0515_test::TempDisk sys{sysPath};
    auto img = disk::blankImage(false);
    disk::initVolume(img, 0, false);
    disk::putFile(img, 0, false, "SOLO.X",
                  std::vector<uint8_t>(5 * disk::kBlock, 0x77));
    disk::removeFile(img, 0, false, "SOLO.X");
    writeImage(dstPath, img);

    {
        auto im = disk::openImage(img, 0);
        REQUIRE(im.has_value());
        REQUIRE(im->directory.permanentFiles().empty());
    }

    ms0515::Emulator emu;
    REQUIRE(emu.loadRomFile(kRomA));
    REQUIRE(emu.mountDisk(0, sys.path().string()));
    REQUIRE(emu.mountDisk(3, dstPath));
    ms0515::VramMirror mirror; mirror.attach(emu); mirror.setOutput(nullptr);
    emu.reset();
    waitForIdle(emu, mirror, 120, 3500);

    typeLine(emu, mirror, "DIR DZ3:");
    waitForIdle(emu, mirror, 150, 6000);
    auto osDir = parseOsDir(screenRows(emu));
    CHECK_MESSAGE(osDir.empty(),
                  "OS DIR after rm-only-file should list no permanent files");

    /* OS still trusts the volume enough to PIP into it — that's only true
     * when the chain-of-empties left by removeFile is well-formed. */
    typeLine(emu, mirror, "PIP DZ3:REUSE.X=DZ0:TT.SYS");
    waitForDiskIdle(emu, mirror, 200, 60000);
    waitForIdle(emu, mirror, 150, 6000);

    auto after = disk::openImage(readFileBytes(dstPath), 0);
    REQUIRE(after.has_value());
    const disk::DirEntry *re = after->directory.find("REUSE.X");
    REQUIRE_MESSAGE(re != nullptr,
                    "OS could not PIP onto a volume whose only file we removed");
    CHECK(re->length > 0);
}

} /* TEST_SUITE */
