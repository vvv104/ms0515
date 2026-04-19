/*
 * Emulator.hpp — High-level C++ wrapper around the MS0515 core board.
 *
 * Provides a clean RAII interface for the frontend and debugger.  Hides
 * the C-style ms0515_board_t struct and exposes:
 *   - ROM loading from file or memory buffer
 *   - Disk image mounting
 *   - Frame-stepping and instruction-stepping
 *   - Read-only access to CPU/memory state for the UI
 *   - Sound and serial callback installation
 *
 * The Emulator owns its underlying ms0515_board_t.  It is non-copyable
 * (the board contains internal back-pointers) but movable.
 */

#ifndef MS0515_EMULATOR_HPP
#define MS0515_EMULATOR_HPP

#include <array>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>

extern "C" {
#include "ms0515/board.h"
#include "ms0515/ms7004.h"
}

namespace ms0515 {

class Emulator {
public:
    using SoundCallback     = std::function<void(int value)>;
    using SerialOutCallback = std::function<bool(uint8_t byte)>;
    using SerialInCallback  = std::function<bool(uint8_t &byte)>;

    Emulator();
    ~Emulator();

    Emulator(const Emulator &)            = delete;
    Emulator &operator=(const Emulator &) = delete;
    Emulator(Emulator &&)                 = delete;
    Emulator &operator=(Emulator &&)      = delete;

    /* ── Lifecycle ──────────────────────────────────────────────────────── */

    void reset();

    void loadRom(std::span<const uint8_t> data);

    [[nodiscard]] bool loadRomFile(std::string_view path);

    [[nodiscard]] bool mountDisk(int drive, std::string_view path);

    void unmountDisk(int drive);

    [[nodiscard]] const std::string &diskPath(int drive) const noexcept
    { return diskPath_[drive]; }

    void enableRamDisk();

    /* ── Snapshot (save/load state) ────────────────────────────────────── */

    [[nodiscard]] std::expected<void, std::string> saveState(std::string_view path);

    [[nodiscard]] std::expected<void, std::string> loadState(std::string_view path);

    [[nodiscard]] uint32_t romCrc32() const noexcept;

    /* ── Execution ──────────────────────────────────────────────────────── */

    [[nodiscard]] bool stepFrame();

    void stepInstruction();

    /* ── Input ──────────────────────────────────────────────────────────── */

    void keyEvent(uint8_t scancode);

    void keyPress(ms7004_key_t key, bool down);

    void keyReleaseAll();

    void keyTick(uint32_t now_ms);

    [[nodiscard]] bool capsOn()   const noexcept;
    [[nodiscard]] bool ruslatOn() const noexcept;
    [[nodiscard]] bool keyHeld(ms7004_key_t key) const noexcept;

    const ms7004_t &keyboard() const noexcept { return kbd7004_; }
    ms7004_t       &keyboard()       noexcept { return kbd7004_; }

    /* ── State accessors ───────────────────────────────────────────────── */

    const ms0515_board_t &board() const noexcept { return *board_; }
    ms0515_board_t       &board()       noexcept { return *board_; }

    const ms0515_cpu_t   &cpu() const noexcept { return board_->cpu; }
    ms0515_cpu_t         &cpu()       noexcept { return board_->cpu; }

    [[nodiscard]] uint16_t readWord(uint16_t address);
    [[nodiscard]] uint8_t  readByte(uint16_t address);
    void     writeWord(uint16_t address, uint16_t value);
    void     writeByte(uint16_t address, uint8_t value);

    [[nodiscard]] const uint8_t *vram()        const noexcept;
    [[nodiscard]] bool           isHires()     const noexcept;
    [[nodiscard]] uint8_t        borderColor() const noexcept;

    /* ── Callbacks ──────────────────────────────────────────────────────── */

    void setSoundCallback(SoundCallback cb);
    void setSerialCallbacks(SerialInCallback in, SerialOutCallback out);

private:
    static void cSoundTrampoline(void *userdata, int value);
    static bool cSerialOutTrampoline(void *userdata, uint8_t byte);
    static bool cSerialInTrampoline(void *userdata, uint8_t *byte);

    void rewirePointers();

    std::unique_ptr<ms0515_board_t> board_;
    ms7004_t kbd7004_;
    std::array<std::string, 4> diskPath_;

    SoundCallback     soundCb_;
    SerialOutCallback serialOutCb_;
    SerialInCallback  serialInCb_;
};

} /* namespace ms0515 */

#endif /* MS0515_EMULATOR_HPP */
