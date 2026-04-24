/*
 * Platform_win32.cpp — Windows platform implementation.
 */

#include "Platform.hpp"

#include <SDL.h>
#include <SDL_syswm.h>

#include <windows.h>
#include <commdlg.h>

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

} /* namespace ms0515_frontend */
