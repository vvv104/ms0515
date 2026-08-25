/*
 * Joystick.hpp — the joystick on the MS7007 port, from the host.
 *
 * The machine's joystick is five switches - right, left, down, up, fire -
 * on port B of the second PPI (Emulator::setJoystick, Emulator::Joy).
 * Here the lines come from the arrow keys and Space (Keys mode: those keys
 * then do not reach the MS7004), or from an SDL game controller (the d-pad
 * or the left stick for the directions, any face button, shoulder or
 * trigger for fire).  The mode is the config's "joystick" word.
 */
#ifndef MS0515_FRONTEND_JOYSTICK_HPP
#define MS0515_FRONTEND_JOYSTICK_HPP

#include <SDL.h>

#include <cstdint>
#include <string>

#include <ms0515/Emulator.hpp>

namespace ms0515_frontend {

enum class JoystickMode { Off, Keys, Gamepad };

class Joystick {
public:
    Joystick() = default;
    ~Joystick();
    Joystick(const Joystick &)            = delete;
    Joystick &operator=(const Joystick &) = delete;

    void setMode(JoystickMode mode);
    [[nodiscard]] JoystickMode mode() const noexcept { return mode_; }

    /* The config's word for a mode ("off" / "keys" / "gamepad") and back;
     * an unknown word is Off. */
    [[nodiscard]] static const char  *modeName(JoystickMode mode);
    [[nodiscard]] static JoystickMode modeFromName(const std::string &name);

    /* A key event in Keys mode: the arrows and Space are the lines.
     * Returns true when the event was the joystick's (not the keyboard's). */
    bool handleKey(const SDL_Event &ev);

    /* SDL_CONTROLLERDEVICEADDED / REMOVED: keep the first controller open. */
    void handleDevice(const SDL_Event &ev);

    /* Once a tick: the lines held now -> the machine (only when changed). */
    void poll(ms0515::Emulator &emu);

    /* The open controller's name, "" when none. */
    [[nodiscard]] std::string gamepadName() const;

private:
    void openPad();
    void closePad();
    [[nodiscard]] uint8_t padBits() const;

    JoystickMode        mode_     = JoystickMode::Off;
    uint8_t             keyBits_  = 0;
    uint8_t             lastBits_ = 0;
    SDL_GameController *pad_      = nullptr;
};

} /* namespace ms0515_frontend */

#endif /* MS0515_FRONTEND_JOYSTICK_HPP */
