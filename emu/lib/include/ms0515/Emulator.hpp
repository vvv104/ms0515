/*
 * Emulator.hpp — High-level C++ wrapper around the MS0515 core board.
 *
 * Deliberately self-contained: the public header pulls in NO C-side
 * core symbols (no <ms0515/board.h>, no <ms0515/ms7004.h>, no scancode
 * macros).  Everything frontend-visible is expressed in plain C++ —
 * the strong `Key` enum mirrors the MS-7004 scancode set; ROM/disk
 * sizes and snapshot APIs use `std::span` / `std::expected`; pixel
 * iteration goes through visitor callbacks.
 *
 * The C-side board state lives behind a forward-declared `Impl` struct
 * accessed through `unique_ptr<Impl>`.  Lib-internal code that needs
 * raw access (Debugger, GdbStub, Disassembler) goes through
 * `lib/src/EmulatorInternal.hpp`; frontends never see the C structs.
 */

#ifndef MS0515_EMULATOR_HPP
#define MS0515_EMULATOR_HPP

#include <ms0515/ScreenReader.hpp>

#include <array>
#include <cstdint>
#include <cstdio>      /* FILE for setScreenDumpFile */
#include <expected>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace ms0515 {

/* Strong enum mirroring the MS-7004 physical key set.  Numeric values
 * are positional and lock-step with the C-side `ms7004_key_t`; a
 * static_assert in Emulator.cpp pins the relationship.  Defined as
 * `enum class` (not `using ms7004_key_t`) so the public header carries
 * its own self-contained type — the C enum stays an implementation
 * detail of lib's input pipeline.  Naming follows C++ conventions:
 * `Key::ShiftL` rather than `MS7004_KEY_SHIFT_L`, `Key::Digit1` for
 * the digit row (a leading digit can't start an identifier). */
enum class Key : uint8_t {
    None = 0,
    F1, F2, F3, F4, F5,
    F6, F7, F8, F9, F10,
    F11, F12, F13, F14,
    Help,        /* ПМ */
    Perform,     /* ИСП */
    F17, F18, F19, F20,
    LBracePipe, SemiPlus,
    Digit1, Digit2, Digit3, Digit4, Digit5,
    Digit6, Digit7, Digit8, Digit9, Digit0,
    MinusEq, RBraceLeftUp, Backspace,
    Tab, J, C, U, K, E, N, G,
    LBracket, RBracket, Z, H, ColonStar, Tilde, Return,
    Ctrl, Caps,
    F,            /* letter F (distinct from F1..F20 above) */
    Y, W, A, P, R, O, L, D,
    V, Backslash, Period, HardSign,
    ShiftL, RusLat, Q, Che,
    S, M, I, T, X, B,
    At, Comma, Slash, Underscore, ShiftR,
    Compose, Space, Kp0Wide, KpEnter,
    Find, Insert, Remove, Select, Prev, Next,
    Up, Down, Left, Right,
    Pf1, Pf2, Pf3, Pf4,
    Kp1, Kp2, Kp3, Kp4, Kp5, Kp6, Kp7, Kp8, Kp9,
    KpDot, KpComma, KpMinus,
};

/* MS0515 single-sided floppy image size in bytes (80 tracks ×
 * 5120 bytes/track).  Asserted in Emulator.cpp to match the C-side
 * `FDC_DISK_SIZE` so a hardware-config change there fails the build
 * here instead of silently desyncing the validation the frontend
 * builds on top of this constant. */
inline constexpr std::size_t kFloppyDiskSize = 409600;

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

    /* Forward-declared opaque state.  Defined in EmulatorInternal.hpp,
     * reachable only from lib/src/*.  The struct is intentionally
     * exposed *as a name* in the public header so the unique_ptr
     * member type-checks; callers outside lib see it as incomplete and
     * can't dereference the result of impl(). */
    struct Impl;

    Emulator();
    ~Emulator();

    Emulator(const Emulator &)            = delete;
    Emulator &operator=(const Emulator &) = delete;
    Emulator(Emulator &&)                 = delete;
    Emulator &operator=(Emulator &&)      = delete;

    /* Internal-use bridge to lib's typed accessors in
     * `ms0515::internal::`.  Public so EmulatorInternal.hpp does not
     * need a friend declaration; useless to non-lib callers because
     * `Impl` is incomplete in this header. */
    [[nodiscard]] Impl       *impl()       noexcept { return impl_.get(); }
    [[nodiscard]] const Impl *impl() const noexcept { return impl_.get(); }

    /* ── Lifecycle ──────────────────────────────────────────────────────── */

    void reset();

    void loadRom(std::span<const uint8_t> data);

    [[nodiscard]] bool loadRomFile(std::string_view path);

    [[nodiscard]] bool mountDisk(int drive, std::string_view path);

    void unmountDisk(int drive);

    [[nodiscard]] const std::string &diskPath(int drive) const noexcept
    { return diskPath_[drive]; }

    void enableRamDisk();

    /* History ring & memory watchpoints used to live here; they moved
     * onto `Debugger` (diagnostic surface) — see Debugger.hpp. */

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

    /* ── Memory bus access ─────────────────────────────────────────────── */

    [[nodiscard]] uint16_t readWord(uint16_t address);
    [[nodiscard]] uint8_t  readByte(uint16_t address);
    void     writeWord(uint16_t address, uint16_t value);
    void     writeByte(uint16_t address, uint8_t value);

    /* ── State accessors ───────────────────────────────────────────────── */

    [[nodiscard]] bool                     isHires()       const noexcept;
    [[nodiscard]] uint8_t                  borderColor()   const noexcept;
    [[nodiscard]] uint16_t                 pc()            const noexcept;
    [[nodiscard]] uint32_t                 frameCyclePos() const noexcept;
    [[nodiscard]] bool                     halted()        const noexcept;
    [[nodiscard]] bool                     waiting()       const noexcept;

    /* ── Screen reading ────────────────────────────────────────────────── */

    /* Decode the current VRAM into a KOI-8 cell snapshot.  The font
     * map is rebuilt automatically on every loadRom / loadState — the
     * frontend never needs to call buildFont directly.  Snapshots are
     * the canonical input to `Terminal::feedSample` and the textual
     * tests; visual rendering still goes through forEachXxxPixel. */
    [[nodiscard]] ScreenReader::Snapshot screenSnapshot();

    /* Stream changed cells to a host FILE* (typically stderr/stdout
     * for headless logging).  Pass nullptr to disable.  No-op when
     * the screen has not changed since the last flush. */
    void setScreenDumpFile(FILE *f) noexcept;
    void flushScreenDump();

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
    void rewirePointers();

    std::unique_ptr<Impl> impl_;
    std::array<std::string, 4> diskPath_;
};

} /* namespace ms0515 */

#endif /* MS0515_EMULATOR_HPP */
