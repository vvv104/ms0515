/*
 * Platform_unix.cpp — Linux / macOS platform implementation.
 */

#include "Platform.hpp"

#include <SDL.h>

#include <array>
#include <cstdio>
#include <string>
#include <unistd.h>

namespace {

/* The file pickers shell out to whatever the desktop already provides:
 * AppleScript's `choose file` through osascript on macOS (part of every
 * install), zenity or kdialog on Linux.  No extra dependency, and no
 * library to vendor — if none of them is there the dialog degrades to
 * what it was before: nothing happens and disks are mounted from the
 * command line. */

/* Run a command, return its first output line with the newline stripped.
 * `failed` reports a non-zero exit (a cancelled dialog also exits non-zero,
 * which is why the callers treat "no output" and "failed" the same way). */
std::string runCapturing(const std::string &cmd, bool *failed = nullptr)
{
    std::FILE *pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        if (failed) *failed = true;
        return {};
    }
    std::string out;
    std::array<char, 512> buf{};
    while (std::fgets(buf.data(), (int)buf.size(), pipe))
        out += buf.data();
    const int rc = pclose(pipe);
    if (failed) *failed = (rc != 0);
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r'))
        out.pop_back();
    return out;
}

/* Wrap for the shell: single quotes, with embedded quotes broken out. */
std::string shellQuote(const std::string &s)
{
    std::string q = "'";
    for (char c : s) {
        if (c == '\'') q += "'\\''";
        else           q += c;
    }
    return q + "'";
}

/* The extensions each dialog kind offers, mirroring the Windows filters. */
const char *const *extensionsFor(ms0515_frontend::FileDialogKind k, int &n)
{
    using K = ms0515_frontend::FileDialogKind;
    static const char *const disk[]  = {"dsk", "img", "hd", "rtfs"};
    static const char *const rom[]   = {"rom", "bin"};
    static const char *const state[] = {"ms0515"};
    switch (k) {
    case K::Disk:  n = 4; return disk;
    case K::Rom:   n = 2; return rom;
    case K::State: n = 1; return state;
    }
    n = 0;
    return nullptr;
}

#ifdef __APPLE__

/* Wrap for an AppleScript string literal. */
std::string asQuote(const std::string &s)
{
    std::string q = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') q += '\\';
        q += c;
    }
    return q + "\"";
}

/* `choose file` / `choose file name`, run through osascript.  A cancelled
 * dialog makes osascript exit non-zero with "User canceled. (-128)"; any
 * other failure (an OS that dislikes the type list, say) is retried once
 * without the filter so the picker still opens. */
std::string macDialog(const std::string &verb, const std::string &tail,
                      ms0515_frontend::FileDialogKind kind, bool withTypes)
{
    std::string script = "POSIX path of (" + verb + tail;
    if (withTypes) {
        int n = 0;
        const char *const *ext = extensionsFor(kind, n);
        if (n > 0) {
            script += " of type {";
            for (int i = 0; i < n; ++i) {
                if (i) script += ", ";
                script += asQuote(ext[i]);
            }
            script += "}";
        }
    }
    script += ")";

    bool failed = false;
    std::string out = runCapturing("osascript -e " + shellQuote(script) +
                                       " 2>/tmp/.ms0515-dialog-err",
                                   &failed);
    if (!failed || !out.empty())
        return out;

    /* Tell "cancelled" from "the script would not run". */
    const std::string err = runCapturing("cat /tmp/.ms0515-dialog-err 2>/dev/null");
    const bool cancelled  = err.find("-128") != std::string::npos;
    if (cancelled || !withTypes)
        return {};
    return macDialog(verb, tail, kind, /*withTypes=*/false);
}

#else /* Linux / FreeBSD */

bool haveTool(const char *path) { return access(path, X_OK) == 0; }

/* zenity and kdialog spell their filters differently; both take the
 * starting folder as a plain path. */
std::string linuxOpen(const std::string &title, const std::string &dir,
                      ms0515_frontend::FileDialogKind kind, bool save,
                      const std::string &defaultName)
{
    int n = 0;
    const char *const *ext = extensionsFor(kind, n);

    if (haveTool("/usr/bin/zenity")) {
        std::string cmd = "/usr/bin/zenity --file-selection";
        if (save) cmd += " --save --confirm-overwrite";
        cmd += " --title=" + shellQuote(title);
        std::string start = dir;
        if (!start.empty() && start.back() != '/') start += '/';
        if (save) start += defaultName;
        if (!start.empty()) cmd += " --filename=" + shellQuote(start);
        if (n > 0) {
            std::string pat = "Supported files |";
            for (int i = 0; i < n; ++i) pat += " *." + std::string(ext[i]);
            cmd += " --file-filter=" + shellQuote(pat);
            cmd += " --file-filter=" + shellQuote("All files | *");
        }
        return runCapturing(cmd + " 2>/dev/null");
    }

    if (haveTool("/usr/bin/kdialog")) {
        std::string pat;
        for (int i = 0; i < n; ++i) pat += (i ? " *." : "*.") + std::string(ext[i]);
        pat += "|Supported files";
        std::string start = dir;
        if (save && !defaultName.empty()) {
            if (!start.empty() && start.back() != '/') start += '/';
            start += defaultName;
        }
        std::string cmd = std::string("/usr/bin/kdialog ") +
                          (save ? "--getsavefilename " : "--getopenfilename ") +
                          shellQuote(start.empty() ? "." : start) + " " +
                          shellQuote(pat) + " --title " + shellQuote(title);
        return runCapturing(cmd + " 2>/dev/null");
    }

    return {};   /* no desktop picker available */
}

#endif

} /* anonymous namespace */

namespace ms0515_frontend {

void platformInit()
{
    /* No special init needed on Unix. */
}

std::string openFileDialog(SDL_Window * /*owner*/, const char *title,
                           FileDialogKind kind,
                           const std::string &initialDir)
{
    const std::string t = title ? title : "Open";
#ifdef __APPLE__
    std::string tail = " with prompt " + asQuote(t);
    if (!initialDir.empty() && access(initialDir.c_str(), F_OK) == 0)
        tail += " default location POSIX file " + asQuote(initialDir);
    return macDialog("choose file", tail, kind, /*withTypes=*/true);
#else
    return linuxOpen(t, initialDir, kind, /*save=*/false, {});
#endif
}

std::string saveFileDialog(SDL_Window * /*owner*/, const char *title,
                           const char *defaultName,
                           FileDialogKind kind,
                           const std::string &initialDir)
{
    const std::string t = title ? title : "Save";
    const std::string n = defaultName ? defaultName : "";
#ifdef __APPLE__
    std::string tail = " with prompt " + asQuote(t);
    if (!n.empty())
        tail += " default name " + asQuote(n);
    if (!initialDir.empty() && access(initialDir.c_str(), F_OK) == 0)
        tail += " default location POSIX file " + asQuote(initialDir);
    /* `choose file name` has no type list — it names a file that need
     * not exist yet. */
    return macDialog("choose file name", tail, kind, /*withTypes=*/false);
#else
    return linuxOpen(t, initialDir, kind, /*save=*/true, n);
#endif
}

std::vector<std::string> systemFontCandidates()
{
#ifdef __APPLE__
    return {
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
        "/Library/Fonts/Arial.ttf",
    };
#else /* Linux / FreeBSD */
    return {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/freefont/FreeSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
    };
#endif
}

std::vector<std::string> symbolFontCandidates()
{
#ifdef __APPLE__
    return {
        "/System/Library/Fonts/Apple Symbols.ttf",
    };
#else
    return {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/noto/NotoSansSymbols2-Regular.ttf",
    };
#endif
}

std::vector<std::string> monoFontCandidates()
{
#ifdef __APPLE__
    return {
        "/System/Library/Fonts/Menlo.ttc",
        "/System/Library/Fonts/Monaco.ttf",
        "/Library/Fonts/Courier New.ttf",
    };
#else
    return {
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
        "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
    };
#endif
}

} /* namespace ms0515_frontend */
