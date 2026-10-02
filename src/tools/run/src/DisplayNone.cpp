/*
 * DisplayNone.cpp - Display for a build without the SDL front end: no
 * sound, no window.
 */

#include "Display.hpp"

namespace ms0515::run {

struct Display::Impl {
    std::string error = "this build of ms0515-run has no window "
                        "(built without the SDL front end)";
};

Display::Display() : impl_(std::make_unique<Impl>()) {}
Display::~Display() = default;

void Display::speaker(uint32_t, int) {}

bool Display::open(const std::string &) { return false; }

bool Display::isOpen() const noexcept { return false; }

const std::string &Display::error() const noexcept { return impl_->error; }

bool Display::frame(ms0515::Emulator &) { return true; }

} /* namespace ms0515::run */
