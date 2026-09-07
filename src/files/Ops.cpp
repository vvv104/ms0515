/*
 * Ops.cpp — the operations between volumes and the host ends.
 */
#include "Ops.hpp"

#include <system_error>

namespace ms0515::files {

namespace {

/* The one question every writing operation asks of its target: is the
 * name taken, and may it be taken over?  "" when the write may go on. */
std::string targetCheck(const Location &to, const std::string &name, const Policy &policy)
{
    const auto there = to.find(name);
    if (!there) return "";
    if (!policy.overwrite) return name + ": already on " + to.device().name;
    if (there->protectedFlag && !policy.touchProtected) return name + ": protected on " + to.device().name;
    return "";
}

std::string sourceCheck(const Entry &entry, const Policy &policy)
{
    if (entry.protectedFlag && !policy.touchProtected) return entry.name + ": protected";
    return "";
}

} // namespace

OpResult copyFiles(const Location &from, const std::vector<Entry> &entries, Location &to, const Policy &policy)
{
    OpResult r;
    for (const auto &e : entries) {
        if (const auto why = targetCheck(to, e.name, policy); !why.empty()) { r.errors.push_back(why); continue; }
        const auto bytes = from.read(e.name);
        if (!bytes) { r.errors.push_back(e.name + ": cannot read"); continue; }
        if (const auto why = to.write(e.name, *bytes, FileMeta{e.date, e.protectedFlag}); !why.empty()) {
            r.errors.push_back(why);
            continue;
        }
        ++r.done;
    }
    return r;
}

OpResult moveFiles(Location &from, const std::vector<Entry> &entries, Location &to, const Policy &policy)
{
    OpResult r;
    for (const auto &e : entries) {
        if (const auto why = sourceCheck(e, policy); !why.empty()) { r.errors.push_back(why); continue; }
        const auto one = copyFiles(from, {e}, to, policy);
        if (!one.ok()) { r.errors.insert(r.errors.end(), one.errors.begin(), one.errors.end()); continue; }
        if (const auto why = from.remove(e.name); !why.empty()) { r.errors.push_back(why); continue; }
        ++r.done;
    }
    return r;
}

OpResult deleteFiles(Location &where, const std::vector<Entry> &entries, const Policy &policy)
{
    OpResult r;
    for (const auto &e : entries) {
        if (const auto why = sourceCheck(e, policy); !why.empty()) { r.errors.push_back(why); continue; }
        if (const auto why = where.remove(e.name); !why.empty()) { r.errors.push_back(why); continue; }
        ++r.done;
    }
    return r;
}

OpResult renameFile(Location &where, const Entry &entry, const std::string &newName, const Policy &policy)
{
    OpResult r;
    if (newName == entry.name) { r.errors.push_back(newName + ": the same name"); return r; }
    if (!Location::validName(newName)) { r.errors.push_back(newName + ": not an RT-11 name"); return r; }
    if (const auto why = sourceCheck(entry, policy); !why.empty()) { r.errors.push_back(why); return r; }
    if (const auto why = where.rename(entry.name, newName); !why.empty()) { r.errors.push_back(why); return r; }
    r.done = 1;
    return r;
}

OpResult protectFiles(Location &where, const std::vector<Entry> &entries, bool on)
{
    OpResult r;
    for (const auto &e : entries) {
        if (const auto why = where.setProtected(e.name, on); !why.empty()) { r.errors.push_back(why); continue; }
        ++r.done;
    }
    return r;
}

OpResult importFiles(const std::vector<std::filesystem::path> &hostFiles, Location &to, const Policy &policy)
{
    OpResult r;
    for (const auto &path : hostFiles) {
        const std::string name = Location::toVolumeName(path.filename().string());
        if (const auto why = targetCheck(to, name, policy); !why.empty()) { r.errors.push_back(why); continue; }
        const auto bytes = readHostFile(path);
        if (!bytes) { r.errors.push_back(path.string() + ": cannot read"); continue; }
        if (const auto why = to.write(name, *bytes, FileMeta{policy.date, false}); !why.empty()) {
            r.errors.push_back(why);
            continue;
        }
        ++r.done;
    }
    return r;
}

OpResult exportFiles(const Location &from, const std::vector<Entry> &entries,
                     const std::filesystem::path &hostDir, const Policy &policy)
{
    OpResult r;
    std::error_code ec;
    std::filesystem::create_directories(hostDir, ec);
    if (ec) { r.errors.push_back(hostDir.string() + ": cannot create"); return r; }
    for (const auto &e : entries) {
        const auto target = hostDir / e.name;
        if (std::filesystem::exists(target, ec) && !policy.overwrite) {
            r.errors.push_back(e.name + ": already in " + hostDir.string());
            continue;
        }
        const auto bytes = from.read(e.name);
        if (!bytes) { r.errors.push_back(e.name + ": cannot read"); continue; }
        if (const auto why = writeHostFile(target, *bytes); !why.empty()) { r.errors.push_back(why); continue; }
        ++r.done;
    }
    return r;
}

std::vector<std::string> clashes(const std::vector<Entry> &entries, const Location &to)
{
    std::vector<std::string> out;
    for (const auto &e : entries)
        if (to.find(e.name)) out.push_back(e.name);
    return out;
}

std::vector<std::string> protectedOnes(const std::vector<Entry> &entries)
{
    std::vector<std::string> out;
    for (const auto &e : entries)
        if (e.protectedFlag) out.push_back(e.name);
    return out;
}

} /* namespace ms0515::files */
