/*
 * Platform_unix.cpp — POSIX signal handling.
 *
 * Stage 2 scope: just SIGINT.  Raw-mode stdin handling lands in Stage 3
 * together with the EMT hook bridge.
 */

#include "Platform.hpp"

#include <atomic>
#include <csignal>

namespace ms0515::cli {

namespace {

std::atomic<bool> g_quit{false};

void handleSigint(int /*signo*/)
{
    g_quit.store(true, std::memory_order_release);
}

}  /* namespace */

bool installInterruptHandler()
{
    struct sigaction sa{};
    sa.sa_handler = handleSigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    return sigaction(SIGINT, &sa, nullptr) == 0;
}

bool shouldQuit()
{
    return g_quit.load(std::memory_order_acquire);
}

}  /* namespace ms0515::cli */
