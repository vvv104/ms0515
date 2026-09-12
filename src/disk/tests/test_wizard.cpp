/*
 * test_wizard.cpp - the disk wizards' model: rows, marks and reasons, the
 * actions on them, and the file a choice is saved to.
 */

#include <doctest/doctest.h>
#include <ms0515/disk/Manifest.hpp>
#include <ms0515/disk/Wizard.hpp>

#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

using namespace ms0515::disk;

namespace {

const char kManifest[] = R"toml(
format  = 1
version = "2026.09.11-4"

[system.omega]
title    = "OMEGA"
image    = "systems/omega.dsk"
media    = ["ss", "dz", "dv"]
requires = ["dz", "dir"]
requires_by_media = { dv = ["dv"] }
prefer   = ["dir-vvv"]
startup  = ["SET TT QUIET"]

[system.mihin]
title    = "OS-16SJ"
image    = "systems/mihin.dsk"
media    = ["ss", "dz"]
requires = ["dz"]

[bundle.dz]
title = "DZ.SYS"
group = "System"
files = ["h/DZ.SYS"]

[bundle.dv]
title   = "DV.SYS"
group   = "System"
systems = ["omega"]
files   = ["h/DV.SYS"]

[bundle.dir-omega]
title    = "DIR (OMEGA)"
group    = "System"
provides = ["dir"]
files    = ["u/omega/DIR.SAV"]

[bundle.dir-vvv]
title    = "DIR (vvv104)"
group    = "System"
provides = ["dir"]
files    = ["u/vvv/DIR.SAV"]

[bundle.sysmac]
title = "SYSMAC.SML"
group = "Development / Assembler"
files = ["d/SYSMAC.SML"]

[bundle.macro-vvv]
title    = "MACRO (vvv104)"
group    = "Development / Assembler"
provides = ["macro11"]
requires = ["sysmac"]
files    = ["d/vvv/MACRO.SAV"]

[bundle.macro-mihin]
title    = "MACRO (Mihin)"
group    = "Development / Assembler"
provides = ["macro11"]
requires = ["sysmac"]
files    = ["d/mihin/MACRO.SAV"]

[bundle.macro-omega]
title    = "MACRO (OMEGA 064)"
group    = "Development / Assembler"
provides = ["macro11"]
systems  = ["omega"]
requires = ["sysmac"]
files    = ["d/omega/MACRO.SAV"]

[bundle.link-vvv]
title    = "LINK (vvv104)"
group    = "Development / Linker"
provides = ["link"]
systems  = ["omega"]
files    = ["d/vvv/LINK.SAV"]

[bundle.pascal]
title    = "Pascal"
group    = "Development / Pascal"
requires = ["macro11", "link"]
prefer   = ["macro-vvv"]
startup  = ["ASSIGN DK: SRC:"]
files    = ["d/PAS1.SAV"]

[bundle.sabot2]
title = "Saboteur 2"
group = "Games"
needs = ["ss", "dz"]
files = ["g/SABOT2.SAV"]
)toml";

const WizardRow *row(const std::vector<WizardRow> &rows, const std::string &key,
                     WizardRow::Kind kind = WizardRow::Kind::bundle)
{
    for (const auto &r : rows) if (r.key == key && r.kind == kind) return &r;
    return nullptr;
}

std::vector<std::string> headings(const std::vector<WizardRow> &rows)
{
    std::vector<std::string> out;
    for (const auto &r : rows) if (r.kind == WizardRow::Kind::group) out.push_back(r.title);
    return out;
}

std::string replaced(std::string s, const std::string &from, const std::string &to)
{
    const auto at = s.find(from);
    REQUIRE(at != std::string::npos);
    return s.replace(at, from.size(), to);
}

bool mentions(const std::vector<std::string> &notices, const std::string &what)
{
    return std::any_of(notices.begin(), notices.end(), [&](const std::string &n) { return n.find(what) != std::string::npos; });
}

}  /* namespace */

TEST_SUITE("Wizard") {

TEST_CASE("rows: the groups in the file's order, the system's parts marked, the alternatives a radio group") {
    const Manifest m = parseManifest(kManifest);
    DiskWizard w(m, "omega", Media::dz, [](const ManifestBundle &b) { return static_cast<int>(b.title.size()); });
    const auto rows = w.rows(true);
    CHECK(headings(rows) == std::vector<std::string>{"Diskette", "Label", "Operating system", "System", "START.COM",
                                                     "Development", "Assembler", "Linker", "Pascal", "Games"});
    REQUIRE(row(rows, "dz"));
    CHECK(row(rows, "dz")->mark == WizardRow::Mark::system);
    CHECK(row(rows, "dz")->blocks == 6);
    CHECK(row(rows, "dv")->mark == WizardRow::Mark::off);            /* a DZ disk does not need it */

    const auto *heading = row(rows, "macro11", WizardRow::Kind::radio);
    REQUIRE(heading);
    const auto at = static_cast<std::size_t>(heading - rows.data());
    std::vector<std::string> buttons;
    for (std::size_t i = at + 1; i < rows.size() && rows[i].radio; ++i) buttons.push_back(rows[i].key);
    CHECK(buttons == std::vector<std::string>{"macro-vvv", "macro-mihin", "macro-omega"});   /* together */
    CHECK_FALSE(row(rows, "sysmac")->radio);
    CHECK_FALSE(row(rows, "link-vvv")->radio);                      /* one build: a checkbox */

    DiskWizard mihin(m, "mihin", Media::dz);                        /* two MACROs visible there: still a group */
    CHECK(row(mihin.rows(true), "macro11", WizardRow::Kind::radio) != nullptr);
}

TEST_CASE("the blocks of what goes on the disk, by the top group it sits in") {
    const Manifest m = parseManifest(kManifest);
    DiskWizard w(m, "omega", Media::dz, [](const ManifestBundle &b) { return static_cast<int>(b.title.size()); });
    REQUIRE(w.toggle("pascal").empty());
    using Groups = std::vector<std::pair<std::string, int>>;
    CHECK(w.blocksByGroup() == Groups{{"System", 6 + 12}, {"Development", 10 + 14 + 13 + 6}});
    CHECK(row(w.rows(), "Development", WizardRow::Kind::group)->summary == "4 selected, 43 blocks");
}

TEST_CASE("alternatives of which this system shows only one are a plain line, not a radio group") {
    const Manifest m = parseManifest(replaced(kManifest, "title    = \"MACRO (Mihin)\"",
                                              "title    = \"MACRO (Mihin)\"\nsystems  = [\"omega\"]"));
    DiskWizard w(m, "mihin", Media::dz);
    const auto rows = w.rows(true);
    CHECK(row(rows, "macro11", WizardRow::Kind::radio) == nullptr);
    REQUIRE(row(rows, "macro-vvv"));
    CHECK_FALSE(row(rows, "macro-vvv")->radio);
}

TEST_CASE("a choice brings its needs, each saying for whom; the preferred alternative is the one on") {
    const Manifest m = parseManifest(kManifest);
    DiskWizard w(m, "omega", Media::dz);
    CHECK(w.toggle("pascal").empty());
    const auto rows = w.rows(true);
    CHECK(row(rows, "pascal")->mark == WizardRow::Mark::on);
    CHECK(row(rows, "macro-vvv")->mark == WizardRow::Mark::added);
    CHECK(row(rows, "macro-vvv")->requiredBy == "Pascal");
    CHECK(row(rows, "sysmac")->mark == WizardRow::Mark::added);
    CHECK(row(rows, "sysmac")->requiredBy == "MACRO (vvv104)");
    CHECK(row(rows, "link-vvv")->mark == WizardRow::Mark::added);
    CHECK(row(rows, "macro-mihin")->mark == WizardRow::Mark::off);
    CHECK(w.selection().bundles == std::vector<std::string>{"pascal"});

    CHECK_FALSE(w.toggle("sysmac").empty());                        /* needed: it stays */
    CHECK_FALSE(w.toggle("dz").empty());                            /* the system's */
}

TEST_CASE("a radio button: picked, it replaces the other; needed, it cannot be cleared; not needed, it can") {
    const Manifest m = parseManifest(kManifest);
    DiskWizard w(m, "omega", Media::dz);
    REQUIRE(w.toggle("pascal").empty());
    CHECK(w.toggle("macro-mihin").empty());
    auto rows = w.rows(true);
    CHECK(row(rows, "macro-mihin")->mark != WizardRow::Mark::off);
    CHECK(row(rows, "macro-vvv")->mark == WizardRow::Mark::off);
    CHECK(w.selection().picks.at("macro11") == "macro-mihin");
    const auto macros = std::count_if(w.resolution().bundles.begin(), w.resolution().bundles.end(),
                                      [](const std::string &b) { return b.rfind("macro-", 0) == 0; });
    CHECK(macros == 1);

    CHECK_FALSE(w.toggle("macro-mihin").empty());                   /* Pascal needs one */

    REQUIRE(w.toggle("pascal").empty());                            /* Pascal gone: nothing needs MACRO */
    rows = w.rows(true);
    CHECK(row(rows, "macro-mihin")->mark == WizardRow::Mark::off);
    CHECK(w.toggle("macro-omega").empty());                         /* a MACRO on its own */
    CHECK(row(w.rows(true), "macro-omega")->mark == WizardRow::Mark::on);
    CHECK(row(w.rows(true), "macro-vvv")->available);                   /* the others stay pickable: they replace it */
    CHECK(row(w.rows(true), "macro-vvv")->why.empty());
    CHECK(w.toggle("macro-vvv").empty());                           /* another replaces it */
    rows = w.rows(true);
    CHECK(row(rows, "macro-vvv")->mark == WizardRow::Mark::on);
    CHECK(row(rows, "macro-omega")->mark == WizardRow::Mark::off);
    CHECK(w.toggle("macro-vvv").empty());                           /* and clears */
    CHECK(row(w.rows(true), "macro-vvv")->mark == WizardRow::Mark::off);
    CHECK(w.resolution().bundles == std::vector<std::string>{"dz", "dir-vvv"});
}

TEST_CASE("what cannot be taken is greyed out with the reason, and toggling it says the same") {
    const Manifest m = parseManifest(kManifest);
    DiskWizard w(m, "omega", Media::dv);
    const auto rows = w.rows(true);
    CHECK(row(rows, "dv")->mark == WizardRow::Mark::system);        /* a DV disk needs it */
    const auto *sab = row(rows, "sabot2");
    REQUIRE(sab);
    CHECK_FALSE(sab->available);
    CHECK(sab->why.find("dv") != std::string::npos);
    CHECK(w.toggle("sabot2") == sab->why);
    CHECK(w.selection().bundles.empty());
}

TEST_CASE("another system or media drops what no longer fits, and says so") {
    const Manifest m = parseManifest(kManifest);
    DiskWizard w(m, "omega", Media::dz);
    REQUIRE(w.toggle("sabot2").empty());
    REQUIRE(w.toggle("pascal").empty());
    CHECK(w.setMedia(Media::dv).empty());
    CHECK(w.selection().bundles == std::vector<std::string>{"pascal"});
    CHECK(mentions(w.notices(), "Saboteur 2"));

    CHECK(w.setSystem("mihin") == "OS-16SJ goes only on ss, dz");  /* the diskette comes first */
    CHECK(w.system() == "omega");
    REQUIRE(w.setMedia(Media::ss).empty());
    CHECK(w.setSystem("mihin").empty());                            /* no LINK there for Pascal */
    CHECK(w.selection().bundles.empty());
    CHECK(mentions(w.notices(), "Pascal"));
    auto rows = w.rows(true);
    CHECK(row(rows, "macro-omega") == nullptr);                     /* another system's build: not shown */
    CHECK(row(rows, "dv") == nullptr);
    CHECK_FALSE(row(rows, "pascal")->available);                     /* shown, with why */

    REQUIRE(w.toggle("sabot2").empty());
    CHECK(w.setMedia(Media::dv).empty());                           /* the system does not go on it: it goes */
    CHECK(mentions(w.notices(), "OS-16SJ"));
    CHECK(w.system().empty());
    CHECK_FALSE(w.ready());
    rows = w.rows();
    CHECK(row(rows, "#system", WizardRow::Kind::group)->open);      /* to choose again */
    CHECK_FALSE(row(rows, "Games", WizardRow::Kind::group)->available);
    REQUIRE(w.setSystem("omega").empty());
    CHECK(mentions(w.notices(), "Saboteur 2"));                     /* kept until a system judged it */
    CHECK(w.selection().bundles.empty());
}

TEST_CASE("the steps: the diskette first, then the system, then the rest") {
    const Manifest m = parseManifest(kManifest);
    DiskWizard w(m);
    CHECK_FALSE(w.ready());
    CHECK_FALSE(w.media().has_value());
    auto rows = w.rows();
    CHECK(headings(rows) == std::vector<std::string>{"Diskette", "Label", "Operating system", "System", "Development", "Games"});
    const auto *diskette = row(rows, "#diskette", WizardRow::Kind::group);
    REQUIRE(diskette);
    CHECK(diskette->open);
    REQUIRE(row(rows, "ss", WizardRow::Kind::media));
    CHECK(row(rows, "ss", WizardRow::Kind::media)->title == "ss - one side, 400 KB");
    CHECK(row(rows, "dv", WizardRow::Kind::media)->radio);
    CHECK(row(rows, "dv", WizardRow::Kind::media)->mark == WizardRow::Mark::off);
    const auto *os = row(rows, "#system", WizardRow::Kind::group);
    REQUIRE(os);
    CHECK_FALSE(os->available);
    CHECK(os->why == "choose the diskette first");
    CHECK(row(rows, "omega", WizardRow::Kind::system) == nullptr);
    CHECK_FALSE(row(rows, "Development", WizardRow::Kind::group)->available);
    CHECK(row(rows, "Development", WizardRow::Kind::group)->why == "choose the system first");
    CHECK(row(rows, "pascal") == nullptr);
    CHECK_FALSE(w.toggle("pascal").empty());
    CHECK(w.setSystem("omega") == "choose the diskette first");

    REQUIRE(w.setMedia(Media::dv).empty());
    rows = w.rows();
    CHECK(row(rows, "#diskette", WizardRow::Kind::group)->open);          /* what is chosen stays in sight */
    CHECK(row(rows, "#diskette", WizardRow::Kind::group)->summary == "dv - one DV volume, 800 KB");
    CHECK(row(rows, "dv", WizardRow::Kind::media)->mark == WizardRow::Mark::on);
    CHECK(row(rows, "#system", WizardRow::Kind::group)->available);
    CHECK(row(rows, "#system", WizardRow::Kind::group)->open);
    CHECK(row(rows, "omega", WizardRow::Kind::system)->available);
    const auto *mihin = row(rows, "mihin", WizardRow::Kind::system);
    REQUIRE(mihin);
    CHECK_FALSE(mihin->available);
    CHECK(mihin->why == "only on ss, dz");
    CHECK_FALSE(row(rows, "Games", WizardRow::Kind::group)->available);

    CHECK(w.setSystem("omega").empty());
    CHECK(w.ready());
    rows = w.rows();
    CHECK(row(rows, "#system", WizardRow::Kind::group)->open);
    CHECK(row(rows, "#system", WizardRow::Kind::group)->summary == "OMEGA");
    CHECK(row(rows, "omega", WizardRow::Kind::system)->mark == WizardRow::Mark::on);
    CHECK(row(rows, "Development", WizardRow::Kind::group)->available);
    CHECK_FALSE(row(rows, "Development", WizardRow::Kind::group)->open);
    CHECK(row(rows, "pascal") == nullptr);                          /* folded */
    CHECK(w.toggle("pascal").empty());                              /* but there */
}

TEST_CASE("the groups are a tree, folded, each saying how many are chosen") {
    const Manifest m = parseManifest(kManifest);
    DiskWizard w(m, "omega", Media::dz);
    REQUIRE(w.toggle("pascal").empty());
    auto rows = w.rows();
    CHECK(headings(rows) == std::vector<std::string>{"Diskette", "Label", "Operating system", "System", "Development", "Games"});
    CHECK(row(rows, "#diskette", WizardRow::Kind::group)->summary == "dz - two sides, 800 KB");
    CHECK(row(rows, "Development", WizardRow::Kind::group)->summary == "4 selected, 0 blocks");   /* chosen or brought along */
    CHECK(row(rows, "System", WizardRow::Kind::group)->summary == "2 selected, 0 blocks");        /* the system's too */
    CHECK(row(rows, "Games", WizardRow::Kind::group)->summary.empty());

    w.toggleFold("Development");
    rows = w.rows();
    CHECK(headings(rows) == std::vector<std::string>{"Diskette", "Label", "Operating system", "System", "Development",
                                                     "Assembler", "Linker", "Pascal", "Games"});
    const auto *assembler = row(rows, "Development / Assembler", WizardRow::Kind::group);
    REQUIRE(assembler);
    CHECK(assembler->depth == 1);
    CHECK_FALSE(assembler->open);
    CHECK(assembler->summary == "2 selected, 0 blocks");
    CHECK(row(rows, "pascal") == nullptr);

    w.toggleFold("Development / Pascal");
    REQUIRE(row(w.rows(), "pascal"));
    CHECK(row(w.rows(), "pascal")->depth == 2);
    CHECK(row(w.rows(), "pascal")->parent == "Development / Pascal");

    w.toggleFold("Development");                                    /* the branch folds whole */
    CHECK(row(w.rows(), "pascal") == nullptr);
    w.reveal("Development / Assembler");                            /* what find does: the way down opened */
    rows = w.rows();
    REQUIRE(row(rows, "macro-vvv"));
    CHECK(row(rows, "macro-vvv")->depth == 3);                      /* under its radio heading */
    CHECK(row(rows, "macro11", WizardRow::Kind::radio)->depth == 2);
    CHECK(row(rows, "pascal")->depth == 2);                         /* opened before, open again */

    const SavedSelection saved = w.saved();
    DiskWizard other(m);
    other.load(saved);
    CHECK(other.ready());
    rows = other.rows();                                            /* opened: where something is chosen */
    CHECK(headings(rows) == std::vector<std::string>{"Diskette", "Label", "Operating system", "System", "Development",
                                                     "Assembler", "Linker", "Pascal", "Games"});
    CHECK(row(rows, "#diskette", WizardRow::Kind::group)->open);
    CHECK(row(rows, "#system", WizardRow::Kind::group)->open);
    CHECK_FALSE(row(rows, "#label", WizardRow::Kind::group)->open);   /* no label typed */
    CHECK_FALSE(row(rows, "System", WizardRow::Kind::group)->open);   /* only the system's own there */
    CHECK(row(rows, "Development / Pascal", WizardRow::Kind::group)->open);
    CHECK(row(rows, "Development / Assembler", WizardRow::Kind::group)->open);   /* what came along too */
    CHECK_FALSE(row(rows, "Games", WizardRow::Kind::group)->open);

    SavedSelection more = saved;
    more.selection.volumeId = "DEV";
    more.selection.picks["dir"] = "dir-omega";                      /* not the native DIR */
    DiskWizard third(m);
    third.load(more);
    rows = third.rows();
    CHECK(row(rows, "#label", WizardRow::Kind::group)->open);
    CHECK(row(rows, "System", WizardRow::Kind::group)->open);
}

TEST_CASE("the system's utilities: its own build marked native and on, another build can take its place") {
    const Manifest m = parseManifest(kManifest);
    DiskWizard w(m, "omega", Media::dz);
    auto rows = w.rows(true);
    REQUIRE(row(rows, "dir", WizardRow::Kind::radio));
    CHECK(row(rows, "dir-vvv")->mark == WizardRow::Mark::system);
    CHECK(row(rows, "dir-vvv")->native);
    CHECK(row(rows, "dir-omega")->mark == WizardRow::Mark::off);
    CHECK_FALSE(row(rows, "dir-omega")->native);
    CHECK(row(rows, "dir-omega")->available);
    CHECK_FALSE(row(rows, "dz")->native);                           /* no alternative: nothing to call native */

    CHECK(w.toggle("dir-omega").empty());
    rows = w.rows(true);
    CHECK(row(rows, "dir-omega")->mark == WizardRow::Mark::system);
    CHECK(row(rows, "dir-vvv")->mark == WizardRow::Mark::off);
    CHECK(row(rows, "dir-vvv")->native);
    CHECK(w.selection().bundles.empty());
    CHECK(w.selection().picks.at("dir") == "dir-omega");
    CHECK_FALSE(w.toggle("dir-omega").empty());                     /* the system needs one */
    CHECK(w.toggle("dir-vvv").empty());
    CHECK(row(w.rows(true), "dir-vvv")->mark == WizardRow::Mark::system);
}

TEST_CASE("a bundle ticked that the system comes to require is the system's - locked - and ticked again when it no longer is") {
    const Manifest m = parseManifest(kManifest);
    DiskWizard w(m, "omega", Media::dz);
    REQUIRE(w.toggle("dv").empty());
    CHECK(row(w.rows(true), "dv")->mark == WizardRow::Mark::on);
    REQUIRE(w.setMedia(Media::dv).empty());
    CHECK(row(w.rows(true), "dv")->mark == WizardRow::Mark::system);
    CHECK_FALSE(w.toggle("dv").empty());                            /* the system's rule: it cannot go */
    REQUIRE(w.setMedia(Media::dz).empty());
    CHECK(row(w.rows(true), "dv")->mark == WizardRow::Mark::on);
}

TEST_CASE("the label after the diskette, START.COM at the end of the system's group: fields to edit") {
    const Manifest m = parseManifest(kManifest);
    DiskWizard w(m);
    auto rows = w.rows();
    REQUIRE(row(rows, "#label", WizardRow::Kind::group));
    CHECK_FALSE(row(rows, "#label", WizardRow::Kind::group)->available);
    CHECK(w.setField(kVolumeIdField, "MYDISK") == "choose the diskette first");

    REQUIRE(w.setMedia(Media::ss).empty());
    rows = w.rows();
    CHECK(row(rows, "#label", WizardRow::Kind::group)->open);           /* opened with the diskette */
    const auto *id = row(rows, kVolumeIdField, WizardRow::Kind::field);
    REQUIRE(id);
    CHECK(id->title == "Volume id");
    CHECK(id->value.empty());
    CHECK(row(rows, kOwnerField, WizardRow::Kind::field)->title == "Owner");
    CHECK(row(rows, kSecondVolumeIdField, WizardRow::Kind::field) == nullptr);   /* one side */
    CHECK(w.setField(kSecondVolumeIdField, "TEXT") == "only a two-sided diskette has a second side");

    REQUIRE(w.setMedia(Media::dz).empty());
    rows = w.rows();
    std::vector<std::string> titles;
    for (const auto &r : rows) if (r.parent == kLabelGroup) titles.push_back(r.title);
    CHECK(titles == std::vector<std::string>{"DZ0: volume id", "DZ0: owner", "DZ2: volume id", "DZ2: owner"});
    CHECK(w.setField(kVolumeIdField, "MYDISK").empty());
    CHECK(w.selection().volumeId == "MYDISK");
    CHECK(row(w.rows(), "#label", WizardRow::Kind::group)->summary == "MYDISK");
    CHECK(w.setField(kOwnerField, "vvv104").empty());
    CHECK(w.selection().owner == "VVV104");
    CHECK(w.setField(kSecondVolumeIdField, "text").empty());
    CHECK(w.setField(kSecondOwnerField, "Me").empty());
    CHECK(w.selection().secondVolumeId == "TEXT");
    CHECK(w.selection().secondOwner == "ME");
    CHECK(row(w.rows(), "#label", WizardRow::Kind::group)->summary == "MYDISK \xC2\xB7 TEXT");
    CHECK(row(w.rows(), kSecondOwnerField, WizardRow::Kind::field)->value == "ME");
    CHECK(row(w.rows(), kVolumeIdField, WizardRow::Kind::field)->value == "MYDISK");
    CHECK(w.setField(kVolumeIdField, "ABCDEFGHIJKLMNOP").empty());
    CHECK(w.selection().volumeId == "ABCDEFGHIJKL");                    /* RT-11 keeps twelve */
    CHECK(w.setField(kVolumeIdField, "").empty());
    CHECK_FALSE(w.selection().volumeId.has_value());
    CHECK(row(w.rows(), "#startup", WizardRow::Kind::group) == nullptr);

    REQUIRE(w.setSystem("omega").empty());
    REQUIRE(w.toggle("pascal").empty());
    w.toggleFold("System");
    rows = w.rows();
    const auto *startup = row(rows, "#startup", WizardRow::Kind::group);
    REQUIRE(startup);
    CHECK(startup->depth == 1);
    CHECK(startup->parent == "System");
    CHECK(startup->open);
    CHECK(startup->summary == "2 lines");
    const auto at = static_cast<std::size_t>(startup - rows.data());
    REQUIRE(at + 4 < rows.size());
    CHECK(rows[at - 1].parent == "System");                             /* after all of the system's rows */
    CHECK(rows[at + 1].kind == WizardRow::Kind::line);
    CHECK(rows[at + 1].title == "SET TT QUIET");
    CHECK(rows[at + 1].requiredBy == "OMEGA");
    CHECK(rows[at + 2].title == "ASSIGN DK: SRC:");
    CHECK(rows[at + 2].requiredBy == "Pascal");
    CHECK(rows[at + 3].kind == WizardRow::Kind::field);                 /* a new line to type */
    CHECK(rows[at + 3].key == "#startup:0");
    CHECK(rows[at + 4].kind == WizardRow::Kind::group);
    CHECK(rows[at + 4].title == "Development");

    CHECK(w.setField("#startup:0", "  R PAS1 ").empty());
    CHECK(w.setField("#startup:1", "DIR").empty());
    CHECK(w.selection().startup == std::vector<std::string>{"R PAS1", "DIR"});
    rows = w.rows();
    CHECK(row(rows, "#startup:0", WizardRow::Kind::field)->value == "R PAS1");
    CHECK(row(rows, "#startup:2", WizardRow::Kind::field)->value.empty());
    CHECK(row(rows, "#startup", WizardRow::Kind::group)->summary == "4 lines");
    CHECK(w.setField("#startup:0", "").empty());                        /* emptied: gone */
    CHECK(w.selection().startup == std::vector<std::string>{"DIR"});
    CHECK(w.setField("#startup:0", "").empty());
    CHECK_FALSE(w.selection().startup.has_value());
    w.toggleFold("System");
    CHECK(row(w.rows(), "#startup", WizardRow::Kind::group) == nullptr);
}

TEST_CASE("the saved choice: its own file, tied to the collection's version") {
    const Manifest m = parseManifest(kManifest);
    CHECK(m.version == "2026.09.11-4");
    DiskWizard w(m, "omega", Media::dz);
    REQUIRE(w.toggle("pascal").empty());
    REQUIRE(w.toggle("macro-omega").empty());
    w.setStartup({"R PAS1"});
    w.setVolumeId(std::string("MYDEV"));
    REQUIRE(w.setField(kOwnerField, "VVV").empty());
    REQUIRE(w.setField(kSecondVolumeIdField, "SIDE1").empty());
    REQUIRE(w.setField(kSecondOwnerField, "ME").empty());

    SavedSelection saved = w.saved();
    CHECK(saved.collection == "2026.09.11-4");
    saved.selection.banner = std::vector<std::string>{"Type a game to run it", "\xD0\x98\xD0\xB3\xD1\x80\xD1\x8B"};   /* no field of its own: the file's */
    const std::string text = selectionToml(saved);
    const SavedSelection back = parseSelection(text);
    CHECK(back.selection.banner == saved.selection.banner);
    CHECK(back.collection == "2026.09.11-4");
    CHECK(back.selection.system == "omega");
    CHECK(back.selection.media == Media::dz);
    CHECK(back.selection.bundles == std::vector<std::string>{"pascal"});
    CHECK(back.selection.picks.at("macro11") == "macro-omega");
    CHECK(back.selection.startup == std::vector<std::string>{"R PAS1"});
    CHECK(back.selection.volumeId == "MYDEV");
    CHECK(back.selection.owner == "VVV");
    CHECK(back.selection.secondVolumeId == "SIDE1");
    CHECK(back.selection.secondOwner == "ME");

    DiskWizard again(m, "mihin", Media::ss);
    again.load(back);
    CHECK(again.resolution().bundles == w.resolution().bundles);
    CHECK(again.notices().empty());
    CHECK(again.saved().selection.banner == saved.selection.banner);

    SavedSelection old = back;
    old.collection = "2026.01.01";
    old.selection.bundles.push_back("cobol");
    again.load(old);
    CHECK(mentions(again.notices(), "cobol"));
    CHECK(mentions(again.notices(), "2026.01.01"));
    CHECK(again.selection().bundles == std::vector<std::string>{"pascal"});

    CHECK_THROWS_AS((void)parseSelection("format = 2\nsystem = \"omega\"\nmedia = \"dz\"\n"), std::runtime_error);
    CHECK_THROWS_AS((void)parseSelection("format = 1\nsystem = \"omega\"\nmedia = \"hd\"\n"), std::runtime_error);
    CHECK_THROWS_AS((void)parseSelection("format = 1\nmedia = \"dz\"\n"), std::runtime_error);
}

}  /* TEST_SUITE */
