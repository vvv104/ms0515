/*
 * GdbStub.cpp — GDB Remote Serial Protocol server implementation.
 *
 * Packet format:    $payload#cc
 *   where cc is two hex digits, sum of payload bytes mod 256.
 * After receiving a packet the host expects '+' (ack) or '-' (nak).
 * We always ack and never request resend.
 *
 * The PDP-11 register file as exchanged with GDB consists of nine
 * 16-bit registers (r0..r5, sp, pc, ps).  Each is encoded little-endian
 * as four hex digits, so the total size of a 'g' reply is 36 hex chars.
 */

#include "ms0515/GdbStub.hpp"
#include "ms0515/Debugger.hpp"
#include "EmulatorInternal.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

#if defined(_WIN32)
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  pragma comment(lib, "ws2_32.lib")
   using socket_t = SOCKET;
#  define MS0515_INVALID_SOCKET INVALID_SOCKET
#  define MS0515_CLOSESOCKET closesocket
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <unistd.h>
#  include <arpa/inet.h>
   using socket_t = int;
#  define MS0515_INVALID_SOCKET (-1)
#  define MS0515_CLOSESOCKET ::close
#endif

namespace ms0515 {

namespace {

/* ── Hex helpers ──────────────────────────────────────────────────────── */

int hexDigit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

bool parseHex(std::string_view s, uint32_t &out)
{
    out = 0;
    if (s.empty()) return false;
    for (char c : s) {
        int d = hexDigit(c);
        if (d < 0) return false;
        out = (out << 4) | static_cast<uint32_t>(d);
    }
    return true;
}

void appendHexByte(std::string &out, uint8_t b)
{
    static const char *kHex = "0123456789abcdef";
    out += kHex[(b >> 4) & 0xF];
    out += kHex[b & 0xF];
}

void appendRegister16(std::string &out, uint16_t value)
{
    appendHexByte(out, static_cast<uint8_t>(value & 0xFF));
    appendHexByte(out, static_cast<uint8_t>((value >> 8) & 0xFF));
}

bool parseRegister16(std::string_view hex, uint16_t &out)
{
    if (hex.size() != 4) return false;
    int a = hexDigit(hex[0]), b = hexDigit(hex[1]);
    int c = hexDigit(hex[2]), d = hexDigit(hex[3]);
    if ((a | b | c | d) < 0) return false;
    uint16_t lo = static_cast<uint16_t>((a << 4) | b);
    uint16_t hi = static_cast<uint16_t>((c << 4) | d);
    out = static_cast<uint16_t>((hi << 8) | lo);
    return true;
}

uint8_t checksum(std::string_view s)
{
    unsigned sum = 0;
    for (char c : s) sum += static_cast<uint8_t>(c);
    return static_cast<uint8_t>(sum & 0xFF);
}

std::string framePacket(std::string_view payload)
{
    std::string out;
    out.reserve(payload.size() + 4);
    out += '$';
    out += payload;
    out += '#';
    appendHexByte(out, checksum(payload));
    return out;
}

bool parseAddrLen(std::string_view body, uint32_t &addr, uint32_t &len)
{
    auto comma = body.find(',');
    if (comma == std::string_view::npos) return false;
    return parseHex(body.substr(0, comma), addr) &&
           parseHex(body.substr(comma + 1), len);
}

bool sendAll(socket_t s, const char *buf, size_t len)
{
    while (len > 0) {
        int n = ::send(s, buf, static_cast<int>(len), 0);
        if (n <= 0) return false;
        buf += n;
        len -= static_cast<size_t>(n);
    }
    return true;
}

/* Map a GDB register index (0..8) to the corresponding 16-bit value
 * in the CPU state.  Returns false for indices out of range. */
bool readGdbRegister(const ms0515_cpu_t &cpu, int index, uint16_t &out)
{
    if (index >= 0 && index <= 7) {
        out = cpu.r[index];
        return true;
    }
    if (index == 8) {
        out = cpu.psw;
        return true;
    }
    return false;
}

bool writeGdbRegister(ms0515_cpu_t &cpu, int index, uint16_t value)
{
    if (index >= 0 && index <= 7) {
        cpu.r[index] = value;
        return true;
    }
    if (index == 8) {
        cpu.psw = value;
        return true;
    }
    return false;
}

constexpr int kGdbRegCount = 9;

} /* anonymous namespace */

/* ── GdbStub ──────────────────────────────────────────────────────────── */

GdbStub::GdbStub(Debugger &dbg) : dbg_(dbg) {}

std::string GdbStub::processPacket(std::string_view payload)
{
    if (payload.empty()) return "";

    char cmd = payload[0];
    std::string_view body = payload.substr(1);

    switch (cmd) {
    case '?': return handleHaltReason();
    case 'g': return handleReadRegisters();
    case 'G': return handleWriteRegisters(body);
    case 'p': return handleReadRegister(body);
    case 'P': return handleWriteRegister(body);
    case 'm': return handleReadMemory(body);
    case 'M': return handleWriteMemory(body);
    case 's': return handleStep();
    case 'c': return handleContinue();
    case 'Z': return handleInsertBreakpoint(body);
    case 'z': return handleRemoveBreakpoint(body);
    case 'q': return handleQuery(body);
    case 'k':
        killed_         = true;
        stopRequested_  = true;
        return "";
    case 'D':
        return "OK";
    default:
        return "";  /* unsupported */
    }
}

/* ── Stop replies ─────────────────────────────────────────────────────── */

std::string GdbStub::makeStopReply()
{
    /* Signal 5 = SIGTRAP — used for breakpoint, single-step, halt. */
    return "T05";
}

std::string GdbStub::handleHaltReason()
{
    return makeStopReply();
}

/* ── Registers ────────────────────────────────────────────────────────── */

std::string GdbStub::handleReadRegisters()
{
    const ms0515_cpu_t &cpu = internal::cpu(dbg_.emulator());
    std::string out;
    out.reserve(kGdbRegCount * 4);
    for (int i = 0; i < kGdbRegCount; ++i) {
        uint16_t v = 0;
        readGdbRegister(cpu, i, v);
        appendRegister16(out, v);
    }
    return out;
}

std::string GdbStub::handleWriteRegisters(std::string_view body)
{
    if (body.size() < kGdbRegCount * 4) return "E01";
    ms0515_cpu_t &cpu = internal::cpu(dbg_.emulator());
    for (int i = 0; i < kGdbRegCount; ++i) {
        uint16_t v = 0;
        if (!parseRegister16(body.substr(i * 4, 4), v)) return "E01";
        writeGdbRegister(cpu, i, v);
    }
    return "OK";
}

std::string GdbStub::handleReadRegister(std::string_view body)
{
    uint32_t idx = 0;
    if (!parseHex(body, idx)) return "E01";
    uint16_t v = 0;
    if (!readGdbRegister(internal::cpu(dbg_.emulator()), static_cast<int>(idx), v)) {
        return "E01";
    }
    std::string out;
    appendRegister16(out, v);
    return out;
}

std::string GdbStub::handleWriteRegister(std::string_view body)
{
    auto eq = body.find('=');
    if (eq == std::string_view::npos) return "E01";
    uint32_t idx = 0;
    if (!parseHex(body.substr(0, eq), idx)) return "E01";
    uint16_t v = 0;
    if (!parseRegister16(body.substr(eq + 1), v)) return "E01";
    if (!writeGdbRegister(internal::cpu(dbg_.emulator()),
                          static_cast<int>(idx), v)) {
        return "E01";
    }
    return "OK";
}

/* ── Memory ───────────────────────────────────────────────────────────── */

std::string GdbStub::handleReadMemory(std::string_view body)
{
    uint32_t addr = 0, len = 0;
    if (!parseAddrLen(body, addr, len)) return "E01";

    Emulator &emu = dbg_.emulator();
    std::string out;
    out.reserve(len * 2);
    for (uint32_t i = 0; i < len; ++i) {
        uint8_t b = emu.readByte(static_cast<uint16_t>(addr + i));
        appendHexByte(out, b);
    }
    return out;
}

std::string GdbStub::handleWriteMemory(std::string_view body)
{
    auto colon = body.find(':');
    if (colon == std::string_view::npos) return "E01";

    uint32_t addr = 0, len = 0;
    if (!parseAddrLen(body.substr(0, colon), addr, len)) return "E01";

    std::string_view hex = body.substr(colon + 1);
    if (hex.size() < len * 2) return "E01";

    Emulator &emu = dbg_.emulator();
    for (uint32_t i = 0; i < len; ++i) {
        int hi = hexDigit(hex[i * 2]);
        int lo = hexDigit(hex[i * 2 + 1]);
        if ((hi | lo) < 0) return "E01";
        emu.writeByte(static_cast<uint16_t>(addr + i),
                      static_cast<uint8_t>((hi << 4) | lo));
    }
    return "OK";
}

/* ── Execution control ────────────────────────────────────────────────── */

std::string GdbStub::handleStep()
{
    dbg_.stepInstruction();
    return makeStopReply();
}

std::string GdbStub::handleContinue()
{
    dbg_.run(/*maxInstructions=*/0);
    return makeStopReply();
}

/* ── Breakpoints ──────────────────────────────────────────────────────── */

std::string GdbStub::handleInsertBreakpoint(std::string_view body)
{
    /* Z type,addr,kind   — we only support type 0 (software bp). */
    if (body.empty() || body[0] != '0') return "";
    auto comma1 = body.find(',', 1);
    if (comma1 == std::string_view::npos || comma1 + 1 >= body.size()) {
        return "E01";
    }
    auto comma2 = body.find(',', comma1 + 1);
    std::string_view addrStr =
        body.substr(comma1 + 1,
                    (comma2 == std::string_view::npos)
                        ? std::string_view::npos
                        : comma2 - (comma1 + 1));
    uint32_t addr = 0;
    if (!parseHex(addrStr, addr)) return "E01";
    dbg_.addBreakpoint(static_cast<uint16_t>(addr));
    return "OK";
}

std::string GdbStub::handleRemoveBreakpoint(std::string_view body)
{
    if (body.empty() || body[0] != '0') return "";
    auto comma1 = body.find(',', 1);
    if (comma1 == std::string_view::npos || comma1 + 1 >= body.size()) {
        return "E01";
    }
    auto comma2 = body.find(',', comma1 + 1);
    std::string_view addrStr =
        body.substr(comma1 + 1,
                    (comma2 == std::string_view::npos)
                        ? std::string_view::npos
                        : comma2 - (comma1 + 1));
    uint32_t addr = 0;
    if (!parseHex(addrStr, addr)) return "E01";
    dbg_.removeBreakpoint(static_cast<uint16_t>(addr));
    return "OK";
}

/* ── Query packets ────────────────────────────────────────────────────── */

std::string GdbStub::handleQuery(std::string_view body)
{
    if (body.rfind("Supported", 0) == 0) {
        return "PacketSize=4000;swbreak+;hwbreak-";
    }
    if (body == "Attached") {
        return "1";
    }
    if (body == "C") {
        /* Current thread ID — we have a single thread. */
        return "QC1";
    }
    if (body == "fThreadInfo") {
        return "m1";
    }
    if (body == "sThreadInfo") {
        return "l";
    }
    return "";
}

/* ── TCP transport ────────────────────────────────────────────────────── */

bool GdbStub::serve(int port)
{
#if defined(_WIN32)
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        return false;
    }
#endif

    socket_t listener = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listener == MS0515_INVALID_SOCKET) {
#if defined(_WIN32)
        WSACleanup();
#endif
        return false;
    }

    int yes = 1;
    ::setsockopt(listener, SOL_SOCKET, SO_REUSEADDR,
                 reinterpret_cast<const char *>(&yes), sizeof yes);

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port        = htons(static_cast<uint16_t>(port));

    if (::bind(listener,
               reinterpret_cast<sockaddr *>(&addr), sizeof addr) != 0 ||
        ::listen(listener, 1) != 0) {
        MS0515_CLOSESOCKET(listener);
#if defined(_WIN32)
        WSACleanup();
#endif
        return false;
    }

    socket_t client = ::accept(listener, nullptr, nullptr);
    MS0515_CLOSESOCKET(listener);
    if (client == MS0515_INVALID_SOCKET) {
#if defined(_WIN32)
        WSACleanup();
#endif
        return false;
    }

    /* Packet read loop.  Buffers raw bytes from the socket and extracts
     * complete $...#cc packets. */
    std::string rxBuffer;
    char        chunk[1024];

    while (!stopRequested_.load()) {
        int n = ::recv(client, chunk, sizeof chunk, 0);
        if (n <= 0) break;
        rxBuffer.append(chunk, static_cast<size_t>(n));

        /* Process every complete packet currently in the buffer. */
        while (true) {
            /* Discard ack/nak bytes. */
            while (!rxBuffer.empty() &&
                   (rxBuffer.front() == '+' || rxBuffer.front() == '-')) {
                rxBuffer.erase(0, 1);
            }
            if (rxBuffer.empty()) break;

            /* Ctrl-C from GDB shows up as a raw 0x03 byte (no framing). */
            if (rxBuffer.front() == '\x03') {
                rxBuffer.erase(0, 1);
                dbg_.requestStop();
                continue;
            }

            if (rxBuffer.front() != '$') {
                /* Junk — drop it. */
                rxBuffer.erase(0, 1);
                continue;
            }
            auto hash = rxBuffer.find('#', 1);
            if (hash == std::string::npos || hash + 2 >= rxBuffer.size()) {
                /* Need more bytes for a complete packet. */
                break;
            }

            std::string_view payload(rxBuffer.data() + 1, hash - 1);
            std::string reply = processPacket(payload);
            rxBuffer.erase(0, hash + 3);

            /* Always ack the inbound packet. */
            if (!sendAll(client, "+", 1)) goto done;

            std::string framed = framePacket(reply);
            if (!sendAll(client, framed.data(), framed.size())) goto done;

            if (killed_) goto done;
        }
    }

done:
    MS0515_CLOSESOCKET(client);
#if defined(_WIN32)
    WSACleanup();
#endif
    return true;
}

} /* namespace ms0515 */
