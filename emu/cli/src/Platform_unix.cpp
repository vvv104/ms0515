/*
 * Platform_unix.cpp — POSIX signal handling + raw stdin via termios.
 */

#include "Platform.hpp"

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

namespace ms0515::cli {

namespace {

std::atomic<bool> g_quit{false};
std::atomic<bool> g_eof{false};
struct termios g_orig{};
bool g_rawSet = false;

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

void requestQuit()
{
    g_quit.store(true, std::memory_order_release);
}

bool setTerminalRawMode()
{
    if (g_rawSet) return true;
    if (!isatty(STDIN_FILENO)) {
        /* Already a pipe / file — nothing to do, just remember we
         * "set" it so restoreTerminal() is harmless. */
        g_rawSet = true;
        return true;
    }
    if (tcgetattr(STDIN_FILENO, &g_orig) != 0) return false;

    struct termios raw = g_orig;
    cfmakeraw(&raw);
    /* ISIG OFF so Ctrl-C lands as a literal 0x03 byte for the guest
     * (RT-11 СУ/C interrupt).  cfmakeraw clears ISIG by default —
     * stating it explicitly so the intent is obvious.  The CLI's own
     * quit escape is Ctrl-]  (handled at the bridge layer). */
    raw.c_lflag &= ~static_cast<tcflag_t>(ISIG);
    /* CRLF translation: leave OPOST off (raw) — we manage line
     * endings explicitly in the .PRINT hook. */
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0) return false;

    g_rawSet = true;
    return true;
}

void restoreTerminal()
{
    if (!g_rawSet) return;
    if (isatty(STDIN_FILENO)) {
        tcsetattr(STDIN_FILENO, TCSANOW, &g_orig);
    }
    g_rawSet = false;
}

size_t readStdinNonBlocking(uint8_t *buf, size_t cap)
{
    if (g_eof.load(std::memory_order_acquire) || cap == 0) return 0;

    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(STDIN_FILENO, &rfds);
    struct timeval tv{};
    tv.tv_sec  = 0;
    tv.tv_usec = 0;
    int sel = select(STDIN_FILENO + 1, &rfds, nullptr, nullptr, &tv);
    if (sel <= 0) return 0;
    if (!FD_ISSET(STDIN_FILENO, &rfds)) return 0;

    ssize_t n = read(STDIN_FILENO, buf, cap);
    if (n < 0) {
        if (errno == EAGAIN || errno == EINTR) return 0;
        g_eof.store(true, std::memory_order_release);
        return 0;
    }
    if (n == 0) {
        g_eof.store(true, std::memory_order_release);
        return 0;
    }
    return static_cast<size_t>(n);
}

bool isStdinEof()
{
    return g_eof.load(std::memory_order_acquire);
}

void writeStdout(const char *data, size_t n)
{
    if (n == 0) return;
    std::fwrite(data, 1, n, stdout);
}

void flushStdout()
{
    std::fflush(stdout);
}

}  /* namespace ms0515::cli */
