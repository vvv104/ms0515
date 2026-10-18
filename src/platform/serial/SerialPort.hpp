/*
 * SerialPort.hpp — a host serial port, for a device on one of the
 * machine's serial lines (the keyboard's: an MS 7004, or a program that
 * emulates one, over a real or a virtual port).
 *
 * The port is opened raw at the given speed, 8 data bits, no parity, one
 * stop bit, no flow control, and never blocks: read() takes what has
 * come, write() hands a byte to the driver.  Serial_win32.cpp (COMn) and
 * Serial_unix.cpp (termios, /dev/tty*) implement it.
 */
#ifndef MS0515_PLATFORM_SERIAL_PORT_HPP
#define MS0515_PLATFORM_SERIAL_PORT_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ms0515::serial {

class SerialPort {
public:
    /* The port `name` (COM7, /dev/ttyUSB0, a pty's /dev/pts/N) opened at
     * `baud`, 8N1; null with the reason in `error` when it cannot be. */
    static std::unique_ptr<SerialPort> open(const std::string &name, int baud,
                                            std::string &error);

    virtual ~SerialPort() = default;

    /* What has come, at most `max` bytes, without waiting: the count read
     * (0 when nothing has), -1 when the port failed. */
    virtual int read(uint8_t *buffer, std::size_t max) = 0;

    /* One byte to the line; false when the port failed. */
    virtual bool write(uint8_t byte) = 0;

    [[nodiscard]] const std::string &name() const noexcept { return name_; }

protected:
    explicit SerialPort(std::string name) : name_(std::move(name)) {}

private:
    std::string name_;
};

/* The serial ports the host has, by the names open() takes. */
std::vector<std::string> listPorts();

} // namespace ms0515::serial

#endif
