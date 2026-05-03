/*
 * EmulatorInternal.hpp — Lib-internal access to the C-side board state.
 *
 * The public `Emulator.hpp` header is deliberately PIMPL-shaped: it
 * forward-declares `Emulator::Impl` and stores a `unique_ptr<Impl>` but
 * never lets callers dereference it.  That keeps frontend code entirely
 * free of `<ms0515/board.h>` / `<ms0515/ms7004.h>` even when it goes
 * through the public lib API.
 *
 * Lib-internal code (Debugger, GdbStub, Disassembler, Emulator.cpp
 * itself) still needs raw access to `ms0515_board_t` / `ms7004_t`.  This
 * header completes the `Emulator::Impl` definition and provides typed
 * free-function accessors in `ms0515::internal::` so those translation
 * units never need to touch the friend mechanism — `internal::cpu(emu)`
 * etc. read straight off the impl pointer.
 *
 * Usage rule: this header MUST be included only by `lib/src/*` sources.
 * Any include from the frontend, tests, or another consumer is a
 * layering bug — the public API surface is `Emulator.hpp` alone.
 */

#ifndef MS0515_EMULATOR_INTERNAL_HPP
#define MS0515_EMULATOR_INTERNAL_HPP

#include "ms0515/Emulator.hpp"

#include <span>

extern "C" {
#include "ms0515/board.h"
#include "ms0515/ms7004.h"
}

namespace ms0515 {

struct Emulator::Impl {
    ms0515_board_t board;
    ms7004_t       kbd7004;

    Emulator::SoundCallback     soundCb;
    Emulator::SerialOutCallback serialOutCb;
    Emulator::SerialInCallback  serialInCb;
};

namespace internal {

inline ms0515_board_t       &board(Emulator &e) noexcept
    { return e.impl()->board; }
inline const ms0515_board_t &board(const Emulator &e) noexcept
    { return e.impl()->board; }

inline ms0515_cpu_t       &cpu(Emulator &e) noexcept
    { return e.impl()->board.cpu; }
inline const ms0515_cpu_t &cpu(const Emulator &e) noexcept
    { return e.impl()->board.cpu; }

inline ms7004_t       &keyboard(Emulator &e) noexcept
    { return e.impl()->kbd7004; }
inline const ms7004_t &keyboard(const Emulator &e) noexcept
    { return e.impl()->kbd7004; }

/* ROM and VRAM byte spans — public API exposes neither (frontend
 * goes through Terminal::decode / forEachXxxPixel for everything it
 * needs), but Terminal.cpp itself and lib tests that exercise the
 * decoder via Terminal::decode need the raw views. */
inline std::span<const uint8_t> rom(const Emulator &e) noexcept
    { return {e.impl()->board.mem.rom, MEM_ROM_SIZE}; }
inline std::span<const uint8_t> vram(const Emulator &e) noexcept
    { return {board_get_vram(&e.impl()->board), MEM_VRAM_SIZE}; }

} /* namespace internal */
} /* namespace ms0515 */

#endif /* MS0515_EMULATOR_INTERNAL_HPP */
