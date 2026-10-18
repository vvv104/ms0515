/*
 * ExternalKeyboard.hpp — a keyboard outside the emulator on a host serial
 * port, put on the machine's keyboard line (Emulator::setExternalKeyboard)
 * in place of the MS 7004 model: a firmware emulator over a virtual port
 * pair, or a real MS 7004 through an adapter.
 *
 * The line runs at the MS 7004's 4800 baud, 8N1.  A port that fails while
 * in use is noticed (failed()) and the app puts the model back.
 */
#ifndef MS0515_FRONTEND_EXTERNAL_KEYBOARD_HPP
#define MS0515_FRONTEND_EXTERNAL_KEYBOARD_HPP

#include <ms0515/Emulator.hpp>

#include "SerialPort.hpp"

#include <cstdint>
#include <deque>
#include <memory>
#include <string>

namespace ms0515_frontend {

class ExternalKeyboard {
public:
    static constexpr int kBaud = 4800;

    /* Open `port` and put it on the emulator's keyboard line; false with
     * the reason in `error`, the model staying, when it cannot be. */
    bool connect(ms0515::Emulator &emu, const std::string &port, std::string &error);

    /* The model back on the line, the port closed. */
    void disconnect(ms0515::Emulator &emu);

    [[nodiscard]] bool connected() const noexcept { return port_ != nullptr; }
    [[nodiscard]] bool failed() const noexcept { return failed_; }
    [[nodiscard]] std::string portName() const { return port_ ? port_->name() : std::string{}; }

private:
    bool next(uint8_t &byte);
    void send(uint8_t byte);

    std::unique_ptr<ms0515::serial::SerialPort> port_;
    std::deque<uint8_t> received_;
    bool failed_ = false;
};

} // namespace ms0515_frontend

#endif
