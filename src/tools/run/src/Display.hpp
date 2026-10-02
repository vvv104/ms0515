/*
 * Display.hpp - the machine's picture in a window and its speaker on the
 * host's sound, for ms0515-run.
 *
 * Neither is there until wanted: a program that prints and ends never
 * touches the host's video or sound.  The sound device opens at the
 * speaker's first move, the window when the tool asks for it - a program
 * was seen to make its picture itself (Machine::graphics).  With the
 * window open the host's keys come from it, as the keys of the machine.
 *
 * Two implementations, picked by the build: SDL's (DisplaySdl.cpp), and
 * one for a build without the SDL front end, which has no window and
 * says so (DisplayNone.cpp).
 */

#ifndef MS0515_RUN_DISPLAY_HPP
#define MS0515_RUN_DISPLAY_HPP

#include <ms0515/Emulator.hpp>

#include <cstdint>
#include <memory>
#include <string>

namespace ms0515::run {

class Display {
public:
    Display();
    ~Display();

    Display(const Display &)            = delete;
    Display &operator=(const Display &) = delete;

    /* The speaker changed its level at `cycle` of the current frame. */
    void speaker(uint32_t cycle, int level);

    /* Open the window, titled `title`.  False when it cannot be had; the
     * reason is in error(). */
    [[nodiscard]] bool open(const std::string &title);
    [[nodiscard]] bool isOpen() const noexcept;
    [[nodiscard]] const std::string &error() const noexcept;

    /* After each frame of the machine: play the frame's sound; with the
     * window open, show the screen and pass the keys pressed to `emu`.
     * False when the window was closed. */
    [[nodiscard]] bool frame(ms0515::Emulator &emu);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} /* namespace ms0515::run */

#endif /* MS0515_RUN_DISPLAY_HPP */
