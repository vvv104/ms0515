/*
 * ExternalKeyboard.cpp — a host serial port on the machine's keyboard line.
 */
#include "ExternalKeyboard.hpp"

namespace ms0515_frontend {

bool ExternalKeyboard::connect(ms0515::Emulator &emu, const std::string &port,
                               std::string &error)
{
    disconnect(emu);
    auto opened = ms0515::serial::SerialPort::open(port, kBaud, error);
    if (!opened) return false;
    port_   = std::move(opened);
    failed_ = false;
    emu.setExternalKeyboard([this](uint8_t &byte) { return next(byte); },
                            [this](uint8_t byte) { send(byte); });
    return true;
}

void ExternalKeyboard::disconnect(ms0515::Emulator &emu)
{
    if (!port_) return;
    emu.setExternalKeyboard({}, {});
    port_.reset();
    received_.clear();
}

/* The keyboard's next byte: what the port has, read a batch at a time. */
bool ExternalKeyboard::next(uint8_t &byte)
{
    if (received_.empty() && port_ && !failed_) {
        uint8_t buf[64];
        const int n = port_->read(buf, sizeof buf);
        if (n < 0) failed_ = true;
        for (int i = 0; i < n; ++i) received_.push_back(buf[i]);
    }
    if (received_.empty()) return false;
    byte = received_.front();
    received_.pop_front();
    return true;
}

void ExternalKeyboard::send(uint8_t byte)
{
    if (port_ && !failed_ && !port_->write(byte)) failed_ = true;
}

} // namespace ms0515_frontend
