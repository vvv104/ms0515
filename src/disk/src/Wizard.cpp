/*
 * Wizard.cpp - the disk wizards' model and the saved-choice file.
 */

#include "ms0515/disk/Wizard.hpp"

#include <toml++/toml.hpp>

#include <algorithm>
#include <set>
#include <stdexcept>

namespace ms0515::disk {

namespace {

bool contains(const std::vector<std::string> &v, const std::string &x)
{
    return std::find(v.begin(), v.end(), x) != v.end();
}

std::string quoted(const std::string &s)
{
    std::string out = "\"";
    for (const char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out + "\"";
}

std::string list(const std::vector<std::string> &v)
{
    std::string out = "[";
    for (std::size_t i = 0; i < v.size(); ++i) out += (i ? ", " : "") + quoted(v[i]);
    return out + "]";
}

bool bareKey(const std::string &k)
{
    return !k.empty() && std::all_of(k.begin(), k.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
    });
}

WizardRow heading(WizardRow::Kind kind, int depth, const std::string &key, const std::string &title)
{
    WizardRow r;
    r.kind = kind;
    r.depth = depth;
    r.key = key;
    r.title = title;
    return r;
}

[[noreturn]] void fail(const std::string &what) { throw std::runtime_error("selection: " + what); }

std::vector<std::string> strings(const toml::table &t, std::string_view key)
{
    std::vector<std::string> out;
    const auto *node = t.get(key);
    if (!node) return out;
    const auto *arr = node->as_array();
    if (!arr) fail(std::string(key) + " is not a list");
    for (const auto &e : *arr) {
        const auto v = e.value<std::string>();
        if (!v) fail(std::string(key) + " holds something that is not a string");
        out.push_back(*v);
    }
    return out;
}

}  /* namespace */

/* ---- the model ------------------------------------------------------------ */

DiskWizard::DiskWizard(const Manifest &manifest, std::string system, Media media,
                       std::function<int(const ManifestBundle &)> blocksOf)
    : m_(manifest), blocksOf_(std::move(blocksOf))
{
    sel_.system = std::move(system);
    sel_.media = media;
    const auto offered = mediaOffered();
    if (!offered.empty() && std::find(offered.begin(), offered.end(), media) == offered.end())
        sel_.media = offered.front();
    resolve();
}

std::vector<Media> DiskWizard::mediaOffered() const
{
    const auto *sys = m_.system(sel_.system);
    return sys ? sys->media : std::vector<Media>{};
}

void DiskWizard::resolve()
{
    res_ = resolveBundles(m_, sel_.system, sel_.media, sel_.bundles, sel_.picks);
}

bool DiskWizard::isSystemPart(const std::string &key) const
{
    if (!contains(res_.bundles, key) || contains(sel_.bundles, key)) return false;
    return std::none_of(res_.addedFor.begin(), res_.addedFor.end(), [&](const auto &a) { return a.first == key; });
}

std::vector<std::string> DiskWizard::alternativesOf(const ManifestBundle &b) const
{
    std::vector<std::string> out;
    for (const auto &name : b.provides) {
        const auto providers = std::count_if(m_.bundles.begin(), m_.bundles.end(), [&](const ManifestBundle &o) {
            return std::find(o.provides.begin(), o.provides.end(), name) != o.provides.end();
        });
        if (providers > 1) out.push_back(name);
    }
    return out;
}

void DiskWizard::dropWhatDoesNotFit()
{
    std::vector<std::string> kept;
    for (const auto &key : sel_.bundles) {
        const auto *b = m_.bundle(key);
        const std::string why = b ? bundleRefusal(m_, *b, sel_.system, sel_.media) : key + " is not in this collection";
        if (why.empty()) kept.push_back(key);
        else notices_.push_back((b ? b->title : key) + " dropped: " + why);
    }
    sel_.bundles = kept;
    std::erase_if(sel_.picks, [&](const auto &p) {
        const auto *b = m_.bundle(p.second);
        return !b || !bundleRefusal(m_, *b, sel_.system, sel_.media).empty();
    });
    resolve();
    /* What still breaks a rule: drop the chosen bundles that fail on their
     * own, the last one if none does. */
    while (!res_.ok && !sel_.bundles.empty()) {
        auto victim = sel_.bundles.end() - 1;
        for (auto it = sel_.bundles.begin(); it != sel_.bundles.end(); ++it)
            if (!resolveBundles(m_, sel_.system, sel_.media, {*it}, sel_.picks).ok) { victim = it; break; }
        const Resolution alone = resolveBundles(m_, sel_.system, sel_.media, {*victim}, sel_.picks);
        notices_.push_back(m_.bundle(*victim)->title + " dropped: " + (alone.ok ? res_.problem : alone.problem));
        sel_.bundles.erase(victim);
        resolve();
    }
}

void DiskWizard::setSystem(const std::string &key)
{
    notices_.clear();
    if (!m_.system(key)) return;
    sel_.system = key;
    const auto offered = mediaOffered();
    if (std::find(offered.begin(), offered.end(), sel_.media) == offered.end() && !offered.empty())
        sel_.media = offered.front();
    dropWhatDoesNotFit();
}

void DiskWizard::setMedia(Media media)
{
    notices_.clear();
    const auto offered = mediaOffered();
    if (std::find(offered.begin(), offered.end(), media) == offered.end()) return;
    sel_.media = media;
    dropWhatDoesNotFit();
}

std::string DiskWizard::toggle(const std::string &key)
{
    const auto *b = m_.bundle(key);
    if (!b) return "no bundle " + key;
    if (isSystemPart(key)) return b->title + " is part of the system";
    if (auto why = bundleRefusal(m_, *b, sel_.system, sel_.media); !why.empty()) return why;

    const Selection before = sel_;
    const bool installed = contains(res_.bundles, key);
    if (installed) {
        if (!contains(sel_.bundles, key)) {
            for (const auto &[added, forWhom] : res_.addedFor)
                if (added == key) return b->title + " is required by " + m_.bundle(forWhom)->title;
        }
        std::erase(sel_.bundles, key);
        resolve();
        if (contains(res_.bundles, key)) {                 /* chosen, and needed as well */
            std::string by;
            for (const auto &[added, forWhom] : res_.addedFor) if (added == key) by = m_.bundle(forWhom)->title;
            sel_ = before;
            resolve();
            return b->title + " is required by " + by;
        }
    } else {
        const auto names = alternativesOf(*b);
        for (const auto &name : names) {
            std::erase_if(sel_.bundles, [&](const std::string &other) {
                const auto *o = m_.bundle(other);
                return other != key && o && std::find(o->provides.begin(), o->provides.end(), name) != o->provides.end();
            });
            sel_.picks[name] = key;
        }
        resolve();
        if (!contains(res_.bundles, key)) sel_.bundles.push_back(key);
    }
    resolve();
    if (!res_.ok) {
        const std::string why = res_.problem;
        sel_ = before;
        resolve();
        return why;
    }
    return "";
}

void DiskWizard::setStartup(std::vector<std::string> lines)
{
    if (lines.empty()) sel_.startup.reset(); else sel_.startup = std::move(lines);
}

void DiskWizard::setVolumeId(std::optional<std::string> id) { sel_.volumeId = std::move(id); }

std::vector<WizardRow> DiskWizard::rows() const
{
    std::vector<WizardRow> out;
    auto visible = [&](const ManifestBundle &b) { return b.systems.empty() || contains(b.systems, sel_.system); };
    auto bundleRow = [&](const ManifestBundle &b, int depth, bool radio) {
        WizardRow r;
        r.kind = WizardRow::Kind::bundle;
        r.depth = depth;
        r.key = b.key;
        r.title = b.title;
        r.radio = radio;
        r.blocks = blocksOf_ ? blocksOf_(b) : 0;
        if (isSystemPart(b.key)) {
            r.mark = WizardRow::Mark::system;
        } else if (contains(sel_.bundles, b.key)) {
            r.mark = WizardRow::Mark::on;
        } else if (contains(res_.bundles, b.key)) {
            r.mark = WizardRow::Mark::added;
            for (const auto &[added, forWhom] : res_.addedFor) if (added == b.key) r.requiredBy = m_.bundle(forWhom)->title;
        } else {
            r.why = bundleRefusal(m_, b, sel_.system, sel_.media);
            if (r.why.empty()) {
                auto chosen = sel_.bundles;
                chosen.push_back(b.key);
                auto picks = sel_.picks;
                for (const auto &name : alternativesOf(b)) picks[name] = b.key;
                const Resolution trial = resolveBundles(m_, sel_.system, sel_.media, chosen, picks);
                if (!trial.ok) r.why = trial.problem;
            }
            r.available = r.why.empty();
        }
        out.push_back(std::move(r));
    };

    std::vector<std::string> groups;
    for (const auto &b : m_.bundles) if (visible(b) && !contains(groups, b.group)) groups.push_back(b.group);
    std::set<std::string> radiosDone;
    for (const auto &group : groups) {
        out.push_back(heading(WizardRow::Kind::group, 0, group, group.empty() ? std::string("Other") : group));
        for (const auto &b : m_.bundles) {
            if (!visible(b) || b.group != group) continue;
            /* A radio group only where this system shows two builds or more:
             * one build alone is a plain line. */
            auto names = alternativesOf(b);
            std::erase_if(names, [&](const std::string &name) {
                return std::count_if(m_.bundles.begin(), m_.bundles.end(), [&](const ManifestBundle &o) {
                    return visible(o) && std::find(o.provides.begin(), o.provides.end(), name) != o.provides.end();
                }) < 2;
            });
            if (names.empty()) { bundleRow(b, 1, false); continue; }
            if (radiosDone.count(names.front())) continue;
            radiosDone.insert(names.front());
            out.push_back(heading(WizardRow::Kind::radio, 1, names.front(), names.front()));
            for (const auto &o : m_.bundles)
                if (visible(o) && std::find(o.provides.begin(), o.provides.end(), names.front()) != o.provides.end())
                    bundleRow(o, 2, true);
        }
    }
    return out;
}

void DiskWizard::load(const SavedSelection &saved)
{
    notices_.clear();
    const Selection &s = saved.selection;
    if (!saved.collection.empty() && !m_.version.empty() && saved.collection != m_.version)
        notices_.push_back("made over collection " + saved.collection + ", this is " + m_.version +
                           ": its rules may have changed");
    if (m_.system(s.system)) sel_.system = s.system;
    else notices_.push_back("system " + s.system + " is not in this collection");
    const auto offered = mediaOffered();
    sel_.media = std::find(offered.begin(), offered.end(), s.media) != offered.end() || offered.empty() ? s.media : offered.front();
    sel_.bundles.clear();
    for (const auto &key : s.bundles) {
        if (m_.bundle(key)) sel_.bundles.push_back(key);
        else notices_.push_back(key + " is not in this collection");
    }
    sel_.picks.clear();
    for (const auto &[name, key] : s.picks) if (m_.bundle(key)) sel_.picks[name] = key;
    sel_.startup = s.startup;
    sel_.volumeId = s.volumeId;
    dropWhatDoesNotFit();
}

SavedSelection DiskWizard::saved() const
{
    return {m_.version, sel_};
}

/* ---- the file ------------------------------------------------------------- */

std::string selectionToml(const SavedSelection &saved)
{
    const Selection &s = saved.selection;
    std::string t;
    t += "# A disk chosen with ms0515-disk compose; build it again with\n";
    t += "#   ms0515-disk compose --repo <the collection> --selection <this file> <out.dsk>\n";
    t += "format     = 1\n";
    if (!saved.collection.empty()) t += "collection = " + quoted(saved.collection) + "\n";
    t += "system     = " + quoted(s.system) + "\n";
    t += std::string("media      = ") + quoted(mediaWord(s.media)) + "\n";
    t += "bundles    = " + list(s.bundles) + "\n";
    if (!s.picks.empty()) {
        t += "picks      = {";
        bool first = true;
        for (const auto &[name, key] : s.picks) {
            t += std::string(first ? " " : ", ") + (bareKey(name) ? name : quoted(name)) + " = " + quoted(key);
            first = false;
        }
        t += " }\n";
    }
    if (s.startup) t += "startup    = " + list(*s.startup) + "\n";
    if (s.volumeId) t += "volume_id  = " + quoted(*s.volumeId) + "\n";
    return t;
}

SavedSelection parseSelection(std::string_view text)
{
    toml::table root;
    try {
        root = toml::parse(text);
    } catch (const toml::parse_error &e) {
        fail("line " + std::to_string(e.source().begin.line) + ": " + std::string(e.description()));
    }
    if (root["format"].value<int64_t>() != 1) fail("format is not 1");
    SavedSelection out;
    out.collection = root["collection"].value_or(std::string());
    const auto system = root["system"].value<std::string>();
    if (!system) fail("no system");
    out.selection.system = *system;
    const auto media = parseMedia(root["media"].value_or(std::string()));
    if (!media) fail("media is ss, dz or dv");
    out.selection.media = *media;
    out.selection.bundles = strings(root, "bundles");
    if (const auto *picks = root["picks"].as_table())
        for (const auto &[k, v] : *picks)
            if (const auto key = v.value<std::string>()) out.selection.picks[std::string(k.str())] = *key;
    if (root.contains("startup")) out.selection.startup = strings(root, "startup");
    if (const auto id = root["volume_id"].value<std::string>()) out.selection.volumeId = *id;
    return out;
}

} /* namespace ms0515::disk */
