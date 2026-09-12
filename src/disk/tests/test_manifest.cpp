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

#include <algorithm>
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
requires = ["dz", "tt"]
requires_by_media = { dv = ["dv"] }
suggests = ["dup"]
startup = ["SET TT QUIET"]

[system.rodionov]
title    = "RT15SJ"
image    = "systems/rodionov.dsk"
media    = ["dz"]
reserved = [{ side = 1, lbn = 792 }, { side = 1, lbn = 799 }]
requires = ["dz", "tt"]
startup  = ["SET TT QUIET"]

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

[bundle.dz]
title = "DZ.SYS"
files = ["handlers/DZ.SYS"]

[bundle.tt]
title = "TT.SYS"
files = ["handlers/TT.SYS"]

[bundle.dv]
title = "DV.SYS"
files = ["handlers/DV.SYS"]

[bundle.sl]
title   = "SL.SYS"
startup = ["SET SL ON"]
files   = ["handlers/SL.SYS"]

[bundle.dup]
title    = "DUP"
provides = ["dup"]
files    = ["handlers/DUP.SAV"]

[preset.games]
title     = "OSA: games"
system    = "osa"
media     = "dz"
volume_id = "GAMES"
bundles   = ["sabot2", "pacman", "docs"]
banner    = ["Type a game to run it"]

[preset.rodionov]
title   = "Rodionov"
system  = "rodionov"
media   = "dz"
bundles = ["docs", "sl"]
startup = ["SET SL ON", "LOAD VM:", "R ROSA3"]
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
    const auto kit = openVolume(exemplar(Media::dv), Vol::dv);
    for (const char *name : {"DZ.SYS", "TT.SYS", "DV.SYS", "SL.SYS"})
        (*files)[std::string("handlers/") + name] = kit->readFile(name);
    (*files)["handlers/DUP.SAV"] = blocks(4, 7);
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
    CHECK(m.systems[0].dependsOn == std::vector<std::string>{"dz", "tt"});
    CHECK(m.systems[0].dependsOnByMedia.at(Media::dv) == std::vector<std::string>{"dv"});
    CHECK(m.systems[0].startup == std::vector<std::string>{"SET TT QUIET"});
    const auto *rod = m.system("rodionov");
    REQUIRE(rod);
    REQUIRE(rod->reserved.size() == 2);
    CHECK(rod->reserved[1].side == 1);
    CHECK(rod->reserved[1].lbn == 799);

    REQUIRE(m.bundles.size() == 8);
    CHECK(m.bundle("sl")->startup == std::vector<std::string>{"SET SL ON"});
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
    CHECK(m.preset("rodionov")->startup == std::vector<std::string>{"SET SL ON", "LOAD VM:", "R ROSA3"});
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
    refused(replaced(good, "requires = [\"dz\", \"tt\"]\nrequires_by_media", "requires = [\"dz\", \"xx\"]\nrequires_by_media"), "a system requiring what nothing provides");
    refused(replaced(good, "suggests = [\"dup\"]", "suggests = [\"xx\"]"), "a system suggesting what nothing provides");
    refused(replaced(good, "{ dv = [\"dv\"] }", "{ hd = [\"dv\"] }"), "a media word that is none, by media");
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

    ManifestBundle missing;
    missing.key = "x";
    missing.title = "X";
    missing.files = {{"games/NOPE.SAV", {}, {}}};
    CHECK_THROWS_WITH_AS((void)bundlePaths(missing, repo), doctest::Contains("games/NOPE.SAV"), std::runtime_error);
    ManifestBundle empty;
    empty.key = "y";
    empty.title = "Y";
    empty.files = {{"games/tetris/*", {}, {}}};
    CHECK_THROWS_AS((void)bundlePaths(empty, repo), std::runtime_error);
}

TEST_CASE("a preset's recipe: the system read, every file named, dated and placed") {
    const Manifest m = parseManifest(kToml);
    const auto repo = repository();
    const ComposeRecipe r = recipeFor(m, selectionOf(m, *m.preset("games")), repo);
    CHECK(r.system == *repo.read("systems/osa.dsk"));
    CHECK(r.media == Media::dz);
    CHECK(r.volumeId == "GAMES");
    CHECK(r.owner == "MS0515 EMU");
    CHECK(r.secondOwner == "MS0515 EMU");
    CHECK(r.startup == std::vector<std::string>{"SET TT QUIET"});
    CHECK(r.banner == std::vector<std::string>{"Type a game to run it"});
    REQUIRE(r.groups.size() == 6);                             /* and DUP, the system's suggestion, last */
    CHECK(r.groups[5].title == "DUP");
    CHECK(r.groups[0].title == "DZ.SYS");                      /* the system's own parts first */
    CHECK(r.groups[1].title == "TT.SYS");
    const auto &sab = r.groups[2];
    CHECK(sab.title == "Saboteur 2");
    CHECK(sab.place == Place::boot);
    REQUIRE(sab.files.size() == 2);
    CHECK(sab.files[0].name == "SABOT2.SAV");
    CHECK(sab.files[1].data == blocks(5, 2));
    CHECK(sab.files[0].date == encodeDate(1990, 11, 9));
    CHECK_FALSE(sab.files[0].protect);
    const auto &docs = r.groups[4];
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
    CHECK(boot->directory.find("SL.SYS") == nullptr);          /* on the exemplar, not chosen */

    Selection dv = selectionOf(m, *m.preset("games"));
    dv.media = Media::dv;
    dv.bundles = {"docs"};
    const ComposeRecipe onDv = recipeFor(m, dv, repo);
    CHECK(onDv.groups[2].title == "DV.SYS");                   /* the media's own requirement */
    CHECK(bootedMonitor(composeDisk(onDv), 0, true, Vol::dv) == "RT11SJ");
}

TEST_CASE("a system's suggestions: ticked for a preset, by its preference, unless the preset chose among them - never required") {
    const Manifest m = parseManifest(kToml);
    CHECK(m.system("osa")->suggests == std::vector<std::string>{"dup"});
    CHECK(m.system("rodionov")->suggests.empty());
    Selection games = selectionOf(m, *m.preset("games"));
    CHECK(games.bundles == std::vector<std::string>{"sabot2", "pacman", "docs", "dup"});
    /* Without it the disk still resolves: a suggestion is no requirement. */
    CHECK(resolveBundles(m, "osa", Media::dz, {"sabot2"}, {}).ok);
    CHECK(suggestedBundles(m, *m.system("osa"), Media::dz, {}) == std::vector<std::string>{"dup"});
    CHECK(suggestedBundles(m, *m.system("osa"), Media::dz, {"dup"}).empty());
    CHECK(suggestedBundles(m, *m.system("rodionov"), Media::dz, {}).empty());
}

TEST_CASE("a system's recipe: its reserved blocks, and a startup of the system's, the bundles' and the selection's lines") {
    const Manifest m = parseManifest(kToml);
    const ComposeRecipe r = recipeFor(m, selectionOf(m, *m.preset("rodionov")), repository());
    CHECK(r.reserved.size() == 2);
    CHECK(r.startup == std::vector<std::string>{"SET TT QUIET", "SET SL ON", "LOAD VM:", "R ROSA3"});   /* SET SL ON once */
    CHECK(planDisk(r).ok);
}

TEST_CASE("a selection that breaks a rule is refused before anything is read") {
    const Manifest m = parseManifest(kToml);
    const auto repo = repository();
    auto selection = [](const char *system, Media media, std::vector<std::string> bundles) {
        Selection s;
        s.system = system;
        s.media = media;
        s.bundles = std::move(bundles);
        return s;
    };
    CHECK_THROWS_WITH_AS((void)recipeFor(m, selection("osa", Media::dv, {"sabot2"}), repo),
                         doctest::Contains("Saboteur 2"), std::runtime_error);
    CHECK_THROWS_AS((void)recipeFor(m, selection("rodionov", Media::ss, {}), repo), std::runtime_error);
    CHECK_THROWS_AS((void)recipeFor(m, selection("mihin", Media::ss, {}), repo), std::runtime_error);
    CHECK_THROWS_AS((void)recipeFor(m, selection("osa", Media::ss, {"tetris"}), repo), std::runtime_error);
}

namespace {

const char kDeps[] = R"toml(
format = 1

[system.omega]
title = "OMEGA"
image = "systems/omega.dsk"
media = ["ss", "dz", "dv"]
requires = ["dz"]
requires_by_media = { dv = ["dv"] }

[system.omega2]
title = "OMEGA vvv104"
image = "systems/omega.dsk"
media = ["ss", "dz", "dv"]
requires = ["dz"]
requires_by_media = { dv = ["dv"] }

[system.mihin]
title = "OS-16SJ"
image = "systems/omega.dsk"
media = ["ss", "dz"]
requires = ["dz"]

[bundle.dz]
title = "DZ.SYS"
files = ["handlers/DZ.SYS"]

[bundle.dv]
title = "DV.SYS"
files = ["handlers/DV.SYS"]

[bundle.sysmac]
title = "SYSMAC.SML"
group = "Development / Libraries"
files = ["dev/SYSMAC.SML"]

[bundle.macro-omega]
title    = "MACRO-11 (OMEGA build)"
group    = "Development / Assembler"
provides = ["macro11"]
systems  = ["omega"]
requires = ["sysmac"]
files    = ["dev/omega/MACRO.SAV"]

[bundle.macro-vvv]
title    = "MACRO-11 (vvv104 build)"
provides = ["macro11"]
systems  = ["omega", "omega2"]
requires = ["sysmac"]
files    = ["dev/vvv/MACRO.SAV"]

[bundle.macro-mihin]
title    = "MACRO-11 (Mihin build)"
provides = ["macro11"]
systems  = ["mihin"]
files    = ["dev/mihin/MACRO.SAV"]

[bundle.link-vvv]
title    = "LINK (vvv104 build)"
provides = ["link"]
systems  = ["omega", "omega2"]
files    = ["dev/vvv/LINK.SAV"]

[bundle.pascal]
title    = "Pascal"
requires = ["macro11", "link"]
prefer   = ["macro-vvv"]
files    = ["dev/PAS1.SAV"]

[bundle.pascal-graphics]
title    = "Pascal graphics"
requires = ["pascal"]
files    = ["dev/PASGRF.OBJ"]

[preset.dev]
title   = "Development"
system  = "omega2"
media   = "dv"
bundles = ["pascal-graphics"]
)toml";

Repository depsRepository()
{
    auto files = std::make_shared<std::map<std::string, std::vector<uint8_t>>>();
    (*files)["systems/omega.dsk"] = exemplar(Media::dv);
    for (const char *p : {"dev/SYSMAC.SML", "dev/omega/MACRO.SAV", "dev/vvv/MACRO.SAV", "dev/mihin/MACRO.SAV",
                          "dev/vvv/LINK.SAV", "dev/PAS1.SAV", "dev/PASGRF.OBJ"})
        (*files)[p] = blocks(2, 7);
    const auto kit = openVolume(exemplar(Media::dv), Vol::dv);
    (*files)["handlers/DZ.SYS"] = kit->readFile("DZ.SYS");
    (*files)["handlers/DV.SYS"] = kit->readFile("DV.SYS");
    Repository repo;
    for (const auto &kv : *files) repo.paths.push_back(kv.first);
    repo.read = [files](const std::string &p) -> std::optional<std::vector<uint8_t>> {
        const auto it = files->find(p);
        if (it == files->end()) return std::nullopt;
        return it->second;
    };
    return repo;
}

}  /* namespace */

TEST_CASE("dependencies: read with their groups, what they provide and prefer") {
    const Manifest m = parseManifest(kDeps);
    const auto *p = m.bundle("pascal");
    REQUIRE(p);
    CHECK(p->dependsOn == std::vector<std::string>{"macro11", "link"});
    CHECK(p->prefer == std::vector<std::string>{"macro-vvv"});
    CHECK(m.bundle("macro-omega")->provides == std::vector<std::string>{"macro11"});
    CHECK(m.bundle("macro-omega")->group == "Development / Assembler");
    CHECK(m.bundle("macro-vvv")->group.empty());
}

TEST_CASE("dependencies: what disks.toml must not say about them") {
    const std::string good = kDeps;
    auto refused = [](const std::string &text, const char *why) {
        CAPTURE(why);
        CHECK_THROWS_AS((void)parseManifest(text), std::runtime_error);
    };
    refused(replaced(good, "requires = [\"pascal\"]", "requires = [\"cobol\"]"), "a need nothing provides");
    refused(replaced(good, "prefer   = [\"macro-vvv\"]", "prefer   = [\"macro-pdp\"]"), "a preference that is no bundle");
    refused(replaced(good, "prefer   = [\"macro-vvv\"]", "prefer   = [\"sysmac\"]"), "a preference that provides none of the needs");
    refused(replaced(good, "title = \"SYSMAC.SML\"", "title = \"SYSMAC.SML\"\nrequires = [\"pascal\"]"), "a cycle through a provided name");
}

TEST_CASE("dependencies: the chosen and what they need, a need before what needs it, each once") {
    const Manifest m = parseManifest(kDeps);
    const Resolution r = resolveBundles(m, "omega2", Media::dv, {"pascal-graphics"});
    REQUIRE_MESSAGE(r.ok, r.problem);
    CHECK(r.bundles == std::vector<std::string>{"dz", "dv", "sysmac", "macro-vvv", "link-vvv", "pascal", "pascal-graphics"});
    CHECK(r.addedFor.size() == 4);
    CHECK(std::find(r.addedFor.begin(), r.addedFor.end(), std::pair<std::string, std::string>{"pascal", "pascal-graphics"}) != r.addedFor.end());
    CHECK(std::find(r.addedFor.begin(), r.addedFor.end(), std::pair<std::string, std::string>{"macro-vvv", "pascal"}) != r.addedFor.end());

    const Resolution again = resolveBundles(m, "omega2", Media::dv, {"pascal", "pascal-graphics", "sysmac"});
    REQUIRE(again.ok);
    CHECK(again.bundles.size() == 7);                        /* chosen twice over: still once each */
}

TEST_CASE("alternatives: one of them, the preferred unless chosen or picked, never two") {
    const Manifest m = parseManifest(kDeps);
    CHECK(candidatesFor(m, "macro11", "omega", Media::dv).size() == 2);
    CHECK(candidatesFor(m, "macro11", "omega2", Media::dv).size() == 1);
    CHECK(candidatesFor(m, "macro11", "omega", Media::dv)[0]->key == "macro-omega");   /* file order */

    auto macroOf = [](const Resolution &r) {
        for (const auto &b : r.bundles) if (b.rfind("macro-", 0) == 0) return b;
        return std::string();
    };
    Resolution r = resolveBundles(m, "omega", Media::dv, {"pascal"});
    REQUIRE(r.ok);
    CHECK(macroOf(r) == "macro-vvv");                        /* the preference, over the file order */

    r = resolveBundles(m, "omega", Media::dv, {"pascal"}, {{"macro11", "macro-omega"}});
    REQUIRE(r.ok);
    CHECK(macroOf(r) == "macro-omega");                      /* the user's pick */

    r = resolveBundles(m, "omega", Media::dv, {"macro-omega", "pascal"});
    REQUIRE(r.ok);
    CHECK(macroOf(r) == "macro-omega");                      /* chosen outright */
    CHECK(std::count_if(r.bundles.begin(), r.bundles.end(), [](const std::string &b) { return b.rfind("macro-", 0) == 0; }) == 1);

    r = resolveBundles(m, "omega", Media::dv, {"macro-omega", "macro-vvv"});
    CHECK_FALSE(r.ok);                                       /* two MACROs */
    CHECK(r.problem.find("MACRO-11 (OMEGA build)") != std::string::npos);
    CHECK(r.problem.find("MACRO-11 (vvv104 build)") != std::string::npos);

    r = resolveBundles(m, "omega", Media::dv, {"pascal"}, {{"macro11", "macro-mihin"}});
    CHECK_FALSE(r.ok);                                       /* a pick that is not for this system */
}

TEST_CASE("a preference per system: what sat next to it on that system's original disks") {
    const std::string text = replaced(replaced(replaced(kDeps,
        "prefer   = [\"macro-vvv\"]", "prefer   = { mihin = [\"macro-mihin\"], default = [\"macro-vvv\"] }"),
        "systems  = [\"mihin\"]\nfiles    = [\"dev/mihin/MACRO.SAV\"]", "files    = [\"dev/mihin/MACRO.SAV\"]"),
        "systems  = [\"omega\", \"omega2\"]\nfiles    = [\"dev/vvv/LINK.SAV\"]", "files    = [\"dev/vvv/LINK.SAV\"]");
    const Manifest m = parseManifest(text);
    CHECK(m.bundle("pascal")->prefer == std::vector<std::string>{"macro-vvv"});
    CHECK(m.bundle("pascal")->preferBySystem.at("mihin") == std::vector<std::string>{"macro-mihin"});
    auto macroOf = [&](const char *system) {
        const Resolution r = resolveBundles(m, system, Media::dz, {"pascal"});
        REQUIRE_MESSAGE(r.ok, r.problem);
        for (const auto &b : r.bundles) if (b.rfind("macro-", 0) == 0) return b;
        return std::string();
    };
    CHECK(macroOf("mihin") == "macro-mihin");
    CHECK(macroOf("omega2") == "macro-vvv");
    CHECK_THROWS_AS((void)parseManifest(replaced(text, "mihin = [\"macro-mihin\"]", "mihin = [\"sysmac\"]")), std::runtime_error);
    CHECK_THROWS_AS((void)parseManifest(replaced(text, "mihin = [\"macro-mihin\"]", "pdp = [\"macro-mihin\"]")), std::runtime_error);
}

TEST_CASE("a system requires a name: its own build preferred, another one picked in its place") {
    const std::string text = replaced(replaced(kDeps,
        "title = \"OMEGA vvv104\"\nimage = \"systems/omega.dsk\"\nmedia = [\"ss\", \"dz\", \"dv\"]\nrequires = [\"dz\"]",
        "title = \"OMEGA vvv104\"\nimage = \"systems/omega.dsk\"\nmedia = [\"ss\", \"dz\", \"dv\"]\nrequires = [\"dz\", \"link\"]\n"
        "prefer = [\"link-vvv\"]"),
        "[bundle.link-vvv]", "[bundle.link-omega]\ntitle    = \"LINK (OMEGA build)\"\nprovides = [\"link\"]\n"
        "files    = [\"dev/omega/LINK.SAV\"]\n\n[bundle.link-vvv]");
    const Manifest m = parseManifest(text);
    CHECK(m.system("omega2")->prefer == std::vector<std::string>{"link-vvv"});
    Resolution r = resolveBundles(m, "omega2", Media::dz, {});
    REQUIRE_MESSAGE(r.ok, r.problem);
    CHECK(r.bundles == std::vector<std::string>{"dz", "link-vvv"});           /* over the file order */
    r = resolveBundles(m, "omega2", Media::dz, {}, {{"link", "link-omega"}});
    REQUIRE_MESSAGE(r.ok, r.problem);
    CHECK(r.bundles == std::vector<std::string>{"dz", "link-omega"});
    CHECK(r.addedFor.empty());                                                /* the system's, still */
    CHECK_THROWS_AS((void)parseManifest(replaced(text, "prefer = [\"link-vvv\"]", "prefer = [\"sysmac\"]")), std::runtime_error);
    CHECK_THROWS_AS((void)parseManifest(replaced(text, "prefer = [\"link-vvv\"]", "prefer = [\"link-pdp\"]")), std::runtime_error);
}

TEST_CASE("the labels: each side's volume id and owner, the second side's on a two-sided disk only") {
    const Manifest m = parseManifest(kDeps);
    Selection s = selectionOf(m, *m.preset("dev"));
    s.volumeId = "DEV";
    s.owner = "VVV104";
    s.secondVolumeId = "TWO";
    s.secondOwner = "ME";
    ComposeRecipe r = recipeFor(m, s, depsRepository());
    CHECK(r.volumeId == "DEV");
    CHECK(r.owner == "VVV104");
    CHECK_FALSE(r.secondVolumeId.has_value());                               /* one DV volume */
    CHECK_FALSE(r.secondOwner.has_value());
    s.media = Media::dz;
    r = recipeFor(m, s, depsRepository());
    CHECK(r.secondVolumeId == "TWO");
    CHECK(r.secondOwner == "ME");
}

TEST_CASE("a need nothing on this system satisfies refuses what needs it, saying which") {
    const Manifest m = parseManifest(kDeps);
    const Resolution r = resolveBundles(m, "mihin", Media::dz, {"pascal-graphics"});
    CHECK_FALSE(r.ok);
    CHECK(r.problem.find("Pascal") != std::string::npos);
    CHECK(r.problem.find("link") != std::string::npos);
    CHECK_FALSE(resolveBundles(m, "omega", Media::dv, {"nothing"}).ok);
    CHECK_FALSE(resolveBundles(m, "mihin", Media::dz, {"macro-vvv"}).ok);   /* not for this system */
}

TEST_CASE("a recipe installs the needs too, before what needs them") {
    const Manifest m = parseManifest(kDeps);
    const ComposeRecipe r = recipeFor(m, selectionOf(m, *m.preset("dev")), depsRepository());
    std::vector<std::string> titles;
    for (const auto &g : r.groups) titles.push_back(g.title);
    CHECK(titles == std::vector<std::string>{"DZ.SYS", "DV.SYS", "SYSMAC.SML", "MACRO-11 (vvv104 build)", "LINK (vvv104 build)", "Pascal", "Pascal graphics"});
    CHECK(planDisk(r).ok);

    Selection s = selectionOf(m, *m.preset("dev"));
    s.system = "omega";
    s.picks = {{"macro11", "macro-omega"}};
    const ComposeRecipe picked = recipeFor(m, s, depsRepository());
    CHECK(picked.groups[3].title == "MACRO-11 (OMEGA build)");

    s.system = "mihin";
    s.media = Media::dz;
    s.picks.clear();
    CHECK_THROWS_AS((void)recipeFor(m, s, depsRepository()), std::runtime_error);
}

TEST_CASE("media words") {
    CHECK(parseMedia("ss") == Media::ss);
    CHECK(parseMedia("dz") == Media::dz);
    CHECK(parseMedia("dv") == Media::dv);
    CHECK_FALSE(parseMedia("DV").has_value());
    CHECK(std::string(mediaWord(Media::dz)) == "dz");
}

}  /* TEST_SUITE */
