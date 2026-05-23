/*
 * Platform_win32.cpp — Windows console + signal handling.
 *
 * Stage 2 scope: just SIGINT.  Raw-mode stdin handling lands in Stage 3
 * together with the EMT hook bridge.
 */

#include "Platform.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <atomic>

namespace ms0515::cli {

namespace {

std::atomic<bool> g_quit{false};

BOOL WINAPI consoleHandler(DWORD signal)
{
    if (signal == CTRL_C_EVENT || signal == CTRL_BREAK_EVENT
        || signal == CTRL_CLOSE_EVENT)
    {
        g_quit.store(true, std::memory_order_release);
        return TRUE;
    }
    return FALSE;
}

}  /* namespace */

bool installInterruptHandler()
{
    return SetConsoleCtrlHandler(consoleHandler, TRUE) != 0;
}

bool shouldQuit()
{
    return g_quit.load(std::memory_order_acquire);
}

}  /* namespace ms0515::cli */
