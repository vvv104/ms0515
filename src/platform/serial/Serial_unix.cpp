/*
 * Serial_unix.cpp — SerialPort over termios (Linux, macOS).
 */
#include "SerialPort.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <utility>

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

namespace ms0515::serial {

namespace {

speed_t speedOf(int baud)
{
    switch (baud) {
    case 1200:  return B1200;
    case 2400:  return B2400;
    case 4800:  return B4800;
    case 9600:  return B9600;
    case 19200: return B19200;
    default:    return 0;
    }
}

class UnixPort final : public SerialPort {
public:
    UnixPort(std::string name, int fd) : SerialPort(std::move(name)), fd_(fd) {}
    ~UnixPort() override { ::close(fd_); }

    UnixPort(const UnixPort &)            = delete;
    UnixPort &operator=(const UnixPort &) = delete;

    int read(uint8_t *buffer, std::size_t max) override
    {
        const ssize_t n = ::read(fd_, buffer, max);
        if (n >= 0) return static_cast<int>(n);
        return (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) ? 0 : -1;
    }

    /* A byte the line cannot take now - nobody at the other end, the
     * driver's buffer full - is dropped, as on a cable with nothing on
     * it; only an error of the port is a failure. */
    bool write(uint8_t byte) override
    {
        for (;;) {
            const ssize_t n = ::write(fd_, &byte, 1);
            if (n == 1) return true;
            if (n < 0 && errno == EINTR) continue;
            return n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK);
        }
    }

private:
    int fd_;
};

} // namespace

std::unique_ptr<SerialPort> SerialPort::open(const std::string &name, int baud,
                                             std::string &error)
{
    const speed_t speed = speedOf(baud);
    if (speed == 0) {
        error = "unsupported speed " + std::to_string(baud);
        return nullptr;
    }
    const int fd = ::open(name.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        error = name + ": " + std::strerror(errno);
        return nullptr;
    }
    termios tio{};
    if (::tcgetattr(fd, &tio) != 0) {
        error = name + ": not a serial port (" + std::strerror(errno) + ")";
        ::close(fd);
        return nullptr;
    }
    ::cfmakeraw(&tio);
    tio.c_cflag &= ~static_cast<tcflag_t>(PARENB | CSTOPB | CSIZE | CRTSCTS);
    tio.c_cflag |= CS8 | CLOCAL | CREAD;
    tio.c_iflag &= ~static_cast<tcflag_t>(IXON | IXOFF | IXANY);
    tio.c_cc[VMIN]  = 0;
    tio.c_cc[VTIME] = 0;
    ::cfsetispeed(&tio, speed);
    ::cfsetospeed(&tio, speed);
    if (::tcsetattr(fd, TCSANOW, &tio) != 0) {
        error = name + ": " + std::strerror(errno);
        ::close(fd);
        return nullptr;
    }
    error.clear();
    return std::make_unique<UnixPort>(name, fd);
}

std::vector<std::string> listPorts()
{
    /* The device nodes serial ports get: built-in and USB adapters on
     * Linux, the call-out devices on macOS. */
    static const char *const kPrefixes[] = {"ttyS", "ttyUSB", "ttyACM", "cu."};
    std::vector<std::string> ports;
    std::error_code ec;
    for (const auto &entry : std::filesystem::directory_iterator("/dev", ec)) {
        const std::string file = entry.path().filename().string();
        for (const char *prefix : kPrefixes)
            if (file.rfind(prefix, 0) == 0) {
                ports.push_back(entry.path().string());
                break;
            }
    }
    std::sort(ports.begin(), ports.end());
    return ports;
}

} // namespace ms0515::serial
