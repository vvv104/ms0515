/*
 * Platform_win32.cpp — Windows platform implementation.
 */

#include "Platform.hpp"

#include <SDL.h>
#include <SDL_syswm.h>

#include <windows.h>
#include <commdlg.h>
#include <fcntl.h>
#include <io.h>

#include <cstdio>

namespace {

/* Filter string for OPENFILENAMEW, selected by dialog kind.  The double-
 * NUL terminator (implicit in wide-string literals) is required by the
 * Win32 API to end the list. */
const wchar_t *filterFor(ms0515_frontend::FileDialogKind k)
{
    using K = ms0515_frontend::FileDialogKind;
    switch (k) {
    case K::Disk:
        return L"Disk images (*.dsk;*.img)\0*.dsk;*.img\0"
               L"All files (*.*)\0*.*\0";
    case K::Rom:
        return L"ROM images (*.rom;*.bin)\0*.rom;*.bin\0"
               L"All files (*.*)\0*.*\0";
    case K::State:
        return L"Snapshots (*.ms0515)\0*.ms0515\0"
               L"All files (*.*)\0*.*\0";
    }
    return L"All files (*.*)\0*.*\0";
}

/* Convert UTF-8 initialDir → wide string for Win32 API. */
std::wstring toWide(const std::string &s)
{
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(),
                                nullptr, 0);
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(),
                        out.data(), n);
    return out;
}

HWND hwndOf(SDL_Window *owner)
{
    SDL_SysWMinfo wmi;
    SDL_VERSION(&wmi.version);
    if (SDL_GetWindowWMInfo(owner, &wmi))
        return wmi.info.win.window;
    return nullptr;
}

} /* anonymous namespace */

namespace ms0515_frontend {

void platformInit()
{
    SetConsoleOutputCP(65001);
}

namespace {

/* Bind a Win32 standard handle to a C runtime FILE*.  Used by
 * attachConsoleForOutput() to wire up stdout/stderr when the OS
 * gave us valid handles but the C runtime (in GUI-subsystem mode)
 * didn't initialise the FILE* slots itself. */
void rebindToHandle(HANDLE h, FILE *cstream, const char *mode)
{
    if (h == nullptr || h == INVALID_HANDLE_VALUE) return;
    int flags = (mode[0] == 'r') ? _O_RDONLY : _O_WRONLY;
    int fd = _open_osfhandle(reinterpret_cast<intptr_t>(h), flags);
    if (fd < 0) return;
    FILE *fp = _fdopen(fd, mode);
    if (fp == nullptr) { _close(fd); return; }
    /* MSVC-supported way of swapping out a FILE* without losing
     * client pointers to `stdout` / `stderr`. */
    *cstream = *fp;
    setvbuf(cstream, nullptr, _IONBF, 0);
}

}  /* anonymous namespace */

void attachConsoleForOutput()
{
    /* Idempotent — multiple calls would otherwise keep re-binding
     * stdout / leaking file descriptors. */
    static bool s_done = false;
    if (s_done) return;
    s_done = true;

    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD  ftypeOut = (hOut && hOut != INVALID_HANDLE_VALUE)
                      ? GetFileType(hOut) : FILE_TYPE_UNKNOWN;

    const bool stdoutUsable =
        ftypeOut == FILE_TYPE_DISK ||
        ftypeOut == FILE_TYPE_PIPE ||
        ftypeOut == FILE_TYPE_CHAR;

    if (stdoutUsable) {
        rebindToHandle(hOut, stdout, "w");
        rebindToHandle(GetStdHandle(STD_ERROR_HANDLE), stderr, "w");
        if (ftypeOut == FILE_TYPE_CHAR) SetConsoleOutputCP(CP_UTF8);
        return;
    }

    /* No inherited stdio — attach the parent process's console, or
     * spin up a fresh window if there isn't one. */
    if (!AttachConsole(ATTACH_PARENT_PROCESS) && !AllocConsole())
        return;
    SetConsoleOutputCP(CP_UTF8);
    FILE *dummy = nullptr;
    (void)freopen_s(&dummy, "CONOUT$", "w", stdout);
    (void)freopen_s(&dummy, "CONOUT$", "w", stderr);
}

std::string openFileDialog(SDL_Window *owner, const char *title,
                           FileDialogKind kind,
                           const std::string &initialDir)
{
    wchar_t buffer[MAX_PATH] = L"";
    wchar_t titleW[128]      = L"";
    if (title)
        MultiByteToWideChar(CP_UTF8, 0, title, -1, titleW,
                            sizeof titleW / sizeof titleW[0]);

    std::wstring initDir = toWide(initialDir);

    OPENFILENAMEW ofn{};
    ofn.lStructSize     = sizeof ofn;
    ofn.hwndOwner       = hwndOf(owner);
    ofn.lpstrFilter     = filterFor(kind);
    ofn.lpstrFile       = buffer;
    ofn.nMaxFile        = MAX_PATH;
    ofn.lpstrTitle      = title ? titleW : nullptr;
    ofn.lpstrInitialDir = initDir.empty() ? nullptr : initDir.c_str();
    ofn.Flags           = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST |
                          OFN_NOCHANGEDIR;

    if (!GetOpenFileNameW(&ofn))
        return {};

    char utf8[MAX_PATH * 4] = "";
    WideCharToMultiByte(CP_UTF8, 0, buffer, -1, utf8, sizeof utf8,
                        nullptr, nullptr);
    return std::string(utf8);
}

std::string saveFileDialog(SDL_Window *owner, const char *title,
                           const char *defaultName,
                           FileDialogKind kind,
                           const std::string &initialDir)
{
    wchar_t buffer[MAX_PATH] = L"";
    if (defaultName)
        MultiByteToWideChar(CP_UTF8, 0, defaultName, -1, buffer, MAX_PATH);

    wchar_t titleW[128] = L"";
    if (title)
        MultiByteToWideChar(CP_UTF8, 0, title, -1, titleW,
                            sizeof titleW / sizeof titleW[0]);

    std::wstring initDir = toWide(initialDir);

    OPENFILENAMEW ofn{};
    ofn.lStructSize     = sizeof ofn;
    ofn.hwndOwner       = hwndOf(owner);
    ofn.lpstrFilter     = filterFor(kind);
    ofn.lpstrFile       = buffer;
    ofn.nMaxFile        = MAX_PATH;
    ofn.lpstrTitle      = title ? titleW : nullptr;
    ofn.lpstrInitialDir = initDir.empty() ? nullptr : initDir.c_str();
    ofn.Flags           = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
    ofn.lpstrDefExt     = kind == FileDialogKind::State ? L"ms0515" : nullptr;

    if (!GetSaveFileNameW(&ofn))
        return {};

    char utf8[MAX_PATH * 4] = "";
    WideCharToMultiByte(CP_UTF8, 0, buffer, -1, utf8, sizeof utf8,
                        nullptr, nullptr);
    return std::string(utf8);
}

std::vector<std::string> systemFontCandidates()
{
    return {
        "C:\\Windows\\Fonts\\segoeui.ttf",
        "C:\\Windows\\Fonts\\tahoma.ttf",
        "C:\\Windows\\Fonts\\arial.ttf",
    };
}

std::vector<std::string> symbolFontCandidates()
{
    return {
        "C:\\Windows\\Fonts\\seguisym.ttf",
    };
}

std::vector<std::string> monoFontCandidates()
{
    return {
        "C:\\Windows\\Fonts\\consola.ttf",   /* Consolas — has Cyrillic */
        "C:\\Windows\\Fonts\\lucon.ttf",     /* Lucida Console */
        "C:\\Windows\\Fonts\\cour.ttf",      /* Courier New */
    };
}

} /* namespace ms0515_frontend */
