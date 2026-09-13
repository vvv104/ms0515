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

/* The machine's text is KOI-8R (RFC 1489): the console types it, the
 * monitor reads its command files in it.  The manifest and the wizard are
 * UTF-8, so what they put on the disk is converted here: ASCII as it is,
 * the Cyrillic letters and Ё to their KOI-8R bytes, anything else '?'. */
std::vector<uint8_t> koi8(const std::string &utf8)
{
    /* KOI-8R 0xC0..0xFF: the lowercase letters, then the uppercase, in
     * the set's own order (ю а б ц д е ф г х и й к л м н о п я р с т у ж в
     * ь ы з ш э щ ч ъ). */
    static constexpr char32_t kLower[32] = {
        0x44E, 0x430, 0x431, 0x446, 0x434, 0x435, 0x444, 0x433, 0x445, 0x438, 0x439, 0x43A, 0x43B, 0x43C, 0x43D, 0x43E,
        0x43F, 0x44F, 0x440, 0x441, 0x442, 0x443, 0x436, 0x432, 0x44C, 0x44B, 0x437, 0x448, 0x44D, 0x449, 0x447, 0x44A};
    auto byteOf = [](char32_t cp) -> uint8_t {
        if (cp < 0x80) return static_cast<uint8_t>(cp);
        if (cp == 0x451) return 0xA3;                       /* ё */
        if (cp == 0x401) return 0xB3;                       /* Ё */
        for (int i = 0; i < 32; ++i) {
            if (cp == kLower[i]) return static_cast<uint8_t>(0xC0 + i);
            if (cp == kLower[i] - 0x20) return static_cast<uint8_t>(0xE0 + i);
        }
        return '?';
    };
    std::vector<uint8_t> out;
    for (std::size_t i = 0; i < utf8.size();) {
        const auto b = static_cast<uint8_t>(utf8[i]);
        int more = b < 0x80 ? 0 : b < 0xE0 ? 1 : b < 0xF0 ? 2 : 3;
        char32_t cp = more == 0 ? b : more == 1 ? (b & 0x1F) : more == 2 ? (b & 0x0F) : (b & 0x07);
        ++i;
        for (; more > 0 && i < utf8.size(); --more, ++i) cp = (cp << 6) | (static_cast<uint8_t>(utf8[i]) & 0x3F);
        out.push_back(byteOf(cp));
    }
    return out;
}

std::vector<uint8_t> startupBytes(const std::vector<std::string> &lines)
{
    std::vector<uint8_t> out;
    for (const auto &l : lines) {
        const auto bytes = koi8(l);
        out.insert(out.end(), bytes.begin(), bytes.end());
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
    auto machine = [](const std::string &utf8) { const auto b = koi8(utf8); return std::string(b.begin(), b.end()); };
    setVolumeId(img, volume, s.ds, id ? machine(*id) : trim(field(0x1D8)), owner ? machine(*owner) : trim(field(0x1E4)),
                volumeKind(s, volume));
}

/* A physical sector of a diskette: the FDC's track, side and 0-based
 * sector, from its byte offset in a raw image. */
struct Sector {
    int track, side, sector;
};

Sector sectorOf(std::size_t byte, bool ds)
{
    const std::size_t cyl = ds ? 2 * kTrackSize : kTrackSize;
    const std::size_t rem = byte % cyl;
    return {static_cast<int>(byte / cyl), static_cast<int>(rem / kTrackSize),
            static_cast<int>(rem % kTrackSize / kBlock)};
}

/* Where a reserved block goes on the media being made: the byte of the
 * same physical sector, and the volume and block the file system there
 * knows it as. */
struct Spot {
    std::size_t byte;
    int         volume;
    int         lbn;
};

Spot spotOf(const ReservedBlock &b, bool srcDs, const Shape &s)
{
    const Sector ph = sectorOf(lbnToByte(b.lbn, b.side, srcDs, Vol::floppy), srcDs);
    if (ph.side && !s.ds)
        throw std::runtime_error("a protected block of the system lies on the second side, which a single-sided disk has not");
    const std::size_t byte = static_cast<std::size_t>(ph.track) * (s.ds ? 2 * kTrackSize : kTrackSize)
                           + static_cast<std::size_t>(ph.side) * kTrackSize
                           + static_cast<std::size_t>(ph.sector) * kBlock;
    if (s.boot == Vol::dv)
        return {byte, 0, (static_cast<int>(byte / kBlock) - kDvRotate + kDsBlocks) % kDsBlocks};
    return {byte, ph.side, lbnFromPhys(ph.track, ph.sector + 1)};
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
 * monitor, with their dates and protection; the free space of each volume
 * ends before the first block the system protects there. */
std::vector<uint8_t> base(const Source &src, const Shape &s, const ComposeRecipe &r)
{
    auto img = blankImage(s.ds);
    initVolume(img, 0, s.ds, {}, s.boot);
    if (s.volumes == 2) initVolume(img, 1, s.ds);
    const bool srcDs = r.system.size() == kDoubleSize;
    std::vector<std::optional<int>> fence(static_cast<std::size_t>(s.volumes));
    for (const auto &b : r.reserved) {
        const Spot at = spotOf(b, srcDs, s);
        auto &f = fence[static_cast<std::size_t>(at.volume)];
        if (!f || at.lbn < *f) f = at.lbn;
    }
    for (int v = 0; v < s.volumes; ++v)
        if (fence[static_cast<std::size_t>(v)])
            endFreeSpaceAt(img, v, s.ds, *fence[static_cast<std::size_t>(v)], volumeKind(s, v));
    const std::string names[] = {"SWAP.SYS", src.monitor + ".SYS"};
    for (const auto &name : names) {
        const auto *e = src.volume.directory.find(name);
        if (!e) throw std::runtime_error("the system image has no " + name);
        putFile(img, 0, s.ds, name, src.volume.readFile(name), PutOptions{e->date, (e->status & kStatusProtected) != 0}, s.boot);
    }
    return img;
}

/* One file put on the boot volume by the composition itself, and its line
 * in the plan; one that does not fit stops the composition. */
void putOwn(std::vector<uint8_t> &img, const Shape &s, const std::string &name, const std::vector<uint8_t> &data,
            const PutOptions &o, std::vector<GroupPlacement> &files)
{
    GroupPlacement p{name, 0, static_cast<int>((data.size() + kBlock - 1) / kBlock), ""};
    try {
        putFile(img, 0, s.ds, name, data, o, s.boot);
    } catch (const std::exception &e) {
        p.volume = -1;
        p.problem = e.what();
    }
    files.push_back(p);
    if (p.volume < 0) throw std::runtime_error(p.problem);
}

/* The startup file, the bootstrap for the media and the protected blocks:
 * what makes the volume a system once the groups are on it. */
void finish(std::vector<uint8_t> &img, const ComposeRecipe &r, const Source &src, const Shape &s,
            std::vector<GroupPlacement> &files)
{
    const auto *e = src.volume.directory.find(src.startup);
    const PutOptions o{e ? e->date : uint16_t{0}, false};
    std::optional<std::vector<std::string>> startup = r.startup;
    if (r.banner) {
        const std::string type = "TYPE BANNER.TXT";
        if (!startup) startup = std::vector<std::string>{};
        if (std::find(startup->begin(), startup->end(), type) == startup->end()) startup->push_back(type);
    }
    if (startup) putOwn(img, s, src.startup, startupBytes(*startup), o, files);
    else if (e) putOwn(img, s, src.startup, src.volume.readFile(src.startup), o, files);
    if (r.banner) putOwn(img, s, "BANNER.TXT", startupBytes(*r.banner), o, files);

    const char *handler = s.boot == Vol::dv ? "DV.SYS" : "DZ.SYS";
    if (!openAt(img, s, 0)->directory.find(handler))
        throw std::runtime_error(std::string("the disk cannot boot: ") + handler + " is not among its files");
    writeBoot(img, 0, s.ds, src.monitor, s.boot);

    const bool srcDs = r.system.size() == kDoubleSize;
    for (const auto &b : r.reserved) {
        const Spot at = spotOf(b, srcDs, s);
        const auto vol = openAt(img, s, at.volume);
        if (vol && vol->hasDirectory)
            for (const auto &f : vol->directory.permanentFiles())
                if (at.lbn >= f.startBlock && at.lbn < f.startBlock + f.length)
                    throw std::runtime_error(f.name + " would overwrite a reserved block of the system (its copy protection)");
        std::memcpy(img.data() + at.byte, r.system.data() + lbnToByte(b.lbn, b.side, srcDs, Vol::floppy), kBlock);
    }
}

/* Put one group on one volume of a scratch copy; "" when it went, else why
 * it did not. */
std::string tryGroup(std::vector<uint8_t> &img, const Shape &s, const ComposeGroup &g, int volume)
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
            const std::string why = tryGroup(out.img, s, g, v);
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
        img = base(*src, s, r);
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
            finish(img, r, *src, s, plan.files);
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
