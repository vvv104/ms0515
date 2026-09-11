/*
 * Compose.cpp - a whole bootable diskette made from scratch: SWAP and the
 * monitor from the exemplar, the groups, the startup file, the bootstrap,
 * the protected blocks.  The plan is the build: every group is put for real
 * on a scratch copy, so the answer to "does it fit" is RT-11's own - blocks,
 * directory entries, the first empty area that takes it.
 */

#include "ms0515/disk/Compose.hpp"

#include "ms0515/disk/Build.hpp"
#include "ms0515/disk/Image.hpp"

#include <algorithm>
#include <numeric>
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

/* A formatted blank holding what only the exemplar has: SWAP.SYS and the
 * monitor, with their dates and protection. */
std::vector<uint8_t> base(const Source &src, const Shape &s)
{
    auto img = blankImage(s.ds);
    initVolume(img, 0, s.ds, {}, s.boot);
    if (s.volumes == 2) initVolume(img, 1, s.ds);
    const std::string names[] = {"SWAP.SYS", src.monitor + ".SYS"};
    for (const auto &name : names) {
        const auto *e = src.volume.directory.find(name);
        if (!e) throw std::runtime_error("the system image has no " + name);
        putFile(img, 0, s.ds, name, src.volume.readFile(name), PutOptions{e->date, (e->status & kStatusProtected) != 0}, s.boot);
    }
    return img;
}

/* The startup file, the bootstrap for the media and the protected blocks:
 * what makes the volume a system once the groups are on it. */
void finish(std::vector<uint8_t> &img, const ComposeRecipe &r, const Source &src, const Shape &s)
{
    const auto *e = src.volume.directory.find(src.startup);
    const PutOptions o{e ? e->date : uint16_t{0}, false};
    if (r.startup) putFile(img, 0, s.ds, src.startup, startupBytes(*r.startup), o, s.boot);
    else if (e) putFile(img, 0, s.ds, src.startup, src.volume.readFile(src.startup), o, s.boot);

    const char *handler = s.boot == Vol::dv ? "DV.SYS" : "DZ.SYS";
    if (!openAt(img, s, 0)->directory.find(handler))
        throw std::runtime_error(std::string("the disk cannot boot: ") + handler + " is not among its files");
    writeBoot(img, 0, s.ds, src.monitor, s.boot);

    const bool srcDs = r.system.size() == kDoubleSize;
    for (const auto &b : r.reserved) {
        const auto vol = openAt(img, s, b.side);
        if (vol && vol->hasDirectory)
            for (const auto &f : vol->directory.permanentFiles())
                if (b.lbn >= f.startBlock && b.lbn < f.startBlock + f.length)
                    throw std::runtime_error(f.name + " would overwrite a reserved block of the system (its copy protection)");
        std::memcpy(img.data() + lbnToByte(b.lbn, b.side, s.ds, Vol::floppy),
                    r.system.data() + lbnToByte(b.lbn, b.side, srcDs, Vol::floppy), kBlock);
    }
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
    for (const auto &b : r.reserved) {
        if (b.side != volume) continue;
        for (const auto &f : openAt(scratch, s, volume)->directory.permanentFiles())
            if (b.lbn >= f.startBlock && b.lbn < f.startBlock + f.length)
                return "would overwrite a reserved block of the system (its copy protection)";
    }
    img = std::move(scratch);
    return "";
}

int groupBlocks(const ComposeGroup &g)
{
    int n = 0;
    for (const auto &f : g.files) n += static_cast<int>((f.data.size() + kBlock - 1) / kBlock);
    return n;
}

/* Groups put on a copy of the image in one order: the placements in the
 * recipe's order, and the first thing that did not fit. */
struct Placing {
    std::vector<uint8_t>        img;
    std::vector<GroupPlacement> groups;
    std::string                 problem;
};

Placing place(const std::vector<uint8_t> &base, const ComposeRecipe &r, const Shape &s,
              const std::vector<std::size_t> &order)
{
    Placing out{base, std::vector<GroupPlacement>(r.groups.size()), ""};
    for (const auto i : order) {
        const auto &g = r.groups[i];
        GroupPlacement p{g.title, -1, groupBlocks(g), ""};
        const int last = g.place == Place::any ? s.volumes - 1 : 0;
        for (int v = 0; v <= last && p.volume < 0; ++v) {
            const std::string why = tryGroup(out.img, r, s, g, v);
            if (why.empty()) p.volume = v; else p.problem = why;
        }
        if (p.volume >= 0) p.problem.clear();
        else if (out.problem.empty()) out.problem = g.title + ": " + p.problem;
        out.groups[i] = std::move(p);
    }
    return out;
}

/* The composition and its plan together: planDisk keeps the plan,
 * composeDisk the image. */
std::vector<uint8_t> compose(const ComposeRecipe &r, ComposePlan &plan)
{
    const Shape s = shapeOf(r.media);
    std::vector<uint8_t> img;
    std::optional<Source> src;
    try {
        src = sourceOf(r.system);
        img = base(*src, s);
        label(img, s, 0, r.volumeId, r.owner);
        if (s.volumes == 2) label(img, s, 1, r.secondVolumeId, r.secondOwner);
    } catch (const std::exception &e) {
        plan.problem = e.what();
        for (const auto &g : r.groups) plan.groups.push_back({g.title, -1, groupBlocks(g), plan.problem});
        return {};
    }

    std::vector<std::size_t> order(r.groups.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    Placing placed = place(img, r, s, order);
    /* Short of room while what may go anywhere took some of the boot
     * volume: what must boot first, the rest after, on either volume. */
    const bool movable = s.volumes == 2 &&
        std::any_of(r.groups.begin(), r.groups.end(), [](const ComposeGroup &g) { return g.place == Place::any; });
    if (!placed.problem.empty() && movable) {
        std::stable_partition(order.begin(), order.end(), [&](std::size_t i) { return r.groups[i].place == Place::boot; });
        Placing again = place(img, r, s, order);
        if (again.problem.empty()) placed = std::move(again);
    }
    img = std::move(placed.img);
    plan.groups = std::move(placed.groups);
    plan.problem = placed.problem;
    if (plan.problem.empty()) {
        try {
            finish(img, r, *src, s);
        } catch (const std::exception &e) {
            plan.problem = e.what();
        }
    }
    for (int v = 0; v < s.volumes; ++v) plan.freeBlocks.push_back(freeBlocks(img, s, v));
    plan.ok = plan.problem.empty();
    return img;
}

}  /* namespace */

std::optional<Media> mediaOf(const std::vector<uint8_t> &image)
{
    const auto specs = detectVolumes(image);
    if (image.size() == kSideSize) {
        /* A side has one reading: a directory there, pointed at or not
         * (older tools left the home block's pointer empty). */
        const auto side = openImage(image, 0);
        return specs.size() == 1 || (side && side->hasDirectory) ? std::optional<Media>(Media::ss) : std::nullopt;
    }
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
