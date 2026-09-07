/*
 * MountSync.cpp — units and the HD follow the slots.
 */
#include "MountSync.hpp"

#include "ms0515/app/Config.hpp"

namespace ms0515::cli {

std::vector<std::string> applyMounts(Emulator &emu, const files::Mounts &mounts)
{
    /* The slots as the emulator's own fields: a two-sided image in
     * dsPath[drive], single-sided ones per unit in fdPath[unit]. */
    app::Config want;
    mounts.store(want);
    std::vector<std::string> errors;

    for (int drive = 0; drive < 2; ++drive) {
        for (int side = 0; side < 2; ++side) {
            const int unit = app::fdcUnitFor(drive, side);
            const std::string &path = !want.dsPath[drive].empty() ? want.dsPath[drive] : want.fdPath[unit];
            if (emu.diskPath(unit) == path) continue;
            if (!emu.diskPath(unit).empty()) emu.unmountDisk(unit);
            if (!path.empty() && !emu.mountDisk(unit, path)) errors.push_back(path + ": cannot mount in unit " + std::to_string(unit));
        }
    }

    if (emu.hdPath() != want.hdPath) {
        if (emu.hdMounted()) emu.unmountHd();
        if (!want.hdPath.empty()) {
            if (emu.mountHd(want.hdPath)) emu.setHdEnabled(true);
            else errors.push_back(want.hdPath + ": cannot mount as the HD");
        }
    }
    return errors;
}

} /* namespace ms0515::cli */
