/*
 * Compose.cpp - a whole bootable diskette from an exemplar and groups of
 * files.  The plan is the build: every group is put for real on a scratch
 * copy, so the answer to "does it fit" is RT-11's own - blocks, directory
 * entries, the first empty area that takes it.
 */

#include "ms0515/disk/Compose.hpp"

#include "ms0515/disk/Build.hpp"
#include "ms0515/disk/Image.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace ms0515::disk {

namespace {

/* How a media is addressed: double-sided or not, the boot volume's kind,
 * and how many volumes it holds. */
struct Shape {
    bool ds;
    Vol  boot;
    int  volumes;
};

Shape shapeOf(Media m)
{
    switch (m) {
    case Media::ss: return {false, Vol::floppy, 1};
    case Media::dz: return {true, Vol::floppy, 2};
    case Media::dv: return {true, Vol::dv, 1};
    }
    return {false, Vol::floppy, 1};
}

Vol volumeKind(const Shape &s, int volume) { return volume == 0 ? s.boot : Vol::floppy; }

std::optional<Image> openAt(const std::vector<uint8_t> &img, const Shape &s, int volume)
{
    return openVolume(img, volumeKind(s, volume), volume);
}

/* RT-11's 6.3: one to six of A-Z 0-9 $, optionally a dot and up to three. */
bool validName(const std::string &name)
{
    auto ok = [](const std::string &part, std::size_t lo, std::size_t hi) {
        if (part.size() < lo || part.size() > hi) return false;
        return std::all_of(part.begin(), part.end(), [](char c) {
            return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '$';
        });
    };
    const auto dot = name.find('.');
    if (dot == std::string::npos) return ok(name, 1, 6);
    return ok(name.substr(0, dot), 1, 6) && ok(name.substr(dot + 1), 0, 3);
}

int freeBlocks(const std::vector<uint8_t> &img, const Shape &s, int volume)
{
    const auto im = openAt(img, s, volume);
    int n = 0;
    if (im && im->hasDirectory)
        for (const auto &e : im->directory.entries) if (e.isEmpty()) n += e.length;
    return n;
}

std::vector<uint8_t> startupBytes(const std::vector<std::string> &lines)
{
    std::vector<uint8_t> out;
    for (const auto &l : lines) {
        out.insert(out.end(), l.begin(), l.end());
        out.push_back('\r');
        out.push_back('\n');
    }
    return out;
}

void label(std::vector<uint8_t> &img, const Shape &s, int volume,
           const std::optional<std::string> &id, const std::optional<std::string> &owner)
{
    if (!id && !owner) return;
    const std::size_t home = lbnToByte(1, volume, s.ds, volumeKind(s, volume));
    auto field = [&](std::size_t off) { return std::string(reinterpret_cast<const char *>(img.data()) + home + off, 12); };
    auto trim = [](std::string v) { while (!v.empty() && v.back() == ' ') v.pop_back(); return v; };
    setVolumeId(img, volume, s.ds, id ? *id : trim(field(0x1D8)), owner ? *owner : trim(field(0x1E4)),
                volumeKind(s, volume));
}

/* The exemplar's boot volume and the monitor it boots. */
struct Source {
    Image       volume;
    std::string monitor;
    std::string startup;     /* the startup file the monitor names, "" none */
};

Source sourceOf(const std::vector<uint8_t> &system)
{
    const auto media = mediaOf(system);
    if (!media) throw std::runtime_error("the system image is no diskette this knows (ss, dz or dv)");
    const Shape s = shapeOf(*media);
    auto vol = openAt(system, s, 0);
    const std::string monitor = bootedMonitor(system, 0, s.ds, s.boot);
    if (!vol || !vol->hasDirectory || monitor.empty())
        throw std::runtime_error("the system image does not boot: no monitor behind its bootstrap");
    std::string startup = startupFile(vol->readFile(monitor + ".SYS"));
    if (startup.empty()) startup = "START.COM";
    return {std::move(*vol), monitor, startup};
}

/* A fresh diskette holding the exemplar's kit, its startup file replaced
 * when lines are given, and the bootstrap of the new media. */
std::vector<uint8_t> rebuilt(const ComposeRecipe &r, const Source &src, const Shape &s)
{
    auto img = blankImage(s.ds);
    initVolume(img, 0, s.ds, {}, s.boot);
    if (s.volumes == 2) initVolume(img, 1, s.ds);
    bool startupPut = false;
    for (const auto &e : src.volume.directory.permanentFiles()) {
        PutOptions o{e.date, (e.status & kStatusProtected) != 0};
        if (e.name == src.startup && r.startup) {
            putFile(img, 0, s.ds, e.name, startupBytes(*r.startup), o, s.boot);
            startupPut = true;
        } else {
            putFile(img, 0, s.ds, e.name, src.volume.readFile(e.name), o, s.boot);
        }
    }
    if (r.startup && !startupPut) putFile(img, 0, s.ds, src.startup, startupBytes(*r.startup), {}, s.boot);
    writeBoot(img, 0, s.ds, src.monitor, s.boot);
    return img;
}

/* The exemplar itself, only its startup file made anew when asked. */
std::vector<uint8_t> kept(const ComposeRecipe &r, const Source &src, const Shape &s)
{
    if (mediaOf(r.system) != r.media)
        throw std::runtime_error("this system is kept as it is, so only its own media can be made");
    auto img = r.system;
    if (r.startup) {
        PutOptions o;
        if (const auto *e = src.volume.directory.find(src.startup)) {
            o.date = e->date;
            removeFile(img, 0, s.ds, src.startup, s.boot);
        }
        putFile(img, 0, s.ds, src.startup, startupBytes(*r.startup), o, s.boot);
    }
    return img;
}

bool reservedIntact(const ComposeRecipe &r, const std::vector<uint8_t> &img)
{
    if (r.rebuild) return true;
    const bool ds = r.system.size() == kDoubleSize;
    return std::all_of(r.reserved.begin(), r.reserved.end(), [&](const ReservedBlock &b) {
        const auto at = lbnToByte(b.lbn, b.side, ds, Vol::floppy);
        return std::memcmp(img.data() + at, r.system.data() + at, kBlock) == 0;
    });
}

/* Put one group on one volume of a scratch copy; "" when it went, else why
 * it did not. */
std::string tryGroup(std::vector<uint8_t> &img, const ComposeRecipe &r, const Shape &s,
                     const ComposeGroup &g, int volume)
{
    auto scratch = img;
    try {
        for (const auto &f : g.files) {
            if (!validName(f.name)) return f.name + " is not an RT-11 name (6.3, A-Z 0-9 $)";
            const auto im = openAt(scratch, s, volume);
            if (im->directory.find(f.name)) return f.name + " is already on the volume";
            putFile(scratch, volume, s.ds, f.name, f.data, PutOptions{f.date, f.protect}, volumeKind(s, volume));
        }
    } catch (const std::exception &e) {
        return std::string("does not fit (") + e.what() + ")";
    }
    if (!reservedIntact(r, scratch)) return "would overwrite a reserved block of the system (its copy protection)";
    img = std::move(scratch);
    return "";
}

int groupBlocks(const ComposeGroup &g)
{
    int n = 0;
    for (const auto &f : g.files) n += static_cast<int>((f.data.size() + kBlock - 1) / kBlock);
    return n;
}

/* The composition and its plan together: planDisk keeps the plan,
 * composeDisk the image. */
std::vector<uint8_t> compose(const ComposeRecipe &r, ComposePlan &plan)
{
    const Shape s = shapeOf(r.media);
    std::vector<uint8_t> img;
    try {
        const Source src = sourceOf(r.system);
        if (r.media == Media::dv && !src.volume.directory.find("DV.SYS"))
            throw std::runtime_error("the system has no DV.SYS, so it cannot boot a DV disk");
        img = r.rebuild ? rebuilt(r, src, s) : kept(r, src, s);
        label(img, s, 0, r.volumeId, r.owner);
        if (s.volumes == 2) label(img, s, 1, r.secondVolumeId, r.secondOwner);
    } catch (const std::exception &e) {
        plan.problem = e.what();
        for (const auto &g : r.groups) plan.groups.push_back({g.title, -1, groupBlocks(g), plan.problem});
        return {};
    }

    for (const auto &g : r.groups) {
        GroupPlacement p{g.title, -1, groupBlocks(g), ""};
        const int last = g.place == Place::any ? s.volumes - 1 : 0;
        for (int v = 0; v <= last && p.volume < 0; ++v) {
            const std::string why = tryGroup(img, r, s, g, v);
            if (why.empty()) p.volume = v; else p.problem = why;
        }
        if (p.volume >= 0) p.problem.clear();
        else if (plan.problem.empty()) plan.problem = g.title + ": " + p.problem;
        plan.groups.push_back(std::move(p));
    }
    for (int v = 0; v < s.volumes; ++v) plan.freeBlocks.push_back(freeBlocks(img, s, v));
    plan.ok = plan.problem.empty();
    return img;
}

}  /* namespace */

std::optional<Media> mediaOf(const std::vector<uint8_t> &image)
{
    const auto specs = detectVolumes(image);
    if (image.size() == kSideSize)
        return specs.size() == 1 ? std::optional<Media>(Media::ss) : std::nullopt;
    if (image.size() != kDoubleSize) return std::nullopt;
    for (const auto &v : specs) if (v.vol == Vol::dv) return Media::dv;
    for (const auto &v : specs) if (v.vol == Vol::floppy && v.side == 0) return Media::dz;
    return std::nullopt;
}

ComposePlan planDisk(const ComposeRecipe &recipe)
{
    ComposePlan plan;
    (void)compose(recipe, plan);
    return plan;
}

std::vector<uint8_t> composeDisk(const ComposeRecipe &recipe)
{
    ComposePlan plan;
    auto img = compose(recipe, plan);
    if (!plan.ok) throw std::runtime_error(plan.problem);
    return img;
}

} /* namespace ms0515::disk */
