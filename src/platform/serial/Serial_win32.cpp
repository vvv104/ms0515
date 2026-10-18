/*
 * Serial_win32.cpp — SerialPort over the Win32 communications API.
 */
#include "SerialPort.hpp"

#include <algorithm>
#include <utility>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace ms0515::serial {

namespace {

std::string lastError()
{
    const DWORD code = GetLastError();
    char text[256] = {};
    const DWORD n = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                   nullptr, code, 0, text, sizeof text, nullptr);
    std::string s(text, n);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == '.'))
        s.pop_back();
    return s.empty() ? "error " + std::to_string(code) : s;
}

class WinPort final : public SerialPort {
public:
    WinPort(std::string name, HANDLE handle) : SerialPort(std::move(name)), handle_(handle) {}
    ~WinPort() override { CloseHandle(handle_); }

    WinPort(const WinPort &)            = delete;
    WinPort &operator=(const WinPort &) = delete;

    int read(uint8_t *buffer, std::size_t max) override
    {
        DWORD got = 0;
        const DWORD want = static_cast<DWORD>(std::min<std::size_t>(max, 4096));
        if (!ReadFile(handle_, buffer, want, &got, nullptr)) return -1;
        return static_cast<int>(got);
    }

    /* A byte nobody takes - the other end of the line not there yet -
     * times out and is dropped, as on a cable with nothing on it; only
     * an error of the port is a failure. */
    bool write(uint8_t byte) override
    {
        DWORD put = 0;
        return WriteFile(handle_, &byte, 1, &put, nullptr) != 0;
    }

private:
    HANDLE handle_;
};

} // namespace

std::unique_ptr<SerialPort> SerialPort::open(const std::string &name, int baud,
                                             std::string &error)
{
    /* \\.\COMn reaches COM10 and above as well as COM1..COM9. */
    const std::string path = name.rfind("\\\\.\\", 0) == 0 ? name : "\\\\.\\" + name;
    HANDLE h = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                           OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        error = name + ": " + lastError();
        return nullptr;
    }
    DCB dcb{};
    dcb.DCBlength = sizeof dcb;
    if (!GetCommState(h, &dcb)) {
        error = name + ": not a serial port (" + lastError() + ")";
        CloseHandle(h);
        return nullptr;
    }
    dcb.BaudRate     = static_cast<DWORD>(baud);
    dcb.ByteSize     = 8;
    dcb.Parity       = NOPARITY;
    dcb.StopBits     = ONESTOPBIT;
    dcb.fBinary      = TRUE;
    dcb.fParity      = FALSE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDtrControl  = DTR_CONTROL_ENABLE;
    dcb.fRtsControl  = RTS_CONTROL_ENABLE;
    dcb.fOutX        = FALSE;
    dcb.fInX         = FALSE;
    COMMTIMEOUTS timeouts{};
    timeouts.ReadIntervalTimeout        = MAXDWORD; /* read returns at once */
    timeouts.WriteTotalTimeoutConstant  = 10;       /* a write never hangs the caller */
    if (!SetCommState(h, &dcb) || !SetCommTimeouts(h, &timeouts)) {
        error = name + ": " + lastError();
        CloseHandle(h);
        return nullptr;
    }
    error.clear();
    return std::make_unique<WinPort>(name, h);
}

std::vector<std::string> listPorts()
{
    /* Every serial port the system knows, real or virtual, is a value
     * under SERIALCOMM, its data the COMn name. */
    std::vector<std::string> ports;
    HKEY key = nullptr;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DEVICEMAP\\SERIALCOMM", 0,
                      KEY_READ, &key) != ERROR_SUCCESS)
        return ports;
    for (DWORD i = 0;; ++i) {
        char value[256];
        DWORD valueLen = sizeof value;
        BYTE data[256];
        DWORD dataLen = sizeof data;
        DWORD type = 0;
        const LONG r = RegEnumValueA(key, i, value, &valueLen, nullptr, &type, data, &dataLen);
        if (r == ERROR_NO_MORE_ITEMS) break;
        if (r != ERROR_SUCCESS || type != REG_SZ || dataLen == 0) continue;
        ports.emplace_back(reinterpret_cast<const char *>(data),
                           strnlen(reinterpret_cast<const char *>(data), dataLen));
    }
    RegCloseKey(key);
    std::sort(ports.begin(), ports.end(), [](const std::string &a, const std::string &b) {
        return a.size() != b.size() ? a.size() < b.size() : a < b;
    });
    return ports;
}

} // namespace ms0515::serial
