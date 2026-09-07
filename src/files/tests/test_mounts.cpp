/*
 * test_mounts.cpp — the images behind the devices: from the emulator's
 * flags and config, by hand, and back into the config.
 */
#include "Mounts.hpp"
#include "scratch.hpp"

#include "ms0515/app/Cli.hpp"
#include "ms0515/app/Config.hpp"
#include "ms0515/disk/Build.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <string>
#include <vector>

using namespace ms0515::files;
namespace disk = ms0515::disk;
namespace app = ms0515::app;

namespace {

std::vector<std::string> names(const std::vector<Device> &devs)
{
    std::vector<std::string> out;
    for (const auto &d : devs) out.push_back(d.name);
    return out;
}

} // namespace

TEST_CASE("a two-sided image in drive A is DZ0: and DZ2:, a single-sided one in drive B is DZ1:")
{
    Scratch s("mounts");
    const auto rod = s.disk("test_rod.dsk", "rod.dsk");
    const auto osa = s.disk("test_osa.dsk", "osa.dsk");
    Mounts m;
    const auto whyA = m.mount(Slot::driveA, rod);
    CHECK_MESSAGE(whyA.empty(), whyA);
    const auto whyB = m.mount(Slot::driveB, osa, 0);
    CHECK_MESSAGE(whyB.empty(), whyB);
    CHECK(names(m.devices()) == std::vector<std::string>{"DZ0:", "DZ2:", "DZ1:"});
    const auto dz2 = m.device("DZ2:");
    REQUIRE(dz2.has_value());
    CHECK(dz2->image == rod);
    CHECK(dz2->spec.side == 1);
    CHECK(dz2->label() == "DZ2: rod.dsk");
    CHECK_FALSE(m.device("DZ3:").has_value());
    REQUIRE(m.mounted(Slot::driveA).has_value());
    CHECK(m.mounted(Slot::driveA)->image == rod);
    m.unmount(Slot::driveA);
    CHECK(names(m.devices()) == std::vector<std::string>{"DZ1:"});
}

TEST_CASE("side 1 of drive B alone is DZ3:, and the HD slot is HD0:")
{
    Scratch s("sides");
    const auto osa = s.disk("test_osa.dsk", "osa.dsk");
    const auto hd = s.file("hd.img", disk::blankLinear(64));
    Mounts m;
    const auto whyB = m.mount(Slot::driveB, osa, 1);
    CHECK_MESSAGE(whyB.empty(), whyB);
    const auto whyHd = m.mount(Slot::hd, hd, 0, /*force=*/true);   /* no directory yet: forced */
    CHECK_MESSAGE(whyHd.empty(), whyHd);
    CHECK(names(m.devices()) == std::vector<std::string>{"DZ3:", "HD0:"});
    CHECK(m.device("HD0:")->spec.vol == disk::Vol::linear);
    CHECK(m.device("DZ3:")->spec.side == 0);   /* side 0 of its own single-sided file */
}

TEST_CASE("what mount() refuses: a missing file, a size no slot takes, no volume without force")
{
    Scratch s("refuse");
    Mounts m;
    CHECK_FALSE(m.mount(Slot::driveA, s.dir() / "missing.dsk").empty());
    const auto odd = s.file("odd.bin", std::vector<uint8_t>(1000, 0));
    CHECK_FALSE(m.mount(Slot::driveA, odd).empty());
    CHECK_FALSE(m.mount(Slot::hd, odd).empty());               /* not a 512-multiple */
    const auto blank = s.file("blank.dsk", disk::blankImage(false));
    CHECK_FALSE(m.mount(Slot::driveA, blank).empty());         /* no RT-11 volume */
    CHECK(m.mount(Slot::driveA, blank, 0, /*force=*/true).empty());
    CHECK(names(m.devices()) == std::vector<std::string>{"DZ0:"});
    CHECK_FALSE(Mounts::describe(odd).empty());
    CHECK(Mounts::describe(blank).find("409600") != std::string::npos);
}

TEST_CASE("the emulator's flags and config give the devices; store() writes them back")
{
    Scratch s("emu");
    const auto rod = s.disk("test_rod.dsk", "rod.dsk");
    const auto osa = s.disk("test_osa.dsk", "osa.dsk");
    app::Config cfg;
    cfg.dsPath[0] = rod.string();
    app::CliArgs cli;
    cli.fdPath[1] = osa.string();          /* --disk1-side0: drive B side 0 */
    const Mounts m = Mounts::fromEmulator(cli, cfg);
    CHECK(names(m.devices()) == std::vector<std::string>{"DZ0:", "DZ2:", "DZ1:"});

    app::Config out;
    m.store(out);
    CHECK(out.dsPath[0] == rod.string());
    CHECK(out.fdPath[1] == osa.string());
    CHECK(out.fdPath[0].empty());

    /* the command line wins over the config for the same drive */
    app::CliArgs cli2;
    cli2.dsPath[0] = osa.string();
    CHECK(Mounts::fromEmulator(cli2, cfg).mounted(Slot::driveA)->image == osa);
}

TEST_CASE("a DV whole-diskette image is one device, DV0:")
{
    Scratch s("dv");
    auto bytes = disk::blankImage(true);
    disk::initVolume(bytes, 0, true, {}, disk::Vol::dv);
    const auto dv = s.file("dv.dsk", bytes);
    Mounts m;
    CHECK(m.mount(Slot::driveA, dv).empty());
    CHECK(names(m.devices()) == std::vector<std::string>{"DV0:"});
    CHECK(m.device("DV0:")->spec.vol == disk::Vol::dv);
}

TEST_CASE("completing a path lists the directories and image files that start with it")
{
    Scratch s("complete");
    s.disk("test_osa.dsk", "osa.dsk");
    s.disk("test_omega.dsk", "omega.dsk");
    s.text("notes.txt", "x");
    s.subdir("older");
    const auto prefix = (s.dir() / "o").string();
    auto got = completePath(prefix);
    std::sort(got.begin(), got.end());
    std::vector<std::string> want = {(s.dir() / "older").string() + static_cast<char>(fs::path::preferred_separator),
                                     (s.dir() / "omega.dsk").string(), (s.dir() / "osa.dsk").string()};
    std::sort(want.begin(), want.end());
    CHECK(got == want);
    CHECK(completePath((s.dir() / "zzz").string()).empty());
}

TEST_CASE("listing a host directory for mounting: '..', the directories, then the image files, each group by name")
{
    Scratch s("listdir");
    s.disk("test_osa.dsk", "osa.dsk");
    s.disk("test_omega.dsk", "omega.dsk");
    s.text("notes.txt", "x");                   /* not a 512-multiple: not an image */
    s.subdir("zeta");
    s.subdir("alpha");
    const auto got = listImages(s.dir());
    REQUIRE(got.size() == 5);
    CHECK(got[0].name == "..");
    CHECK(got[0].directory);
    CHECK(got[0].path == s.dir().parent_path());
    CHECK(got[1].name == "alpha");
    CHECK(got[1].directory);
    CHECK(got[2].name == "zeta");
    CHECK(got[3].name == "omega.dsk");
    CHECK_FALSE(got[3].directory);
    CHECK(got[3].path == s.dir() / "omega.dsk");
    CHECK(got[3].bytes == fs::file_size(s.dir() / "omega.dsk"));
    CHECK(got[4].name == "osa.dsk");
    /* a root has no '..' */
    const auto root = listImages(s.dir().root_path());
    CHECK((root.empty() || root[0].name != ".."));
    /* a directory that is not there lists nothing */
    CHECK(listImages(s.dir() / "nope").empty());
}
