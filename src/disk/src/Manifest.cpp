/*
 * Manifest.cpp - disks.toml read with toml++, its rules, and the recipe a
 * choice over it makes.
 */

#include "ms0515/disk/Manifest.hpp"

#include "ms0515/disk/Build.hpp"

#include <toml++/toml.hpp>

#include <algorithm>
#include <map>
#include <stdexcept>

namespace ms0515::disk {

namespace {

[[noreturn]] void fail(const std::string &what) { throw std::runtime_error("disks.toml: " + what); }

std::string str(const toml::table &t, std::string_view key, const std::string &where, bool required)
{
    const auto *node = t.get(key);
    if (!node) {
        if (required) fail(where + " has no " + std::string(key));
        return "";
    }
    const auto v = node->value<std::string>();
    if (!v) fail(where + ": " + std::string(key) + " is not a string");
    return *v;
}

std::vector<std::string> strings(const toml::table &t, std::string_view key, const std::string &where)
{
    std::vector<std::string> out;
    const auto *node = t.get(key);
    if (!node) return out;
    const auto *arr = node->as_array();
    if (!arr) fail(where + ": " + std::string(key) + " is not a list");
    for (const auto &e : *arr) {
        const auto v = e.value<std::string>();
        if (!v) fail(where + ": " + std::string(key) + " holds something that is not a string");
        out.push_back(*v);
    }
    return out;
}

Media media(const std::string &word, const std::string &where)
{
    const auto m = parseMedia(word);
    if (!m) fail(where + ": \"" + word + "\" is no media (ss, dz, dv)");
    return *m;
}

std::vector<Media> medias(const toml::table &t, std::string_view key, const std::string &where)
{
    std::vector<Media> out;
    for (const auto &w : strings(t, key, where)) out.push_back(media(w, where));
    return out;
}

struct Ymd { int y, m, d; };

std::optional<Ymd> ymd(const std::string &s)
{
    if (s.size() != 10 || s[4] != '-' || s[7] != '-') return std::nullopt;
    for (std::size_t i : {0u, 1u, 2u, 3u, 5u, 6u, 8u, 9u}) if (s[i] < '0' || s[i] > '9') return std::nullopt;
    return Ymd{std::stoi(s.substr(0, 4)), std::stoi(s.substr(5, 2)), std::stoi(s.substr(8, 2))};
}

void checkDate(const std::string &s, const std::string &where)
{
    if (s.empty()) return;
    const auto d = ymd(s);
    if (!d || d->m < 1 || d->m > 12 || d->d < 1 || d->d > 31)
        fail(where + ": \"" + s + "\" is no date (YYYY-MM-DD)");
    if (d->y < 1972 || d->y > 2003) fail(where + ": " + s + " - RT-11 V5.04 shows only 1972..2003");
}

uint16_t encoded(const std::string &s)
{
    if (s.empty()) return 0;
    const auto d = ymd(s);
    return encodeDate(d->y, d->m, d->d);
}

const toml::table &section(const toml::table &root, std::string_view key)
{
    static const toml::table kNone;
    const auto *node = root.get(key);
    if (!node) return kNone;
    const auto *t = node->as_table();
    if (!t) fail(std::string(key) + " is not a table");
    return *t;
}

ManifestSystem readSystem(const std::string &key, const toml::table &t)
{
    const std::string where = "system." + key;
    ManifestSystem s{key, str(t, "title", where, true), str(t, "image", where, true),
                     medias(t, "media", where), t["rebuild"].value_or(true), {}};
    if (s.media.empty()) fail(where + " boots from no media");
    if (const auto *arr = t["reserved"].as_array()) {
        for (const auto &e : *arr) {
            const auto *b = e.as_table();
            const auto side = b ? (*b)["side"].value<int64_t>() : std::nullopt;
            const auto lbn = b ? (*b)["lbn"].value<int64_t>() : std::nullopt;
            if (!side || !lbn) fail(where + ": a reserved block is { side = N, lbn = N }");
            s.reserved.push_back({static_cast<int>(*side), static_cast<int>(*lbn)});
        }
    }
    return s;
}

ManifestFile readFileEntry(const toml::node &e, const std::string &where)
{
    if (const auto p = e.value<std::string>()) return {*p, std::nullopt, std::nullopt};
    const auto *t = e.as_table();
    if (!t) fail(where + ": a file is a path or { path = ..., as = ..., date = ... }");
    ManifestFile f{str(*t, "path", where, true), std::nullopt, std::nullopt};
    if (t->contains("as")) f.as = str(*t, "as", where, true);
    if (t->contains("date")) {
        f.date = str(*t, "date", where, true);
        checkDate(*f.date, where);
    }
    return f;
}

ManifestBundle readBundle(const std::string &key, const toml::table &t)
{
    const std::string where = "bundle." + key;
    ManifestBundle b{key, str(t, "title", where, true), {}, Place::boot,
                     medias(t, "needs", where), strings(t, "systems", where),
                     str(t, "date", where, false), t["protect"].value_or(false)};
    checkDate(b.date, where);
    const std::string volume = str(t, "volume", where, false);
    if (volume == "any") b.place = Place::any;
    else if (!volume.empty() && volume != "boot") fail(where + ": volume is \"boot\" or \"any\", not \"" + volume + "\"");
    const auto *files = t["files"].as_array();
    if (!files || files->empty()) fail(where + " has no files");
    for (const auto &e : *files) b.files.push_back(readFileEntry(e, where));
    return b;
}

ManifestPreset readPreset(const std::string &key, const toml::table &t)
{
    const std::string where = "preset." + key;
    ManifestPreset p{key, str(t, "title", where, true), str(t, "system", where, true),
                     media(str(t, "media", where, true), where), strings(t, "bundles", where),
                     std::nullopt, std::nullopt};
    if (t.contains("startup")) p.startup = strings(t, "startup", where);
    if (t.contains("volume_id")) p.volumeId = str(t, "volume_id", where, true);
    return p;
}

/* What only the whole file can tell: names that point nowhere. */
void crossCheck(const Manifest &m)
{
    for (const auto &b : m.bundles)
        for (const auto &s : b.systems)
            if (!m.system(s)) fail("bundle." + b.key + " is for system " + s + ", which is not there");
    for (const auto &p : m.presets) {
        const auto *sys = m.system(p.system);
        if (!sys) fail("preset." + p.key + " names system " + p.system + ", which is not there");
        if (std::find(sys->media.begin(), sys->media.end(), p.media) == sys->media.end())
            fail("preset." + p.key + ": " + p.system + " does not boot from " + mediaWord(p.media));
        for (const auto &b : p.bundles)
            if (!m.bundle(b)) fail("preset." + p.key + " names bundle " + b + ", which is not there");
    }
}

/* RT-11's 6.3 in capitals: what a glob lets through. */
bool rt11Name(const std::string &name)
{
    auto ok = [](const std::string &part, std::size_t lo, std::size_t hi) {
        return part.size() >= lo && part.size() <= hi &&
               std::all_of(part.begin(), part.end(), [](char c) {
                   return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '$';
               });
    };
    const auto dot = name.find('.');
    if (dot == std::string::npos) return ok(name, 1, 6);
    return ok(name.substr(0, dot), 1, 6) && ok(name.substr(dot + 1), 0, 3);
}

bool globMatch(std::string_view pat, std::string_view s)
{
    if (pat.empty()) return s.empty();
    if (pat[0] == '*') return globMatch(pat.substr(1), s) || (!s.empty() && globMatch(pat, s.substr(1)));
    return !s.empty() && pat[0] == s[0] && globMatch(pat.substr(1), s.substr(1));
}

std::string baseName(const std::string &path)
{
    const auto slash = path.rfind('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

}  /* namespace */

std::optional<Media> parseMedia(std::string_view word)
{
    if (word == "ss") return Media::ss;
    if (word == "dz") return Media::dz;
    if (word == "dv") return Media::dv;
    return std::nullopt;
}

const char *mediaWord(Media m)
{
    switch (m) {
    case Media::ss: return "ss";
    case Media::dz: return "dz";
    case Media::dv: return "dv";
    }
    return "?";
}

const ManifestSystem *Manifest::system(std::string_view key) const
{
    for (const auto &s : systems) if (s.key == key) return &s;
    return nullptr;
}

const ManifestBundle *Manifest::bundle(std::string_view key) const
{
    for (const auto &b : bundles) if (b.key == key) return &b;
    return nullptr;
}

const ManifestPreset *Manifest::preset(std::string_view key) const
{
    for (const auto &p : presets) if (p.key == key) return &p;
    return nullptr;
}

Manifest parseManifest(std::string_view text)
{
    toml::table root;
    try {
        root = toml::parse(text);
    } catch (const toml::parse_error &e) {
        fail("line " + std::to_string(e.source().begin.line) + ": " + std::string(e.description()));
    }
    if (root["format"].value<int64_t>() != 1) fail("format is not 1 - this reads format 1 only");
    Manifest m;
    if (root.contains("owner")) m.owner = str(root, "owner", "the file", true);
    /* toml++ keeps a table's keys sorted; the file's order is the source's. */
    auto inOrder = [](const toml::table &t) {
        std::vector<std::pair<std::string, const toml::table *>> out;
        for (const auto &[k, v] : t) {
            const auto *sub = v.as_table();
            if (!sub) fail(std::string(k.str()) + " is not a table");
            out.emplace_back(std::string(k.str()), sub);
        }
        std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) {
            return a.second->source().begin < b.second->source().begin;
        });
        return out;
    };
    for (const auto &[k, t] : inOrder(section(root, "system"))) m.systems.push_back(readSystem(k, *t));
    for (const auto &[k, t] : inOrder(section(root, "bundle"))) m.bundles.push_back(readBundle(k, *t));
    for (const auto &[k, t] : inOrder(section(root, "preset"))) m.presets.push_back(readPreset(k, *t));
    crossCheck(m);
    return m;
}

Selection selectionOf(const ManifestPreset &preset)
{
    return {preset.system, preset.media, preset.bundles, preset.startup, preset.volumeId};
}

std::string bundleRefusal(const Manifest &m, const ManifestBundle &b, const std::string &system, Media media)
{
    (void)m;
    if (!b.systems.empty() && std::find(b.systems.begin(), b.systems.end(), system) == b.systems.end())
        return b.title + " is not for this system";
    if (!b.needs.empty() && std::find(b.needs.begin(), b.needs.end(), media) == b.needs.end())
        return b.title + " cannot live on a " + mediaWord(media) + " disk";
    return "";
}

std::vector<std::string> bundlePaths(const ManifestBundle &b, const Repository &repo)
{
    std::vector<std::string> out;
    for (const auto &f : b.files) {
        if (f.pattern.find('*') == std::string::npos) {
            if (std::find(repo.paths.begin(), repo.paths.end(), f.pattern) == repo.paths.end())
                throw std::runtime_error(b.title + ": " + f.pattern + " is not in the collection");
            out.push_back(f.pattern);
            continue;
        }
        std::vector<std::string> hits;
        for (const auto &p : repo.paths)
            if (globMatch(f.pattern, p) && rt11Name(baseName(p))) hits.push_back(p);
        if (hits.empty()) throw std::runtime_error(b.title + ": " + f.pattern + " matches nothing");
        std::sort(hits.begin(), hits.end());
        out.insert(out.end(), hits.begin(), hits.end());
    }
    return out;
}

ComposeRecipe recipeFor(const Manifest &m, const Selection &s, const Repository &repo)
{
    const auto *sys = m.system(s.system);
    if (!sys) throw std::runtime_error("no system " + s.system);
    if (std::find(sys->media.begin(), sys->media.end(), s.media) == sys->media.end())
        throw std::runtime_error(sys->title + " does not boot from a " + mediaWord(s.media) + " disk");
    std::vector<const ManifestBundle *> chosen;
    for (const auto &key : s.bundles) {
        const auto *b = m.bundle(key);
        if (!b) throw std::runtime_error("no bundle " + key);
        if (const auto why = bundleRefusal(m, *b, s.system, s.media); !why.empty()) throw std::runtime_error(why);
        chosen.push_back(b);
    }

    auto read = [&](const std::string &path) {
        auto bytes = repo.read(path);
        if (!bytes) throw std::runtime_error("cannot read " + path);
        return std::move(*bytes);
    };
    ComposeRecipe r;
    r.system = read(sys->image);
    r.rebuild = sys->rebuild;
    r.reserved = sys->reserved;
    r.media = s.media;
    r.startup = s.startup;
    r.volumeId = s.volumeId;
    r.owner = m.owner;
    if (s.media == Media::dz) r.secondOwner = m.owner;
    for (const auto *b : chosen) {
        std::map<std::string, const ManifestFile *> byPath;
        for (const auto &f : b->files) byPath[f.pattern] = &f;
        ComposeGroup g{b->title, b->place, {}};
        for (const auto &path : bundlePaths(*b, repo)) {
            const auto it = byPath.find(path);
            const ManifestFile *f = it == byPath.end() ? nullptr : it->second;
            const std::string date = f && f->date ? *f->date : b->date;
            g.files.push_back({f && f->as ? *f->as : baseName(path), read(path), encoded(date), b->protect});
        }
        r.groups.push_back(std::move(g));
    }
    return r;
}

} /* namespace ms0515::disk */
