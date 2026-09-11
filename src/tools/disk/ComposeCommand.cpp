/*
 * ComposeCommand.cpp - `ms0515-disk compose` over a local copy of the
 * software collection: list what disks.toml offers, build a preset or every
 * preset, or a disk of one's own choosing, printing where everything went.
 */

#include "ComposeCommand.hpp"

#include "WizardTui.hpp"

#include <ms0515/disk/Compose.hpp>
#include <ms0515/disk/Manifest.hpp>
#include <ms0515/disk/Wizard.hpp>

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;
using namespace ms0515::disk;

namespace ms0515::tools {

namespace {

int usage()
{
    std::fputs(
        "usage: ms0515-disk compose --repo DIR [--open CHOICE.toml]   the wizard\n"
        "       ms0515-disk compose --repo DIR --selection CHOICE.toml <out.dsk> [--plan]\n"
        "       ms0515-disk compose --repo DIR --list\n"
        "       ms0515-disk compose --repo DIR --preset KEY <out.dsk> [--plan]\n"
        "       ms0515-disk compose --repo DIR --all <outdir> [--plan]\n"
        "       ms0515-disk compose --repo DIR --system KEY --media ss|dz|dv [--add B1,B2...]\n"
        "                           [--pick NAME=BUNDLE]... [--startup LINE]... [--volume-id ID] <out.dsk> [--plan]\n"
        "  DIR is a local copy of the software collection (disks.toml at its root).\n"
        "  --plan prints where everything would go and writes nothing.  What a bundle requires\n"
        "  comes with it; --pick chooses among alternatives (--pick macro11=macro-omega).\n",
        stderr);
    return 2;
}

std::optional<std::vector<uint8_t>> readBytes(const fs::path &p)
{
    std::ifstream f(p, std::ios::binary);
    if (!f) return std::nullopt;
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

/* Every file under the collection's root but its .git, as relative paths. */
Repository localRepository(const fs::path &root)
{
    Repository repo;
    for (auto it = fs::recursive_directory_iterator(root); it != fs::recursive_directory_iterator(); ++it) {
        if (it->is_directory() && it->path().filename() == ".git") { it.disable_recursion_pending(); continue; }
        if (it->is_regular_file()) repo.paths.push_back(fs::relative(it->path(), root).generic_string());
    }
    repo.read = [root](const std::string &p) { return readBytes(root / fs::path(p)); };
    return repo;
}

std::vector<std::string> splitList(std::string_view s)
{
    std::vector<std::string> out;
    std::stringstream in{std::string(s)};
    for (std::string item; std::getline(in, item, ',');) if (!item.empty()) out.push_back(item);
    return out;
}

void list(const Manifest &m)
{
    std::puts("systems:");
    for (const auto &s : m.systems) {
        std::string media;
        for (const auto x : s.media) media += std::string(media.empty() ? "" : ",") + mediaWord(x);
        std::string parts;
        for (const auto &need : s.dependsOn) parts += std::string(parts.empty() ? " requires " : ",") + need;
        std::printf("  %-10s %-9s %s%s\n", s.key.c_str(), media.c_str(), s.title.c_str(), parts.c_str());
    }
    std::puts("bundles:");
    auto joined = [](const std::vector<std::string> &v) {
        std::string out;
        for (const auto &x : v) out += std::string(out.empty() ? "" : ",") + x;
        return out;
    };
    std::string group = "\x01";
    for (const auto &b : m.bundles) {
        if (b.group != group) {
            group = b.group;
            if (!group.empty()) std::printf(" %s\n", group.c_str());
        }
        std::string notes;
        if (!b.systems.empty()) notes += " [" + joined(b.systems) + "]";
        if (!b.provides.empty()) notes += " provides " + joined(b.provides);
        if (!b.dependsOn.empty()) notes += " requires " + joined(b.dependsOn);
        std::printf("  %-14s %s%s\n", b.key.c_str(), b.title.c_str(), notes.c_str());
    }
    std::puts("presets:");
    for (const auto &p : m.presets)
        std::printf("  %-10s %s on %s: %s\n", p.key.c_str(), mediaWord(p.media), p.system.c_str(), p.title.c_str());
}

/* A title padded to `width` characters - the manifest's titles are UTF-8,
 * and printf pads bytes. */
std::string padded(const std::string &s, std::size_t width)
{
    std::size_t chars = 0;
    for (const char c : s) if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++chars;
    return chars >= width ? s : s + std::string(width - chars, ' ');
}

void printPlan(const ComposePlan &plan)
{
    for (const auto &g : plan.groups) {
        const std::string title = padded(g.title, 40);
        if (g.volume >= 0)
            std::printf("  %s %4d blocks  %s\n", title.c_str(), g.blocks, g.volume == 0 ? "boot volume" : "second volume");
        else
            std::printf("  %s %4d blocks  NOT PLACED: %s\n", title.c_str(), g.blocks, g.problem.c_str());
    }
    for (std::size_t v = 0; v < plan.freeBlocks.size(); ++v)
        std::printf("  free on the %s volume: %d blocks\n", v == 0 ? "boot" : "second", plan.freeBlocks[v]);
}

/* One disk: plan it, print the plan, write it unless only planning. */
int build(const Manifest &m, const Selection &s, const Repository &repo, const fs::path &out, bool planOnly)
{
    try {
        const ComposeRecipe recipe = recipeFor(m, s, repo);
        const ComposePlan plan = planDisk(recipe);
        std::printf("%s (%s, %s)\n", out.generic_string().c_str(), s.system.c_str(), mediaWord(s.media));
        for (const auto &[added, forWhom] : resolveBundles(m, s.system, s.media, s.bundles, s.picks).addedFor)
            std::printf("  + %s, required by %s\n", m.bundle(added)->title.c_str(), m.bundle(forWhom)->title.c_str());
        printPlan(plan);
        if (!plan.ok) { std::fprintf(stderr, "error: %s\n", plan.problem.c_str()); return 1; }
        if (planOnly) return 0;
        const auto image = composeDisk(recipe);
        std::ofstream f(out, std::ios::binary);
        f.write(reinterpret_cast<const char *>(image.data()), static_cast<std::streamsize>(image.size()));
        if (!f) { std::fprintf(stderr, "error: cannot write %s\n", out.generic_string().c_str()); return 1; }
        return 0;
    } catch (const std::exception &e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}

struct Args {
    std::string repo, preset, system, media, volumeId, out, all, open, selection;
    std::vector<std::string> add, startup, picks;
    bool listOnly = false, planOnly = false, startupGiven = false;
};

std::optional<Args> parseArgs(int argc, char **argv)
{
    Args a;
    for (int i = 2; i < argc; ++i) {
        const std::string_view s = argv[i];
        const bool more = i + 1 < argc;
        if (s == "--repo" && more) a.repo = argv[++i];
        else if (s == "--preset" && more) a.preset = argv[++i];
        else if (s == "--all" && more) a.all = argv[++i];
        else if (s == "--system" && more) a.system = argv[++i];
        else if (s == "--media" && more) a.media = argv[++i];
        else if (s == "--add" && more) { for (auto &b : splitList(argv[++i])) a.add.push_back(b); }
        else if (s == "--startup" && more) { a.startup.emplace_back(argv[++i]); a.startupGiven = true; }
        else if (s == "--volume-id" && more) a.volumeId = argv[++i];
        else if (s == "--pick" && more) a.picks.emplace_back(argv[++i]);
        else if (s == "--open" && more) a.open = argv[++i];
        else if (s == "--selection" && more) a.selection = argv[++i];
        else if (s == "--list") a.listOnly = true;
        else if (s == "--plan") a.planOnly = true;
        else if (a.out.empty() && !s.starts_with("--")) a.out = std::string(s);
        else return std::nullopt;
    }
    return a;
}

std::optional<SavedSelection> readSelection(const std::string &path)
{
    const auto bytes = readBytes(path);
    if (!bytes) { std::fprintf(stderr, "error: cannot read %s\n", path.c_str()); return std::nullopt; }
    try {
        return parseSelection(std::string(bytes->begin(), bytes->end()));
    } catch (const std::exception &e) {
        std::fprintf(stderr, "error: %s: %s\n", path.c_str(), e.what());
        return std::nullopt;
    }
}

/* The wizard, full screen, until its Quit. */
int wizard(const Manifest &m, const Repository &repo, const std::string &open)
{
    WizardTui tui(m, repo);
    if (!open.empty()) {
        const auto saved = readSelection(open);
        if (!saved) return 1;
        tui.open(*saved);
    }
    auto screen = ftxui::ScreenInteractive::Fullscreen();
    auto view = ftxui::Renderer([&] { return tui.render(screen.dimx(), screen.dimy()); });
    auto app = ftxui::CatchEvent(view, [&](const ftxui::Event &e) {
        const bool handled = tui.onEvent(e);
        if (tui.quit()) screen.Exit();
        return handled;
    });
    screen.Loop(app);
    return 0;
}

/* A saved choice built without a screen: its notices said, then the plan. */
int fromSelection(const Manifest &m, const Repository &repo, const std::string &path, const fs::path &out, bool planOnly)
{
    const auto saved = readSelection(path);
    if (!saved) return 1;
    DiskWizard w(m, saved->selection.system, saved->selection.media);
    w.load(*saved);
    for (const auto &n : w.notices()) std::fprintf(stderr, "note: %s\n", n.c_str());
    return build(m, w.selection(), repo, out, planOnly);
}

std::optional<Selection> selectionOfArgs(const Args &a)
{
    const auto media = parseMedia(a.media);
    if (a.system.empty() || !media) return std::nullopt;
    Selection s;
    s.system = a.system;
    s.media = *media;
    s.bundles = a.add;
    if (a.startupGiven) s.startup = a.startup;
    if (!a.volumeId.empty()) s.volumeId = a.volumeId;
    for (const auto &pick : a.picks) {
        const auto eq = pick.find('=');
        if (eq == std::string::npos || eq == 0 || eq + 1 == pick.size()) return std::nullopt;
        s.picks[pick.substr(0, eq)] = pick.substr(eq + 1);
    }
    return s;
}

}  /* namespace */

int composeCommand(int argc, char **argv)
{
    const auto a = parseArgs(argc, argv);
    if (!a || a->repo.empty()) return usage();
    Manifest m;
    try {
        const auto text = readBytes(fs::path(a->repo) / "disks.toml");
        if (!text) { std::fprintf(stderr, "error: no disks.toml in %s\n", a->repo.c_str()); return 1; }
        m = parseManifest(std::string(text->begin(), text->end()));
    } catch (const std::exception &e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    if (a->listOnly) { list(m); return 0; }
    const Repository repo = localRepository(a->repo);
    const bool anyMode = !a->all.empty() || !a->preset.empty() || !a->system.empty() || !a->selection.empty();
    if (!anyMode) return a->out.empty() ? wizard(m, repo, a->open) : usage();

    if (!a->all.empty()) {
        if (!a->planOnly) fs::create_directories(a->all);
        int failed = 0;
        for (const auto &p : m.presets)
            failed += build(m, selectionOf(p), repo, fs::path(a->all) / (p.key + ".dsk"), a->planOnly) != 0;
        return failed ? 1 : 0;
    }
    if (a->out.empty()) return usage();
    if (!a->selection.empty()) return fromSelection(m, repo, a->selection, a->out, a->planOnly);
    if (!a->preset.empty()) {
        const auto *p = m.preset(a->preset);
        if (!p) { std::fprintf(stderr, "error: no preset %s in disks.toml\n", a->preset.c_str()); return 1; }
        return build(m, selectionOf(*p), repo, a->out, a->planOnly);
    }
    const auto s = selectionOfArgs(*a);
    if (!s) return usage();
    return build(m, *s, repo, a->out, a->planOnly);
}

} /* namespace ms0515::tools */
