/*
 * Config.hpp — re-export shims pointing at libapp.
 *
 * The schema, loader, writer, argument parser and path helpers all live
 * in libapp now (so ms0515-cli uses the same flags and same YAML).
 * This header keeps the legacy `ms0515_frontend::` names alive so the
 * rest of the frontend keeps compiling unchanged.  New code can use
 * `ms0515::app::` directly.
 */
#pragma once

#include "Platform.hpp"   /* FileDialogKind */
#include "ms0515/app/Cli.hpp"
#include "ms0515/app/Config.hpp"
#include "ms0515/app/Paths.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace ms0515_frontend {

using ms0515::app::CliArgs;
using ms0515::app::Config;
using ms0515::app::fdcUnitFor;
using ms0515::app::parseArgs;

/* Frontend-flavoured Paths — adds the FileDialogKind-aware default
 * directory helper that the SDL frontend needs.  Everything else
 * passes through to libapp. */
class Paths {
public:
    static std::string exeDir()
        { return ms0515::app::Paths::exeDir(); }

    static std::vector<std::filesystem::path> searchRoots()
        { return ms0515::app::Paths::searchRoots(); }

    static std::string timestamped(std::string_view prefix,
                                   std::string_view ext)
        { return ms0515::app::Paths::timestamped(prefix, ext); }

    static int parseNumber(const std::string &s)
        { return ms0515::app::Paths::parseNumber(s); }

    /* Suggested starting folder for a file dialog of the given kind:
     * the bundled assets folder next to the executable. */
    static std::string initialDirFor(FileDialogKind kind);
};

} /* namespace ms0515_frontend */
