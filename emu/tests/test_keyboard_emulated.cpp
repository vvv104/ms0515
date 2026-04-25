/*
 * test_keyboard_emulated.cpp — End-to-end MS7004 keyboard emulation tests.
 *
 * Drives keystrokes through the full pipeline:
 *
 *   ms7004_key()  →  kbd UART  →  CPU  →  OS  →  VRAM
 *
 * For each scenario we tap a key with optional modifiers, let the OS
 * echo the resulting character to the screen, then read the VRAM via
 * `ms0515::ScreenReader` and compare against the expected character.
 * Tests target the MS7004 hardware emulation specifically — the
 * SDL/OSK input layers above `ms7004_key()` are out of scope.
 *
 * Reference disk: `assets/disks/kbtest_osa.dsk` — an OSA (RT-11)
 * image that boots straight to the dot prompt with echo enabled.
 *
 * After a successful boot the screen looks roughly like this
 * (pixels rendered as KOI-8 codes by ScreenReader):
 *
 *     row 0:  "          НГМД готов, идет загрузка операционной системы ..."
 *     row 1:  ""
 *     row 2:  "ОСА    Версия 1.0"
 *     row 3:  ""
 *     row 4:  ".[cursor]"          ← prompt (a single '.' on the line)
 */

#include <doctest/doctest.h>
#include <ms0515/Emulator.hpp>
#include <ms0515/ScreenReader.hpp>
#include <ms0515/board.h>
#include <ms0515/memory.h>
#include <ms0515/ms7004.h>

#include <algorithm>
#include <filesystem>
#include <random>
#include <string>

namespace fs = std::filesystem;

#ifndef ASSETS_DIR
#error "ASSETS_DIR must be defined by the build system"
#endif

namespace {

constexpr const char *kRomFile  = ASSETS_DIR "/rom/ms0515-roma.rom";
constexpr const char *kDiskFile = ASSETS_DIR "/disks/kbtest_osa.dsk";

/* Boot is bounded by frames so a stuck OS does not freeze the test
 * suite.  Real-time, 600 frames at 50 Hz = 12 emulated seconds; OSA
 * comfortably reaches the dot prompt well before that. */
constexpr int kBootFramesMax = 600;

/* Frames to wait after each key press for the OS to echo. */
constexpr int kEchoFrames = 4;

/* ── TempDisk: writeable copy of a pristine asset disk ──────────────────── */

/*
 * Tests must NEVER write directly to assets/disks/*.dsk.  Some Soviet
 * OSes flush dirty buffer pages back on file close and corrupt the
 * image (see KNOWN_ISSUES.md "type STARTS.COM disk-corruption").  Each
 * test mounts its own copy in the OS temp dir; the original stays
 * pristine.  Copy is removed on destructor — ignore failures since the
 * file may still be open inside Emulator at that point.
 */
class TempDisk {
    fs::path path_;
public:
    explicit TempDisk(const fs::path &source)
    {
        std::random_device rd;
        const auto stem = source.stem().string();
        const auto ext  = source.extension().string();
        path_ = fs::temp_directory_path()
              / fs::path{"ms0515_kbtest_" + std::to_string(rd())
                         + "_" + stem + ext};
        fs::copy_file(source, path_, fs::copy_options::overwrite_existing);
    }
    ~TempDisk()
    {
        std::error_code ec;
        fs::remove(path_, ec);
    }
    TempDisk(const TempDisk &)            = delete;
    TempDisk &operator=(const TempDisk &) = delete;

    [[nodiscard]] const fs::path &path() const noexcept { return path_; }
};

/* ── Helpers ─────────────────────────────────────────────────────────────── */

static void runFrames(ms0515::Emulator &emu, int frames)
{
    for (int i = 0; i < frames; ++i)
        (void)emu.stepFrame();
}

[[nodiscard]]
static ms0515::ScreenReader::Snapshot readScreen(const ms0515::Emulator &emu,
                                                 ms0515::ScreenReader &sr)
{
    return sr.readScreen({emu.vram(), MEM_VRAM_SIZE}, emu.isHires());
}

/*
 * Spin frames until OSA reaches the dot prompt: row 4 (0-indexed)
 * starts with '.' AND the screen has been stable for `stableFrames`
 * frames in a row.  Returns true if reached within `kBootFramesMax`.
 */
[[nodiscard]]
static bool bootToPrompt(ms0515::Emulator &emu, ms0515::ScreenReader &sr,
                         int stableFrames = 20)
{
    constexpr int kPromptRow = 4;
    auto prev = readScreen(emu, sr);
    int stable = 0;

    for (int frame = 0; frame < kBootFramesMax; ++frame) {
        (void)emu.stepFrame();
        auto cur = readScreen(emu, sr);

        const std::string row = cur.row(kPromptRow);
        const bool atPrompt   = !row.empty() && row[0] == '.';

        if (atPrompt && cur.cells == prev.cells)
            ++stable;
        else
            stable = 0;

        if (atPrompt && stable >= stableFrames)
            return true;

        prev = cur;
    }
    return false;
}

/*
 * Tap a single key with optional modifiers.  Modifier scancodes are
 * pressed before the key and released after — matches how a real user
 * holds Shift/Ctrl while typing.  RUS/LAT toggle has its own helper
 * because it is a toggle, not a modifier.
 */
[[maybe_unused]]
static void tapKey(ms0515::Emulator &emu, ms7004_key_t key,
                   bool shift = false, bool ctrl = false,
                   int echoFrames = kEchoFrames)
{
    if (shift) emu.keyPress(MS7004_KEY_SHIFT_L, true);
    if (ctrl)  emu.keyPress(MS7004_KEY_CTRL,    true);
    emu.keyPress(key, true);
    runFrames(emu, 1);
    emu.keyPress(key, false);
    if (ctrl)  emu.keyPress(MS7004_KEY_CTRL,    false);
    if (shift) emu.keyPress(MS7004_KEY_SHIFT_L, false);
    runFrames(emu, echoFrames);
}

/* Toggle the РУС/ЛАТ language mode (single press flips). */
[[maybe_unused]]
static void toggleRusLat(ms0515::Emulator &emu, int echoFrames = kEchoFrames)
{
    emu.keyPress(MS7004_KEY_RUSLAT, true);
    runFrames(emu, 1);
    emu.keyPress(MS7004_KEY_RUSLAT, false);
    runFrames(emu, echoFrames);
}

/* ── Suite ──────────────────────────────────────────────────────────────── */

TEST_SUITE("KeyboardEmulated") {

/*
 * Smoke test — boots the OSA test image to the dot prompt.  This is
 * the foundation every keyboard scenario relies on; if it fails the
 * test image needs reviewing before any keystroke test will work.
 */
TEST_CASE("boot OSA to prompt") {
    ms0515::Emulator emu;
    REQUIRE(emu.loadRomFile(kRomFile));

    ms0515::ScreenReader sr;
    sr.buildFont({emu.board().mem.rom, MEM_ROM_SIZE});

    TempDisk td{kDiskFile};
    REQUIRE(emu.mountDisk(0, td.path().string()));

    emu.reset();
    CHECK_MESSAGE(bootToPrompt(emu, sr),
                  "OSA did not reach a stable dot-prompt within "
                  "kBootFramesMax frames");
}

}  /* TEST_SUITE */

}  /* namespace */
