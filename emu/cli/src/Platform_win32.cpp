/*
 * Platform_win32.cpp — Windows console + signal handling + raw stdin.
 */

#include "Platform.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <atomic>
#include <cstdio>
#include <fcntl.h>
#include <io.h>

namespace ms0515::cli {

namespace {

std::atomic<bool> g_quit{false};

HANDLE g_stdin       = INVALID_HANDLE_VALUE;
HANDLE g_stdout      = INVALID_HANDLE_VALUE;
DWORD  g_stdinModeIn = 0;
UINT   g_prevOutCp   = 0;
UINT   g_prevInCp    = 0;
bool   g_rawSet      = false;
std::atomic<bool> g_eof{false};

BOOL WINAPI consoleHandler(DWORD signal)
{
    /* CTRL_C_EVENT and CTRL_BREAK_EVENT only fire when
     * ENABLE_PROCESSED_INPUT is set — which we deliberately clear so
     * Ctrl-C reaches the guest as СУ/C.  The handler still catches
     * window-close / logoff / shutdown so the CLI can restore the
     * terminal before the process dies. */
    if (signal == CTRL_CLOSE_EVENT || signal == CTRL_LOGOFF_EVENT
        || signal == CTRL_SHUTDOWN_EVENT)
    {
        g_quit.store(true, std::memory_order_release);
        restoreTerminal();
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

void requestQuit()
{
    g_quit.store(true, std::memory_order_release);
}

bool setTerminalRawMode()
{
    if (g_rawSet) return true;

    g_stdin  = GetStdHandle(STD_INPUT_HANDLE);
    g_stdout = GetStdHandle(STD_OUTPUT_HANDLE);
    if (g_stdin == INVALID_HANDLE_VALUE || g_stdout == INVALID_HANDLE_VALUE) {
        return false;
    }

    /* Save current input mode (we only mutate stdin's mode). */
    if (!GetConsoleMode(g_stdin, &g_stdinModeIn)) {
        /* stdin might be a pipe — treat as already-raw. */
        g_stdinModeIn = 0;
    } else {
        /* Strip line-input, echo AND processed-input.  Without
         * ENABLE_PROCESSED_INPUT, Ctrl-C lands as a regular KEY_EVENT
         * with UnicodeChar = 0x03 instead of triggering the
         * Ctrl-handler — the guest then sees СУ/C as RT-11 expects.
         * The CLI's own quit escape is Ctrl-] (0x1D); window-close
         * events still route through SetConsoleCtrlHandler regardless
         * of this flag. */
        DWORD mode = g_stdinModeIn;
        mode &= ~(DWORD)(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT
                       | ENABLE_PROCESSED_INPUT);
        SetConsoleMode(g_stdin, mode);
    }

    /* UTF-8 output + input.  The guest produces KOI-8R (we convert
     * before writing); receiving UTF-8 input lets us round-trip
     * Cyrillic typed at the host shell back to KOI-8 for .TTYIN. */
    g_prevOutCp = GetConsoleOutputCP();
    g_prevInCp  = GetConsoleCP();
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    /* Enable virtual-terminal processing on stdout so ESC sequences
     * and bare backspaces emitted by the guest's TT driver (RT-11 uses
     * ESC K = erase-to-EOL when echoing typed chars, plus BS to undo
     * pre-echoed space) are interpreted as cursor controls instead of
     * being printed as literal `?K` / `^H` glyphs. */
    DWORD outMode = 0;
    if (GetConsoleMode(g_stdout, &outMode)) {
        SetConsoleMode(g_stdout, outMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }

    /* Set stdout to BINARY mode so the C runtime doesn't translate
     * our "\r\n" pairs to "\r\r\n" on the way out (Windows CRT's
     * default text mode adds \r before every \n).  We already emit
     * a literal CR+LF where we want one, and a bare CR where we want
     * cursor-only — translation would silently break the latter and
     * double the former. */
    _setmode(_fileno(stdout), _O_BINARY);

    g_rawSet = true;
    return true;
}

void restoreTerminal()
{
    if (!g_rawSet) return;
    if (g_stdinModeIn != 0) {
        SetConsoleMode(g_stdin, g_stdinModeIn);
    }
    if (g_prevOutCp != 0) {
        SetConsoleOutputCP(g_prevOutCp);
    }
    if (g_prevInCp != 0) {
        SetConsoleCP(g_prevInCp);
    }
    g_rawSet = false;
}

size_t readStdinNonBlocking(uint8_t *buf, size_t cap)
{
    if (g_eof.load(std::memory_order_acquire) || cap == 0) return 0;
    if (g_stdin == INVALID_HANDLE_VALUE) return 0;

    DWORD ftype = GetFileType(g_stdin);

    if (ftype == FILE_TYPE_CHAR) {
        /* Console input — pull events directly via ReadConsoleInputW
         * so we can filter out focus / mouse / window-resize records
         * that the GetNumberOfConsoleInputEvents-+-ReadFile path would
         * otherwise stall on.  We translate each KEY_EVENT with
         * bKeyDown == TRUE into UTF-8 bytes; key-up events are
         * silently dropped (the guest just needs the keystroke). */
        size_t out = 0;
        DWORD  avail = 0;
        while (out < cap
               && GetNumberOfConsoleInputEvents(g_stdin, &avail)
               && avail > 0)
        {
            INPUT_RECORD rec{};
            DWORD got = 0;
            if (!ReadConsoleInputW(g_stdin, &rec, 1, &got) || got == 0) {
                break;
            }
            if (rec.EventType != KEY_EVENT) continue;
            if (!rec.Event.KeyEvent.bKeyDown) continue;
            WCHAR wc   = rec.Event.KeyEvent.uChar.UnicodeChar;
            WORD  vkey = rec.Event.KeyEvent.wVirtualKeyCode;

            /* Arrow keys deliver UnicodeChar = 0 on Windows consoles.
             * Synthesise the same ESC[A/B/C/D sequences that POSIX
             * terminals emit in raw mode, so the bridge's ESC state
             * machine sees a uniform byte stream from both platforms. */
            if (wc == 0) {
                const char *seq = nullptr;
                switch (vkey) {
                case VK_UP:    seq = "\x1B[A"; break;
                case VK_DOWN:  seq = "\x1B[B"; break;
                case VK_RIGHT: seq = "\x1B[C"; break;
                case VK_LEFT:  seq = "\x1B[D"; break;
                default: break;
                }
                if (seq != nullptr) {
                    for (const char *p = seq; *p != '\0' && out < cap; ++p)
                        buf[out++] = static_cast<uint8_t>(*p);
                    continue;
                }
            }

            /* Fall back to the physical-key VK code only when the OS
             * gave us no Unicode character at all — typically because
             * a layout doesn't define one for a particular key.  We
             * deliberately respect the active layout when it does
             * produce a char: a Russian-layout user pressing the 'D'
             * physical key gets Cyrillic в (0x0432), and the bridge
             * will route that to the MS-7004 'W' key under RUS mode.
             * The earlier "always force vkey for letters" override
             * blocked Cyrillic input entirely. */
            if (wc == 0 &&
                ((vkey >= '0' && vkey <= '9') ||
                 (vkey >= 'A' && vkey <= 'Z')))
            {
                wc = static_cast<WCHAR>(vkey);
            }
            if (wc == 0) continue;
            /* Encode the UTF-16 code unit into UTF-8 bytes for the
             * shared UTF-8 → KOI-8 decoder downstream.  Surrogate
             * pairs are uncommon enough on console input that we
             * accept a one-shot loss on lone surrogates. */
            uint32_t cp = static_cast<uint32_t>(wc);
            uint8_t enc[4];
            size_t  encLen = 0;
            if (cp < 0x80u) {
                enc[encLen++] = static_cast<uint8_t>(cp);
            } else if (cp < 0x800u) {
                enc[encLen++] = static_cast<uint8_t>(0xC0u | (cp >> 6));
                enc[encLen++] = static_cast<uint8_t>(0x80u | (cp & 0x3Fu));
            } else {
                enc[encLen++] = static_cast<uint8_t>(0xE0u | (cp >> 12));
                enc[encLen++] = static_cast<uint8_t>(0x80u | ((cp >> 6) & 0x3Fu));
                enc[encLen++] = static_cast<uint8_t>(0x80u | (cp & 0x3Fu));
            }
            for (size_t i = 0; i < encLen && out < cap; ++i) {
                buf[out++] = enc[i];
            }
        }
        return out;
    }

    DWORD want = 0;
    if (ftype == FILE_TYPE_PIPE) {
        DWORD pending = 0;
        if (!PeekNamedPipe(g_stdin, nullptr, 0, nullptr, &pending, nullptr)
            || pending == 0)
        {
            if (GetLastError() == ERROR_BROKEN_PIPE) {
                g_eof.store(true, std::memory_order_release);
            }
            return 0;
        }
        want = static_cast<DWORD>(pending < cap ? pending : cap);
    } else if (ftype == FILE_TYPE_DISK) {
        want = static_cast<DWORD>(cap > 4096 ? 4096 : cap);
    } else {
        return 0;
    }

    DWORD n = 0;
    if (!ReadFile(g_stdin, buf, want, &n, nullptr)) {
        g_eof.store(true, std::memory_order_release);
        return 0;
    }
    if (n == 0) {
        g_eof.store(true, std::memory_order_release);
    }
    return n;
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
