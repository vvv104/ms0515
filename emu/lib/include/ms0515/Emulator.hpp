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

/* Frontend-facing alias for the C-side keyboard scancode enum.  Use
 * this name in frontend code instead of `ms7004_key_t` so the
 * frontend never has to include `<ms0515/ms7004.h>` directly. */
using Key = ::ms7004_key_t;

/* Frontend-facing constants mirroring the C-side hardware sizes. */
inline constexpr std::size_t kFloppyDiskSize = FDC_DISK_SIZE;

/* User-tunable keyboard timing settings.  Frontend reads/writes
 * these via Emulator::keyboardSettings() / applyKeyboardConfig()
 * — the underlying ms7004_t is not exposed. */
struct KeyboardSettings {
    bool     autoGameMode    = false;
    uint32_t typingDelayMs   = 0;
    uint32_t typingPeriodMs  = 0;
    uint32_t gameDelayMs     = 0;
    uint32_t gamePeriodMs    = 0;
};

/* Decoded pixel attributes for the 320×200 colour mode.  The lib
 * does the bit-fiddling (which byte holds the pixel data, where the
 * attribute lives, MSB-first ordering) so the frontend never needs
 * to touch raw VRAM.  Frontend palette translation (GRB → RGBA) and
 * plotting stay on its side. */
struct LoResAttr {
    bool    flash;       /* swap fg/bg at ~2 Hz when set                 */
    bool    bright;      /* full intensity when set, half when clear     */
    uint8_t bgGrb;       /* 3-bit GRB index for unlit pixels             */
    uint8_t fgGrb;       /* 3-bit GRB index for lit pixels               */
};

using HiResPixelCb = std::function<void(int x, int y, bool lit)>;
using LoResPixelCb = std::function<void(int x, int y, bool lit,
                                         const LoResAttr &)>;

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

    /* Size the event history ring (0 disables).  Can be toggled any
     * time — resizing discards the current contents. */
    void enableHistory(std::size_t nEvents);

    /* Set a memory-write watchpoint.  When len > 0, every byte or word
     * write to [addr, addr+len) pushes a MEMW event into the history
     * ring.  Pass len=0 to clear. */
    void setMemoryWatch(std::uint16_t addr, std::uint16_t len);

    /* Same for reads — emits MEMR events.  Can fire thousands of times
     * per polling loop; size the history ring accordingly. */
    void setReadWatch(std::uint16_t addr, std::uint16_t len);

    /* ── Snapshot (save/load state) ────────────────────────────────────── */

    [[nodiscard]] std::expected<void, std::string> saveState(std::string_view path);

    [[nodiscard]] std::expected<void, std::string> loadState(std::string_view path);

    [[nodiscard]] uint32_t romCrc32() const noexcept;

    /* ── Execution ──────────────────────────────────────────────────────── */

    [[nodiscard]] bool stepFrame();

    void stepInstruction();

    /* ── Input ──────────────────────────────────────────────────────────── */

    void keyEvent(uint8_t scancode);

    void keyPress(Key key, bool down);

    void keyReleaseAll();

    void keyTick(uint32_t now_ms);

    [[nodiscard]] bool capsOn()   const noexcept;
    [[nodiscard]] bool ruslatOn() const noexcept;
    [[nodiscard]] bool keyHeld(Key key) const noexcept;

    /* User-tunable keyboard settings.  Frontend reads via
     * keyboardSettings() and writes via applyKeyboardConfig()
     * (which also resets the live repeat timers to the typing
     * preset; the auto-game-mode heuristic flips them later). */
    [[nodiscard]] KeyboardSettings keyboardSettings() const noexcept;
    void                           applyKeyboardConfig(
                                       const KeyboardSettings &s) noexcept;
    [[nodiscard]] bool             keyboardInGameMode() const noexcept;

    /* ── Internal-use accessors ────────────────────────────────────────── */

    /* These return raw C structs and exist for code that has to
     * touch hardware directly (debugger, GDB stub, OnScreenKeyboard
     * scancode injection).  Frontend should NOT use them — use the
     * specific lib API methods below instead. */
    const ms7004_t       &keyboard() const noexcept { return kbd7004_; }
    ms7004_t             &keyboard()       noexcept { return kbd7004_; }
    const ms0515_board_t &board()    const noexcept { return *board_; }
    ms0515_board_t       &board()          noexcept { return *board_; }
    const ms0515_cpu_t   &cpu()      const noexcept { return board_->cpu; }
    ms0515_cpu_t         &cpu()            noexcept { return board_->cpu; }

    [[nodiscard]] uint16_t readWord(uint16_t address);
    [[nodiscard]] uint8_t  readByte(uint16_t address);
    void     writeWord(uint16_t address, uint16_t value);
    void     writeByte(uint16_t address, uint8_t value);

    /* ── State accessors ───────────────────────────────────────────────── */

    [[nodiscard]] std::span<const uint8_t> rom()  const noexcept;
    [[nodiscard]] std::span<const uint8_t> vram() const noexcept;
    [[nodiscard]] bool                     isHires()     const noexcept;
    [[nodiscard]] uint8_t                  borderColor() const noexcept;
    [[nodiscard]] uint16_t                 pc()          const noexcept;
    [[nodiscard]] uint32_t                 frameCyclePos() const noexcept;

    /* Visit every pixel of the current frame in raster-scan order
     * and invoke `cb` with its coordinates and decoded attributes.
     * The lib walks VRAM and unpacks the bit/byte layout; the
     * frontend's callback only has to plot — palette lookup and
     * RGBA assembly stay on the frontend side because they're
     * display-format concerns.  Use the variant matching the
     * current mode (isHires()); calling the wrong one is a no-op. */
    void forEachHiResPixel(const HiResPixelCb &cb) const;
    void forEachLoResPixel(const LoResPixelCb &cb) const;

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
