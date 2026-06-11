/*
 * Rtfs.cpp — parse/serialize/mangle/auto-fill for `.rtfs` descriptors.
 * Pure data handling; no filesystem access (FolderVolume owns host I/O).
 */

#include "ms0515/disk/Rtfs.hpp"

#include "ms0515/disk/Build.hpp"     /* encodeDate / decodeDate */
#include "ms0515/disk/Layout.hpp"    /* kBlock */

#include <algorithm>
#include <cctype>
#include <charconv>
#include <format>

namespace ms0515::disk {

namespace {

std::string_view trim(std::string_view s)
{
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' ||
                          s.back() == '\r')) s.remove_suffix(1);
    return s;
}

bool fail(std::string *error, std::string msg)
{
    if (error) *error = std::move(msg);
    return false;
}

/* "date=1994-02-18" -> encoded word; false on malformed/out-of-range. */
bool parseDateAttr(std::string_view v, uint16_t &out, std::string *error)
{
    int p[3]{};
    std::size_t start = 0;
    for (int i = 0; i < 3; ++i) {
        const std::size_t end = (i < 2) ? v.find('-', start) : v.size();
        if (end == std::string_view::npos || end == start)
            return fail(error, "malformed date (want YYYY-MM-DD)");
        auto [ptr, ec] = std::from_chars(v.data() + start, v.data() + end, p[i]);
        if (ec != std::errc{} || ptr != v.data() + end)
            return fail(error, "malformed date (want YYYY-MM-DD)");
        start = end + 1;
    }
    try { out = encodeDate(p[0], p[1], p[2]); }
    catch (const std::exception &e) { return fail(error, e.what()); }
    return true;
}

/* Parse one "file:" value: "RT11 | host | attr attr ...". */
bool parseFileLine(std::string_view v, RtfsFile &f, std::string *error)
{
    const std::size_t p1 = v.find('|');
    if (p1 == std::string_view::npos)
        return fail(error, "file line needs 'RT11NAME | hostname | attrs'");
    const std::size_t p2 = v.find('|', p1 + 1);

    f.rt11Name = std::string(trim(v.substr(0, p1)));
    f.hostName = std::string(trim(p2 == std::string_view::npos
                                  ? v.substr(p1 + 1)
                                  : v.substr(p1 + 1, p2 - p1 - 1)));
    if (f.rt11Name.empty() || f.hostName.empty())
        return fail(error, "file line needs both an RT-11 and a host name");

    std::string_view attrs = (p2 == std::string_view::npos)
                           ? std::string_view{} : trim(v.substr(p2 + 1));
    while (!attrs.empty()) {
        const std::size_t sp = attrs.find(' ');
        std::string_view tok = trim(attrs.substr(0, sp));
        attrs = (sp == std::string_view::npos)
              ? std::string_view{} : trim(attrs.substr(sp + 1));
        if (tok.empty()) continue;
        if (tok == "protected")               f.isProtected = true;
        else if (tok == "deleted")            f.deleted = true;
        else if (tok.starts_with("date=")) {
            if (!parseDateAttr(tok.substr(5), f.date, error)) return false;
        }
        else return fail(error, std::format("unknown file attribute '{}'", tok));
    }
    return true;
}

}  /* namespace */

std::optional<RtfsDescriptor>
parseRtfs(std::string_view text, std::string *error)
{
    RtfsDescriptor d;
    bool sawDevice = false, sawBlocks = false;

    std::size_t pos = 0;
    while (pos <= text.size()) {
        const std::size_t nl = text.find('\n', pos);
        std::string_view line = trim(text.substr(
            pos, nl == std::string_view::npos ? text.size() - pos : nl - pos));
        pos = (nl == std::string_view::npos) ? text.size() + 1 : nl + 1;
        if (line.empty() || line.front() == '#') continue;

        const std::size_t colon = line.find(':');
        if (colon == std::string_view::npos) {
            fail(error, std::format("malformed line '{}'", line));
            return std::nullopt;
        }
        const std::string_view key = trim(line.substr(0, colon));
        const std::string_view val = trim(line.substr(colon + 1));

        if (key == "device") {
            if (val == "hd")          d.device = RtfsDescriptor::Device::Hd;
            else if (val == "floppy") d.device = RtfsDescriptor::Device::Floppy;
            else { fail(error, std::format("unknown device '{}'", val));
                   return std::nullopt; }
            sawDevice = true;
        } else if (key == "blocks") {
            auto [ptr, ec] = std::from_chars(val.data(), val.data() + val.size(),
                                             d.blocks);
            if (ec != std::errc{} || ptr != val.data() + val.size()) {
                fail(error, "blocks: wants a number"); return std::nullopt; }
            sawBlocks = true;
        } else if (key == "volume-id") {
            d.volumeId = std::string(val);
        } else if (key == "owner") {
            d.owner = std::string(val);
        } else if (key == "boot") {
            d.bootHost = std::string(val);
        } else if (key == "file") {
            RtfsFile f;
            if (!parseFileLine(val, f, error)) return std::nullopt;
            for (const auto &prev : d.files)
                if (prev.rt11Name == f.rt11Name) {
                    fail(error, std::format("duplicate RT-11 name '{}'",
                                            f.rt11Name));
                    return std::nullopt;
                }
            d.files.push_back(std::move(f));
        } else {
            fail(error, std::format("unknown key '{}'", key));
            return std::nullopt;
        }
    }

    if (!sawDevice) { fail(error, "missing 'device:'"); return std::nullopt; }
    if (!sawBlocks) { fail(error, "missing 'blocks:'"); return std::nullopt; }
    if (d.blocks <= 0 || d.blocks > kRtfsMaxBlocks) {
        fail(error, std::format("blocks must be 1..{}", kRtfsMaxBlocks));
        return std::nullopt;
    }
    if (d.device == RtfsDescriptor::Device::Floppy &&
        d.blocks != kRtfsFloppyBlocks) {
        fail(error, std::format("a floppy device is exactly {} blocks",
                                kRtfsFloppyBlocks));
        return std::nullopt;
    }
    return d;
}

std::string serializeRtfs(const RtfsDescriptor &d)
{
    std::string out = "# MS0515 folder-backed block device\n";
    out += std::format("device: {}\n",
                       d.device == RtfsDescriptor::Device::Hd ? "hd" : "floppy");
    out += std::format("blocks: {}\n", d.blocks);
    if (d.volumeId != "RT11A")
        out += std::format("volume-id: {}\n", d.volumeId);
    if (!d.owner.empty())
        out += std::format("owner: {}\n", d.owner);
    if (!d.bootHost.empty())
        out += std::format("boot: {}\n", d.bootHost);
    for (const auto &f : d.files) {
        out += std::format("file: {} | {} |", f.rt11Name, f.hostName);
        if (f.date) {
            const auto dp = decodeDate(f.date);
            out += std::format(" date={:04d}-{:02d}-{:02d}",
                               dp.year, dp.month, dp.day);
        }
        if (f.isProtected) out += " protected";
        if (f.deleted)     out += " deleted";
        out += '\n';
    }
    return out;
}

namespace {

/* Uppercase + filter to the RAD50 charset, truncated to `maxLen`. */
std::string rad50Clean(std::string_view in, std::size_t maxLen)
{
    std::string out;
    for (const char c : in) {
        if (out.size() == maxLen) break;
        const unsigned char u = static_cast<unsigned char>(c);
        const char up = static_cast<char>(std::toupper(u));
        if ((up >= 'A' && up <= 'Z') || (up >= '0' && up <= '9') || up == '$')
            out += up;
    }
    return out;
}

}  /* namespace */

std::string mangleRt11Name(std::string_view hostName,
                           const std::vector<std::string> &taken)
{
    const std::size_t dot = hostName.rfind('.');
    std::string base = rad50Clean(
        dot == std::string_view::npos ? hostName : hostName.substr(0, dot), 6);
    std::string ext = (dot == std::string_view::npos)
        ? std::string{} : rad50Clean(hostName.substr(dot + 1), 3);
    if (base.empty()) base = "FILE";

    auto compose = [&](const std::string &b) {
        return ext.empty() ? b : b + "." + ext;
    };
    auto isTaken = [&](const std::string &name) {
        return std::find(taken.begin(), taken.end(), name) != taken.end();
    };

    std::string name = compose(base);
    for (int n = 2; isTaken(name); ++n) {
        const std::string suffix = std::to_string(n);
        std::string b = base.substr(0, 6 - suffix.size()) + suffix;
        name = compose(b);
    }
    return name;
}

void autoFillRtfs(RtfsDescriptor &d, const std::vector<RtfsHostFile> &listing)
{
    if (!d.files.empty()) return;

    /* Partition: SWAP.SYS, RT11SJ.SYS, other .SYS, then the rest — each
     * group in listing order. */
    auto rank = [](const std::string &mangled) {
        if (mangled == "SWAP.SYS")   return 0;
        if (mangled == "RT11SJ.SYS") return 1;
        if (mangled.ends_with(".SYS")) return 2;
        return 3;
    };
    std::vector<std::size_t> order(listing.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(),
        [&](std::size_t a, std::size_t b) {
            return rank(mangleRt11Name(listing[a].name))
                 < rank(mangleRt11Name(listing[b].name));
        });

    const int capacity = d.blocks - rtfsDataStart();
    int used = 0;
    std::vector<std::string> taken;
    for (const std::size_t i : order) {
        const auto &hf = listing[i];
        const int nblk = static_cast<int>(
            (hf.size + kBlock - 1) / static_cast<uint64_t>(kBlock));
        if (used + nblk > capacity) continue;        /* doesn't fit — skip */
        RtfsFile f;
        f.rt11Name = mangleRt11Name(hf.name, taken);
        f.hostName = hf.name;
        f.date     = hf.date;
        taken.push_back(f.rt11Name);
        d.files.push_back(std::move(f));
        used += nblk;
    }
}

std::vector<RtfsExtent>
rtfsLayout(const RtfsDescriptor &d, const std::vector<uint64_t> &liveSizes,
           int dirSegments)
{
    std::vector<RtfsExtent> out;
    int cur = rtfsDataStart(dirSegments);
    for (std::size_t i = 0; i < d.files.size(); ++i) {
        const auto &f = d.files[i];
        if (f.deleted) continue;
        const uint64_t size = (i < liveSizes.size()) ? liveSizes[i] : 0;
        const int nblk = static_cast<int>(
            (size + kBlock - 1) / static_cast<uint64_t>(kBlock));
        RtfsExtent e;
        e.file   = &f;
        e.blocks = nblk;
        if (cur + nblk <= d.blocks) {
            e.start = cur;
            cur += nblk;
        } else {
            e.start = -1;                            /* does not fit */
        }
        out.push_back(e);
    }
    return out;
}

} /* namespace ms0515::disk */
