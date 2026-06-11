#include <doctest/doctest.h>

#include <ms0515/disk/FolderVolume.hpp>
#include <ms0515/disk/Image.hpp>
#include <ms0515/disk/Layout.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace ms0515::disk;
namespace fs = std::filesystem;

#ifndef TESTS_BUILD_DIR
#error "TESTS_BUILD_DIR must be defined by the build system"
#endif

namespace {

fs::path freshDir(const char *name)
{
    fs::path d = fs::path(TESTS_BUILD_DIR) / "rtfs-fixtures" / name;
    fs::remove_all(d);
    fs::create_directories(d);
    return d;
}

void writeFile(const fs::path &p, const std::string &content)
{
    std::ofstream(p, std::ios::binary).write(content.data(),
        static_cast<std::streamsize>(content.size()));
}

fs::path writeDescriptor(const fs::path &dir, int blocks = 100)
{
    fs::path p = dir / "device.rtfs";
    writeFile(p, "device: hd\nblocks: " + std::to_string(blocks) + "\n");
    return p;
}

/* Assemble the whole virtual device into a buffer so the proven linear
 * Image/Directory reader can audit what the volume serves. */
std::vector<uint8_t> assemble(FolderVolume &vol)
{
    std::vector<uint8_t> img(static_cast<std::size_t>(vol.blocks()) * kBlock);
    for (int lbn = 0; lbn < vol.blocks(); ++lbn)
        vol.readBlock(lbn, img.data() + static_cast<std::size_t>(lbn) * kBlock);
    return img;
}

}  /* namespace */

TEST_SUITE("FolderVolume") {

TEST_CASE("open auto-fills an empty descriptor and serves a valid volume") {
    auto dir = freshDir("autofill");
    writeFile(dir / "swap.sys", std::string(600, 'S'));      /* 2 blocks */
    writeFile(dir / "hello.txt", "hello rtfs");              /* 1 block  */
    auto descPath = writeDescriptor(dir);

    std::string err;
    auto vol = FolderVolume::open(descPath.string(), &err);
    REQUIRE_MESSAGE(vol != nullptr, err);
    CHECK(vol->blocks() == 100);
    CHECK(vol->deviceType() == RtfsDescriptor::Device::Hd);

    /* Descriptor got auto-filled and saved (SWAP.SYS pinned first). */
    REQUIRE(vol->descriptor().files.size() == 2);
    CHECK(vol->descriptor().files[0].rt11Name == "SWAP.SYS");
    CHECK(vol->descriptor().files[1].rt11Name == "HELLO.TXT");
    std::ifstream back(descPath);
    std::string text(std::istreambuf_iterator<char>(back), {});
    CHECK(text.find("file: SWAP.SYS | swap.sys |") != std::string::npos);

    /* The proven linear reader parses the generated volume. */
    auto im = openLinearImage(assemble(*vol));
    REQUIRE(im.has_value());
    REQUIRE(im->hasDirectory);
    auto perm = im->directory.permanentFiles();
    REQUIRE(perm.size() == 2);
    CHECK(perm[0].name == "SWAP.SYS");
    CHECK(perm[0].length == 2);
    CHECK(perm[1].name == "HELLO.TXT");
    auto data = im->readFile("HELLO.TXT");
    REQUIRE(data.size() == static_cast<std::size_t>(kBlock));
    CHECK(std::string(data.begin(), data.begin() + 10) == "hello rtfs");
}

TEST_CASE("external edits and new files are visible on directory re-read") {
    auto dir = freshDir("external");
    writeFile(dir / "a.dat", "AAAA");
    auto vol = FolderVolume::open(writeDescriptor(dir).string());
    REQUIRE(vol != nullptr);
    (void)assemble(*vol);                       /* initial directory read */

    writeFile(dir / "a.dat", std::string(700, 'B'));   /* grow to 2 blocks */
    writeFile(dir / "new.txt", "fresh");               /* new host file    */

    auto im = openLinearImage(assemble(*vol));
    REQUIRE(im.has_value());
    const DirEntry *a = im->directory.find("A.DAT");
    REQUIRE(a != nullptr);
    CHECK(a->length == 2);
    CHECK(im->directory.find("NEW.TXT") != nullptr);
    auto data = im->readFile("A.DAT");
    CHECK(data[0] == 'B');
}

TEST_CASE("a vanished host file shows as NAME.BAD with zero blocks") {
    auto dir = freshDir("missing");
    writeFile(dir / "gone.dat", "payload");
    writeFile(dir / "kept.dat", "stays");
    auto vol = FolderVolume::open(writeDescriptor(dir).string());
    REQUIRE(vol != nullptr);
    (void)assemble(*vol);

    fs::remove(dir / "gone.dat");

    auto im = openLinearImage(assemble(*vol));
    REQUIRE(im.has_value());
    CHECK(im->directory.find("GONE.DAT") == nullptr);
    const DirEntry *bad = im->directory.find("GONE.BAD");
    REQUIRE(bad != nullptr);
    CHECK(bad->length == 0);
    CHECK(im->directory.find("KEPT.DAT") != nullptr);
}

TEST_CASE("guest data writes land in the host file") {
    auto dir = freshDir("write");
    writeFile(dir / "data.bin", std::string(1024, 'x'));    /* 2 blocks */
    auto vol = FolderVolume::open(writeDescriptor(dir).string());
    REQUIRE(vol != nullptr);
    (void)assemble(*vol);

    const int start = rtfsDataStart();          /* DATA.BIN is the only file */
    std::vector<uint8_t> blk(kBlock, 'Z');
    vol->writeBlock(start + 1, blk.data());

    std::ifstream f(dir / "data.bin", std::ios::binary);
    std::string content(std::istreambuf_iterator<char>(f), {});
    REQUIRE(content.size() == 1024);
    CHECK(content[0] == 'x');
    CHECK(content[512] == 'Z');
    CHECK(content[1023] == 'Z');
}

TEST_CASE("unbacked (free) blocks behave as scratch storage") {
    auto dir = freshDir("scratch");
    auto vol = FolderVolume::open(writeDescriptor(dir).string());
    REQUIRE(vol != nullptr);

    std::vector<uint8_t> blk(kBlock, 0xAB), back(kBlock, 0);
    vol->writeBlock(50, blk.data());
    vol->readBlock(50, back.data());
    CHECK(back[0] == 0xAB);
    CHECK(back[511] == 0xAB);
    vol->readBlock(51, back.data());            /* untouched reads zeros */
    CHECK(back[0] == 0);
}

} /* TEST_SUITE */
