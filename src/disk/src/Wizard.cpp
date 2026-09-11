/*
 * Wizard.cpp - the disk wizards' model and the saved-choice file.
 */

#include "ms0515/disk/Wizard.hpp"

#include <toml++/toml.hpp>

#include <algorithm>
#include <cctype>
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

/* "Development / Pascal" -> {"Development", "Pascal"}; no name: "Other". */
std::vector<std::string> groupPath(const std::string &group)
{
    std::vector<std::string> out;
    std::size_t at = 0;
    for (;;) {
        const auto cut = group.find('/', at);
        std::string part = group.substr(at, cut == std::string::npos ? std::string::npos : cut - at);
        const auto first = part.find_first_not_of(' '), last = part.find_last_not_of(' ');
        if (first != std::string::npos) out.push_back(part.substr(first, last - first + 1));
        if (cut == std::string::npos) break;
        at = cut + 1;
    }
    if (out.empty()) out.emplace_back("Other");
    return out;
}

/* The label's fields: each side's volume id and owner. */
struct LabelField {
    const char *key;
    const char *what;
    std::optional<std::string> Selection::*member;
    bool        secondSide;
};

const LabelField kLabelFields[] = {
    {kVolumeIdField, "volume id", &Selection::volumeId, false},
    {kOwnerField, "owner", &Selection::owner, false},
    {kSecondVolumeIdField, "volume id", &Selection::secondVolumeId, true},
    {kSecondOwnerField, "owner", &Selection::secondOwner, true},
};

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

const char *mediaTitle(Media m)
{
    switch (m) {
    case Media::ss: return "ss - one side, 400 KB";
    case Media::dz: return "dz - two sides, 800 KB";
    case Media::dv: return "dv - one DV volume, 800 KB";
    }
    return "";
}

/* A group of bundles and the groups under it, as disks.toml's "A / B" names
 * make them. */
struct DiskWizard::Branch {
    std::string key;
    std::string title;
    std::vector<const ManifestBundle *> bundles;
    std::vector<Branch> children;
};

DiskWizard::DiskWizard(const Manifest &manifest, std::function<int(const ManifestBundle &)> blocksOf)
    : m_(manifest), blocksOf_(std::move(blocksOf)), open_{kDisketteGroup, kStartupGroup}
{
}

DiskWizard::DiskWizard(const Manifest &manifest, std::string system, Media media,
                       std::function<int(const ManifestBundle &)> blocksOf)
    : m_(manifest), blocksOf_(std::move(blocksOf)), media_(media), open_{kStartupGroup}
{
    if (const auto *sys = m_.system(system)) {
        sel_.system = std::move(system);
        if (!sys->media.empty() && std::find(sys->media.begin(), sys->media.end(), media) == sys->media.end())
            media_ = sys->media.front();
    } else {
        open_.insert(kSystemGroup);
    }
    sel_.media = *media_;
    resolve();
}

void DiskWizard::resolve()
{
    if (!ready()) { res_ = {}; own_ = {}; return; }
    res_ = resolveBundles(m_, sel_.system, sel_.media, sel_.bundles, sel_.picks);
    own_ = resolveBundles(m_, sel_.system, sel_.media, {}, sel_.picks);
}

/* What the system requires on this media: its, even ticked by hand before
 * (DV.SYS ticked on dz, then the disk made dv). */
bool DiskWizard::isSystemPart(const std::string &key) const
{
    return contains(own_.bundles, key) && contains(res_.bundles, key);
}

std::string DiskWizard::systemRefusal(const ManifestSystem &s) const
{
    if (!media_) return "choose the diskette first";
    if (std::find(s.media.begin(), s.media.end(), *media_) != s.media.end()) return "";
    std::string words;
    for (const auto m : s.media) words += (words.empty() ? "" : ", ") + std::string(mediaWord(m));
    return "only on " + words;
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

std::string DiskWizard::setMedia(Media media)
{
    notices_.clear();
    if (!media_) {                                   /* the first step done: the next opens */
        open_.insert(kLabelGroup);
        open_.insert(kSystemGroup);
    }
    media_ = media;
    sel_.media = media;
    if (const auto *sys = m_.system(sel_.system)) {
        if (auto why = systemRefusal(*sys); !why.empty()) {
            notices_.push_back(sys->title + " dropped: it goes " + why);
            sel_.system.clear();
            open_.insert(kSystemGroup);
            resolve();
        } else {
            dropWhatDoesNotFit();
        }
    }
    return "";
}

std::string DiskWizard::setSystem(const std::string &key)
{
    const auto *sys = m_.system(key);
    if (!sys) return "no system " + key;
    if (!media_) return "choose the diskette first";
    if (auto why = systemRefusal(*sys); !why.empty()) return sys->title + " goes " + why;
    notices_.clear();
    sel_.system = key;
    dropWhatDoesNotFit();
    return "";
}

std::string DiskWizard::toggle(const std::string &key)
{
    const auto *b = m_.bundle(key);
    if (!b) return "no bundle " + key;
    if (!ready()) return "choose the diskette and the system first";
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

void DiskWizard::toggleFold(const std::string &groupKey)
{
    if (!open_.erase(groupKey)) open_.insert(groupKey);
}

void DiskWizard::reveal(const std::string &groupKey)
{
    for (auto at = groupKey.find(" / "); at != std::string::npos; at = groupKey.find(" / ", at + 3))
        open_.insert(groupKey.substr(0, at));
    open_.insert(groupKey);
}

void DiskWizard::setStartup(std::vector<std::string> lines)
{
    if (lines.empty()) sel_.startup.reset(); else sel_.startup = std::move(lines);
}

void DiskWizard::setVolumeId(std::optional<std::string> id) { sel_.volumeId = std::move(id); }

std::string DiskWizard::setField(const std::string &key, const std::string &value)
{
    const auto first = value.find_first_not_of(' '), last = value.find_last_not_of(' ');
    const std::string text = first == std::string::npos ? std::string() : value.substr(first, last - first + 1);
    for (const auto &f : kLabelFields) {
        if (key != f.key) continue;
        if (!media_) return "choose the diskette first";
        if (f.secondSide && media_ != Media::dz) return "only a two-sided diskette has a second side";
        std::string label = text.substr(0, 12);
        for (auto &c : label) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        sel_.*f.member = label.empty() ? std::nullopt : std::optional<std::string>(label);
        return "";
    }
    const std::string prefix = kStartupField;
    if (key.rfind(prefix, 0) != 0 || key.size() == prefix.size()) return "no field " + key;
    if (!ready()) return "choose the diskette and the system first";
    const auto at = static_cast<std::size_t>(std::stoul(key.substr(prefix.size())));
    auto lines = sel_.startup.value_or(std::vector<std::string>{});
    if (at < lines.size()) {
        if (text.empty()) lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(at));
        else lines[at] = text;
    } else if (!text.empty()) {
        lines.push_back(text);
    }
    setStartup(std::move(lines));
    return "";
}

/* ---- the rows ------------------------------------------------------------- */

WizardRow::Mark DiskWizard::markOf(const ManifestBundle &b) const
{
    if (isSystemPart(b.key)) return WizardRow::Mark::system;
    if (contains(sel_.bundles, b.key)) return WizardRow::Mark::on;
    if (contains(res_.bundles, b.key)) return WizardRow::Mark::added;
    return WizardRow::Mark::off;
}

WizardRow DiskWizard::bundleRow(const ManifestBundle &b, int depth, bool radio) const
{
    WizardRow r = heading(WizardRow::Kind::bundle, depth, b.key, b.title);
    r.radio = radio;
    r.blocks = blocksOf_ ? blocksOf_(b) : 0;
    r.mark = markOf(b);
    if (const auto *sys = m_.system(sel_.system); sys && !alternativesOf(b).empty()) r.native = contains(sys->prefer, b.key);
    if (r.mark == WizardRow::Mark::added) {
        for (const auto &[added, forWhom] : res_.addedFor) if (added == b.key) r.requiredBy = m_.bundle(forWhom)->title;
    } else if (r.mark == WizardRow::Mark::off) {
        r.why = bundleRefusal(m_, b, sel_.system, sel_.media);
        if (r.why.empty()) {
            /* As toggle() would take it: picking an alternative puts the
             * one chosen before out. */
            auto chosen = sel_.bundles;
            auto picks = sel_.picks;
            for (const auto &name : alternativesOf(b)) {
                std::erase_if(chosen, [&](const std::string &other) {
                    const auto *o = m_.bundle(other);
                    return o && std::find(o->provides.begin(), o->provides.end(), name) != o->provides.end();
                });
                picks[name] = b.key;
            }
            chosen.push_back(b.key);
            const Resolution trial = resolveBundles(m_, sel_.system, sel_.media, chosen, picks);
            if (!trial.ok) r.why = trial.problem;
        }
        r.available = r.why.empty();
    }
    return r;
}

/* The first two steps: the diskette, then the system on it. */
void DiskWizard::stepRows(std::vector<WizardRow> &out, bool everything) const
{
    WizardRow diskette = heading(WizardRow::Kind::group, 0, kDisketteGroup, "Diskette");
    diskette.open = everything || open_.count(kDisketteGroup) != 0;
    if (media_) diskette.summary = mediaTitle(*media_);
    out.push_back(diskette);
    if (diskette.open) {
        for (const auto m : {Media::ss, Media::dz, Media::dv}) {
            WizardRow r = heading(WizardRow::Kind::media, 1, mediaWord(m), mediaTitle(m));
            r.parent = kDisketteGroup;
            r.radio = true;
            r.mark = media_ == m ? WizardRow::Mark::on : WizardRow::Mark::off;
            out.push_back(std::move(r));
        }
    }
    labelRows(out, everything);
    WizardRow os = heading(WizardRow::Kind::group, 0, kSystemGroup, "Operating system");
    os.available = media_.has_value();
    if (!os.available) os.why = "choose the diskette first";
    os.open = os.available && (everything || open_.count(kSystemGroup) != 0);
    if (const auto *sys = m_.system(sel_.system)) os.summary = sys->title;
    out.push_back(os);
    if (!os.open) return;
    for (const auto &s : m_.systems) {
        WizardRow r = heading(WizardRow::Kind::system, 1, s.key, s.title);
        r.parent = kSystemGroup;
        r.radio = true;
        r.mark = s.key == sel_.system ? WizardRow::Mark::on : WizardRow::Mark::off;
        r.why = systemRefusal(s);
        r.available = r.why.empty();
        out.push_back(std::move(r));
    }
}

DiskWizard::Branch DiskWizard::tree() const
{
    Branch root;
    for (const auto &b : m_.bundles) {
        if (ready() && !b.systems.empty() && !contains(b.systems, sel_.system)) continue;
        Branch *at = &root;
        std::string key;
        for (const auto &part : groupPath(b.group)) {
            key += (key.empty() ? "" : " / ") + part;
            auto it = std::find_if(at->children.begin(), at->children.end(), [&](const Branch &c) { return c.title == part; });
            if (it == at->children.end()) {
                at->children.push_back({key, part, {}, {}});
                it = at->children.end() - 1;
            }
            at = &*it;
        }
        at->bundles.push_back(&b);
    }
    return root;
}

void DiskWizard::branchRows(std::vector<WizardRow> &out, const Branch &branch, int depth, bool everything) const
{
    WizardRow g = heading(WizardRow::Kind::group, depth, branch.key, branch.title);
    if (const auto cut = branch.key.rfind(" / "); cut != std::string::npos) g.parent = branch.key.substr(0, cut);
    if (!ready()) {
        g.available = false;
        g.why = "choose the system first";
        out.push_back(std::move(g));
        return;
    }
    int on = 0, added = 0;
    auto count = [&](const Branch &b, const auto &self) -> void {
        for (const auto *bundle : b.bundles) {
            const auto mark = markOf(*bundle);
            if (mark == WizardRow::Mark::on) ++on;
            if (mark == WizardRow::Mark::added) ++added;
        }
        for (const auto &c : b.children) self(c, self);
    };
    count(branch, count);
    if (on) g.summary = std::to_string(on) + " chosen";
    if (added) g.summary += (g.summary.empty() ? "" : ", ") + std::to_string(added) + " added";
    g.open = everything || open_.count(branch.key) != 0;
    out.push_back(g);
    if (!g.open) return;
    leafRows(out, branch, depth + 1);
    for (const auto &c : branch.children) branchRows(out, c, depth + 1, everything);
}

/* A group's own bundles; the alternatives among them a radio group - where
 * it shows two builds or more, one build alone being a plain line. */
void DiskWizard::leafRows(std::vector<WizardRow> &out, const Branch &branch, int depth) const
{
    auto providers = [&](const std::string &name) {
        std::vector<const ManifestBundle *> v;
        for (const auto *o : branch.bundles)
            if (std::find(o->provides.begin(), o->provides.end(), name) != o->provides.end()) v.push_back(o);
        return v;
    };
    std::set<std::string> radiosDone;
    for (const auto *b : branch.bundles) {
        auto names = alternativesOf(*b);
        std::erase_if(names, [&](const std::string &name) { return providers(name).size() < 2; });
        if (names.empty()) {
            out.push_back(bundleRow(*b, depth, false));
            out.back().parent = branch.key;
            continue;
        }
        if (!radiosDone.insert(names.front()).second) continue;
        WizardRow r = heading(WizardRow::Kind::radio, depth, names.front(), names.front());
        r.parent = branch.key;
        out.push_back(std::move(r));
        for (const auto *o : providers(names.front())) {
            out.push_back(bundleRow(*o, depth + 1, true));
            out.back().parent = branch.key;
        }
    }
}

void DiskWizard::labelRows(std::vector<WizardRow> &out, bool everything) const
{
    WizardRow label = heading(WizardRow::Kind::group, 0, kLabelGroup, "Label");
    label.available = media_.has_value();
    if (!label.available) label.why = "choose the diskette first";
    label.open = label.available && (everything || open_.count(kLabelGroup) != 0);
    const bool twoSides = media_ == Media::dz;
    label.summary = sel_.volumeId.value_or("");
    if (twoSides && sel_.secondVolumeId)
        label.summary += (label.summary.empty() ? "" : " \xC2\xB7 ") + *sel_.secondVolumeId;
    out.push_back(label);
    if (!label.open) return;
    for (const auto &f : kLabelFields) {
        if (f.secondSide && !twoSides) continue;
        std::string title = f.what;
        if (twoSides) title = std::string(f.secondSide ? "DZ2: " : "DZ0: ") + f.what;
        else title[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(title[0])));
        WizardRow r = heading(WizardRow::Kind::field, 1, f.key, title);
        r.parent = kLabelGroup;
        r.value = (sel_.*f.member).value_or("");
        r.summary = "up to 12 characters";
        out.push_back(std::move(r));
    }
}

/* The top group holding the system's parts - where its startup file shows. */
std::string DiskWizard::startupHome() const
{
    for (const auto &key : res_.bundles)
        if (isSystemPart(key)) return groupPath(m_.bundle(key)->group).front();
    return "";
}

void DiskWizard::startupRows(std::vector<WizardRow> &out, int depth, const std::string &parent, bool everything) const
{
    std::vector<std::pair<std::string, std::string>> fixed;       /* the line, whose */
    auto add = [&](const std::string &line, const std::string &from) {
        if (std::none_of(fixed.begin(), fixed.end(), [&](const auto &f) { return f.first == line; }))
            fixed.emplace_back(line, from);
    };
    const auto *sys = m_.system(sel_.system);
    if (sys && sys->startup) for (const auto &line : *sys->startup) add(line, sys->title);
    for (const auto &key : res_.bundles)
        if (const auto *b = m_.bundle(key)) for (const auto &line : b->startup) add(line, b->title);
    const auto own = sel_.startup.value_or(std::vector<std::string>{});

    WizardRow g = heading(WizardRow::Kind::group, depth, kStartupGroup, "START.COM");
    g.parent = parent;
    const auto count = fixed.size() + own.size();
    g.summary = std::to_string(count) + (count == 1 ? " line" : " lines");
    g.open = everything || open_.count(kStartupGroup) != 0;
    out.push_back(g);
    if (!g.open) return;
    for (const auto &[line, from] : fixed) {
        WizardRow r = heading(WizardRow::Kind::line, depth + 1, line, line);
        r.parent = kStartupGroup;
        r.requiredBy = from;
        out.push_back(std::move(r));
    }
    for (std::size_t i = 0; i <= own.size(); ++i) {
        WizardRow r = heading(WizardRow::Kind::field, depth + 1, kStartupField + std::to_string(i), "");
        r.parent = kStartupGroup;
        if (i < own.size()) r.value = own[i]; else r.summary = "a new line";
        out.push_back(std::move(r));
    }
}

std::vector<WizardRow> DiskWizard::rows(bool everything) const
{
    std::vector<WizardRow> out;
    stepRows(out, everything);
    if (!ready()) {
        for (const auto &top : tree().children) branchRows(out, top, 0, everything);
        return out;
    }
    const std::string home = startupHome();
    for (const auto &top : tree().children) {
        branchRows(out, top, 0, everything);
        if (top.key == home && (everything || open_.count(top.key) != 0)) startupRows(out, 1, top.key, everything);
    }
    if (home.empty()) startupRows(out, 0, "", everything);
    return out;
}

void DiskWizard::load(const SavedSelection &saved)
{
    notices_.clear();
    const Selection &s = saved.selection;
    if (!saved.collection.empty() && !m_.version.empty() && saved.collection != m_.version)
        notices_.push_back("made over collection " + saved.collection + ", this is " + m_.version +
                           ": its rules may have changed");
    media_ = s.media;
    sel_.system.clear();
    if (const auto *sys = m_.system(s.system)) {
        sel_.system = s.system;
        if (!sys->media.empty() && std::find(sys->media.begin(), sys->media.end(), s.media) == sys->media.end())
            media_ = sys->media.front();
    } else {
        notices_.push_back("system " + s.system + " is not in this collection");
    }
    sel_.media = *media_;
    sel_.bundles.clear();
    for (const auto &key : s.bundles) {
        if (m_.bundle(key)) sel_.bundles.push_back(key);
        else notices_.push_back(key + " is not in this collection");
    }
    sel_.picks.clear();
    for (const auto &[name, key] : s.picks) if (m_.bundle(key)) sel_.picks[name] = key;
    sel_.startup = s.startup;
    sel_.volumeId = s.volumeId;
    sel_.owner = s.owner;
    sel_.secondVolumeId = s.secondVolumeId;
    sel_.secondOwner = s.secondOwner;
    open_ = {kStartupGroup};
    if (sel_.system.empty()) open_.insert(kSystemGroup);
    if (ready()) dropWhatDoesNotFit(); else resolve();
    openWhatIsChosen();
}

std::vector<std::pair<std::string, int>> DiskWizard::blocksByGroup() const
{
    std::vector<std::pair<std::string, int>> out;
    for (const auto &key : res_.bundles) {
        const auto *b = m_.bundle(key);
        const std::string top = groupPath(b->group).front();
        const int n = blocksOf_ ? blocksOf_(*b) : 0;
        const auto it = std::find_if(out.begin(), out.end(), [&](const auto &g) { return g.first == top; });
        if (it == out.end()) out.emplace_back(top, n); else it->second += n;
    }
    return out;
}

/* A choice read from a file: the diskette and the system open, the label
 * when one is typed, and the way down to every bundle chosen or brought
 * along, to a build other than the system's own, to its START.COM lines. */
void DiskWizard::openWhatIsChosen()
{
    open_.insert(kDisketteGroup);
    if (sel_.volumeId || sel_.owner || sel_.secondVolumeId || sel_.secondOwner) open_.insert(kLabelGroup);
    if (!ready()) return;
    open_.insert(kSystemGroup);
    const auto *sys = m_.system(sel_.system);
    auto pathOf = [](const ManifestBundle &b) {
        std::string key;
        for (const auto &part : groupPath(b.group)) key += (key.empty() ? "" : " / ") + part;
        return key;
    };
    for (const auto &key : res_.bundles) {
        const auto *b = m_.bundle(key);
        const auto mark = markOf(*b);
        const bool foreign = mark == WizardRow::Mark::system && !alternativesOf(*b).empty() && !contains(sys->prefer, key);
        if (mark == WizardRow::Mark::on || mark == WizardRow::Mark::added || foreign) reveal(pathOf(*b));
    }
    if (sel_.startup) if (const auto home = startupHome(); !home.empty()) reveal(home);
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
    if (s.owner) t += "owner      = " + quoted(*s.owner) + "\n";
    if (s.secondVolumeId) t += "second_volume_id = " + quoted(*s.secondVolumeId) + "\n";
    if (s.secondOwner) t += "second_owner     = " + quoted(*s.secondOwner) + "\n";
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
    if (const auto v = root["owner"].value<std::string>()) out.selection.owner = *v;
    if (const auto v = root["second_volume_id"].value<std::string>()) out.selection.secondVolumeId = *v;
    if (const auto v = root["second_owner"].value<std::string>()) out.selection.secondOwner = *v;
    return out;
}

} /* namespace ms0515::disk */
