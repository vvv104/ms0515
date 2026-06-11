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

void FolderVolume::noteDescriptorStamp()
{
    std::error_code ec;
    descStamp_ = fs::last_write_time(descriptorPath_, ec);
    descSize_  = fs::file_size(descriptorPath_, ec);
}

void FolderVolume::maybeReloadDescriptor()
{
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

    /* Extents from current host sizes. */
    extents_.clear();
    int cur = rtfsDataStart(kSegments);
    for (std::size_t i = 0; i < desc_.files.size(); ++i) {
        if (desc_.files[i].deleted) continue;
        uint64_t size = 0;
        if (auto sz = hostSize(hostPath(desc_.files[i].hostName)))
            size = *sz;
        const int nblk = static_cast<int>(
            (size + kBlock - 1) / static_cast<uint64_t>(kBlock));
        if (cur + nblk > desc_.blocks) continue;       /* doesn't fit */
        extents_.push_back({i, cur, nblk});
        cur += nblk;
    }
    generateDirectory();
}

void FolderVolume::saveDescriptor()
{
    {
        std::ofstream f(descriptorPath_, std::ios::binary | std::ios::trunc);
        const std::string text = serializeRtfs(desc_);
        f.write(text.data(), static_cast<std::streamsize>(text.size()));
    }
    noteDescriptorStamp();          /* our own writes must not self-trigger */
}

/*
 * generateDirectory — render the descriptor's live extents as RT-11
 * directory segments (chained, kSegments reserved), one permanent entry
 * per extent, then a single empty entry covering the free tail, then the
 * end-of-segment marker.
 */
void FolderVolume::generateDirectory()
{
    dirImage_.assign(static_cast<std::size_t>(kSegments) * 2 * kBlock, 0);

    std::size_t seg = 0, p = 10;
    int segFirstBlock = rtfsDataStart(kSegments);
    auto segBase = [&](std::size_t s) { return s * 2 * kBlock; };

    auto openSegment = [&](std::size_t s, int firstBlock) {
        uint8_t *h = dirImage_.data() + segBase(s);
        putw(h + 0, kSegments);                        /* segments total   */
        putw(h + 2, 0);                                /* next: none (yet) */
        putw(h + 8, static_cast<uint16_t>(firstBlock));/* first data block */
        p = 10;
    };
    openSegment(0, segFirstBlock);

    std::size_t inSeg = 0;
    for (const auto &e : extents_) {
        if (inSeg == kMaxPerSegment && seg + 1 < kSegments) {
            putw(dirImage_.data() + segBase(seg) + 2,
                 static_cast<uint16_t>(seg + 2));      /* link (1-based)   */
            putw(dirImage_.data() + segBase(seg) + p, kStatusEndOfSeg);
            ++seg;
            openSegment(seg, e.start);
            inSeg = 0;
        }
        const auto &f = desc_.files[e.fileIndex];
        char nm[6], ex[3];
        splitName(f.rt11Name, nm, ex);
        uint8_t *s = dirImage_.data() + segBase(seg);
        const uint16_t status = static_cast<uint16_t>(
            kStatusPermanent | (f.isProtected ? kStatusProtected : 0));
        putEntry(s, p, status, encodeRad50(nm), encodeRad50(nm + 3),
                 encodeRad50(ex), static_cast<uint16_t>(e.blocks));
        putw(s + p + 12, f.date);
        p += kEntrySize;
        ++inSeg;
    }

    /* Free-space entry + end-of-segment in the last used segment. */
    const int used = extents_.empty()
        ? rtfsDataStart(kSegments)
        : extents_.back().start + extents_.back().blocks;
    uint8_t *s = dirImage_.data() + segBase(seg);
    putEntry(s, p, kStatusEmpty, 0x00D5, 0x6739, 0x26F4,
             static_cast<uint16_t>(desc_.blocks - used));
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
    for (int n = 2; fs::exists(hostPath(name)); ++n) {
        const auto dot = base.rfind('.');
        name = (dot == std::string::npos)
             ? base + "-" + std::to_string(n)
             : base.substr(0, dot) + "-" + std::to_string(n) + base.substr(dot);
    }
    return name;
}

/*
 * reparseDirectory — read the guest-edited segments back, diff against
 * the descriptor, and make the folder match: created entries materialize
 * host files (content taken from scratch blocks the guest staged), gone
 * entries turn `deleted`, renames are tracked by the entry's start block,
 * shrunk lengths truncate the host file.  Ends with a rescan, which also
 * rebuilds the canonical directory image.
 */
void FolderVolume::reparseDirectory()
{
    struct Parsed { std::string name; int start, length; uint16_t status, date; };
    std::vector<Parsed> parsed;

    std::size_t seg = 0;
    for (int guard = 0; guard < kSegments; ++guard) {
        std::span<const uint8_t> buf(dirImage_.data() + seg * 2 * kBlock,
                                     2 * static_cast<std::size_t>(kBlock));
        auto d = parseSegment(buf);
        if (!d) return;            /* mid-edit garbage: wait for more writes */
        for (const auto &e : d->entries)
            if (e.isPermanent() || (e.status & kStatusTentative))
                parsed.push_back({e.name, e.startBlock, e.length,
                                  e.status, e.date});
        const uint16_t next = getw(dirImage_.data() + seg * 2 * kBlock + 2);
        if (next == 0 || next > kSegments) break;
        seg = next - 1;
    }

    std::vector<bool> seen(desc_.files.size(), false);
    std::vector<RtfsFile> created;

    for (const auto &p : parsed) {
        /* match by name */
        bool matched = false;
        for (std::size_t fi = 0; fi < desc_.files.size(); ++fi) {
            if (desc_.files[fi].deleted || seen[fi]) continue;
            if (desc_.files[fi].rt11Name != p.name) continue;
            seen[fi] = true;
            matched = true;
            desc_.files[fi].isProtected = (p.status & kStatusProtected) != 0;
            desc_.files[fi].date = p.date;
            if (const Extent *e = extentAt(p.start);
                e && p.length < e->blocks) {
                std::error_code ec;       /* guest shrank it (PIP .CLOSE) */
                fs::resize_file(hostPath(desc_.files[fi].hostName),
                                static_cast<uint64_t>(p.length) * kBlock, ec);
            }
            break;
        }
        if (matched) continue;

        /* rename: an existing extent starts exactly here */
        if (const Extent *e = extentAt(p.start);
            e && !seen[e->fileIndex] && p.start == e->start) {
            seen[e->fileIndex] = true;
            desc_.files[e->fileIndex].rt11Name = p.name;
            desc_.files[e->fileIndex].isProtected =
                (p.status & kStatusProtected) != 0;
            desc_.files[e->fileIndex].date = p.date;
            continue;
        }

        /* new file: materialize from scratch blocks (zeros elsewhere) */
        RtfsFile nf;
        nf.rt11Name    = p.name;
        nf.hostName    = materializeHostName(p.name);
        nf.date        = p.date;
        nf.isProtected = (p.status & kStatusProtected) != 0;
        std::ofstream out(hostPath(nf.hostName), std::ios::binary);
        for (int b = 0; b < p.length; ++b) {
            std::vector<uint8_t> blk(kBlock, 0);
            if (auto it = scratch_.find(p.start + b); it != scratch_.end()) {
                blk = it->second;
                scratch_.erase(it);
            }
            out.write(reinterpret_cast<const char *>(blk.data()), kBlock);
        }
        created.push_back(std::move(nf));
    }

    /* Entries the guest dropped turn `deleted` (host files kept). */
    for (std::size_t fi = 0; fi < desc_.files.size(); ++fi)
        if (!desc_.files[fi].deleted && !seen[fi])
            desc_.files[fi].deleted = true;
    for (auto &nf : created) desc_.files.push_back(std::move(nf));

    saveDescriptor();
    rescan();
}

} /* namespace ms0515::disk */
