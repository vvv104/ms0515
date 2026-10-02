/*
 * HostKeys.hpp - the bytes of the host's stdin as keys for the machine.
 *
 * A terminal sends UTF-8 characters and, for the keys that have none,
 * escape sequences; a pipe or a file sends text.  HostKeys takes the
 * bytes as they come, in pieces of any size, and hands the Typist a
 * KOI-8R character for each character and a key for each arrow
 * (ESC [ A..D, ESC O A..D).  A sequence it does not know is dropped
 * whole, so its tail is not typed as text.
 *
 * One byte is the host's own: Ctrl-] (035), the way out of a program
 * that will not end - Ctrl-C goes to the machine, as on the machine.
 */

#ifndef MS0515_RUN_HOSTKEYS_HPP
#define MS0515_RUN_HOSTKEYS_HPP

#include <ms0515/Typist.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace ms0515::run {

class HostKeys {
public:
    /* Type the next bytes of stdin.  True when Ctrl-] was among them. */
    bool feed(std::span<const uint8_t> bytes, ms0515::Typist &typist);

private:
    enum class State { text, escape, sequence };

    void character(ms0515::Typist &typist);

    State                  state_ = State::text;
    std::array<uint8_t, 4> utf8_{};     /* a character not complete yet */
    std::size_t            utf8Len_ = 0;
};

} /* namespace ms0515::run */

#endif /* MS0515_RUN_HOSTKEYS_HPP */
