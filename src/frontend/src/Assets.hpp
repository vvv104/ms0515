/*
 * Assets.hpp — thin re-exports of the shared libapp helpers (ROM/disk
 * discovery, image validation, screen composition and screenshots).
 *
 * The shared logic lives in libapp so ms0515-cli sees identical
 * behaviour; this header simply forwards the legacy
 * `ms0515_frontend::` names that older frontend code uses.
 */
#pragma once

#include "ms0515/app/Disks.hpp"
#include "ms0515/app/Config.hpp"   /* resolveRom */
#include "ms0515/app/Screen.hpp"

#include <string>

namespace ms0515_frontend {

using ms0515::app::discoverRoms;
using ms0515::app::validateSingleSideImage;
using ms0515::app::validateDoubleSidedImage;
using ms0515::app::validateHdImage;
using ms0515::app::saveScreenshot;
using ms0515::app::kScreenWidth;
using ms0515::app::kScreenHeight;

/* Default ROM picked when neither --rom nor ms0515.yaml's rom: is set.
 * Wrapper around ms0515::app::resolveRom("", "") so old call sites
 * keep working unchanged. */
inline std::string findDefaultRom()
{
    return ms0515::app::resolveRom(/*cliRomPath=*/"", /*cfgRomPath=*/"");
}

} /* namespace ms0515_frontend */
