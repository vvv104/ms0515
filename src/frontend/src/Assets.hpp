/*
 * Assets.hpp — frontend-only PNG screenshot helper + thin
 * re-exports of the shared libapp helpers (ROM/disk discovery,
 * image validation).
 *
 * The shared logic lives in libapp so ms0515-cli sees identical
 * behaviour; this header simply forwards the legacy
 * `ms0515_frontend::` names that older frontend code uses.
 */
#pragma once

#include "ms0515/app/Disks.hpp"
#include "ms0515/app/Config.hpp"   /* resolveRom */

#include <string>

namespace ms0515_frontend {

class Video;

using ms0515::app::discoverRoms;
using ms0515::app::validateSingleSideImage;
using ms0515::app::validateDoubleSidedImage;
using ms0515::app::validateHdImage;

/* Default ROM picked when neither --rom nor ms0515.yaml's rom: is set.
 * Wrapper around ms0515::app::resolveRom("", "") so old call sites
 * keep working unchanged. */
inline std::string findDefaultRom()
{
    return ms0515::app::resolveRom(/*cliRomPath=*/"", /*cfgRomPath=*/"");
}

/* Save a PNG screenshot of the emulated framebuffer.  When `path` is
 * empty, auto-generates a timestamped filename next to the exe.
 * Returns the resulting file path on success, an empty string on
 * failure.  Frontend-only — uses stb_image_write. */
std::string saveScreenshot(const Video &video, const std::string &path);

} /* namespace ms0515_frontend */
