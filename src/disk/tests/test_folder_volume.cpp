#include <doctest/doctest.h>

#include <ms0515/disk/Build.hpp>
#include <ms0515/disk/FolderVolume.hpp>
#include <ms0515/disk/Image.hpp>
#include <ms0515/disk/Layout.hpp>

#include <cstring>
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

TEST_CASE("a vanished host file simply drops out; returning re-enters it") {
    auto dir = freshDir("missing");
    writeFile(dir / "gone.dat", "payload");
    writeFile(dir / "kept.dat", "stays");
    fs::path desc = writeDescriptor(dir);
    auto vol = FolderVolume::open(desc.string());
    REQUIRE(vol != nullptr);
    (void)assemble(*vol);

    fs::remove(dir / "gone.dat");

    auto im = openLinearImage(assemble(*vol));
    REQUIRE(im.has_value());
    CHECK(im->directory.find("GONE.DAT") == nullptr);
    CHECK(im->directory.find("KEPT.DAT") != nullptr);
    CHECK(vol->descriptor().files.size() == 1);    /* line erased */

    /* The file coming back (e.g. renamed back) re-enters as a new one. */
    writeFile(dir / "gone.dat", "again");
    im = openLinearImage(assemble(*vol));
    REQUIRE(im.has_value());
    CHECK(im->directory.find("GONE.DAT") != nullptr);
}

TEST_CASE("volume-id and owner: descriptor -> home block, guest INIT -> descriptor") {
    auto dir = freshDir("identity");
    fs::path desc = dir / "device.rtfs";
    writeFile(desc, "device: hd\nblocks: 100\nvolume-id: MYVOL\nowner: VVV\n");
    auto vol = FolderVolume::open(desc.string());
    REQUIRE(vol != nullptr);

    std::vector<uint8_t> home(kBlock, 0);
    vol->readBlock(1, home.data());
    CHECK(std::string(reinterpret_cast<char *>(&home[0x1D8]), 5) == "MYVOL");
    CHECK(std::string(reinterpret_cast<char *>(&home[0x1E4]), 3) == "VVV");

    /* Guest INIT writes a new home block — descriptor adopts it. */
    std::memset(&home[0x1D8], ' ', 12);
    std::memcpy(&home[0x1D8], "NEWID", 5);
    std::memset(&home[0x1E4], ' ', 12);
    std::memcpy(&home[0x1E4], "OWNER2", 6);
    vol->writeBlock(1, home.data());
    CHECK(vol->descriptor().volumeId == "NEWID");
    CHECK(vol->descriptor().owner == "OWNER2");
    std::ifstream d(desc);
    std::string text(std::istreambuf_iterator<char>(d), {});
    CHECK(text.find("volume-id: NEWID") != std::string::npos);
    CHECK(text.find("owner: OWNER2") != std::string::npos);
}

TEST_CASE("protected flag flows descriptor -> directory entry") {
    auto dir = freshDir("prot");
    writeFile(dir / "lock.dat", "x");
    fs::path desc = dir / "device.rtfs";
    writeFile(desc, "device: hd\nblocks: 100\n"
                    "file: LOCK.DAT | lock.dat | protected\n");
    auto vol = FolderVolume::open(desc.string());
    REQUIRE(vol != nullptr);
    auto im = openLinearImage(assemble(*vol));
    REQUIRE(im.has_value());
    const DirEntry *e = im->directory.find("LOCK.DAT");
    REQUIRE(e != nullptr);
    CHECK((e->status & kStatusProtected) != 0);
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

/* ── guest directory edits (stage 2b) ────────────────────────────────────── */

namespace {

/* Build the directory blocks a guest write would carry: a linear image
 * with the wanted post-state files (sizes shape the entry lengths). */
std::vector<uint8_t> dirBlocksFor(
    const std::vector<std::pair<std::string, int>> &filesAndBlocks)
{
    auto img = blankLinear(100);
    initVolume(img, 0, false, {}, Vol::linear);
    for (const auto &[name, nblk] : filesAndBlocks)
        putFile(img, 0, false, name,
                std::vector<uint8_t>(static_cast<std::size_t>(nblk) * kBlock,
                                     0x11),
                {}, Vol::linear);
    return {img.begin() + 6 * kBlock, img.begin() + 14 * kBlock};
}

}  /* namespace */

TEST_CASE("guest delete marks the descriptor entry deleted, host file kept") {
    auto dir = freshDir("gdelete");
    writeFile(dir / "doomed.dat", std::string(512, 'D'));
    auto vol = FolderVolume::open(writeDescriptor(dir).string());
    REQUIRE(vol != nullptr);
    (void)assemble(*vol);

    auto blocks = dirBlocksFor({});              /* empty directory */
    vol->writeRange(6, 8, blocks.data());

    REQUIRE(vol->descriptor().files.size() == 1);
    CHECK(vol->descriptor().files[0].deleted);
    CHECK(fs::exists(dir / "doomed.dat"));       /* host file survives */
    auto im = openLinearImage(assemble(*vol));
    REQUIRE(im.has_value());
    CHECK(im->directory.find("DOOMED.DAT") == nullptr);
}

TEST_CASE("guest create materializes a host file from staged scratch blocks") {
    auto dir = freshDir("gcreate");
    auto vol = FolderVolume::open(writeDescriptor(dir).string());
    REQUIRE(vol != nullptr);
    (void)assemble(*vol);

    /* PIP flow: stage data into free space, then commit the dir entry. */
    std::vector<uint8_t> payload(kBlock, 0xCD);
    vol->writeBlock(rtfsDataStart(), payload.data());
    auto blocks = dirBlocksFor({{"K.SAV", 1}});
    vol->writeRange(6, 8, blocks.data());

    REQUIRE(vol->descriptor().files.size() == 1);
    CHECK(vol->descriptor().files[0].rt11Name == "K.SAV");
    CHECK(vol->descriptor().files[0].hostName == "k.sav");
    std::ifstream f(dir / "k.sav", std::ios::binary);
    REQUIRE(f.good());
    std::string content(std::istreambuf_iterator<char>(f), {});
    REQUIRE(content.size() == static_cast<std::size_t>(kBlock));
    CHECK(static_cast<uint8_t>(content[0]) == 0xCD);
}

TEST_CASE("guest rename follows the start block") {
    auto dir = freshDir("grename");
    writeFile(dir / "old.dat", std::string(512, 'O'));
    auto vol = FolderVolume::open(writeDescriptor(dir).string());
    REQUIRE(vol != nullptr);
    (void)assemble(*vol);

    auto blocks = dirBlocksFor({{"NEW.DAT", 1}});   /* same slot, new name */
    vol->writeRange(6, 8, blocks.data());

    REQUIRE(vol->descriptor().files.size() == 1);
    CHECK(vol->descriptor().files[0].rt11Name == "NEW.DAT");
    CHECK(vol->descriptor().files[0].hostName == "old.dat");
    CHECK_FALSE(vol->descriptor().files[0].deleted);
}

TEST_CASE("manual .rtfs edits are picked up on the next directory read") {
    auto dir = freshDir("manual");
    writeFile(dir / "a.dat", "AAAA");
    writeFile(dir / "b.dat", "BBBB");
    fs::path desc = writeDescriptor(dir);
    auto vol = FolderVolume::open(desc.string());
    REQUIRE(vol != nullptr);
    (void)assemble(*vol);                      /* auto-fill + first read */

    /* Hand-edit: custom RT-11 name for a.dat, b.dat hidden. */
    writeFile(desc,
        "device: hd\nblocks: 100\nvolume-id: HAND\n"
        "file: CUSTOM.NAM | a.dat |\n"
        "file: B.DAT | b.dat | deleted\n");

    auto im = openLinearImage(assemble(*vol));
    REQUIRE(im.has_value());
    CHECK(im->directory.find("CUSTOM.NAM") != nullptr);
    CHECK(im->directory.find("A.DAT") == nullptr);
    CHECK(im->directory.find("B.DAT") == nullptr);   /* hidden by hand */
    std::vector<uint8_t> home(kBlock, 0);
    vol->readBlock(1, home.data());
    CHECK(std::string(reinterpret_cast<char *>(&home[0x1D8]), 4) == "HAND");

    /* A geometry edit is ignored until remount (device size is wired in). */
    writeFile(desc,
        "device: hd\nblocks: 500\nfile: CUSTOM.NAM | a.dat |\n");
    (void)assemble(*vol);
    CHECK(vol->blocks() == 100);
    CHECK(vol->descriptor().volumeId == "HAND");     /* reload skipped */

    /* A malformed edit keeps the current state. */
    writeFile(desc, "device: banana\n");
    auto im2 = openLinearImage(assemble(*vol));
    REQUIRE(im2.has_value());
    CHECK(im2->directory.find("CUSTOM.NAM") != nullptr);
}

TEST_CASE("guest boot-block writes materialize the hidden boot file") {
    auto dir = freshDir("boot");
    writeFile(dir / "f.dat", "data");
    fs::path desc = dir / "device.rtfs";
    writeFile(desc, "device: floppy\nblocks: 800\n");
    auto vol = FolderVolume::open(desc.string());
    REQUIRE(vol != nullptr);
    (void)assemble(*vol);

    std::vector<uint8_t> b0(kBlock, 0xB0), b2(kBlock, 0xB2), back(kBlock, 0);
    vol->writeBlock(0, b0.data());          /* primary boot   */
    vol->writeBlock(2, b2.data());          /* bootstrap head */

    /* Descriptor gained the boot line; the file holds both blocks. */
    std::ifstream d(desc);
    std::string text(std::istreambuf_iterator<char>(d), {});
    CHECK(text.find("boot: boot.bin") != std::string::npos);
    REQUIRE(fs::exists(dir / "boot.bin"));
    CHECK(fs::file_size(dir / "boot.bin") == 2u * kBlock);

    vol->readBlock(0, back.data());
    CHECK(back[0] == 0xB0);
    vol->readBlock(2, back.data());
    CHECK(back[0] == 0xB2);
    vol->readBlock(1, back.data());          /* home block stays generated */
    CHECK(back[0x1C0] == 0xFF);

    /* The boot file never shows up inside RT-11. */
    auto im = openLinearImage(assemble(*vol));
    REQUIRE(im.has_value());
    CHECK(im->directory.find("BOOT.BIN") == nullptr);
    CHECK(im->directory.find("F.DAT") != nullptr);
}

} /* TEST_SUITE */
