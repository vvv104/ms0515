/*
 * Assets.hpp — ROM/disk discovery, image validation, screenshot saving.
 *
 * Helpers that touch the assets/ folder (or the user-selected files)
 * but don't belong in Config or Cli.  Live separately from main.cpp
 * just to keep that file from drowning.
 */
#pragma once

#include <optional>
#include <string>
#include <vector>

namespace ms0515_frontend {

class Video;

/* Search a handful of likely locations for the default ROM.  The
 * working directory may be anywhere (double-clicked .exe, IDE,
 * shortcut), so we try several candidates relative to the
 * executable's own path.  Returns the empty string if nothing is
 * found. */
std::string findDefaultRom();

/* All .rom files under assets/rom/ next to the exe and in cwd, sorted,
 * deduplicated.  Used to populate the File → ROM submenu. */
std::vector<std::string> discoverRoms();

/* Validate that `path` points to a 409600-byte single-side disk
 * image; return std::nullopt on success, a human-readable reason on
 * failure (wrong size, double-sided file, stat error). */
std::optional<std::string> validateSingleSideImage(const std::string &path);

/* Validate that `path` is a 819200-byte double-sided disk image. */
std::optional<std::string> validateDoubleSidedImage(const std::string &path);

/* Save a PNG screenshot of the emulated framebuffer.  When `path` is
 * empty, auto-generates a timestamped filename next to the exe.
 * Returns the resulting file path on success, an empty string on
 * failure. */
std::string saveScreenshot(const Video &video, const std::string &path);

} /* namespace ms0515_frontend */
