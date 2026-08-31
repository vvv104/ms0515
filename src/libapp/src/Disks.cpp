/*
 * Disks.cpp — image discovery / validation / mount.
 */

#include "ms0515/app/Disks.hpp"
#include "ms0515/app/Cli.hpp"
#include "ms0515/app/Config.hpp"   /* fdcUnitFor */
#include "ms0515/app/Paths.hpp"

#include <ms0515/Emulator.hpp>     /* Emulator, kFloppyDiskSize */

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fmt/format.h>

namespace ms0515::app {

namespace {

std::optional<std::string>
validateDiskImage(const std::string &path, std::uintmax_t expected)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    auto sz = fs::file_size(path, ec);
    if (ec)
        return fmt::format("cannot stat '{}': {}", path, ec.message());
    if (sz == expected)
        return std::nullopt;

    const bool wantedDouble = (expected == 2 * ms0515::kFloppyDiskSize);
    const bool sizeIsOther  = (sz == (wantedDouble ? ms0515::kFloppyDiskSize
                                                   : 2 * ms0515::kFloppyDiskSize));
    if (sizeIsOther) {
        return wantedDouble
            ? fmt::format(
                "'{}' is a single-side image ({} bytes).  Use "
                "--diskN-side0 (or -dNs0) to mount it on one side of "
                "a drive.", path, ms0515::kFloppyDiskSize)
            : fmt::format(
                "'{}' is a double-sided image ({} bytes).  Use "
                "--diskN (or -dN) to mount a whole double-sided drive "
                "from one image.", path, 2 * ms0515::kFloppyDiskSize);
    }
    return fmt::format(
        "'{}' has unrecognised disk format (size {} bytes; expected {} "
        "for a {} image).",
        path, static_cast<unsigned long long>(sz), expected,
        wantedDouble ? "double-sided" : "single-side");
}

}  /* anonymous namespace */

namespace {

/* True for a `.rtfs` folder-device descriptor (validated by the mount). */
bool isRtfsDescriptor(const std::string &path)
{
    std::string lower = path;
    for (auto &c : lower)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return lower.ends_with(".rtfs");
}

}  /* anonymous namespace */

std::optional<std::string>
validateSingleSideImage(const std::string &path)
{
    if (isRtfsDescriptor(path)) {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(path, ec))
            return fmt::format("cannot read descriptor '{}'", path);
        return std::nullopt;
    }
    return validateDiskImage(path, ms0515::kFloppyDiskSize);
}

std::optional<std::string>
validateDoubleSidedImage(const std::string &path)
{
    return validateDiskImage(path, 2 * ms0515::kFloppyDiskSize);
}

std::optional<std::string>
validateHdImage(const std::string &path)
{
    namespace fs = std::filesystem;

    /* A `.rtfs` descriptor mounts a folder-backed volume: existence is
     * checked here, the descriptor itself is validated by the mount
     * (device type, size, syntax). */
    if (isRtfsDescriptor(path)) {
        std::error_code ec;
        if (!fs::is_regular_file(path, ec))
            return fmt::format("cannot read descriptor '{}'", path);
        return std::nullopt;
    }

    constexpr std::uintmax_t kHdBlock     = 512;       /* RT-11 block size   */
    constexpr std::uintmax_t kHdMaxBlocks = 65535;     /* RT-11 volume limit */
    constexpr std::uintmax_t kHdMaxBytes  = kHdMaxBlocks * kHdBlock;
    std::error_code ec;
    auto sz = fs::file_size(path, ec);
    if (ec)
        return fmt::format("cannot stat '{}': {}", path, ec.message());
    if (sz == 0 || (sz % kHdBlock) != 0)
        return fmt::format(
            "'{}' is not a valid HD image (size {} bytes; expected a "
            "positive multiple of {}).",
            path, static_cast<unsigned long long>(sz), kHdBlock);
    if (sz > kHdMaxBytes)
        return fmt::format(
            "'{}' is {} bytes; an RT-11 volume tops out at {} blocks "
            "(~32 MB / {} bytes).",
            path, static_cast<unsigned long long>(sz),
            static_cast<unsigned long long>(kHdMaxBlocks),
            static_cast<unsigned long long>(kHdMaxBytes));
    return std::nullopt;
}

std::vector<std::string> discoverRoms()
{
    namespace fs = std::filesystem;
    std::error_code ec;
    std::vector<std::string> result;
    for (const auto &root : Paths::searchRoots()) {
        fs::path romDir = root / "assets" / "rom";
        if (!fs::is_directory(romDir, ec)) continue;
        for (const auto &entry : fs::directory_iterator(romDir, ec)) {
            if (!entry.is_regular_file(ec)) continue;
            if (entry.path().extension() != ".rom") continue;
            std::string p = entry.path().lexically_normal().string();
            if (std::find(result.begin(), result.end(), p) == result.end())
                result.push_back(p);
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

bool mountDisksFromCli(ms0515::Emulator &emu, const CliArgs &cli)
{
    for (int drive = 0; drive < 2; ++drive) {
        const bool wantDs = !cli.dsPath[drive].empty();
        const bool wantS0 = !cli.fdPath[fdcUnitFor(drive, 0)].empty();
        const bool wantS1 = !cli.fdPath[fdcUnitFor(drive, 1)].empty();

        if (wantDs && (wantS0 || wantS1)) {
            std::fprintf(stderr,
                "error: --disk%d (-d%d) is mutually exclusive with "
                "--disk%d-sideN (-d%dsN); pick one.  Skipping drive %d.\n",
                drive, drive, drive, drive, drive);
            continue;
        }

        if (wantDs) {
            if (auto err = validateDoubleSidedImage(cli.dsPath[drive])) {
                std::fprintf(stderr,
                    "error: cannot mount disk %d (double-sided): %s\n",
                    drive, err->c_str());
                continue;
            }
            const int u0 = fdcUnitFor(drive, 0);
            const int u1 = fdcUnitFor(drive, 1);
            const bool ok0 = emu.mountDisk(u0, cli.dsPath[drive]);
            const bool ok1 = emu.mountDisk(u1, cli.dsPath[drive]);
            if (!ok0 || !ok1) {
                std::fprintf(stderr,
                    "error: failed to mount double-sided '%s' on drive %d\n",
                    cli.dsPath[drive].c_str(), drive);
                if (ok0) emu.unmountDisk(u0);
                if (ok1) emu.unmountDisk(u1);
                return false;
            }
            continue;
        }

        for (int side = 0; side < 2; ++side) {
            const int unit = fdcUnitFor(drive, side);
            if (cli.fdPath[unit].empty()) continue;
            if (auto err = validateSingleSideImage(cli.fdPath[unit])) {
                std::fprintf(stderr,
                    "error: cannot mount disk %d side %d: %s\n",
                    drive, side, err->c_str());
                continue;
            }
            if (!emu.mountDisk(unit, cli.fdPath[unit])) {
                std::fprintf(stderr,
                    "error: failed to mount '%s' on drive %d side %d\n",
                    cli.fdPath[unit].c_str(), drive, side);
                return false;
            }
        }
    }

    /* Paravirtual hard disk (HD:) — independent of the floppy drives.  The
     * controller's presence and the mounted image are separate: a non-empty
     * path implies an enabled controller, but the controller can also be
     * enabled with no media (an offline drive). */
    if (cli.hdEnabled || !cli.hdPath.empty())
        emu.setHdEnabled(true);
    if (!cli.hdPath.empty()) {
        if (auto err = validateHdImage(cli.hdPath)) {
            std::fprintf(stderr, "error: cannot mount HD: %s\n", err->c_str());
        } else if (!emu.mountHd(cli.hdPath)) {
            std::fprintf(stderr, "error: failed to mount HD image '%s'\n",
                         cli.hdPath.c_str());
            return false;
        }
    }
    return true;
}

} /* namespace ms0515::app */
