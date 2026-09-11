/*
 * wizard_web.cpp - the browser's disk wizard: DiskWizard and the composer
 * behind flat C entry points, JSON out.
 *
 * The page hands over the collection's disks.toml and the list of its files
 * with their sizes (index.json); the files themselves it fetches into the
 * module's file system under /software/ when wiz_needed() names them, so a
 * plan or a build reads only what the choice uses.  The rules are the native
 * wizard's, compiled from the same sources.
 */
#include <emscripten.h>

#include <ms0515/disk/Compose.hpp>
#include <ms0515/disk/Manifest.hpp>
#include <ms0515/disk/Wizard.hpp>

#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace ms0515::disk;

namespace {

const std::string kRoot = "/software/";

struct Session {
    Manifest                    manifest;
    Repository                  repo;
    std::map<std::string, long> sizes;
    std::unique_ptr<DiskWizard> wizard;
};

std::unique_ptr<Session> gSession;
std::string gError;
std::string gText;

std::string esc(const std::string &s)
{
    std::string out;
    for (const char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        if (static_cast<unsigned char>(c) < 0x20) { out += ' '; continue; }
        out += c;
    }
    return out;
}

std::string str(const std::string &s) { return "\"" + esc(s) + "\""; }

std::string strings(const std::vector<std::string> &v)
{
    std::string out = "[";
    for (std::size_t i = 0; i < v.size(); ++i) out += (i ? "," : "") + str(v[i]);
    return out + "]";
}

std::vector<std::string> lines(const char *text)
{
    std::vector<std::string> out;
    std::stringstream in(text ? text : "");
    for (std::string l; std::getline(in, l);) if (!l.empty()) out.push_back(l);
    return out;
}

int blocksOf(const Session &s, const ManifestBundle &b)
{
    long bytes = 0;
    try {
        for (const auto &p : bundlePaths(b, s.repo))
            if (const auto it = s.sizes.find(p); it != s.sizes.end()) bytes += (it->second + 511) / 512 * 512;
    } catch (const std::exception &) {
    }
    return static_cast<int>(bytes / 512);
}

const char *kindOf(const WizardRow &r)
{
    switch (r.kind) {
    case WizardRow::Kind::group:  return "group";
    case WizardRow::Kind::radio:  return "radio";
    case WizardRow::Kind::media:  return "media";
    case WizardRow::Kind::system: return "system";
    case WizardRow::Kind::field:  return "field";
    case WizardRow::Kind::line:   return "line";
    case WizardRow::Kind::bundle: break;
    }
    return "bundle";
}

const char *mark(const WizardRow &r)
{
    switch (r.mark) {
    case WizardRow::Mark::on:     return "on";
    case WizardRow::Mark::added:  return "added";
    case WizardRow::Mark::system: return "system";
    case WizardRow::Mark::off:    break;
    }
    return "off";
}

std::string rowsJson(const DiskWizard &w)
{
    std::string out = "[";
    for (const auto &r : w.rows()) {
        if (out.size() > 1) out += ",";
        out += std::string("{\"kind\":\"") + kindOf(r) + "\",\"depth\":" + std::to_string(r.depth) + ",\"key\":" + str(r.key)
             + ",\"title\":" + str(r.title) + ",\"parent\":" + str(r.parent) + ",\"mark\":\"" + mark(r) + "\""
             + ",\"radio\":" + (r.radio ? "true" : "false") + ",\"available\":" + (r.available ? "true" : "false")
             + ",\"why\":" + str(r.why) + ",\"requiredBy\":" + str(r.requiredBy) + ",\"blocks\":" + std::to_string(r.blocks)
             + ",\"open\":" + (r.open ? "true" : "false") + ",\"summary\":" + str(r.summary)
             + ",\"native\":" + (r.native ? "true" : "false") + ",\"value\":" + str(r.value) + "}";
    }
    return out + "]";
}

}  /* namespace */

extern "C" {

/* Start over a collection: disks.toml's text, its files' paths and sizes one
 * a line each, in the same order.  0 with wiz_error() when the file is not
 * right. */
EMSCRIPTEN_KEEPALIVE int wiz_open(const char *manifest, const char *paths, const char *sizes)
{
    try {
        auto s = std::make_unique<Session>();
        s->manifest = parseManifest(manifest);
        const auto p = lines(paths), z = lines(sizes);
        for (std::size_t i = 0; i < p.size(); ++i) {
            s->repo.paths.push_back(p[i]);
            s->sizes[p[i]] = i < z.size() ? std::stol(z[i]) : 0;
        }
        s->repo.read = [](const std::string &path) -> std::optional<std::vector<uint8_t>> {
            std::ifstream f(kRoot + path, std::ios::binary);
            if (!f) return std::nullopt;
            return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        };
        if (s->manifest.systems.empty()) { gError = "disks.toml names no system"; return 0; }
        Session *raw = s.get();
        s->wizard = std::make_unique<DiskWizard>(s->manifest, [raw](const ManifestBundle &b) { return blocksOf(*raw, b); });
        gSession = std::move(s);
        return 1;
    } catch (const std::exception &e) {
        gError = e.what();
        return 0;
    }
}

EMSCRIPTEN_KEEPALIVE const char *wiz_error(void) { return gError.c_str(); }

/* Everything the page draws: the choice, the rows to show, the notices. */
EMSCRIPTEN_KEEPALIVE const char *wiz_state(void)
{
    if (!gSession) return "{}";
    const DiskWizard &w = *gSession->wizard;
    const auto &sel = w.selection();
    gText = "{\"version\":" + str(gSession->manifest.version) + ",\"ready\":" + (w.ready() ? "true" : "false")
          + ",\"system\":" + str(sel.system) + ",\"media\":" + str(w.media() ? mediaWord(*w.media()) : "")
          + ",\"startup\":" + strings(sel.startup.value_or(std::vector<std::string>{}))
          + ",\"volumeId\":" + str(sel.volumeId.value_or("")) + ",\"notices\":" + strings(w.notices())
          + ",\"rows\":" + rowsJson(w) + "}";
    return gText.c_str();
}

/* The first two steps, each "" when taken, else why not. */
EMSCRIPTEN_KEEPALIVE const char *wiz_set_media(const char *word)
{
    const auto m = parseMedia(word ? word : "");
    gText = !gSession ? std::string("no collection") : !m ? std::string("no media ") + (word ? word : "") : gSession->wizard->setMedia(*m);
    return gText.c_str();
}

EMSCRIPTEN_KEEPALIVE const char *wiz_set_system(const char *key)
{
    gText = gSession ? gSession->wizard->setSystem(key) : std::string("no collection");
    return gText.c_str();
}

/* A group opened or closed. */
EMSCRIPTEN_KEEPALIVE void wiz_fold(const char *key) { if (gSession) gSession->wizard->toggleFold(key); }

/* Space on a row: "" when taken, else why not. */
EMSCRIPTEN_KEEPALIVE const char *wiz_toggle(const char *key)
{
    gText = gSession ? gSession->wizard->toggle(key) : std::string("no collection");
    return gText.c_str();
}

/* A field's text - the volume id, a START.COM line: "" when taken, else why not. */
EMSCRIPTEN_KEEPALIVE const char *wiz_set_field(const char *key, const char *value)
{
    gText = gSession ? gSession->wizard->setField(key ? key : "", value ? value : "") : std::string("no collection");
    return gText.c_str();
}

/* The files a plan or a build of the current choice reads, as a JSON list of
 * paths: the system's image and every file of every bundle it installs. */
EMSCRIPTEN_KEEPALIVE const char *wiz_needed(void)
{
    if (!gSession || !gSession->wizard->ready()) return "[]";
    const DiskWizard &w = *gSession->wizard;
    std::vector<std::string> paths;
    if (const auto *sys = gSession->manifest.system(w.selection().system)) paths.push_back(sys->image);
    for (const auto &key : w.resolution().bundles) {
        try {
            for (const auto &p : bundlePaths(*gSession->manifest.bundle(key), gSession->repo)) paths.push_back(p);
        } catch (const std::exception &) {
        }
    }
    gText = strings(paths);
    return gText.c_str();
}

/* The plan of the current choice, its files fetched: {ok, problem, volumes:
 * [{name, used, capacity, free}], startup: [lines], groups: [{name, blocks}]}
 * - the blocks by top group, the system's own counted with its parts'. */
EMSCRIPTEN_KEEPALIVE const char *wiz_plan(void)
{
    if (!gSession) return "{}";
    const DiskWizard &w = *gSession->wizard;
    if (!w.ready()) return "{\"ok\":false,\"problem\":\"\",\"volumes\":[],\"startup\":[],\"groups\":[]}";
    try {
        const ComposeRecipe r = recipeFor(gSession->manifest, w.selection(), gSession->repo);
        const ComposePlan plan = planDisk(r);
        const int capacity = r.media == Media::dv ? 1586 : 786;
        std::string volumes = "[";
        int used = 0;
        for (std::size_t v = 0; v < plan.freeBlocks.size(); ++v) {
            used += capacity - plan.freeBlocks[v];
            const std::string name = r.media == Media::dv ? "DV0:" : v == 0 ? "DZ0:" : "DZ2:";
            volumes += std::string(v ? "," : "") + "{\"name\":" + str(name) + ",\"used\":" + std::to_string(capacity - plan.freeBlocks[v])
                     + ",\"capacity\":" + std::to_string(capacity) + ",\"free\":" + std::to_string(plan.freeBlocks[v]) + "}";
        }
        auto groups = w.blocksByGroup();
        int bundles = 0;
        for (const auto &g : groups) bundles += g.second;
        if (plan.ok && used > bundles) {
            if (groups.empty()) groups.emplace_back("System", 0);
            groups.front().second += used - bundles;
        }
        std::string byGroup = "[";
        for (const auto &[name, blocks] : groups)
            byGroup += std::string(byGroup.size() > 1 ? "," : "") + "{\"name\":" + str(name) + ",\"blocks\":" + std::to_string(blocks) + "}";
        gText = std::string("{\"ok\":") + (plan.ok ? "true" : "false") + ",\"problem\":" + str(plan.problem)
              + ",\"volumes\":" + volumes + "],\"startup\":" + strings(r.startup.value_or(std::vector<std::string>{}))
              + ",\"groups\":" + byGroup + "]}";
    } catch (const std::exception &e) {
        gText = "{\"ok\":false,\"problem\":" + str(e.what()) + ",\"volumes\":[],\"startup\":[],\"groups\":[]}";
    }
    return gText.c_str();
}

/* The disk, written to `out` in the module's file system.  0 with
 * wiz_error() when it cannot be built. */
EMSCRIPTEN_KEEPALIVE int wiz_build(const char *out)
{
    if (!gSession) { gError = "no collection"; return 0; }
    if (!gSession->wizard->ready()) { gError = "choose the diskette and the system first"; return 0; }
    try {
        const auto image = composeDisk(recipeFor(gSession->manifest, gSession->wizard->selection(), gSession->repo));
        std::ofstream f(out, std::ios::binary);
        f.write(reinterpret_cast<const char *>(image.data()), static_cast<std::streamsize>(image.size()));
        return f ? 1 : (gError = "cannot write the image", 0);
    } catch (const std::exception &e) {
        gError = e.what();
        return 0;
    }
}

/* The choice as its own file (TOML), and one read back: 0 with wiz_error()
 * when the text is no such file; what it names that this collection lacks
 * comes as notices in wiz_state(). */
EMSCRIPTEN_KEEPALIVE const char *wiz_save(void)
{
    gText = gSession ? selectionToml(gSession->wizard->saved()) : std::string();
    return gText.c_str();
}

EMSCRIPTEN_KEEPALIVE int wiz_load(const char *text)
{
    if (!gSession) { gError = "no collection"; return 0; }
    try {
        gSession->wizard->load(parseSelection(text));
        return 1;
    } catch (const std::exception &e) {
        gError = e.what();
        return 0;
    }
}

/* A bundle's details: {title, group, provides, requires, startup, files, blocks}. */
EMSCRIPTEN_KEEPALIVE const char *wiz_details(const char *key)
{
    const auto *b = gSession ? gSession->manifest.bundle(key) : nullptr;
    if (!b) return "{}";
    std::vector<std::string> files;
    try {
        for (const auto &p : bundlePaths(*b, gSession->repo)) files.push_back(p.substr(p.rfind('/') + 1));
    } catch (const std::exception &e) {
        files.push_back(e.what());
    }
    gText = "{\"title\":" + str(b->title) + ",\"group\":" + str(b->group) + ",\"provides\":" + strings(b->provides)
          + ",\"requires\":" + strings(b->dependsOn) + ",\"startup\":" + strings(b->startup) + ",\"files\":" + strings(files)
          + ",\"blocks\":" + std::to_string(blocksOf(*gSession, *b)) + "}";
    return gText.c_str();
}

}  /* extern "C" */
