/*
 * FolderVolume.cpp — host folder as an RT-11 block device: generated
 * home/directory, host-file-backed data blocks, and guest directory-edit
 * reparse (created entries materialize host files, drops become
 * `deleted`, renames follow the start block).
 */

#include "ms0515/disk/FolderVolume.hpp"

#include "ms0515/disk/Directory.hpp"
#include "ms0515/disk/Layout.hpp"

#include "Internal.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <span>

namespace fs = std::filesystem;

namespace ms0515::disk {

using namespace internal;

namespace {

constexpr int kSegments = 4;                      /* directory segments    */
constexpr std::size_t kEntrySize = 14;            /* no extra bytes        */
/* Entries per 2-block segment: header 10 B, EOS marker needs 2 B.  Keep
 * one slot spare so the guest can always add an entry to a segment. */
constexpr std::size_t kMaxPerSegment =
    (2 * static_cast<std::size_t>(kBlock) - 10 - 2) / kEntrySize - 1;

std::optional<uint64_t> hostSize(const fs::path &p)
{
    std::error_code ec;
    const auto sz = fs::file_size(p, ec);
    if (ec) return std::nullopt;
    return sz;
}

}  /* namespace */

std::unique_ptr<FolderVolume>
FolderVolume::open(const std::string &descriptorPath, std::string *error)
{
    std::ifstream f(descriptorPath, std::ios::binary);
    if (!f) {
        if (error) *error = "cannot read descriptor " + descriptorPath;
        return nullptr;
    }
    std::string text(std::istreambuf_iterator<char>(f), {});

    auto desc = parseRtfs(text, error);
    if (!desc) return nullptr;

    auto vol = std::unique_ptr<FolderVolume>(new FolderVolume);
    vol->descriptorPath_ = descriptorPath;
    vol->folder_ = fs::path(descriptorPath).parent_path().string();
    vol->descriptorName_ = fs::path(descriptorPath).filename().string();
    vol->desc_ = std::move(*desc);
    vol->noteDescriptorStamp();
    vol->rescan();
    return vol;
}

std::unique_ptr<FolderVolume>
FolderVolume::openInMemory(const std::string &folderPath, RtfsDescriptor desc)
{
    std::error_code ec;
    if (!fs::is_directory(folderPath, ec))
        return nullptr;
    const bool floppy = desc.device == RtfsDescriptor::Device::Floppy;
    if (desc.blocks <= rtfsDataStart(kSegments) || desc.blocks > kRtfsMaxBlocks ||
        (floppy && desc.blocks != kRtfsFloppyBlocks))
        return nullptr;

    auto vol = std::unique_ptr<FolderVolume>(new FolderVolume);
    vol->folder_ = folderPath;
    vol->listedOnly_ = !desc.files.empty();
    vol->desc_ = std::move(desc);
    vol->rescan();
    return vol;
}

FolderVolume::~FolderVolume()
{
    if (!descriptorPath_.empty())
        return;
    std::error_code ec;
    for (const auto &[host, slot] : slots_)
        if (slot.tentative) fs::remove(hostPath(host), ec);
}

bool FolderVolume::admit(const std::string &rt11Name)
{
    for (const auto &f : desc_.files)
        if (!f.deleted && f.rt11Name == rt11Name) return false;

    std::error_code ec;
    for (const auto &de : fs::directory_iterator(folder_, ec)) {
        if (!de.is_regular_file(ec)) continue;
        const std::string host = de.path().filename().string();
        if (host == descriptorName_ || host == desc_.bootHost) continue;
        if (mangleRt11Name(host) != rt11Name) continue;
        bool listed = false;
        for (const auto &f : desc_.files)
            if (f.hostName == host) { listed = true; break; }
        if (listed) continue;

        RtfsFile nf;
        nf.rt11Name = rt11Name;
        nf.hostName = host;
        desc_.files.push_back(std::move(nf));
        saveDescriptor();
        rescan();
        if (slots_[host].start >= 0) return true;
        desc_.files.pop_back();                 /* no room for it */
        rescan();
        return false;
    }
    return false;
}

void FolderVolume::noteDescriptorStamp()
{
    std::error_code ec;
    descStamp_ = fs::last_write_time(descriptorPath_, ec);
    descSize_  = fs::file_size(descriptorPath_, ec);
}

void FolderVolume::maybeReloadDescriptor()
{
    if (descriptorPath_.empty())
        return;                     /* in memory: nothing outside to reload */
    std::error_code ec;
    const auto stamp = fs::last_write_time(descriptorPath_, ec);
    const auto size  = fs::file_size(descriptorPath_, ec);
    if (ec || (stamp == descStamp_ && size == descSize_))
        return;
    noteDescriptorStamp();          /* don't re-parse a broken file forever */

    std::ifstream f(descriptorPath_, std::ios::binary);
    if (!f) return;
    std::string text(std::istreambuf_iterator<char>(f), {});
    auto d = parseRtfs(text);
    if (!d) return;                              /* malformed: keep state  */
    if (d->device != desc_.device || d->blocks != desc_.blocks)
        return;          /* geometry is fixed at mount time; remount first */
    desc_ = std::move(*d);
}

std::string FolderVolume::hostPath(const std::string &name) const
{
    return (fs::path(folder_) / name).string();
}

/*
 * rescan — reconcile the descriptor with the folder: auto-fill an empty
 * descriptor, drop entries whose host file is gone (a renamed host simply
 * re-enters as a new file), append host files that are not yet listed,
 * then derive the extent table from current host sizes.
 */
void FolderVolume::rescan()
{
    maybeReloadDescriptor();        /* pick up manual .rtfs edits first */

    std::vector<RtfsHostFile> listing;
    std::error_code ec;
    for (const auto &de : fs::directory_iterator(folder_, ec)) {
        if (!de.is_regular_file(ec)) continue;
        const std::string name = de.path().filename().string();
        if (name == descriptorName_ || name == desc_.bootHost) continue;
        if (de.path().extension() == kRtfsExtension) continue;
        if (listedOnly_) {          /* the folder's other files stay out */
            bool listed = false;
            for (const auto &df : desc_.files)
                if (df.hostName == name) { listed = true; break; }
            if (!listed) continue;
        }
        listing.push_back({name, de.file_size(ec), 0});
    }
    auto inFolder = [&](const std::string &host) {
        for (const auto &hf : listing)
            if (hf.name == host) return true;
        return false;
    };

    const bool wasEmpty = desc_.files.empty();
    if (wasEmpty) {
        autoFillRtfs(desc_, listing);
        if (!desc_.files.empty()) saveDescriptor();
    } else {
        bool changed = false;

        /* Drop entries whose host file vanished (deleted lines too — they
         * reference nothing anymore). */
        std::vector<RtfsFile> kept;
        for (auto &df : desc_.files) {
            if (inFolder(df.hostName)) kept.push_back(std::move(df));
            else changed = true;
        }
        desc_.files = std::move(kept);

        /* Append files the descriptor doesn't know yet. */
        std::vector<std::string> taken;
        for (const auto &df : desc_.files) taken.push_back(df.rt11Name);
        for (const auto &hf : listing) {
            bool known = false;
            for (const auto &df : desc_.files)
                if (df.hostName == hf.name) { known = true; break; }
            if (known) continue;
            RtfsFile nf;
            nf.rt11Name = mangleRt11Name(hf.name, taken);
            nf.hostName = hf.name;
            taken.push_back(nf.rt11Name);
            desc_.files.push_back(std::move(nf));
            changed = true;
        }
        if (changed) saveDescriptor();
    }

    layOut();
    generateDirectory();
}

void FolderVolume::saveDescriptor()
{
    if (descriptorPath_.empty())
        return;                     /* in memory: the descriptor has no file */
    {
        std::ofstream f(descriptorPath_, std::ios::binary | std::ios::trunc);
        const std::string text = serializeRtfs(desc_);
        f.write(text.data(), static_cast<std::streamsize>(text.size()));
    }
    noteDescriptorStamp();          /* our own writes must not self-trigger */
}

/*
 * layOut — give every live file its place on the volume and derive the
 * extent table.  A file keeps the start block it has: the guest holds the
 * directory in its memory between reads, and a file that moved under it
 * would be written to where it no longer is.  So space freed in the
 * middle stays a hole (an empty entry, as on RT-11), and only a file
 * with no place yet - at open, or new in the folder - is put into the
 * first hole that takes it.  A closed file is as long as its host file;
 * a tentative one holds the space the guest's entry says, whatever has
 * been written of it.  A file that outgrew its place into the next one
 * (a host edit) is placed anew.
 */
void FolderVolume::layOut()
{
    for (auto it = slots_.begin(); it != slots_.end();) {
        bool live = false;
        for (const auto &f : desc_.files)
            if (!f.deleted && f.hostName == it->first) { live = true; break; }
        it = live ? std::next(it) : slots_.erase(it);
    }

    std::vector<Slot *> placed;
    for (const auto &f : desc_.files) {
        if (f.deleted) continue;
        Slot &s = slots_[f.hostName];
        if (!s.tentative) {
            const uint64_t size = hostSize(hostPath(f.hostName)).value_or(0);
            s.blocks = static_cast<int>(
                (size + kBlock - 1) / static_cast<uint64_t>(kBlock));
        }
        if (s.start >= 0) placed.push_back(&s);
    }
    const auto byStart = [](const Slot *a, const Slot *b) {
        return a->start < b->start;
    };
    std::stable_sort(placed.begin(), placed.end(), byStart);
    for (std::size_t i = 0; i < placed.size(); ++i) {
        Slot &s = *placed[i];
        const int limit = i + 1 < placed.size() ? placed[i + 1]->start
                                                : desc_.blocks;
        if (s.start < rtfsDataStart(kSegments) || s.start + s.blocks > limit)
            s.start = -1;
    }
    std::erase_if(placed, [](const Slot *s) { return s->start < 0; });

    for (const auto &f : desc_.files) {
        if (f.deleted) continue;
        Slot &s = slots_[f.hostName];
        if (s.start >= 0) continue;
        int cur = rtfsDataStart(kSegments);
        for (const Slot *other : placed) {
            if (other->start - cur >= s.blocks) break;
            cur = std::max(cur, other->start + other->blocks);
        }
        if (cur + s.blocks > desc_.blocks) continue;        /* doesn't fit */
        s.start = cur;
        placed.insert(std::upper_bound(placed.begin(), placed.end(), &s, byStart),
                      &s);
    }

    extents_.clear();
    for (std::size_t i = 0; i < desc_.files.size(); ++i) {
        if (desc_.files[i].deleted) continue;
        const Slot &s = slots_[desc_.files[i].hostName];
        if (s.start >= 0) extents_.push_back({i, s.start, s.blocks});
    }
    std::stable_sort(extents_.begin(), extents_.end(),
                     [](const Extent &a, const Extent &b) { return a.start < b.start; });
}

/*
 * generateDirectory — render the extents as RT-11 directory segments
 * (chained, kSegments reserved): an entry per file - permanent, or
 * tentative with the job and channel the guest gave it - an empty entry
 * for every hole between files and for the free tail, then the
 * end-of-segment marker.
 */
void FolderVolume::generateDirectory()
{
    dirImage_.assign(static_cast<std::size_t>(kSegments) * 2 * kBlock, 0);

    std::size_t seg = 0, p = 10, inSeg = 0;
    int cur = rtfsDataStart(kSegments);
    auto segBase = [&](std::size_t s) { return s * 2 * kBlock; };

    auto openSegment = [&](std::size_t s, int firstBlock) {
        uint8_t *h = dirImage_.data() + segBase(s);
        putw(h + 0, kSegments);                        /* segments total   */
        putw(h + 2, 0);                                /* next: none (yet) */
        putw(h + 8, static_cast<uint16_t>(firstBlock));/* first data block */
        p = 10;
    };
    openSegment(0, cur);

    /* One entry; false when the directory has no room for it. */
    auto emit = [&](uint16_t status, uint16_t n1, uint16_t n2, uint16_t ext,
                    int length, uint16_t jobChannel, uint16_t date) {
        if (inSeg >= kMaxPerSegment) {
            if (seg + 1 >= kSegments) return false;
            putw(dirImage_.data() + segBase(seg) + 2,
                 static_cast<uint16_t>(seg + 2));      /* link (1-based)   */
            putw(dirImage_.data() + segBase(seg) + p, kStatusEndOfSeg);
            ++seg;
            openSegment(seg, cur);
            inSeg = 0;
        }
        uint8_t *s = dirImage_.data() + segBase(seg);
        putEntry(s, p, status, n1, n2, ext, static_cast<uint16_t>(length));
        putw(s + p + 10, jobChannel);
        putw(s + p + 12, date);
        p += kEntrySize;
        ++inSeg;
        cur += length;
        return true;
    };
    auto emitEmpty = [&](int length) {
        return emit(kStatusEmpty, 0x00D5, 0x6739, 0x26F4, length, 0, 0);
    };

    for (const auto &e : extents_) {
        if (e.start > cur && !emitEmpty(e.start - cur)) break;
        const auto &f = desc_.files[e.fileIndex];
        const Slot &slot = slots_[f.hostName];
        char nm[6], ex[3];
        splitName(f.rt11Name, nm, ex);
        const uint16_t status = slot.tentative
            ? kStatusTentative
            : static_cast<uint16_t>(kStatusPermanent |
                                    (f.isProtected ? kStatusProtected : 0));
        if (!emit(status, encodeRad50(nm), encodeRad50(nm + 3), encodeRad50(ex),
                  e.blocks, slot.jobChannel, f.date))
            break;
    }

    /* The free tail (one slot is kept spare for it) + end-of-segment. */
    uint8_t *s = dirImage_.data() + segBase(seg);
    putEntry(s, p, kStatusEmpty, 0x00D5, 0x6739, 0x26F4,
             static_cast<uint16_t>(desc_.blocks - cur));
    putw(s + p + kEntrySize, kStatusEndOfSeg);
    putw(dirImage_.data() + 4, static_cast<uint16_t>(seg + 1)); /* highest */
}

const FolderVolume::Extent *FolderVolume::extentAt(int lbn) const
{
    for (const auto &e : extents_)
        if (lbn >= e.start && lbn < e.start + e.blocks) return &e;
    return nullptr;
}

/* Boot blocks live in the (RT-11-invisible) boot host file: its block 0 is
 * LBN 0, blocks 1..4 are LBN 2..5 (LBN 1 is the generated home block). */
static int bootFileBlock(int lbn)
{
    if (lbn == 0) return 0;
    if (lbn >= 2 && lbn <= 5) return lbn - 1;
    return -1;
}

void FolderVolume::readBlock(int lbn, uint8_t *out)
{
    std::memset(out, 0, kBlock);
    if (lbn < 0 || lbn >= desc_.blocks) return;

    if (const int bb = bootFileBlock(lbn); bb >= 0 && !desc_.bootHost.empty()) {
        std::ifstream f(hostPath(desc_.bootHost), std::ios::binary);
        if (!f) return;
        f.seekg(static_cast<std::streamoff>(bb) * kBlock);
        f.read(reinterpret_cast<char *>(out), kBlock);
        return;
    }
    if (lbn == 1) {
        const auto home = makeHomeBlock(desc_.volumeId, desc_.owner);
        std::memcpy(out, home.data(), kBlock);
        return;
    }
    const int dirEnd = kDirLbn + 2 * kSegments;
    if (lbn >= kDirLbn && lbn < dirEnd) {
        rescan();                  /* directory reads see external changes */
        std::memcpy(out,
                    dirImage_.data() +
                        static_cast<std::size_t>(lbn - kDirLbn) * kBlock,
                    kBlock);
        return;
    }
    if (const Extent *e = extentAt(lbn)) {
        std::ifstream f(hostPath(desc_.files[e->fileIndex].hostName),
                        std::ios::binary);
        if (!f) return;
        f.seekg(static_cast<std::streamoff>(lbn - e->start) * kBlock);
        f.read(reinterpret_cast<char *>(out), kBlock);  /* short read = 0s */
        return;
    }
    if (auto it = scratch_.find(lbn); it != scratch_.end())
        std::memcpy(out, it->second.data(), kBlock);
}

void FolderVolume::writeBlock(int lbn, const uint8_t *in)
{
    writeRange(lbn, 1, in);
}

void FolderVolume::writeRange(int lbn, int count, const uint8_t *in)
{
    const int dirEnd = kDirLbn + 2 * kSegments;
    bool touchedDir = false;

    for (int i = 0; i < count; ++i, in += kBlock) {
        const int b = lbn + i;
        if (b < 0 || b >= desc_.blocks) continue;

        if (b == 1) {
            /* Guest INIT writes a fresh home block: adopt its volume id
             * and owner into the descriptor (offsets per makeHomeBlock). */
            auto field = [&](int off) {
                std::string s(reinterpret_cast<const char *>(in) + off, 12);
                while (!s.empty() && (s.back() == ' ' || s.back() == '\0'))
                    s.pop_back();
                return s;
            };
            const std::string vid = field(0x1D8), own = field(0x1E4);
            if (vid != desc_.volumeId || own != desc_.owner) {
                desc_.volumeId = vid;
                desc_.owner    = own;
                saveDescriptor();
            }
            continue;
        }
        if (const int bb = bootFileBlock(b); bb >= 0) {
            /* Guest COPY/BOOT: materialize/extend the hidden boot file. */
            if (desc_.bootHost.empty()) {
                desc_.bootHost = "boot.bin";
                saveDescriptor();
            }
            const std::string path = hostPath(desc_.bootHost);
            std::fstream f(path, std::ios::binary | std::ios::in | std::ios::out);
            if (!f) {
                std::ofstream(path, std::ios::binary).close();
                f.open(path, std::ios::binary | std::ios::in | std::ios::out);
            }
            if (!f) continue;
            f.seekp(0, std::ios::end);
            for (auto have = static_cast<std::streamoff>(f.tellp());
                 have < static_cast<std::streamoff>(bb) * kBlock; have += kBlock) {
                const std::vector<char> zero(kBlock, 0);
                f.write(zero.data(), kBlock);
            }
            f.seekp(static_cast<std::streamoff>(bb) * kBlock);
            f.write(reinterpret_cast<const char *>(in), kBlock);
            continue;
        }
        if (b >= kDirLbn && b < dirEnd) {
            std::memcpy(dirImage_.data() +
                            static_cast<std::size_t>(b - kDirLbn) * kBlock,
                        in, kBlock);
            /* Diff only once the SECOND half of a segment pair lands: a
             * floppy writes a segment as two single-sector transfers, and
             * judging the half-written first block would corrupt the
             * descriptor.  (An HD writes the whole segment in one DMA, so
             * its range always covers the second half too.) */
            if ((b - kDirLbn) % 2 == 1)
                touchedDir = true;
            continue;
        }
        if (const Extent *e = extentAt(b)) {
            const std::string path =
                hostPath(desc_.files[e->fileIndex].hostName);
            std::fstream f(path, std::ios::binary | std::ios::in | std::ios::out);
            if (!f) continue;
            f.seekp(static_cast<std::streamoff>(b - e->start) * kBlock);
            f.write(reinterpret_cast<const char *>(in), kBlock);
            continue;
        }
        scratch_[b].assign(in, in + kBlock);
    }

    /* Diff guest directory edits only after the whole transfer landed, so
     * a segment rewritten by PIP is judged in its final, consistent form. */
    if (touchedDir)
        reparseDirectory();
}

/*
 * materializeHostName — pick a host file name for a guest-created RT-11
 * file: the lowercased RT-11 name, de-conflicted with a numeric tail.
 */
std::string FolderVolume::materializeHostName(const std::string &rt11) const
{
    std::string base = rt11;
    for (auto &c : base)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    std::string name = base;
    /* A host file of that name is in the way - unless the volume lists
     * its files and this one is not among them: then the guest never saw
     * it, and writes over it as it would over its own earlier output. */
    const auto taken = [&](const std::string &host) {
        if (!fs::exists(hostPath(host))) return false;
        if (!listedOnly_) return true;
        for (const auto &df : desc_.files)
            if (df.hostName == host) return true;
        return false;
    };
    for (int n = 2; taken(name); ++n) {
        const auto dot = base.rfind('.');
        name = (dot == std::string::npos)
             ? base + "-" + std::to_string(n)
             : base.substr(0, dot) + "-" + std::to_string(n) + base.substr(dot);
    }
    return name;
}

/*
 * reparseDirectory — read the guest-edited segments back, diff against
 * the descriptor, and make the folder match.  Every entry of the guest's
 * - permanent or tentative - is a file, at the start block the guest gave
 * it:
 *
 *   - an entry of a file we know (the same name at the same place, else
 *     the same name) updates it; one that turned permanent is a .CLOSE,
 *     and its host file becomes exactly the length closed at;
 *   - an entry at a known file's place under another name is a rename;
 *   - any other is a new file: a tentative one (.ENTER) gets a host file
 *     holding what the guest has staged so far, to be written on; a
 *     permanent one gets its whole length.
 *
 * Files the guest's directory no longer has are gone (dropGoneFiles).
 * Ends with a rescan, which rebuilds the directory image - now the same
 * layout the guest wrote.
 */
void FolderVolume::reparseDirectory()
{
    std::vector<GuestEntry> entries;
    if (!readGuestEntries(entries))
        return;                    /* mid-edit garbage: wait for more writes */

    std::vector<bool> seen(desc_.files.size(), false);
    auto live = [&](std::size_t fi) {
        return !desc_.files[fi].deleted && !seen[fi];
    };
    auto startOf = [&](std::size_t fi) {
        return slots_[desc_.files[fi].hostName].start;
    };
    auto adopt = [&](std::size_t fi, GuestEntry &e) {
        seen[fi] = true;
        e.taken = true;
        adoptGuestEntry(desc_.files[fi], e);
    };

    for (auto &e : entries)                     /* the name at its place */
        for (std::size_t fi = 0; fi < seen.size() && !e.taken; ++fi)
            if (live(fi) && desc_.files[fi].rt11Name == e.name &&
                startOf(fi) == e.start)
                adopt(fi, e);
    for (auto &e : entries)                     /* the name, moved */
        for (std::size_t fi = 0; fi < seen.size() && !e.taken; ++fi)
            if (live(fi) && desc_.files[fi].rt11Name == e.name)
                adopt(fi, e);
    for (auto &e : entries)                     /* the place, renamed */
        for (std::size_t fi = 0; fi < seen.size() && !e.taken; ++fi)
            if (live(fi) && startOf(fi) == e.start) {
                desc_.files[fi].rt11Name = e.name;
                adopt(fi, e);
            }

    std::vector<RtfsFile> created;
    for (const auto &e : entries)
        if (!e.taken) created.push_back(createGuestFile(e));

    dropGoneFiles(seen, created);
    for (auto &nf : created) desc_.files.push_back(std::move(nf));

    saveDescriptor();
    rescan();
}

/* The guest's directory as written: its permanent and tentative entries.
 * False when a segment does not parse. */
bool FolderVolume::readGuestEntries(std::vector<GuestEntry> &entries) const
{
    std::size_t seg = 0;
    for (int guard = 0; guard < kSegments; ++guard) {
        std::span<const uint8_t> buf(dirImage_.data() + seg * 2 * kBlock,
                                     2 * static_cast<std::size_t>(kBlock));
        auto d = parseSegment(buf);
        if (!d) return false;
        for (const auto &e : d->entries)
            if (e.isPermanent() || (e.status & kStatusTentative))
                entries.push_back({e.name, e.startBlock, e.length, e.status,
                                   e.date, e.jobChannel, false});
        const uint16_t next = getw(dirImage_.data() + seg * 2 * kBlock + 2);
        if (next == 0 || next > kSegments) break;
        seg = next - 1;
    }
    return true;
}

/* The guest's entry `e` is the known file `f`: its flags, date and place;
 * a permanent entry fixes the host file's length - closed at it, or
 * shrunk to it (PIP .CLOSE). */
void FolderVolume::adoptGuestEntry(RtfsFile &f, const GuestEntry &e)
{
    Slot &s = slots_[f.hostName];
    const bool permanent = (e.status & kStatusPermanent) != 0;
    f.isProtected = (e.status & kStatusProtected) != 0;
    f.date = e.date;
    if (permanent) {
        const uint64_t want = static_cast<uint64_t>(e.length) * kBlock;
        const uint64_t have = hostSize(hostPath(f.hostName)).value_or(0);
        if (s.tentative ? have != want : have > want) {
            std::error_code ec;
            fs::resize_file(hostPath(f.hostName), want, ec);
        }
    }
    s.start      = e.start;
    s.blocks     = e.length;
    s.tentative  = !permanent;
    s.jobChannel = permanent ? uint16_t{0} : e.jobChannel;
}

/* A file of the guest's we do not know: its host file, made from the
 * scratch blocks the guest staged.  A file being written (.ENTER) holds
 * what has been written so far; a finished one its whole length, zeros
 * where nothing was staged. */
RtfsFile FolderVolume::createGuestFile(const GuestEntry &e)
{
    const bool permanent = (e.status & kStatusPermanent) != 0;
    RtfsFile nf;
    nf.rt11Name    = e.name;
    nf.hostName    = materializeHostName(e.name);
    nf.date        = e.date;
    nf.isProtected = (e.status & kStatusProtected) != 0;

    int blocks = permanent ? e.length : 0;
    if (!permanent)
        for (int b = 0; b < e.length; ++b)
            if (scratch_.count(e.start + b)) blocks = b + 1;
    std::ofstream out(hostPath(nf.hostName), std::ios::binary);
    for (int b = 0; b < blocks; ++b) {
        std::vector<uint8_t> blk(kBlock, 0);
        if (auto it = scratch_.find(e.start + b); it != scratch_.end()) {
            blk = it->second;
            scratch_.erase(it);
        }
        out.write(reinterpret_cast<const char *>(blk.data()), kBlock);
    }
    slots_[nf.hostName] = {e.start, e.length, !permanent,
                           permanent ? uint16_t{0} : e.jobChannel};
    return nf;
}

/*
 * dropGoneFiles — the files the guest's directory no longer has (not
 * `seen`).  With a descriptor file they are marked `deleted` there and
 * their host files kept.  With the descriptor in memory nothing would
 * remember them: the host file is removed, and when the file was written
 * over - a closed file of the same name is there, in the descriptor or
 * among the `created` - that one takes the host name the old one had.
 */
void FolderVolume::dropGoneFiles(const std::vector<bool> &seen,
                                 std::vector<RtfsFile> &created)
{
    std::vector<RtfsFile> kept;
    std::vector<RtfsFile> gone;
    for (std::size_t fi = 0; fi < desc_.files.size(); ++fi) {
        RtfsFile &f = desc_.files[fi];
        if (f.deleted || seen[fi]) {
            kept.push_back(std::move(f));
        } else if (!descriptorPath_.empty()) {
            f.deleted = true;
            kept.push_back(std::move(f));
        } else {
            gone.push_back(std::move(f));
        }
    }
    desc_.files = std::move(kept);

    for (const auto &old : gone) {
        std::error_code ec;
        fs::remove(hostPath(old.hostName), ec);
        auto takeOver = [&](RtfsFile &f) {
            if (f.deleted || f.rt11Name != old.rt11Name ||
                f.hostName == old.hostName || slots_[f.hostName].tentative)
                return false;
            fs::rename(hostPath(f.hostName), hostPath(old.hostName), ec);
            if (ec) return false;
            slots_[old.hostName] = slots_[f.hostName];
            slots_.erase(f.hostName);
            f.hostName = old.hostName;
            return true;
        };
        bool taken = false;
        for (auto &f : desc_.files)
            if (!taken) taken = takeOver(f);
        for (auto &f : created)
            if (!taken) taken = takeOver(f);
        if (!taken) slots_.erase(old.hostName);
    }
}

} /* namespace ms0515::disk */
