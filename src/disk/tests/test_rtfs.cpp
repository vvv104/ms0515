#include <doctest/doctest.h>

#include <ms0515/disk/Build.hpp>   /* encodeDate */
#include <ms0515/disk/Rtfs.hpp>

using namespace ms0515::disk;

TEST_SUITE("Rtfs descriptor") {

/* ── parse ───────────────────────────────────────────────────────────────── */

TEST_CASE("parses a minimal hd descriptor") {
    auto d = parseRtfs("device: hd\nblocks: 20000\n");
    REQUIRE(d.has_value());
    CHECK(d->device == RtfsDescriptor::Device::Hd);
    CHECK(d->blocks == 20000);
    CHECK(d->volumeId == "RT11A");
    CHECK(d->bootHost.empty());
    CHECK(d->files.empty());
}

TEST_CASE("parses files with attributes, comments and blanks") {
    const char *text =
        "# a folder device\n"
        "\n"
        "device: floppy\n"
        "blocks: 800\n"
        "volume-id: MYVOL\n"
        "boot: boot.bin\n"
        "file: SWAP.SYS | swap.sys | date=1994-02-18\n"
        "file: RT11SJ.SYS | rt11sj.sys | date=1994-02-18 protected\n"
        "file: OLD.TXT | old notes.txt | deleted\n"
        "file: PLAIN.DAT | plain.dat |\n";
    auto d = parseRtfs(text);
    REQUIRE(d.has_value());
    CHECK(d->device == RtfsDescriptor::Device::Floppy);
    CHECK(d->volumeId == "MYVOL");
    CHECK(d->bootHost == "boot.bin");
    REQUIRE(d->files.size() == 4);
    CHECK(d->files[0].rt11Name == "SWAP.SYS");
    CHECK(d->files[0].hostName == "swap.sys");
    CHECK(d->files[0].date == encodeDate(1994, 2, 18));
    CHECK_FALSE(d->files[0].isProtected);
    CHECK(d->files[1].isProtected);
    CHECK(d->files[2].hostName == "old notes.txt");   /* spaces survive */
    CHECK(d->files[2].deleted);
    CHECK(d->files[3].date == 0);
}

TEST_CASE("rejects malformed descriptors with a reason") {
    std::string err;
    CHECK_FALSE(parseRtfs("blocks: 100\n", &err).has_value());          /* no device */
    CHECK_FALSE(parseRtfs("device: tape\nblocks: 100\n").has_value());  /* bad type  */
    CHECK_FALSE(parseRtfs("device: hd\n").has_value());                 /* no blocks */
    CHECK_FALSE(parseRtfs("device: hd\nblocks: 0\n").has_value());
    CHECK_FALSE(parseRtfs("device: hd\nblocks: 65536\n").has_value());  /* over cap  */
    CHECK_FALSE(parseRtfs("device: floppy\nblocks: 801\n").has_value());/* not 800   */
    CHECK_FALSE(parseRtfs("device: hd\nblocks: 100\nfile: ONLYNAME\n").has_value());
    /* duplicate RT-11 names are an error — the directory must be unambiguous */
    CHECK_FALSE(parseRtfs("device: hd\nblocks: 100\n"
                          "file: A.TXT | a.txt |\n"
                          "file: A.TXT | b.txt |\n").has_value());
    CHECK_FALSE(err.empty());
}

/* ── serialize round-trip ────────────────────────────────────────────────── */

TEST_CASE("serialize -> parse reproduces the descriptor") {
    RtfsDescriptor d;
    d.device   = RtfsDescriptor::Device::Floppy;
    d.blocks   = 800;
    d.volumeId = "DEMO";
    d.bootHost = "boot.bin";
    d.files = {
        {"SWAP.SYS",   "swap.sys",   encodeDate(1994, 2, 18), false, false},
        {"RT11SJ.SYS", "rt11sj.sys", encodeDate(1994, 2, 18), true,  false},
        {"NOTES.TXT",  "my notes (old).txt", 0, false, true},
    };
    auto back = parseRtfs(serializeRtfs(d));
    REQUIRE(back.has_value());
    CHECK(back->device == d.device);
    CHECK(back->blocks == d.blocks);
    CHECK(back->volumeId == d.volumeId);
    CHECK(back->bootHost == d.bootHost);
    REQUIRE(back->files.size() == d.files.size());
    for (size_t i = 0; i < d.files.size(); ++i) {
        CHECK(back->files[i].rt11Name    == d.files[i].rt11Name);
        CHECK(back->files[i].hostName    == d.files[i].hostName);
        CHECK(back->files[i].date        == d.files[i].date);
        CHECK(back->files[i].isProtected == d.files[i].isProtected);
        CHECK(back->files[i].deleted     == d.files[i].deleted);
    }
}

/* ── name mangling ───────────────────────────────────────────────────────── */

TEST_CASE("mangleRt11Name maps host names into 6.3 RAD50") {
    CHECK(mangleRt11Name("swap.sys") == "SWAP.SYS");
    CHECK(mangleRt11Name("minesweeper.pas") == "MINESW.PAS");
    CHECK(mangleRt11Name("a-b_c d.txt") == "ABCD.TXT");      /* invalid chars drop */
    CHECK(mangleRt11Name("readme") == "README");              /* no extension      */
    CHECK(mangleRt11Name("archive.tar.gz") == "ARCHIV.GZ");   /* last dot wins     */
    CHECK(mangleRt11Name(".profile") == "FILE.PRO");          /* empty base        */
    CHECK(mangleRt11Name("отчёт.txt") == "FILE.TXT");         /* non-ASCII drops   */
    CHECK(mangleRt11Name("pay$roll.da$") == "PAY$RO.DA$");    /* $ is RAD50        */
}

TEST_CASE("mangleRt11Name resolves collisions with a numeric tail") {
    std::vector<std::string> taken = {"MINESW.PAS"};
    CHECK(mangleRt11Name("minesweeper.pas", taken) == "MINES2.PAS");
    taken.push_back("MINES2.PAS");
    CHECK(mangleRt11Name("minesweeper.pas", taken) == "MINES3.PAS");
    /* short names keep their text and append the digit */
    taken = {"A.TXT"};
    CHECK(mangleRt11Name("a.txt", taken) == "A2.TXT");
}

/* ── auto-fill ───────────────────────────────────────────────────────────── */

TEST_CASE("autoFillRtfs puts SWAP, RT11SJ, other .SYS first, then the rest") {
    RtfsDescriptor d;
    d.device = RtfsDescriptor::Device::Hd;
    d.blocks = 1000;
    autoFillRtfs(d, {
        {"game.sav",   512},
        {"dz.sys",     2048},
        {"rt11sj.sys", 40960},
        {"readme.txt", 100},
        {"swap.sys",   13824},
    });
    REQUIRE(d.files.size() == 5);
    CHECK(d.files[0].rt11Name == "SWAP.SYS");
    CHECK(d.files[1].rt11Name == "RT11SJ.SYS");
    CHECK(d.files[2].rt11Name == "DZ.SYS");
    CHECK(d.files[3].rt11Name == "GAME.SAV");
    CHECK(d.files[4].rt11Name == "README.TXT");
}

TEST_CASE("autoFillRtfs skips files that do not fit and is a no-op when filled") {
    RtfsDescriptor d;
    d.device = RtfsDescriptor::Device::Hd;
    d.blocks = 20;                       /* data area = 20 - 14 = 6 blocks */
    autoFillRtfs(d, {
        {"big.dat",   5 * 512},          /* 5 blocks — fits  */
        {"huge.dat", 10 * 512},          /* 10 blocks — skip */
        {"tiny.dat",       10},          /* 1 block — fits   */
    });
    REQUIRE(d.files.size() == 2);
    CHECK(d.files[0].rt11Name == "BIG.DAT");
    CHECK(d.files[1].rt11Name == "TINY.DAT");

    autoFillRtfs(d, {{"other.dat", 10}});    /* already filled -> no-op */
    CHECK(d.files.size() == 2);
}

/* ── layout derivation ───────────────────────────────────────────────────── */

TEST_CASE("rtfsLayout assigns sequential extents, skipping deleted entries") {
    RtfsDescriptor d;
    d.device = RtfsDescriptor::Device::Hd;
    d.blocks = 100;
    d.files = {
        {"A.DAT", "a.dat", 0, false, false},
        {"B.DAT", "b.dat", 0, false, true},     /* deleted: no blocks */
        {"C.DAT", "c.dat", 0, false, false},
    };
    auto ext = rtfsLayout(d, {3 * 512, 0, 700});   /* live sizes incl. deleted slot */
    REQUIRE(ext.size() == 2);
    CHECK(ext[0].file->rt11Name == "A.DAT");
    CHECK(ext[0].start == rtfsDataStart());        /* 14 */
    CHECK(ext[0].blocks == 3);
    CHECK(ext[1].file->rt11Name == "C.DAT");
    CHECK(ext[1].start == rtfsDataStart() + 3);
    CHECK(ext[1].blocks == 2);                     /* 700 B -> 2 blocks */
}

TEST_CASE("rtfsLayout marks entries past the end as not fitting") {
    RtfsDescriptor d;
    d.device = RtfsDescriptor::Device::Hd;
    d.blocks = 16;                                  /* data area = 2 blocks */
    d.files = {
        {"A.DAT", "a.dat", 0, false, false},
        {"B.DAT", "b.dat", 0, false, false},
    };
    auto ext = rtfsLayout(d, {2 * 512, 512});
    REQUIRE(ext.size() == 2);
    CHECK(ext[0].start == 14);
    CHECK(ext[0].blocks == 2);
    CHECK(ext[1].start == -1);                      /* does not fit */
}

} /* TEST_SUITE */
