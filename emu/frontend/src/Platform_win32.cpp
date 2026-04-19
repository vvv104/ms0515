/*
 * Platform_win32.cpp — Windows platform implementation.
 */

#include "Platform.hpp"

#include <SDL.h>
#include <SDL_syswm.h>

#include <windows.h>
#include <commdlg.h>

namespace ms0515_frontend {

void platformInit()
{
    SetConsoleOutputCP(65001);
}

std::string openFileDialog(SDL_Window *owner, const char *title)
{
    HWND hwnd = nullptr;
    SDL_SysWMinfo wmi;
    SDL_VERSION(&wmi.version);
    if (SDL_GetWindowWMInfo(owner, &wmi))
        hwnd = wmi.info.win.window;

    wchar_t buffer[MAX_PATH] = L"";
    wchar_t titleW[128]      = L"";
    if (title)
        MultiByteToWideChar(CP_UTF8, 0, title, -1, titleW,
                            sizeof titleW / sizeof titleW[0]);

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner   = hwnd;
    ofn.lpstrFilter =
        L"Disk images (*.dsk;*.img)\0*.dsk;*.img\0All files (*.*)\0*.*\0";
    ofn.lpstrFile   = buffer;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrTitle  = title ? titleW : nullptr;
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST |
                      OFN_NOCHANGEDIR;

    if (!GetOpenFileNameW(&ofn))
        return {};

    char utf8[MAX_PATH * 4] = "";
    WideCharToMultiByte(CP_UTF8, 0, buffer, -1, utf8, sizeof utf8,
                        nullptr, nullptr);
    return std::string(utf8);
}

std::string saveFileDialog(SDL_Window *owner, const char *title,
                           const char *defaultName)
{
    HWND hwnd = nullptr;
    SDL_SysWMinfo wmi;
    SDL_VERSION(&wmi.version);
    if (SDL_GetWindowWMInfo(owner, &wmi))
        hwnd = wmi.info.win.window;

    wchar_t buffer[MAX_PATH] = L"";
    if (defaultName)
        MultiByteToWideChar(CP_UTF8, 0, defaultName, -1, buffer, MAX_PATH);

    wchar_t titleW[128] = L"";
    if (title)
        MultiByteToWideChar(CP_UTF8, 0, title, -1, titleW,
                            sizeof titleW / sizeof titleW[0]);

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner   = hwnd;
    ofn.lpstrFilter =
        L"Snapshots (*.ms0515)\0*.ms0515\0All files (*.*)\0*.*\0";
    ofn.lpstrFile   = buffer;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrTitle  = title ? titleW : nullptr;
    ofn.Flags       = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
    ofn.lpstrDefExt = L"ms0515";

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
