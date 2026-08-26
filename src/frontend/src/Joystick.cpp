/*
 * Joystick.cpp — the joystick on the MS7007 port, from the host.
 */
#include "Joystick.hpp"

#include <cstring>

namespace ms0515_frontend {

namespace {

using Joy = ms0515::Emulator::Joy;

constexpr Sint16 kAxisThreshold = 16000;   /* of 32767: the stick well off centre */

/* The arrow keys and Space as the lines; 0 for any other key. */
uint8_t keyLine(SDL_Scancode sc)
{
    switch (sc) {
    case SDL_SCANCODE_RIGHT: return Joy::Right;
    case SDL_SCANCODE_LEFT:  return Joy::Left;
    case SDL_SCANCODE_DOWN:  return Joy::Down;
    case SDL_SCANCODE_UP:    return Joy::Up;
    case SDL_SCANCODE_SPACE: return Joy::Fire;
    default:                 return 0;
    }
}

} /* namespace */

Joystick::~Joystick() { closePad(); }

const char *Joystick::modeName(JoystickMode mode)
{
    switch (mode) {
    case JoystickMode::Keys:    return "keys";
    case JoystickMode::Gamepad: return "gamepad";
    default:                    return "off";
    }
}

JoystickMode Joystick::modeFromName(const std::string &name)
{
    if (name == "keys")    return JoystickMode::Keys;
    if (name == "gamepad") return JoystickMode::Gamepad;
    return JoystickMode::Off;
}

void Joystick::setMode(JoystickMode mode)
{
    mode_    = mode;
    keyBits_ = 0;
    if (mode == JoystickMode::Gamepad) openPad(); else closePad();
}

bool Joystick::handleKey(const SDL_Event &ev)
{
    if (mode_ != JoystickMode::Keys) return false;
    if (ev.type != SDL_KEYDOWN && ev.type != SDL_KEYUP) return false;
    const uint8_t line = keyLine(ev.key.keysym.scancode);
    if (!line) return false;
    if (ev.type == SDL_KEYDOWN) keyBits_ |= line;
    else                        keyBits_ &= static_cast<uint8_t>(~line);
    return true;
}

void Joystick::handleDevice(const SDL_Event &ev)
{
    if (mode_ != JoystickMode::Gamepad) return;
    if (ev.type == SDL_CONTROLLERDEVICEADDED && !pad_)
        openPad();
    else if (ev.type == SDL_CONTROLLERDEVICEREMOVED && pad_ &&
             ev.cdevice.which == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad_)))
        closePad();
}

void Joystick::poll(ms0515::Emulator &emu)
{
    uint8_t bits = 0;
    if (mode_ == JoystickMode::Keys)    bits = keyBits_;
    if (mode_ == JoystickMode::Gamepad) bits = padBits();
    if (bits == lastBits_) return;
    lastBits_ = bits;
    emu.setJoystick(bits);
}

std::string Joystick::gamepadName() const
{
    if (!pad_) return {};
    const char *name = SDL_GameControllerName(pad_);
    return name ? name : "gamepad";
}

void Joystick::openPad()
{
    if (pad_) return;
    const int n = SDL_NumJoysticks();
    for (int i = 0; i < n && !pad_; ++i)
        if (SDL_IsGameController(i))
            pad_ = SDL_GameControllerOpen(i);
}

void Joystick::closePad()
{
    if (pad_) { SDL_GameControllerClose(pad_); pad_ = nullptr; }
}

/* The d-pad or the left stick for the directions; any face button, shoulder
 * or trigger for fire. */
uint8_t Joystick::padBits() const
{
    if (!pad_) return 0;
    auto btn  = [&](SDL_GameControllerButton b) { return SDL_GameControllerGetButton(pad_, b) != 0; };
    auto axis = [&](SDL_GameControllerAxis a)   { return SDL_GameControllerGetAxis(pad_, a); };
    uint8_t bits = 0;
    if (btn(SDL_CONTROLLER_BUTTON_DPAD_RIGHT) || axis(SDL_CONTROLLER_AXIS_LEFTX) >  kAxisThreshold) bits |= Joy::Right;
    if (btn(SDL_CONTROLLER_BUTTON_DPAD_LEFT)  || axis(SDL_CONTROLLER_AXIS_LEFTX) < -kAxisThreshold) bits |= Joy::Left;
    if (btn(SDL_CONTROLLER_BUTTON_DPAD_DOWN)  || axis(SDL_CONTROLLER_AXIS_LEFTY) >  kAxisThreshold) bits |= Joy::Down;
    if (btn(SDL_CONTROLLER_BUTTON_DPAD_UP)    || axis(SDL_CONTROLLER_AXIS_LEFTY) < -kAxisThreshold) bits |= Joy::Up;
    if (btn(SDL_CONTROLLER_BUTTON_A) || btn(SDL_CONTROLLER_BUTTON_B) ||
        btn(SDL_CONTROLLER_BUTTON_X) || btn(SDL_CONTROLLER_BUTTON_Y) ||
        btn(SDL_CONTROLLER_BUTTON_LEFTSHOULDER) || btn(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) ||
        axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT) > kAxisThreshold || axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > kAxisThreshold)
        bits |= Joy::Fire;
    return bits;
}

} /* namespace ms0515_frontend */
