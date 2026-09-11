/*
 * test_manifest.cpp - disks.toml read, its rules applied, and a choice made
 * over it turned into a recipe that composes.
 */

#include <doctest/doctest.h>
#include <ms0515/disk/Build.hpp>
#include <ms0515/disk/Compose.hpp>
#include <ms0515/disk/Image.hpp>
#include <ms0515/disk/Manifest.hpp>

#include "exemplar_fixture.hpp"

#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

using namespace ms0515::disk;
using namespace ms0515::disk::fixture;

namespace {

const char kToml[] = R"(
format = 1
owner  = "MS0515 EMU"

[system.osa]
title = "OSA 1.0"
image = "systems/osa.dsk"
media = ["ss", "dz", "dv"]

[system.rodionov]
title    = "RT15SJ"
image    = "systems/rodionov.dsk"
media    = ["dz"]
rebuild  = false
reserved = [{ side = 1, lbn = 792 }, { side = 1, lbn = 799 }]

[bundle.sabot2]
title   = "Saboteur 2"
needs   = ["ss", "dz"]
systems = ["osa"]
date    = "1990-11-09"
files   = ["games/sabot2/SABOT2.SAV", "games/sabot2/SABOT2.DAT"]

[bundle.pacman]
title = "Pac-Man"
date  = "1989-04-30"
files = ["games/pacman/*"]

[bundle.docs]
title   = "Docs"
volume  = "any"
protect = true
files   = [{ path = "docs/K13U.SAV", as = "K52.SAV", date = "1991-01-31" }, "docs/R15.DOC"]

[preset.games]
title     = "OSA: games"
system    = "osa"
media     = "dz"
volume_id = "GAMES"
bundles   = ["sabot2", "pacman", "docs"]

[preset.rodionov]
title   = "Rodionov"
system  = "rodionov"
media   = "dz"
bundles = ["docs"]
startup = ["SET TT QUIET", "LOAD VM:", "R ROSA3"]
)";

std::vector<uint8_t> blocks(int n, uint8_t fill) { return std::vector<uint8_t>(static_cast<std::size_t>(n) * kBlock, fill); }

Repository repository()
{
    auto files = std::make_shared<std::map<std::string, std::vector<uint8_t>>>();
    (*files)["systems/osa.dsk"] = exemplar(Media::dv);
    (*files)["systems/rodionov.dsk"] = exemplar(Media::dz);
    (*files)["games/sabot2/SABOT2.SAV"] = blocks(2, 1);
    (*files)["games/sabot2/SABOT2.DAT"] = blocks(5, 2);
    (*files)["games/pacman/SP13.SAV"] = blocks(1, 3);
    (*files)["games/pacman/LABRN.DAT"] = blocks(1, 4);
    (*files)["games/pacman/README.md"] = text("# Pac-Man");
    (*files)["games/pacman/notes.txt"] = text("lowercase");
    (*files)["docs/K13U.SAV"] = blocks(3, 5);
    (*files)["docs/R15.DOC"] = blocks(1, 6);
    Repository repo;
    for (const auto &kv : *files) repo.paths.push_back(kv.first);
    repo.read = [files](const std::string &p) -> std::optional<std::vector<uint8_t>> {
        const auto it = files->find(p);
        if (it == files->end()) return std::nullopt;
        return it->second;
    };
    return repo;
}

std::string replaced(std::string s, const std::string &from, const std::string &to)
{
    const auto at = s.find(from);
    REQUIRE(at != std::string::npos);
    return s.replace(at, from.size(), to);
}

}  /* namespace */

TEST_SUITE("Manifest") {

TEST_CASE("disks.toml read: systems, bundles and presets as written, in order") {
    const Manifest m = parseManifest(kToml);
    CHECK(m.owner == "MS0515 EMU");
    REQUIRE(m.systems.size() == 2);
    CHECK(m.systems[0].key == "osa");
    CHECK(m.systems[0].image == "systems/osa.dsk");
    CHECK(m.systems[0].media == std::vector<Media>{Media::ss, Media::dz, Media::dv});
    CHECK(m.systems[0].rebuild);
    const auto *rod = m.system("rodionov");
    REQUIRE(rod);
    CHECK_FALSE(rod->rebuild);
    REQUIRE(rod->reserved.size() == 2);
    CHECK(rod->reserved[1].side == 1);
    CHECK(rod->reserved[1].lbn == 799);

    REQUIRE(m.bundles.size() == 3);
    CHECK(m.bundles[0].key == "sabot2");
    CHECK(m.bundles[0].needs == std::vector<Media>{Media::ss, Media::dz});
    CHECK(m.bundles[0].systems == std::vector<std::string>{"osa"});
    CHECK(m.bundles[0].place == Place::boot);
    const auto *docs = m.bundle("docs");
    REQUIRE(docs);
    CHECK(docs->place == Place::any);
    CHECK(docs->protect);
    REQUIRE(docs->files.size() == 2);
    CHECK(docs->files[0].pattern == "docs/K13U.SAV");
    CHECK(docs->files[0].as == "K52.SAV");
    CHECK(docs->files[0].date == "1991-01-31");
    CHECK_FALSE(docs->files[1].as.has_value());

    REQUIRE(m.presets.size() == 2);
    CHECK(m.presets[0].key == "games");
    CHECK(m.presets[0].media == Media::dz);
    CHECK(m.presets[0].volumeId == "GAMES");
    CHECK(m.presets[0].bundles == std::vector<std::string>{"sabot2", "pacman", "docs"});
    CHECK_FALSE(m.presets[0].startup.has_value());
    CHECK(m.preset("rodionov")->startup == std::vector<std::string>{"SET TT QUIET", "LOAD VM:", "R ROSA3"});
    CHECK(m.preset("nothing") == nullptr);
}

TEST_CASE("what disks.toml must not say") {
    const std::string good = kToml;
    auto refused = [](const std::string &text, const char *why) {
        CAPTURE(why);
        CHECK_THROWS_AS((void)parseManifest(text), std::runtime_error);
    };
    refused("format = 1\n[system.x\n", "a TOML syntax error");
    refused(replaced(good, "format = 1", "format = 2"), "a format this does not know");
    refused(replaced(good, "media    = [\"dz\"]", "media    = [\"hd\"]"), "a media word that is none");
    refused(replaced(good, "bundles   = [\"sabot2\",", "bundles   = [\"tetris\","), "a preset naming a bundle that is not there");
    refused(replaced(good, "system    = \"osa\"", "system    = \"mihin\""), "a preset naming a system that is not there");
    refused(replaced(good, "media     = \"dz\"\nvolume_id", "media     = \"xx\"\nvolume_id"), "a preset's media word that is none");
    refused(replaced(good, "system  = \"rodionov\"\nmedia   = \"dz\"", "system  = \"rodionov\"\nmedia   = \"ss\""), "a preset on a media its system does not boot");
    refused(replaced(good, "\"1990-11-09\"", "\"2004-01-01\""), "a date RT-11 V5.04 cannot show");
    refused(replaced(good, "\"1989-04-30\"", "\"1989-13-30\""), "no date at all");
    refused(replaced(good, "systems = [\"osa\"]", "systems = [\"mihin\"]"), "a bundle for a system that is not there");
    refused(replaced(good, "volume  = \"any\"", "volume  = \"side1\""), "a volume word that is none");
    refused(replaced(good, "image = \"systems/osa.dsk\"\n", ""), "a system with no image");
}

TEST_CASE("a bundle refused for a system or a media says why") {
    const Manifest m = parseManifest(kToml);
    const auto &sab = *m.bundle("sabot2");
    CHECK(bundleRefusal(m, sab, "osa", Media::dz).empty());
    CHECK(bundleRefusal(m, sab, "osa", Media::ss).empty());
    CHECK_FALSE(bundleRefusal(m, sab, "osa", Media::dv).empty());
    CHECK_FALSE(bundleRefusal(m, sab, "rodionov", Media::dz).empty());
    CHECK(bundleRefusal(m, *m.bundle("pacman"), "rodionov", Media::dz).empty());
}

TEST_CASE("a bundle's paths: in order, globs in name order and RT-11 names only") {
    const Manifest m = parseManifest(kToml);
    const auto repo = repository();
    CHECK(bundlePaths(*m.bundle("pacman"), repo) == std::vector<std::string>{"games/pacman/LABRN.DAT", "games/pacman/SP13.SAV"});
    CHECK(bundlePaths(*m.bundle("sabot2"), repo) == std::vector<std::string>{"games/sabot2/SABOT2.SAV", "games/sabot2/SABOT2.DAT"});

    ManifestBundle missing{"x", "X", {{"games/NOPE.SAV", {}, {}}}, Place::boot, {}, {}, "", false};
    CHECK_THROWS_WITH_AS((void)bundlePaths(missing, repo), doctest::Contains("games/NOPE.SAV"), std::runtime_error);
    ManifestBundle empty{"y", "Y", {{"games/tetris/*", {}, {}}}, Place::boot, {}, {}, "", false};
    CHECK_THROWS_AS((void)bundlePaths(empty, repo), std::runtime_error);
}

TEST_CASE("a preset's recipe: the system read, every file named, dated and placed") {
    const Manifest m = parseManifest(kToml);
    const auto repo = repository();
    const ComposeRecipe r = recipeFor(m, selectionOf(*m.preset("games")), repo);
    CHECK(r.system == *repo.read("systems/osa.dsk"));
    CHECK(r.rebuild);
    CHECK(r.media == Media::dz);
    CHECK(r.volumeId == "GAMES");
    CHECK(r.owner == "MS0515 EMU");
    CHECK(r.secondOwner == "MS0515 EMU");
    CHECK_FALSE(r.startup.has_value());
    REQUIRE(r.groups.size() == 3);
    CHECK(r.groups[0].title == "Saboteur 2");
    CHECK(r.groups[0].place == Place::boot);
    REQUIRE(r.groups[0].files.size() == 2);
    CHECK(r.groups[0].files[0].name == "SABOT2.SAV");
    CHECK(r.groups[0].files[1].data == blocks(5, 2));
    CHECK(r.groups[0].files[0].date == encodeDate(1990, 11, 9));
    CHECK_FALSE(r.groups[0].files[0].protect);
    const auto &docs = r.groups[2];
    CHECK(docs.place == Place::any);
    CHECK(docs.files[0].name == "K52.SAV");
    CHECK(docs.files[0].date == encodeDate(1991, 1, 31));
    CHECK(docs.files[1].date == 0);                          /* no date anywhere: none */
    CHECK(docs.files[0].protect);

    const auto img = composeDisk(r);
    const auto boot = openVolume(img, Vol::floppy, 0);
    CHECK(boot->directory.find("SABOT2.DAT") != nullptr);
    CHECK(boot->directory.find("K52.SAV") != nullptr);
    CHECK(bootedMonitor(img, 0, true) == "RT11SJ");
}

TEST_CASE("a kept system's recipe carries its reserved blocks and the startup") {
    const Manifest m = parseManifest(kToml);
    const ComposeRecipe r = recipeFor(m, selectionOf(*m.preset("rodionov")), repository());
    CHECK_FALSE(r.rebuild);
    CHECK(r.reserved.size() == 2);
    CHECK(r.startup == std::vector<std::string>{"SET TT QUIET", "LOAD VM:", "R ROSA3"});
    CHECK(planDisk(r).ok);
}

TEST_CASE("a selection that breaks a rule is refused before anything is read") {
    const Manifest m = parseManifest(kToml);
    const auto repo = repository();
    Selection s{"osa", Media::dv, {"sabot2"}, std::nullopt, std::nullopt};
    CHECK_THROWS_WITH_AS((void)recipeFor(m, s, repo), doctest::Contains("Saboteur 2"), std::runtime_error);
    s = {"rodionov", Media::ss, {}, std::nullopt, std::nullopt};
    CHECK_THROWS_AS((void)recipeFor(m, s, repo), std::runtime_error);
    s = {"mihin", Media::ss, {}, std::nullopt, std::nullopt};
    CHECK_THROWS_AS((void)recipeFor(m, s, repo), std::runtime_error);
    s = {"osa", Media::ss, {"tetris"}, std::nullopt, std::nullopt};
    CHECK_THROWS_AS((void)recipeFor(m, s, repo), std::runtime_error);
}

TEST_CASE("media words") {
    CHECK(parseMedia("ss") == Media::ss);
    CHECK(parseMedia("dz") == Media::dz);
    CHECK(parseMedia("dv") == Media::dv);
    CHECK_FALSE(parseMedia("DV").has_value());
    CHECK(std::string(mediaWord(Media::dz)) == "dz");
}

}  /* TEST_SUITE */
