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

/* ── files open for output (the way MACRO and LINK write) ────────────────── */

namespace {

constexpr std::size_t kEntry0 = 10, kEntrySize = 14;

void setStatus(std::vector<uint8_t> &dirBlocks, int entry, uint16_t status,
               uint16_t jobChannel = 0)
{
    uint8_t *e = dirBlocks.data() + kEntry0 + static_cast<std::size_t>(entry) * kEntrySize;
    e[0] = static_cast<uint8_t>(status & 0xFF);
    e[1] = static_cast<uint8_t>(status >> 8);
    e[10] = static_cast<uint8_t>(jobChannel & 0xFF);
    e[11] = static_cast<uint8_t>(jobChannel >> 8);
}

RtfsDescriptor memoryHd(int blocks = 100)
{
    RtfsDescriptor d;
    d.device = RtfsDescriptor::Device::Hd;
    d.blocks = blocks;
    return d;
}

std::string hostBytes(const fs::path &p)
{
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), {}};
}

}  /* namespace */

TEST_CASE("two files open for output at once are written and closed apart") {
    auto dir = freshDir("twoout");
    auto vol = FolderVolume::openInMemory(dir.string(), memoryHd());
    REQUIRE(vol != nullptr);
    (void)assemble(*vol);
    const int first = rtfsDataStart();

    /* .ENTER twice: A.OBJ gets 40 blocks at the start, A.LST 30 after it. */
    auto entered = dirBlocksFor({{"A.OBJ", 40}, {"A.LST", 30}});
    setStatus(entered, 0, kStatusTentative, 0x0103);
    setStatus(entered, 1, kStatusTentative, 0x0104);
    vol->writeRange(6, 8, entered.data());

    /* The host files are there, and not the size of the space set aside. */
    REQUIRE(fs::exists(dir / "a.obj"));
    REQUIRE(fs::exists(dir / "a.lst"));
    CHECK(fs::file_size(dir / "a.obj") == 0);
    CHECK(fs::file_size(dir / "a.lst") == 0);

    /* The monitor reads the directory again before it closes a file: the
     * entries must still be its own - tentative, with job and channel. */
    {
        auto im = openLinearImage(assemble(*vol));
        REQUIRE(im.has_value());
        const auto &es = im->directory.entries;
        REQUIRE(es.size() >= 2);
        CHECK(es[0].name == "A.OBJ");
        CHECK(es[0].status == kStatusTentative);
        CHECK(es[0].jobChannel == 0x0103);
        CHECK(es[0].startBlock == first);
        CHECK(es[0].length == 40);
        CHECK(es[1].name == "A.LST");
        CHECK(es[1].status == kStatusTentative);
        CHECK(es[1].jobChannel == 0x0104);
        CHECK(es[1].startBlock == first + 40);
        CHECK(es[1].length == 30);
    }

    std::vector<uint8_t> a(kBlock, 0xAA), b(kBlock, 0xBB), c(kBlock, 0xCC);
    vol->writeBlock(first, a.data());
    vol->writeBlock(first + 1, a.data());
    vol->writeBlock(first + 40, b.data());

    /* .CLOSE A.OBJ at two blocks: the rest of its space is free again,
     * A.LST stays where it is. */
    auto closedObj = dirBlocksFor({{"A.OBJ", 2}, {"GAP.TMP", 38}, {"A.LST", 30}});
    setStatus(closedObj, 1, kStatusEmpty);
    setStatus(closedObj, 2, kStatusTentative, 0x0104);
    vol->writeRange(6, 8, closedObj.data());
    CHECK(hostBytes(dir / "a.obj") == std::string(2 * kBlock, '\xAA'));
    {
        auto im = openLinearImage(assemble(*vol));
        REQUIRE(im.has_value());
        const DirEntry *lst = im->directory.find("A.OBJ");
        REQUIRE(lst != nullptr);
        CHECK(lst->length == 2);
        bool tentativeKept = false;
        for (const auto &e : im->directory.entries)
            if (e.name == "A.LST" && e.status == kStatusTentative &&
                e.startBlock == first + 40 && e.length == 30)
                tentativeKept = true;
        CHECK(tentativeKept);
    }

    vol->writeBlock(first + 41, c.data());
    auto closedLst = dirBlocksFor({{"A.OBJ", 2}, {"GAP.TMP", 38}, {"A.LST", 2}});
    setStatus(closedLst, 1, kStatusEmpty);
    vol->writeRange(6, 8, closedLst.data());
    CHECK(hostBytes(dir / "a.lst") ==
          std::string(kBlock, '\xBB') + std::string(kBlock, '\xCC'));

    auto im = openLinearImage(assemble(*vol));
    REQUIRE(im.has_value());
    const DirEntry *lst = im->directory.find("A.LST");
    REQUIRE(lst != nullptr);
    CHECK(lst->startBlock == first + 40);
    CHECK(lst->length == 2);

    /* Both were closed: both stay when the volume goes. */
    vol.reset();
    CHECK(fs::exists(dir / "a.obj"));
    CHECK(fs::exists(dir / "a.lst"));
}

TEST_CASE("a file written over an existing one takes its place on the host") {
    auto dir = freshDir("replace");
    writeFile(dir / "a.obj", std::string(kBlock, 'O'));
    auto vol = FolderVolume::openInMemory(dir.string(), memoryHd());
    REQUIRE(vol != nullptr);
    (void)assemble(*vol);
    const int first = rtfsDataStart();

    /* .ENTER A.OBJ while A.OBJ exists: a second, tentative entry. */
    auto entered = dirBlocksFor({{"A.OBJ", 1}, {"B.OBJ", 10}});
    std::memcpy(entered.data() + kEntry0 + kEntrySize + 2,
                entered.data() + kEntry0 + 2, 6);           /* the same name */
    setStatus(entered, 1, kStatusTentative, 0x0101);
    vol->writeRange(6, 8, entered.data());
    CHECK(hostBytes(dir / "a.obj") == std::string(kBlock, 'O'));   /* not yet */

    std::vector<uint8_t> n(kBlock, 'N');
    vol->writeBlock(first + 1, n.data());

    /* .CLOSE: the old entry is gone, the new one is the file. */
    auto closed = dirBlocksFor({{"GAP.TMP", 1}, {"A.OBJ", 1}});
    setStatus(closed, 0, kStatusEmpty);
    vol->writeRange(6, 8, closed.data());

    CHECK(hostBytes(dir / "a.obj") == std::string(kBlock, 'N'));
    int files = 0;
    for (const auto &de : fs::directory_iterator(dir)) { (void)de; ++files; }
    CHECK(files == 1);
    auto im = openLinearImage(assemble(*vol));
    REQUIRE(im.has_value());
    const DirEntry *e = im->directory.find("A.OBJ");
    REQUIRE(e != nullptr);
    CHECK(e->startBlock == first + 1);
    CHECK(im->readFile("A.OBJ")[0] == 'N');
}

TEST_CASE("an in-memory volume has no descriptor to remember a deleted file in") {
    auto dir = freshDir("memdelete");
    writeFile(dir / "doomed.dat", std::string(512, 'D'));
    writeFile(dir / "kept.dat", std::string(512, 'K'));
    auto vol = FolderVolume::openInMemory(dir.string(), memoryHd());
    REQUIRE(vol != nullptr);
    (void)assemble(*vol);

    /* The guest deletes DOOMED.DAT: the host file goes with it. */
    auto im = openLinearImage(assemble(*vol));
    REQUIRE(im.has_value());
    const bool doomedFirst = im->directory.entries[0].name == "DOOMED.DAT";
    auto blocks = dirBlocksFor({{"DOOMED.DAT", 1}, {"KEPT.DAT", 1}});
    if (!doomedFirst) blocks = dirBlocksFor({{"KEPT.DAT", 1}, {"DOOMED.DAT", 1}});
    setStatus(blocks, doomedFirst ? 0 : 1, kStatusEmpty);
    vol->writeRange(6, 8, blocks.data());

    CHECK_FALSE(fs::exists(dir / "doomed.dat"));
    CHECK(fs::exists(dir / "kept.dat"));
    CHECK(vol->descriptor().files.size() == 1);
}

TEST_CASE("openInMemory serves a folder without ever writing a descriptor") {
    auto dir = freshDir("inmemory");
    writeFile(dir / "swap.sys", std::string(600, 'S'));      /* 2 blocks */
    writeFile(dir / "hello.txt", "hello rtfs");              /* 1 block  */

    RtfsDescriptor desc;
    desc.device = RtfsDescriptor::Device::Hd;
    desc.blocks = 100;
    auto vol = FolderVolume::openInMemory(dir.string(), desc);
    REQUIRE(vol != nullptr);
    CHECK(vol->blocks() == 100);
    CHECK(vol->deviceType() == RtfsDescriptor::Device::Hd);
    REQUIRE(vol->descriptor().files.size() == 2);
    CHECK(vol->descriptor().files[0].rt11Name == "SWAP.SYS");

    auto im = openLinearImage(assemble(*vol));
    REQUIRE(im.has_value());
    CHECK(im->directory.find("HELLO.TXT") != nullptr);

    /* Everything that saves a file-backed descriptor: a new home block,
     * a host file appearing, a guest directory rewrite. */
    std::vector<uint8_t> home(kBlock, 0);
    vol->readBlock(1, home.data());
    std::memcpy(home.data() + 0x1D8, "NEWVOL      ", 12);
    vol->writeBlock(1, home.data());
    CHECK(vol->descriptor().volumeId == "NEWVOL");

    writeFile(dir / "late.dat", "late");
    auto im2 = openLinearImage(assemble(*vol));
    REQUIRE(im2.has_value());
    CHECK(im2->directory.find("LATE.DAT") != nullptr);

    const int dirLbn = 6;                    /* first directory segment */
    std::vector<uint8_t> seg(2 * kBlock, 0);
    vol->readBlock(dirLbn, seg.data());
    vol->readBlock(dirLbn + 1, seg.data() + kBlock);
    vol->writeRange(dirLbn, 2, seg.data());

    int files = 0;
    for (const auto &de : fs::directory_iterator(dir)) {
        ++files;
        CHECK(de.path().extension() != ".rtfs");
    }
    CHECK(files == 3);

    /* A descriptor that names its files is the whole volume: the folder's
     * other files stay out, now and when they appear later; what the
     * guest creates comes in. */
    RtfsDescriptor listed = desc;
    listed.files.push_back({"HELLO.TXT", "hello.txt"});
    auto only = FolderVolume::openInMemory(dir.string(), listed);
    REQUIRE(only != nullptr);
    writeFile(dir / "later.dat", "later");
    auto im3 = openLinearImage(assemble(*only));
    REQUIRE(im3.has_value());
    CHECK(im3->directory.find("HELLO.TXT") != nullptr);
    CHECK(im3->directory.find("SWAP.SYS") == nullptr);
    CHECK(im3->directory.find("LATE.DAT") == nullptr);
    CHECK(im3->directory.find("LATER.DAT") == nullptr);
    REQUIRE(only->descriptor().files.size() == 1);

    /* A size the device cannot have is refused. */
    RtfsDescriptor bad;
    bad.blocks = 0;
    CHECK(FolderVolume::openInMemory(dir.string(), bad) == nullptr);
    CHECK(FolderVolume::openInMemory((dir / "missing").string(), desc) == nullptr);
}

} /* TEST_SUITE */
