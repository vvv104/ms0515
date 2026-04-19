/*
 * Emulator.cpp — Implementation of the C++ wrapper around ms0515_board_t.
 */

#include "ms0515/Emulator.hpp"

#include <fstream>
#include <string>
#include <utility>

extern "C" {
#include "ms0515/snapshot.h"
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

} /* anonymous namespace */

namespace ms0515 {

Emulator::Emulator()
    : board_(std::make_unique<ms0515_board_t>())
{
    board_init(board_.get());
    ms7004_init(&kbd7004_, &board_->kbd);

    /* Wire USART TX → ms7004 command channel. */
    board_->kbd.tx_callback = [](void *ctx, uint8_t byte) {
        ms7004_host_byte(static_cast<ms7004_t *>(ctx), byte);
    };
    board_->kbd.tx_callback_ctx = &kbd7004_;
}

Emulator::~Emulator()
{
    for (int i = 0; i < 4; ++i)
        fdc_detach(&board_->fdc, i);
    board_ramdisk_free(board_.get());
}

/* ── Lifecycle ──────────────────────────────────────────────────────────── */

void Emulator::reset()
{
    board_reset(board_.get());
    ms7004_reset(&kbd7004_);
}

void Emulator::loadRom(std::span<const uint8_t> data)
{
    board_load_rom(board_.get(), data.data(), static_cast<uint32_t>(data.size()));
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
    if (!fdc_attach(&board_->fdc, drive, pathStr.c_str(), /*read_only=*/false))
        return false;
    diskPath_[drive] = std::move(pathStr);
    return true;
}

void Emulator::unmountDisk(int drive)
{
    if (drive < 0 || drive >= 4)
        return;
    fdc_detach(&board_->fdc, drive);
    diskPath_[drive].clear();
}

void Emulator::enableRamDisk()
{
    board_ramdisk_enable(board_.get());
}

/* ── Execution ──────────────────────────────────────────────────────────── */

bool Emulator::stepFrame()
{
    return board_step_frame(board_.get());
}

void Emulator::stepInstruction()
{
    board_step_cpu(board_.get());
}

/* ── Input ──────────────────────────────────────────────────────────────── */

void Emulator::keyEvent(uint8_t scancode)
{
    board_key_event(board_.get(), scancode);
}

void Emulator::keyPress(ms7004_key_t key, bool down)
{
    ms7004_key(&kbd7004_, key, down);
}

void Emulator::keyReleaseAll()
{
    ms7004_release_all(&kbd7004_);
}

void Emulator::keyTick(uint32_t now_ms)
{
    ms7004_tick(&kbd7004_, now_ms);
}

bool Emulator::capsOn()   const noexcept { return ms7004_caps_on(&kbd7004_); }
bool Emulator::ruslatOn() const noexcept { return ms7004_ruslat_on(&kbd7004_); }

bool Emulator::keyHeld(ms7004_key_t key) const noexcept
{
    return ms7004_is_held(&kbd7004_, key);
}

/* ── Memory bus access ──────────────────────────────────────────────────── */

uint16_t Emulator::readWord(uint16_t address)
{
    return board_read_word(board_.get(), address);
}

uint8_t Emulator::readByte(uint16_t address)
{
    return board_read_byte(board_.get(), address);
}

void Emulator::writeWord(uint16_t address, uint16_t value)
{
    board_write_word(board_.get(), address, value);
}

void Emulator::writeByte(uint16_t address, uint8_t value)
{
    board_write_byte(board_.get(), address, value);
}

/* ── Video state ────────────────────────────────────────────────────────── */

const uint8_t *Emulator::vram() const noexcept
{
    return board_get_vram(board_.get());
}

bool Emulator::isHires() const noexcept
{
    return board_is_hires(board_.get());
}

uint8_t Emulator::borderColor() const noexcept
{
    return board_get_border_color(board_.get());
}

/* ── Callbacks ──────────────────────────────────────────────────────────── */

void Emulator::setSoundCallback(SoundCallback cb)
{
    soundCb_ = std::move(cb);
    board_set_sound_callback(board_.get(),
                             soundCb_ ? &Emulator::cSoundTrampoline : nullptr,
                             this);
}

void Emulator::setSerialCallbacks(SerialInCallback in, SerialOutCallback out)
{
    serialInCb_  = std::move(in);
    serialOutCb_ = std::move(out);
    board_set_serial_callbacks(
        board_.get(),
        serialInCb_  ? &Emulator::cSerialInTrampoline  : nullptr,
        serialOutCb_ ? &Emulator::cSerialOutTrampoline : nullptr,
        this);
}

void Emulator::cSoundTrampoline(void *userdata, int value)
{
    auto *self = static_cast<Emulator *>(userdata);
    if (self->soundCb_)
        self->soundCb_(value);
}

bool Emulator::cSerialOutTrampoline(void *userdata, uint8_t byte)
{
    auto *self = static_cast<Emulator *>(userdata);
    return self->serialOutCb_ ? self->serialOutCb_(byte) : false;
}

bool Emulator::cSerialInTrampoline(void *userdata, uint8_t *byte)
{
    auto *self = static_cast<Emulator *>(userdata);
    return self->serialInCb_ ? self->serialInCb_(*byte) : false;
}

/* ── Internal pointer rewiring ──────────────────────────────────────────── */

void Emulator::rewirePointers()
{
    board_->cpu.board = board_.get();

    board_->kbd.tx_callback = [](void *ctx, uint8_t byte) {
        ms7004_host_byte(static_cast<ms7004_t *>(ctx), byte);
    };
    board_->kbd.tx_callback_ctx = &kbd7004_;

    kbd7004_.uart = &board_->kbd;

    if (soundCb_)
        board_set_sound_callback(board_.get(), &Emulator::cSoundTrampoline, this);
    if (serialInCb_ || serialOutCb_)
        board_set_serial_callbacks(
            board_.get(),
            serialInCb_  ? &Emulator::cSerialInTrampoline  : nullptr,
            serialOutCb_ ? &Emulator::cSerialOutTrampoline : nullptr,
            this);
}

/* ── Snapshot ──────────────────────────────────────────────────────────── */

uint32_t Emulator::romCrc32() const noexcept
{
    return snap_crc32(board_->mem.rom, MEM_ROM_SIZE);
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
    snap_error_t err = snap_save(board_.get(), &kbd7004_,
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
        fdc_detach(&board_->fdc, i);

    uint32_t saved_crc = 0;
    char *disk_paths[4] = {};
    snap_io_t io{nullptr, fstreamRead, fstreamSeekIn, &f};
    snap_error_t err = snap_load(board_.get(), &kbd7004_,
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
            if (fdc_attach(&board_->fdc, i, disk_paths[i], false))
                diskPath_[i] = disk_paths[i];
            free(disk_paths[i]);  // NOLINT — allocated by C snap_load
        }
    }

    return {};
}

} /* namespace ms0515 */
