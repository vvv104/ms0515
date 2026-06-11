/*
 * FolderVolume.cpp — host folder as an RT-11 block device (read path +
 * data writes; guest directory-write reparse lands in the next stage).
 */

#include "ms0515/disk/FolderVolume.hpp"

#include "ms0515/disk/Directory.hpp"
#include "ms0515/disk/Layout.hpp"

#include "Internal.hpp"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

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
    vol->rescan();
    return vol;
}

std::string FolderVolume::hostPath(const std::string &name) const
{
    return (fs::path(folder_) / name).string();
}

/*
 * rescan — reconcile the descriptor with the folder: auto-fill an empty
 * descriptor, append host files that are not yet listed, then derive the
 * extent table from current host sizes (a vanished host file keeps its
 * entry but yields a zero-length, missing extent -> NAME.BAD display).
 */
void FolderVolume::rescan()
{
    std::vector<RtfsHostFile> listing;
    std::error_code ec;
    for (const auto &de : fs::directory_iterator(folder_, ec)) {
        if (!de.is_regular_file(ec)) continue;
        const std::string name = de.path().filename().string();
        if (name == descriptorName_ || name == desc_.bootHost) continue;
        if (de.path().extension() == kRtfsExtension) continue;
        listing.push_back({name, de.file_size(ec), 0});
    }

    const bool wasEmpty = desc_.files.empty();
    if (wasEmpty) {
        autoFillRtfs(desc_, listing);
    } else {
        /* Append files the descriptor doesn't know yet. */
        std::vector<std::string> taken;
        for (const auto &df : desc_.files) taken.push_back(df.rt11Name);
        bool changed = false;
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
    if (wasEmpty && !desc_.files.empty()) saveDescriptor();

    /* Extents from current host sizes. */
    std::vector<uint64_t> sizes(desc_.files.size(), 0);
    std::vector<bool> missing(desc_.files.size(), false);
    for (std::size_t i = 0; i < desc_.files.size(); ++i) {
        if (desc_.files[i].deleted) continue;
        if (auto sz = hostSize(hostPath(desc_.files[i].hostName)))
            sizes[i] = *sz;
        else
            missing[i] = true;
    }
    extents_.clear();
    int cur = rtfsDataStart(kSegments);
    for (std::size_t i = 0; i < desc_.files.size(); ++i) {
        if (desc_.files[i].deleted) continue;
        const int nblk = static_cast<int>(
            (sizes[i] + kBlock - 1) / static_cast<uint64_t>(kBlock));
        if (cur + nblk > desc_.blocks) continue;       /* doesn't fit */
        extents_.push_back({i, cur, nblk, missing[i]});
        cur += nblk;
    }
    generateDirectory();
}

void FolderVolume::saveDescriptor() const
{
    std::ofstream f(descriptorPath_, std::ios::binary | std::ios::trunc);
    const std::string text = serializeRtfs(desc_);
    f.write(text.data(), static_cast<std::streamsize>(text.size()));
}

/*
 * generateDirectory — render the descriptor's live extents as RT-11
 * directory segments (chained, kSegments reserved), one permanent entry
 * per extent, a missing host shown as NAME.BAD, then a single empty entry
 * covering the free tail, then the end-of-segment marker.
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
        std::string shown = f.rt11Name;
        if (e.missing) {                               /* NAME.BAD marker  */
            const auto dot = shown.rfind('.');
            shown = shown.substr(0, dot) + ".BAD";
        }
        char nm[6], ex[3];
        splitName(shown, nm, ex);
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

void FolderVolume::readBlock(int lbn, uint8_t *out)
{
    std::memset(out, 0, kBlock);
    if (lbn < 0 || lbn >= desc_.blocks) return;

    if (lbn == 1) {
        const auto home = makeHomeBlock(desc_.volumeId, "");
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
        if (e->missing) return;                        /* .BAD reads zeros */
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
    if (lbn < 0 || lbn >= desc_.blocks) return;

    const int dirEnd = kDirLbn + 2 * kSegments;
    if (lbn >= kDirLbn && lbn < dirEnd) {
        /* Guest directory edits (create/delete/rename) are stage A2b. */
        return;
    }
    if (const Extent *e = extentAt(lbn)) {
        if (e->missing) return;
        const std::string path = hostPath(desc_.files[e->fileIndex].hostName);
        std::fstream f(path, std::ios::binary | std::ios::in | std::ios::out);
        if (!f) return;
        f.seekp(static_cast<std::streamoff>(lbn - e->start) * kBlock);
        f.write(reinterpret_cast<const char *>(in), kBlock);
        return;
    }
    scratch_[lbn].assign(in, in + kBlock);
}

} /* namespace ms0515::disk */
