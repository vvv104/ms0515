/*
 * Mounts.cpp — the images behind the devices, from the emulator's flags
 * and config and back into the config.
 */
#include "Mounts.hpp"

#include "ms0515/app/Cli.hpp"
#include "ms0515/app/Config.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <fstream>
#include <system_error>

namespace ms0515::files {

namespace {

constexpr uintmax_t kSingleSided = 409600;
constexpr uintmax_t kDoubleSided = 819200;

std::vector<uint8_t> readAll(const std::filesystem::path &path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

bool hasVolume(const std::vector<disk::VolumeSpec> &specs, disk::Vol vol, int side = 0)
{
    return std::any_of(specs.begin(), specs.end(),
                       [&](const disk::VolumeSpec &s) { return s.vol == vol && s.side == side; });
}

/* Does the RT-11 directory parse through this lens?  detectVolumes() is
 * stricter (it wants the home block's directory pointer, which many of
 * the machine's diskettes never had), so a floppy side is judged by the
 * directory alone. */
bool directoryParses(const std::vector<uint8_t> &bytes, disk::Vol vol, int side)
{
    const auto img = disk::openVolume(bytes, vol, side);
    return img && img->hasDirectory;
}

} // namespace

Mounts Mounts::fromEmulator(const app::CliArgs &cli, const app::Config &config)
{
    Mounts m;
    for (int drive = 0; drive < 2; ++drive) {
        const bool cliHas = !cli.dsPath[drive].empty()
            || !cli.fdPath[app::fdcUnitFor(drive, 0)].empty()
            || !cli.fdPath[app::fdcUnitFor(drive, 1)].empty();
        const std::string &ds = cliHas ? cli.dsPath[drive] : config.dsPath[drive];
        if (!ds.empty()) {
            (void)m.mount(drive == 0 ? Slot::driveA : Slot::driveB, ds, 0, /*force=*/true);
            continue;
        }
        for (int side = 0; side < 2; ++side) {
            const std::string &fd = cliHas ? cli.fdPath[app::fdcUnitFor(drive, side)]
                                           : config.fdPath[app::fdcUnitFor(drive, side)];
            if (!fd.empty()) (void)m.mount(drive == 0 ? Slot::driveA : Slot::driveB, fd, side, /*force=*/true);
        }
    }
    const std::string &hd = !cli.hdPath.empty() ? cli.hdPath : config.hdPath;
    if (!hd.empty()) (void)m.mount(Slot::hd, hd, 0, /*force=*/true);
    return m;
}

std::string Mounts::mount(Slot slot, const std::filesystem::path &image, int side, bool force)
{
    std::error_code ec;
    const auto size = std::filesystem::file_size(image, ec);
    if (ec) return image.string() + ": cannot read";
    const auto bytes = readAll(image);
    if (bytes.size() != size) return image.string() + ": cannot read";
    const auto specs = disk::detectVolumes(bytes);

    if (slot == Slot::hd) {
        if (size == 0 || size % disk::kBlock != 0) return image.string() + ": not a multiple of 512 bytes";
        if (!force && !directoryParses(bytes, disk::Vol::linear, 0)) return image.string() + ": no RT-11 volume inside (mount anyway to initialise it)";
        hd_ = image;
        return "";
    }
    FloppySlot &fd = fd_[slot == Slot::driveA ? 0 : 1];
    if (size == kDoubleSided) {
        const bool any = hasVolume(specs, disk::Vol::dv) || hasVolume(specs, disk::Vol::mz)
            || directoryParses(bytes, disk::Vol::floppy, 0) || directoryParses(bytes, disk::Vol::floppy, 1);
        if (!force && !any) return image.string() + ": no RT-11 volume inside (mount anyway to initialise it)";
        fd = FloppySlot{};
        fd.ds = image;
        fd.dsVolumes = specs;
        return "";
    }
    if (size == kSingleSided) {
        if (side != 0 && side != 1) return "side must be 0 or 1";
        if (!force && !directoryParses(bytes, disk::Vol::floppy, 0)) return image.string() + ": no RT-11 volume inside (mount anyway to initialise it)";
        fd.ds.clear();
        fd.dsVolumes.clear();
        fd.side[side] = image;
        return "";
    }
    return fmt::format("{}: {} bytes - a floppy image is 409600 (one side) or 819200 (two)", image.string(), size);
}

void Mounts::unmount(Slot slot)
{
    if (slot == Slot::hd) hd_.clear();
    else fd_[slot == Slot::driveA ? 0 : 1] = FloppySlot{};
}

std::optional<Mount> Mounts::mounted(Slot slot) const
{
    if (slot == Slot::hd) {
        if (hd_.empty()) return std::nullopt;
        return Mount{slot, hd_, disk::detectVolumes(readAll(hd_))};
    }
    const FloppySlot &fd = fd_[slot == Slot::driveA ? 0 : 1];
    if (!fd.ds.empty()) return Mount{slot, fd.ds, fd.dsVolumes};
    for (int side = 0; side < 2; ++side)
        if (!fd.side[side].empty()) return Mount{slot, fd.side[side], disk::detectVolumes(readAll(fd.side[side]))};
    return std::nullopt;
}

std::vector<Device> Mounts::floppyDevices(int drive) const
{
    std::vector<Device> out;
    const FloppySlot &fd = fd_[drive];
    if (!fd.ds.empty()) {
        if (hasVolume(fd.dsVolumes, disk::Vol::dv))
            out.push_back(Device{fmt::format("DV{}:", drive), fd.ds, disk::VolumeSpec{disk::Vol::dv, 0}});
        else if (hasVolume(fd.dsVolumes, disk::Vol::mz))
            out.push_back(Device{fmt::format("MZ{}:", drive), fd.ds, disk::VolumeSpec{disk::Vol::mz, 0}});
        else
            for (int side = 0; side < 2; ++side)
                out.push_back(Device{fmt::format("DZ{}:", app::fdcUnitFor(drive, side)), fd.ds,
                                     disk::VolumeSpec{disk::Vol::floppy, side}});
        return out;
    }
    for (int side = 0; side < 2; ++side)
        if (!fd.side[side].empty())
            out.push_back(Device{fmt::format("DZ{}:", app::fdcUnitFor(drive, side)), fd.side[side],
                                 disk::VolumeSpec{disk::Vol::floppy, 0}});
    return out;
}

std::vector<Device> Mounts::devices() const
{
    std::vector<Device> out = floppyDevices(0);
    const auto b = floppyDevices(1);
    out.insert(out.end(), b.begin(), b.end());
    if (!hd_.empty()) out.push_back(Device{"HD0:", hd_, disk::VolumeSpec{disk::Vol::linear, 0}});
    return out;
}

std::optional<Device> Mounts::device(const std::string &name) const
{
    for (const auto &d : devices())
        if (d.name == name) return d;
    return std::nullopt;
}

void Mounts::store(app::Config &config) const
{
    for (int drive = 0; drive < 2; ++drive) {
        config.dsPath[drive] = fd_[drive].ds.string();
        for (int side = 0; side < 2; ++side)
            config.fdPath[app::fdcUnitFor(drive, side)] = fd_[drive].side[side].string();
    }
    config.hdPath = hd_.string();
    if (!hd_.empty()) config.hdEnabled = true;
}

std::string Mounts::describe(const std::filesystem::path &image)
{
    std::error_code ec;
    const auto size = std::filesystem::file_size(image, ec);
    if (ec) return image.string() + ": cannot read";
    if (size == kSingleSided) return fmt::format("{} bytes: one side of a floppy (DZ, side 0 or 1 of a drive)", size);
    if (size == kDoubleSided) return fmt::format("{} bytes: a two-sided floppy (both sides of a drive), or a DV/MZ volume", size);
    if (size > 0 && size % disk::kBlock == 0) return fmt::format("{} bytes: {} blocks, an HD/LD container", size, size / disk::kBlock);
    return fmt::format("{} bytes: no slot takes this size", size);
}

std::vector<std::string> completePath(const std::string &prefix)
{
    namespace fs = std::filesystem;
    std::vector<std::string> out;
    const fs::path p(prefix);
    fs::path dir = p.parent_path();
    std::string stem = p.filename().string();
    if (prefix.empty() || prefix.back() == '/' || prefix.back() == '\\') {
        dir = p;
        stem.clear();
    }
    if (dir.empty()) dir = ".";
    std::error_code ec;
    for (const auto &entry : fs::directory_iterator(dir, ec)) {
        const std::string name = entry.path().filename().string();
        if (name.rfind(stem, 0) != 0) continue;
        if (entry.is_directory(ec)) {
            out.push_back(entry.path().string() + static_cast<char>(fs::path::preferred_separator));
            continue;
        }
        const auto size = entry.file_size(ec);
        if (!ec && size > 0 && size % disk::kBlock == 0) out.push_back(entry.path().string());
    }
    return out;
}

} /* namespace ms0515::files */
