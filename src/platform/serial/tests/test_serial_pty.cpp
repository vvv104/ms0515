/*
 * test_serial_pty.cpp — a serial line both ways, on POSIX: a
 * pseudo-terminal pair, the port opened by the slave's name, the master
 * the device at the other end.
 */
#include <doctest/doctest.h>

#include "SerialPort.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <unistd.h>

using ms0515::serial::SerialPort;

namespace {

/* What came from `read` within a second, until `want` bytes. */
template <typename Read>
std::vector<uint8_t> collect(Read read, std::size_t want)
{
    std::vector<uint8_t> got;
    for (int i = 0; i < 100 && got.size() < want; ++i) {
        uint8_t buf[64];
        const int n = read(buf, sizeof buf);
        REQUIRE(n >= 0);
        got.insert(got.end(), buf, buf + n);
        if (got.size() < want) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return got;
}

} // namespace

TEST_CASE("bytes go both ways through a port opened by name") {
    const int master = posix_openpt(O_RDWR | O_NOCTTY);
    REQUIRE(master >= 0);
    REQUIRE(grantpt(master) == 0);
    REQUIRE(unlockpt(master) == 0);
    const char *slave = ptsname(master);
    REQUIRE(slave != nullptr);
    fcntl(master, F_SETFL, fcntl(master, F_GETFL) | O_NONBLOCK);

    std::string error;
    auto port = SerialPort::open(slave, 4800, error);
    REQUIRE_MESSAGE(port != nullptr, error);
    CHECK(port->name() == slave);

    /* Nothing has come yet: read() returns at once with none. */
    uint8_t none[4];
    CHECK(port->read(none, sizeof none) == 0);

    /* The device to the port: the keyboard's answer to the ID probe. */
    const uint8_t answer[] = {0x01, 0x00};
    REQUIRE(::write(master, answer, sizeof answer) == static_cast<ssize_t>(sizeof answer));
    const auto got = collect([&](uint8_t *b, std::size_t n) { return port->read(b, n); }, 2);
    CHECK(got == std::vector<uint8_t>{0x01, 0x00});

    /* The port to the device: the probe itself, and a byte with the top bit. */
    CHECK(port->write(0xAB));
    CHECK(port->write(0x00));
    CHECK(port->write(0xFF));
    const auto sent = collect([&](uint8_t *b, std::size_t n) {
        const ssize_t r = ::read(master, b, n);
        return r < 0 ? 0 : static_cast<int>(r);
    }, 3);
    CHECK(sent == std::vector<uint8_t>{0xAB, 0x00, 0xFF});

    port.reset();
    ::close(master);
}
