/*
 * Config.cpp — frontend-only path helper.  Everything else lives in
 * libapp (see ms0515/app/{Paths,Config,Cli}.cpp).
 */

#include "Config.hpp"

namespace ms0515_frontend {

std::string Paths::initialDirFor(FileDialogKind kind)
{
    std::string base = ms0515::app::Paths::exeDir();
    switch (kind) {
    case FileDialogKind::Disk:  return base + "assets/disks";
    case FileDialogKind::Rom:   return base + "assets/rom";
    case FileDialogKind::State: return base;
    }
    return base;
}

} /* namespace ms0515_frontend */
