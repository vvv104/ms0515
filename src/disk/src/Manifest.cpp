/*
 * Manifest.cpp - disks.toml read with toml++, its rules, and the recipe a
 * choice over it makes.
 */

#include "ms0515/disk/Manifest.hpp"
#include "Internal.hpp"

#include "ms0515/disk/Build.hpp"

#include <toml++/toml.hpp>

#include <algorithm>
#include <functional>
#include <map>
#include <set>
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

/* A preset's lines: one multiline string, or the list of strings. */
std::vector<std::string> lines(const toml::table &t, std::string_view key, const std::string &where);

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

std::vector<std::string> lines(const toml::table &t, std::string_view key, const std::string &where)
{
    const auto *node = t.get(key);
    if (node && node->is_string()) return internal::splitLines(node->value<std::string>().value_or(std::string()));
    return strings(t, key, where);
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

/* A table's date, checked; nullopt when it has none. */
std::optional<std::string> dateOf(const toml::table &t, const std::string &where)
{
    if (!t.contains("date")) return std::nullopt;
    auto d = str(t, "date", where, true);
    checkDate(d, where);
    return d;
}

const toml::table &tableOf(const toml::table &t, std::string_view key, const std::string &where, const char *shape)
{
    const auto *node = t.get(key);
    const auto *sub = node ? node->as_table() : nullptr;
    if (!sub) fail(where + (node ? ": " : " has no ") + std::string(key) + (node ? std::string(" is ") + shape : ""));
    return *sub;
}

/* Format 2: the monitor's file, SWAP.SYS's length, the dates. */
void readSystemFiles(ManifestSystem &s, const toml::table &t, const std::string &where)
{
    if (t.contains("image")) fail(where + ": an image is format 1's - format 2 names the monitor's file");
    const auto &mon = tableOf(t, "monitor", where, "{ path = ..., date = ... }");
    s.monitor = str(mon, "path", where + ".monitor", true);
    s.monitorDate = dateOf(mon, where + ".monitor");
    const auto &swap = tableOf(t, "swap", where, "{ blocks = N, date = ... }");
    const auto blocks = swap["blocks"].value<int64_t>();
    if (!blocks || *blocks < 1 || *blocks > 1000) fail(where + ": swap is { blocks = N, date = ... }, N a length in blocks");
    s.swapBlocks = static_cast<int>(*blocks);
    s.swapDate = dateOf(swap, where + ".swap");
    if (t.contains("startup_date")) {
        s.startupDate = str(t, "startup_date", where, true);
        checkDate(*s.startupDate, where);
    }
}

ManifestSystem readSystem(const std::string &key, const toml::table &t, int format)
{
    const std::string where = "system." + key;
    ManifestSystem s;
    s.key = key;
    s.title = str(t, "title", where, true);
    s.kit = str(t, "kit", where, false);
    if (format == 1) s.image = str(t, "image", where, true);
    else readSystemFiles(s, t, where);
    s.media = medias(t, "media", where);
    if (s.media.empty()) fail(where + " boots from no media");
    s.dependsOn = strings(t, "requires", where);
    if (t.contains("requires_by_media")) {
        const auto *table = t["requires_by_media"].as_table();
        if (!table) fail(where + ": requires_by_media is { media = [...] }");
        for (const auto &[k, v] : *table) {
            (void)v;
            const std::string word(k.str());
            s.dependsOnByMedia[media(word, where)] = strings(*table, word, where + ".requires_by_media");
        }
    }
    s.prefer = strings(t, "prefer", where);
    s.suggests = strings(t, "suggests", where);
    if (t.contains("startup")) s.startup = strings(t, "startup", where);
    if (const auto *arr = t["reserved"].as_array()) {
        for (const auto &e : *arr) {
            const auto *b = e.as_table();
            const auto side = b ? (*b)["side"].value<int64_t>() : std::nullopt;
            const auto lbn = b ? (*b)["lbn"].value<int64_t>() : std::nullopt;
            const auto path = b ? (*b)["path"].value<std::string>() : std::nullopt;
            if (!side || !lbn || (format == 1) == path.has_value())
                fail(where + (format == 1 ? ": a reserved block is { side = N, lbn = N }"
                                          : ": a reserved block is { side = N, lbn = N, path = ... } - its bytes' file"));
            s.reserved.push_back({static_cast<int>(*side), static_cast<int>(*lbn), path.value_or("")});
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
    ManifestBundle b;
    b.key = key;
    b.title = str(t, "title", where, true);
    b.needs = medias(t, "needs", where);
    b.systems = strings(t, "systems", where);
    b.date = str(t, "date", where, false);
    b.protect = t["protect"].value_or(false);
    b.group = str(t, "group", where, false);
    b.kit = str(t, "kit", where, false);
    b.provides = strings(t, "provides", where);
    b.dependsOn = strings(t, "requires", where);
    if (const auto *table = t["prefer"].as_table()) {
        for (const auto &[k, v] : *table) {
            (void)v;
            const std::string system(k.str());
            auto list = strings(*table, system, where + ".prefer");
            if (system == "default") b.prefer = std::move(list);
            else b.preferBySystem[system] = std::move(list);
        }
    } else {
        b.prefer = strings(t, "prefer", where);
    }
    b.startup = strings(t, "startup", where);
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
                     std::nullopt, std::nullopt, std::nullopt, false};
    if (t.contains("startup")) p.startup = lines(t, "startup", where);
    if (t.contains("banner")) p.banner = lines(t, "banner", where);
    if (t.contains("clear_screen")) p.clearScreen = t["clear_screen"].value<bool>().value_or(false);
    if (t.contains("volume_id")) p.volumeId = str(t, "volume_id", where, true);
    return p;
}

bool satisfies(const ManifestBundle &b, std::string_view need)
{
    return b.key == need || std::find(b.provides.begin(), b.provides.end(), need) != b.provides.end();
}

/* Every bundle that could satisfy one of `b`'s needs, whatever the system. */
std::vector<const ManifestBundle *> possibleNeeds(const Manifest &m, const ManifestBundle &b)
{
    std::vector<const ManifestBundle *> out;
    for (const auto &need : b.dependsOn)
        for (const auto &other : m.bundles)
            if (satisfies(other, need)) out.push_back(&other);
    return out;
}

/* The needs: each satisfiable, each preference an alternative of one of
 * them, and no bundle needing itself through any chain of alternatives. */
void checkDependencies(const Manifest &m)
{
    for (const auto &b : m.bundles) {
        for (const auto &need : b.dependsOn)
            if (std::none_of(m.bundles.begin(), m.bundles.end(), [&](const auto &o) { return satisfies(o, need); }))
                fail("bundle." + b.key + " requires " + need + ", which no bundle is or provides");
        auto checkPrefs = [&](const std::vector<std::string> &prefs) {
            for (const auto &pref : prefs) {
                const auto *o = m.bundle(pref);
                if (!o) fail("bundle." + b.key + " prefers " + pref + ", which is not there");
                if (std::none_of(b.dependsOn.begin(), b.dependsOn.end(), [&](const auto &need) { return satisfies(*o, need); }))
                    fail("bundle." + b.key + " prefers " + pref + ", which satisfies none of what it requires");
            }
        };
        checkPrefs(b.prefer);
        for (const auto &[system, prefs] : b.preferBySystem) {
            if (!m.system(system)) fail("bundle." + b.key + " has a preference for system " + system + ", which is not there");
            checkPrefs(prefs);
        }
    }
    std::map<const ManifestBundle *, int> state;          /* 1 on the path, 2 done */
    std::function<void(const ManifestBundle &)> visit = [&](const ManifestBundle &b) {
        state[&b] = 1;
        for (const auto *o : possibleNeeds(m, b)) {
            if (state[o] == 1) fail("bundle." + b.key + " and bundle." + o->key + " require each other");
            if (state[o] == 0) visit(*o);
        }
        state[&b] = 2;
    };
    for (const auto &b : m.bundles) if (state[&b] == 0) visit(b);
}

/* What only the whole file can tell: names that point nowhere. */
void crossCheck(const Manifest &m)
{
    for (const auto &b : m.bundles)
        for (const auto &s : b.systems)
            if (!m.system(s)) fail("bundle." + b.key + " is for system " + s + ", which is not there");
    auto satisfiable = [&](const std::string &need) {
        return std::any_of(m.bundles.begin(), m.bundles.end(), [&](const auto &o) { return satisfies(o, need); });
    };
    for (const auto &s : m.systems) {
        for (const auto &need : s.dependsOn)
            if (!satisfiable(need)) fail("system." + s.key + " requires " + need + ", which no bundle is or provides");
        std::vector<std::string> all = s.dependsOn;
        for (const auto &[md, needs] : s.dependsOnByMedia) {
            for (const auto &need : needs)
                if (!satisfiable(need)) fail("system." + s.key + " requires " + need + ", which no bundle is or provides");
            all.insert(all.end(), needs.begin(), needs.end());
        }
        for (const auto &need : s.suggests)
            if (!satisfiable(need)) fail("system." + s.key + " suggests " + need + ", which no bundle is or provides");
        all.insert(all.end(), s.suggests.begin(), s.suggests.end());
        for (const auto &pref : s.prefer) {
            const auto *o = m.bundle(pref);
            if (!o) fail("system." + s.key + " prefers " + pref + ", which is not there");
            if (std::none_of(all.begin(), all.end(), [&](const auto &need) { return satisfies(*o, need); }))
                fail("system." + s.key + " prefers " + pref + ", which satisfies none of what it requires or suggests");
        }
    }
    checkDependencies(m);
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

std::string Manifest::kitTitle(std::string_view key) const
{
    for (const auto &k : kits) if (k.key == key) return k.title;
    return std::string(key);
}

bool Manifest::kitIsCommon(std::string_view key) const
{
    for (const auto &k : kits) if (k.key == key) return k.common;
    return false;
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
    const auto format = root["format"].value<int64_t>();
    if (format != 1 && format != 2) fail("format is not 1 or 2 - this reads those only");
    Manifest m;
    m.format = static_cast<int>(*format);
    if (root.contains("owner")) m.owner = str(root, "owner", "the file", true);
    if (root.contains("version")) m.version = str(root, "version", "the file", true);
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
    for (const auto &[k, t] : inOrder(section(root, "system"))) m.systems.push_back(readSystem(k, *t, m.format));
    for (const auto &[k, t] : inOrder(section(root, "bundle"))) m.bundles.push_back(readBundle(k, *t));
    for (const auto &[k, t] : inOrder(section(root, "kit")))
        m.kits.push_back({k, str(*t, "title", "kit." + k, true),
                          (*t)["common"].value_or(false)});
    for (const auto &[k, t] : inOrder(section(root, "preset"))) m.presets.push_back(readPreset(k, *t));
    crossCheck(m);
    return m;
}

Selection selectionOf(const ManifestPreset &preset)
{
    Selection s;
    s.system = preset.system;
    s.media = preset.media;
    s.bundles = preset.bundles;                     /* exactly what it names: a system's suggestions are the wizard's to tick */
    s.startup = preset.startup;
    s.banner = preset.banner;
    s.clearScreen = preset.clearScreen;
    s.volumeId = preset.volumeId;
    return s;
}

std::vector<std::string> suggestedBundles(const Manifest &m, const ManifestSystem &sys, Media media,
                                          const std::vector<std::string> &chosen)
{
    std::vector<std::string> out;
    /* Satisfied by a chosen bundle that is or provides the name - or, the
     * name being a bundle's key, one that provides what that bundle
     * provides: another build of DIR in the suggested one's place. */
    auto satisfied = [&](const std::string &need) {
        const auto *asBundle = m.bundle(need);
        for (const auto &list : {chosen, out})
            for (const auto &key : list) {
                const auto *b = m.bundle(key);
                if (!b) continue;
                if (satisfies(*b, need)) return true;
                if (asBundle)
                    for (const auto &name : asBundle->provides)
                        if (satisfies(*b, name)) return true;
            }
        return false;
    };
    for (const auto &need : sys.suggests) {
        if (satisfied(need)) continue;
        const auto cands = candidatesFor(m, need, sys.key, media);
        if (cands.empty()) continue;
        const ManifestBundle *pick = cands.front();
        for (const auto &pref : sys.prefer)
            if (const auto it = std::find_if(cands.begin(), cands.end(), [&](const auto *c) { return c->key == pref; }); it != cands.end()) {
                pick = *it;
                break;
            }
        out.push_back(pick->key);
    }
    return out;
}

std::vector<const ManifestBundle *> candidatesFor(const Manifest &m, std::string_view need,
                                                  const std::string &system, Media media)
{
    std::vector<const ManifestBundle *> out;
    for (const auto &b : m.bundles)
        if (satisfies(b, need) && bundleRefusal(m, b, system, media).empty()) out.push_back(&b);
    return out;
}

namespace {

/* The resolution's walk: `visit` installs a bundle after what it needs. */
struct Resolver {
    const Manifest                            &m;
    const std::string                         &system;
    Media                                      media;
    const std::vector<std::string>            &chosen;
    const std::map<std::string, std::string>  &picks;
    Resolution                                 r;
    std::set<std::string>                      onPath;

    bool installed(const std::string &key) const
    {
        return std::find(r.bundles.begin(), r.bundles.end(), key) != r.bundles.end();
    }

    /* The bundle that satisfies `need` for `who`, or nullptr with the reason. */
    const ManifestBundle *provider(const ManifestBundle &who, const std::string &need)
    {
        const auto cands = candidatesFor(m, need, system, media);
        if (cands.empty()) {
            r.problem = who.title + " requires " + need + ", and nothing for this system provides it";
            return nullptr;
        }
        auto taken = [&](const std::string &key) {
            return installed(key) || onPath.count(key) || std::find(chosen.begin(), chosen.end(), key) != chosen.end();
        };
        for (const auto *c : cands) if (taken(c->key)) return c;
        if (const auto it = picks.find(need); it != picks.end()) {
            for (const auto *c : cands) if (c->key == it->second) return c;
            r.problem = it->second + " is no choice for " + need + " on this system";
            return nullptr;
        }
        const auto bySystem = who.preferBySystem.find(system);
        for (const auto &pref : bySystem != who.preferBySystem.end() ? bySystem->second : who.prefer)
            for (const auto *c : cands) if (c->key == pref) return c;
        return cands.front();
    }

    bool visit(const ManifestBundle &b, const std::string &forWhom)
    {
        if (installed(b.key) || onPath.count(b.key)) return true;
        onPath.insert(b.key);
        for (const auto &need : b.dependsOn) {
            const auto *p = provider(b, need);
            if (!p || !visit(*p, b.key)) return false;
        }
        onPath.erase(b.key);
        r.bundles.push_back(b.key);
        if (!forWhom.empty()) r.addedFor.emplace_back(b.key, forWhom);
        return true;
    }

    /* Two installed bundles providing one name. */
    bool alternativesApart()
    {
        std::map<std::string, const ManifestBundle *> byName;
        for (const auto &key : r.bundles) {
            const auto *b = m.bundle(key);
            for (const auto &name : b->provides) {
                const auto [it, fresh] = byName.emplace(name, b);
                if (!fresh) {
                    r.problem = "only one of " + it->second->title + " and " + b->title + " can go on a disk (" + name + ")";
                    return false;
                }
            }
        }
        return true;
    }
};

}  /* namespace */

Resolution resolveBundles(const Manifest &m, const std::string &system, Media media,
                          const std::vector<std::string> &chosen, const std::map<std::string, std::string> &picks)
{
    Resolver w{m, system, media, chosen, picks, {}, {}};
    for (const auto &key : chosen) {
        const auto *b = m.bundle(key);
        if (!b) { w.r.problem = "no bundle " + key; return w.r; }
        if (auto why = bundleRefusal(m, *b, system, media); !why.empty()) { w.r.problem = why; return w.r; }
    }
    if (const auto *sys = m.system(system)) {
        /* The system's own parts first, as if a bundle of the system's name
         * required them. */
        ManifestBundle own;
        own.title = sys->title;
        own.dependsOn = sys->dependsOn;
        own.prefer = sys->prefer;
        if (const auto it = sys->dependsOnByMedia.find(media); it != sys->dependsOnByMedia.end())
            own.dependsOn.insert(own.dependsOn.end(), it->second.begin(), it->second.end());
        for (const auto &need : own.dependsOn) {
            const auto *p = w.provider(own, need);
            if (!p || !w.visit(*p, "")) return w.r;
        }
    }
    for (const auto &key : chosen)
        if (!w.visit(*m.bundle(key), "")) return w.r;
    /* A bundle chosen outright is not "added for" anyone, even when a
     * bundle chosen before it needed it. */
    std::erase_if(w.r.addedFor, [&](const auto &a) { return std::find(chosen.begin(), chosen.end(), a.first) != chosen.end(); });
    w.r.ok = w.alternativesApart();
    return w.r;
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

std::vector<std::string> systemPaths(const ManifestSystem &s)
{
    if (!s.image.empty()) return {s.image};
    std::vector<std::string> out{s.monitor};
    for (const auto &b : s.reserved) out.push_back(b.path);
    return out;
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
    const Resolution resolution = resolveBundles(m, s.system, s.media, s.bundles, s.picks);
    if (!resolution.ok) throw std::runtime_error(resolution.problem);
    std::vector<const ManifestBundle *> chosen;
    for (const auto &key : resolution.bundles) chosen.push_back(m.bundle(key));
    /* TYPE is PIP's on these monitors: without it the banner is an error
     * at boot and the rest of STARTS.COM goes unread. */
    if ((s.banner || s.clearScreen) && std::none_of(chosen.begin(), chosen.end(), [](const auto *b) { return satisfies(*b, "pip"); }))
        throw std::runtime_error("a banner needs PIP on the disk - TYPE is its - and no bundle chosen provides pip");

    auto read = [&](const std::string &path) {
        auto bytes = repo.read(path);
        if (!bytes) throw std::runtime_error("cannot read " + path);
        return std::move(*bytes);
    };
    ComposeRecipe r;
    if (!sys->image.empty()) r.system = read(sys->image);
    else {
        const auto slash = sys->monitor.find_last_of('/');
        r.files = ComposeSystem{slash == std::string::npos ? sys->monitor : sys->monitor.substr(slash + 1),
                                read(sys->monitor), encoded(sys->monitorDate.value_or("")),
                                sys->swapBlocks, encoded(sys->swapDate.value_or("")),
                                encoded(sys->startupDate.value_or(""))};
    }
    for (const auto &b : sys->reserved) {
        ReservedBlock block{b.side, b.lbn, {}};
        if (!b.path.empty()) {
            block.data = read(b.path);
            if (block.data.size() != kBlock)
                throw std::runtime_error(b.path + " is no block: a protected block's file is 512 bytes");
        }
        r.reserved.push_back(std::move(block));
    }
    r.media = s.media;
    std::vector<std::string> startup = sys->startup.value_or(std::vector<std::string>{});
    for (const auto *b : chosen) startup.insert(startup.end(), b->startup.begin(), b->startup.end());
    if (s.startup) startup.insert(startup.end(), s.startup->begin(), s.startup->end());
    std::vector<std::string> once;
    for (const auto &line : startup)
        if (std::find(once.begin(), once.end(), line) == once.end()) once.push_back(line);
    if (sys->startup || !once.empty()) r.startup = once;
    r.banner = s.banner;
    r.clearScreen = s.clearScreen;
    r.volumeId = s.volumeId;
    r.owner = s.owner ? s.owner : m.owner;
    if (s.media == Media::dz) {
        r.secondVolumeId = s.secondVolumeId;
        r.secondOwner = s.secondOwner ? s.secondOwner : m.owner;
    }
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
