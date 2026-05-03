/*
 * Emulator.cpp — Implementation of the C++ wrapper around ms0515_board_t.
 */

#include "EmulatorInternal.hpp"  /* completes Emulator::Impl + C-side includes */

#include <fstream>
#include <string>
#include <utility>

extern "C" {
#include "ms0515/core/snapshot.h"
}

namespace {

/* snap_io_t adapter for std::fstream */

bool fstreamWrite(void *ctx, const void *data, size_t n)
{
    auto *s = static_cast<std::ofstream *>(ctx);
    s->write(static_cast<const char *>(data), static_cast<std::streamsize>(n));
    return s->good();
}

bool fstreamRead(void *ctx, void *data, size_t n)
{
    auto *s = static_cast<std::ifstream *>(ctx);
    s->read(static_cast<char *>(data), static_cast<std::streamsize>(n));
    return s->good();
}

bool fstreamSeekIn(void *ctx, long offset)
{
    auto *s = static_cast<std::ifstream *>(ctx);
    s->seekg(offset, std::ios::cur);
    return s->good();
}

void cSoundTrampoline(void *userdata, int value)
{
    auto *self = static_cast<ms0515::Emulator *>(userdata);
    if (auto &cb = self->impl()->soundCb)
        cb(value);
}

bool cSerialOutTrampoline(void *userdata, uint8_t byte)
{
    auto *self = static_cast<ms0515::Emulator *>(userdata);
    auto &cb = self->impl()->serialOutCb;
    return cb ? cb(byte) : false;
}

bool cSerialInTrampoline(void *userdata, uint8_t *byte)
{
    auto *self = static_cast<ms0515::Emulator *>(userdata);
    auto &cb = self->impl()->serialInCb;
    return cb ? cb(*byte) : false;
}

} /* anonymous namespace */

namespace ms0515 {

/* Lock the public `Key` enum class to the C-side scancode-table indices
 * in `ms7004.c::kScancode`.  If anyone reorders `ms7004_key_t` without
 * mirroring the change in `Key`, the build fails here instead of
 * silently misrouting key events.  We assert representative entries
 * across each cluster plus the total count. */
static_assert(static_cast<int>(Key::None)        == MS7004_KEY_NONE);
static_assert(static_cast<int>(Key::F1)          == MS7004_KEY_F1);
static_assert(static_cast<int>(Key::F14)         == MS7004_KEY_F14);
static_assert(static_cast<int>(Key::Help)        == MS7004_KEY_HELP);
static_assert(static_cast<int>(Key::Perform)     == MS7004_KEY_PERFORM);
static_assert(static_cast<int>(Key::F20)         == MS7004_KEY_F20);
static_assert(static_cast<int>(Key::Digit1)      == MS7004_KEY_1);
static_assert(static_cast<int>(Key::Digit0)      == MS7004_KEY_0);
static_assert(static_cast<int>(Key::Backspace)   == MS7004_KEY_BS);
static_assert(static_cast<int>(Key::Tab)         == MS7004_KEY_TAB);
static_assert(static_cast<int>(Key::Return)      == MS7004_KEY_RETURN);
static_assert(static_cast<int>(Key::Ctrl)        == MS7004_KEY_CTRL);
static_assert(static_cast<int>(Key::Caps)        == MS7004_KEY_CAPS);
static_assert(static_cast<int>(Key::F)           == MS7004_KEY_F);
static_assert(static_cast<int>(Key::HardSign)    == MS7004_KEY_HARDSIGN);
static_assert(static_cast<int>(Key::ShiftL)      == MS7004_KEY_SHIFT_L);
static_assert(static_cast<int>(Key::RusLat)      == MS7004_KEY_RUSLAT);
static_assert(static_cast<int>(Key::Underscore)  == MS7004_KEY_UNDERSCORE);
static_assert(static_cast<int>(Key::ShiftR)      == MS7004_KEY_SHIFT_R);
static_assert(static_cast<int>(Key::Compose)     == MS7004_KEY_COMPOSE);
static_assert(static_cast<int>(Key::Space)       == MS7004_KEY_SPACE);
static_assert(static_cast<int>(Key::KpEnter)     == MS7004_KEY_KP_ENTER);
static_assert(static_cast<int>(Key::Find)        == MS7004_KEY_FIND);
static_assert(static_cast<int>(Key::Next)        == MS7004_KEY_NEXT);
static_assert(static_cast<int>(Key::Up)          == MS7004_KEY_UP);
static_assert(static_cast<int>(Key::Right)       == MS7004_KEY_RIGHT);
static_assert(static_cast<int>(Key::Pf1)         == MS7004_KEY_PF1);
static_assert(static_cast<int>(Key::Pf4)         == MS7004_KEY_PF4);
static_assert(static_cast<int>(Key::Kp1)         == MS7004_KEY_KP_1);
static_assert(static_cast<int>(Key::Kp9)         == MS7004_KEY_KP_9);
static_assert(static_cast<int>(Key::KpMinus)     == MS7004_KEY_KP_MINUS);
static_assert(static_cast<int>(Key::KpMinus) + 1 == MS7004_KEY__COUNT,
              "Key enum is out of sync with ms7004_key_t");

/* Pin the public floppy-image-size constant to the C-side hardware
 * geometry.  If `FDC_DISK_SIZE` ever changes, the literal in
 * Emulator.hpp must be updated to match (and the failing build line
 * here is where to look). */
static_assert(kFloppyDiskSize == FDC_DISK_SIZE,
              "kFloppyDiskSize disagrees with core FDC_DISK_SIZE");

Emulator::Emulator()
    : impl_(std::make_unique<Impl>())
{
    board_init(&impl_->board);
    ms7004_init(&impl_->kbd7004, &impl_->board.kbd);

    /* Wire USART TX → ms7004 command channel. */
    impl_->board.kbd.tx_callback = [](void *ctx, uint8_t byte) {
        ms7004_host_byte(static_cast<ms7004_t *>(ctx), byte);
    };
    impl_->board.kbd.tx_callback_ctx = &impl_->kbd7004;
}

Emulator::~Emulator()
{
    for (int i = 0; i < 4; ++i)
        fdc_detach(&impl_->board.fdc, i);
    board_ramdisk_free(&impl_->board);
}

/* ── Lifecycle ──────────────────────────────────────────────────────────── */

void Emulator::reset()
{
    board_reset(&impl_->board);
    ms7004_reset(&impl_->kbd7004);
}

void Emulator::loadRom(std::span<const uint8_t> data)
{
    board_load_rom(&impl_->board, data.data(), static_cast<uint32_t>(data.size()));
}

bool Emulator::loadRomFile(std::string_view path)
{
    std::ifstream f(std::string{path}, std::ios::binary | std::ios::ate);
    if (!f)
        return false;

    auto size = f.tellg();
    if (size <= 0)
        return false;

    f.seekg(0);
    std::vector<uint8_t> buffer(static_cast<std::size_t>(size));
    f.read(reinterpret_cast<char *>(buffer.data()),
           static_cast<std::streamsize>(size));
    if (!f)
        return false;

    loadRom(buffer);
    return true;
}

bool Emulator::mountDisk(int drive, std::string_view path)
{
    if (drive < 0 || drive >= 4)
        return false;
    std::string pathStr{path};
    if (!fdc_attach(&impl_->board.fdc, drive, pathStr.c_str(), /*read_only=*/false))
        return false;
    diskPath_[drive] = std::move(pathStr);
    return true;
}

bool Emulator::diskActive(int unit) const noexcept
{
    if (unit < 0 || unit >= 4) return false;
    return impl_->board.fdc.drives[unit].activity_remaining > 0;
}

void Emulator::unmountDisk(int drive)
{
    if (drive < 0 || drive >= 4)
        return;
    fdc_detach(&impl_->board.fdc, drive);
    diskPath_[drive].clear();
}

void Emulator::enableRamDisk()
{
    board_ramdisk_enable(&impl_->board);
}

/* ── Execution ──────────────────────────────────────────────────────────── */

bool Emulator::stepFrame()
{
    return board_step_frame(&impl_->board);
}

void Emulator::stepInstruction()
{
    board_step_cpu(&impl_->board);
}

/* ── Input ──────────────────────────────────────────────────────────────── */

void Emulator::keyEvent(uint8_t scancode)
{
    board_key_event(&impl_->board, scancode);
}

void Emulator::keyPress(Key key, bool down)
{
    ms7004_key(&impl_->kbd7004, static_cast<ms7004_key_t>(key), down);
}

void Emulator::keyReleaseAll()
{
    ms7004_release_all(&impl_->kbd7004);
}

void Emulator::keyTick(uint32_t now_ms)
{
    ms7004_tick(&impl_->kbd7004, now_ms);
}

bool Emulator::capsOn()   const noexcept { return ms7004_caps_on(&impl_->kbd7004); }
bool Emulator::ruslatOn() const noexcept { return ms7004_ruslat_on(&impl_->kbd7004); }

bool Emulator::keyHeld(Key key) const noexcept
{
    return ms7004_is_held(&impl_->kbd7004, static_cast<ms7004_key_t>(key));
}

/* ── Memory bus access ──────────────────────────────────────────────────── */

uint16_t Emulator::readWord(uint16_t address)
{
    return board_read_word(&impl_->board, address);
}

uint8_t Emulator::readByte(uint16_t address)
{
    return board_read_byte(&impl_->board, address);
}

void Emulator::writeWord(uint16_t address, uint16_t value)
{
    board_write_word(&impl_->board, address, value);
}

void Emulator::writeByte(uint16_t address, uint8_t value)
{
    board_write_byte(&impl_->board, address, value);
}

/* ── Video state ────────────────────────────────────────────────────────── */

bool Emulator::isHires() const noexcept
{
    return board_is_hires(&impl_->board);
}

uint8_t Emulator::borderColor() const noexcept
{
    return board_get_border_color(&impl_->board);
}

uint16_t Emulator::pc() const noexcept
{
    return impl_->board.cpu.r[CPU_REG_PC];
}

uint32_t Emulator::frameCyclePos() const noexcept
{
    return impl_->board.frame_cycle_pos;
}

bool Emulator::halted() const noexcept
{
    return impl_->board.cpu.halted;
}

bool Emulator::waiting() const noexcept
{
    return impl_->board.cpu.waiting;
}

KeyboardSettings Emulator::keyboardSettings() const noexcept
{
    KeyboardSettings s;
    s.autoGameMode    = impl_->kbd7004.auto_game_mode;
    s.typingDelayMs   = impl_->kbd7004.repeat_typing_delay_ms;
    s.typingPeriodMs  = impl_->kbd7004.repeat_typing_period_ms;
    s.gameDelayMs     = impl_->kbd7004.repeat_game_delay_ms;
    s.gamePeriodMs    = impl_->kbd7004.repeat_game_period_ms;
    return s;
}

void Emulator::applyKeyboardConfig(const KeyboardSettings &s) noexcept
{
    impl_->kbd7004.auto_game_mode             = s.autoGameMode;
    impl_->kbd7004.repeat_typing_delay_ms     = s.typingDelayMs;
    impl_->kbd7004.repeat_typing_period_ms    = s.typingPeriodMs;
    impl_->kbd7004.repeat_game_delay_ms       = s.gameDelayMs;
    impl_->kbd7004.repeat_game_period_ms      = s.gamePeriodMs;
    /* Pick typing or game preset based on whether the auto-game-mode
     * heuristic has currently flipped us into game mode. */
    ms7004_recompute_live_repeat(&impl_->kbd7004);
}

bool Emulator::keyboardInGameMode() const noexcept
{
    return impl_->kbd7004.in_game_mode;
}

/* ── Frame iteration ────────────────────────────────────────────────────── */

void Emulator::forEachHiResPixel(const HiResPixelCb &cb) const
{
    if (!isHires()) return;
    const uint8_t *vramBytes = board_get_vram(&impl_->board);
    /* 640 mono pixels per scanline, 200 scanlines.  Each 16-bit
     * word in VRAM holds 16 pixels: the low byte is shifted out
     * first (8 left pixels), then the high byte (8 right pixels),
     * MSB = leftmost pixel.  See docs/hardware/video.md. */
    for (int y = 0; y < 200; ++y) {
        const uint8_t *row = vramBytes + y * 80;   /* 40 words = 80 bytes */
        for (int wx = 0; wx < 40; ++wx) {
            const uint8_t lo = row[wx * 2 + 0];
            const uint8_t hi = row[wx * 2 + 1];
            for (int p = 0; p < 8; ++p)
                cb(wx * 16 + p,     y, ((lo >> (7 - p)) & 1) != 0);
            for (int p = 0; p < 8; ++p)
                cb(wx * 16 + 8 + p, y, ((hi >> (7 - p)) & 1) != 0);
        }
    }
}

void Emulator::forEachLoResPixel(const LoResPixelCb &cb) const
{
    if (isHires()) return;
    const uint8_t *vramBytes = board_get_vram(&impl_->board);
    /* 320 colour pixels per scanline, 200 scanlines.  Each 16-bit
     * word in VRAM = 8 pixels: low byte is the pixel data
     * (MSB = leftmost), high byte is the attribute byte
     * (F=flash, I=bright, GRB' = bg, GRB = fg). */
    for (int y = 0; y < 200; ++y) {
        const uint8_t *row = vramBytes + y * 80;   /* 40 words = 80 bytes */
        for (int wx = 0; wx < 40; ++wx) {
            const uint8_t pix  = row[wx * 2 + 0];
            const uint8_t attr = row[wx * 2 + 1];
            const LoResAttr a {
                .flash  = ((attr >> 7) & 1) != 0,
                .bright = ((attr >> 6) & 1) != 0,
                .bgGrb  = static_cast<uint8_t>((attr >> 3) & 0x07),
                .fgGrb  = static_cast<uint8_t>((attr >> 0) & 0x07),
            };
            for (int p = 0; p < 8; ++p) {
                const bool lit = ((pix >> (7 - p)) & 1) != 0;
                cb(wx * 8 + p, y, lit, a);
            }
        }
    }
}

/* ── Callbacks ──────────────────────────────────────────────────────────── */

void Emulator::setSoundCallback(SoundCallback cb)
{
    impl_->soundCb = std::move(cb);
    board_set_sound_callback(&impl_->board,
                             impl_->soundCb ? &cSoundTrampoline : nullptr,
                             this);
}

void Emulator::setSerialCallbacks(SerialInCallback in, SerialOutCallback out)
{
    impl_->serialInCb  = std::move(in);
    impl_->serialOutCb = std::move(out);
    board_set_serial_callbacks(
        &impl_->board,
        impl_->serialInCb  ? &cSerialInTrampoline  : nullptr,
        impl_->serialOutCb ? &cSerialOutTrampoline : nullptr,
        this);
}

/* ── Internal pointer rewiring ──────────────────────────────────────────── */

void Emulator::rewirePointers()
{
    impl_->board.cpu.board = &impl_->board;

    impl_->board.kbd.tx_callback = [](void *ctx, uint8_t byte) {
        ms7004_host_byte(static_cast<ms7004_t *>(ctx), byte);
    };
    impl_->board.kbd.tx_callback_ctx = &impl_->kbd7004;

    impl_->kbd7004.uart = &impl_->board.kbd;

    if (impl_->soundCb)
        board_set_sound_callback(&impl_->board, &cSoundTrampoline, this);
    if (impl_->serialInCb || impl_->serialOutCb)
        board_set_serial_callbacks(
            &impl_->board,
            impl_->serialInCb  ? &cSerialInTrampoline  : nullptr,
            impl_->serialOutCb ? &cSerialOutTrampoline : nullptr,
            this);
}

/* ── Snapshot ──────────────────────────────────────────────────────────── */

uint32_t Emulator::romCrc32() const noexcept
{
    return snap_crc32(impl_->board.mem.rom, MEM_ROM_SIZE);
}

std::expected<void, std::string> Emulator::saveState(std::string_view path)
{
    std::ofstream f(std::string{path}, std::ios::binary);
    if (!f)
        return std::unexpected{"Cannot open file for writing"};

    const char *paths[4] = {};
    for (int i = 0; i < 4; i++) {
        if (!diskPath_[i].empty())
            paths[i] = diskPath_[i].c_str();
    }

    snap_io_t io{fstreamWrite, nullptr, nullptr, &f};
    snap_error_t err = snap_save(&impl_->board, &impl_->kbd7004,
                                 romCrc32(), paths, &io);

    if (err != SNAP_OK)
        return std::unexpected{"Failed to write snapshot data"};
    return {};
}

std::expected<void, std::string> Emulator::loadState(std::string_view path)
{
    std::ifstream f(std::string{path}, std::ios::binary);
    if (!f)
        return std::unexpected{"Cannot open snapshot file"};

    uint32_t expected_crc = romCrc32();

    /* Detach all disks before overwriting FDC state */
    for (int i = 0; i < 4; i++)
        fdc_detach(&impl_->board.fdc, i);

    uint32_t saved_crc = 0;
    char *disk_paths[4] = {};
    snap_io_t io{nullptr, fstreamRead, fstreamSeekIn, &f};
    snap_error_t err = snap_load(&impl_->board, &impl_->kbd7004,
                                 &saved_crc, disk_paths, &io);

    if (err != SNAP_OK) {
        constexpr const char *msgs[] = {
            "OK", "I/O error", "Not a snapshot file",
            "Unsupported version", "ROM mismatch", "Corrupt data"
        };
        rewirePointers();
        return std::unexpected{std::string{msgs[err < 6 ? err : 5]}};
    }

    if (saved_crc != expected_crc) {
        rewirePointers();
        for (int i = 0; i < 4; i++)
            free(disk_paths[i]);  // NOLINT — allocated by C snap_load
        return std::unexpected{"ROM CRC mismatch — snapshot was saved with a different ROM"};
    }

    rewirePointers();

    /* Re-mount disk images */
    for (int i = 0; i < 4; i++) {
        diskPath_[i].clear();
        if (disk_paths[i]) {
            if (fdc_attach(&impl_->board.fdc, i, disk_paths[i], false))
                diskPath_[i] = disk_paths[i];
            free(disk_paths[i]);  // NOLINT — allocated by C snap_load
        }
    }

    return {};
}

} /* namespace ms0515 */
