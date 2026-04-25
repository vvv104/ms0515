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
#include <ms0515/keyboard.h>
#include <ms0515/memory.h>
#include <ms0515/ms7004.h>

#include <algorithm>
#include <cstdio>
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

/* ── Latin letter test data ─────────────────────────────────────────────── */

struct LetterCase {
    ms7004_key_t key;
    char         upper;   /* echo without Shift — OSA monitor default */
    char         lower;   /* echo with Shift — Shift inverts case in OSA */
};

/*
 * 26 Latin letters in alphabetic order.  OSA monitor (RT-11 derived)
 * uppercases all command-line input by default; pressing Shift
 * inverts to lowercase.  Per-letter assertions name the failing key
 * directly in the failure report.  Shift scancode generation itself
 * is covered by the unit tests in test_ms7004.cpp.
 */
constexpr LetterCase kLetters[] = {
    {MS7004_KEY_A, 'A', 'a'}, {MS7004_KEY_B, 'B', 'b'},
    {MS7004_KEY_C, 'C', 'c'}, {MS7004_KEY_D, 'D', 'd'},
    {MS7004_KEY_E, 'E', 'e'}, {MS7004_KEY_F, 'F', 'f'},
    {MS7004_KEY_G, 'G', 'g'}, {MS7004_KEY_H, 'H', 'h'},
    {MS7004_KEY_I, 'I', 'i'}, {MS7004_KEY_J, 'J', 'j'},
    {MS7004_KEY_K, 'K', 'k'}, {MS7004_KEY_L, 'L', 'l'},
    {MS7004_KEY_M, 'M', 'm'}, {MS7004_KEY_N, 'N', 'n'},
    {MS7004_KEY_O, 'O', 'o'}, {MS7004_KEY_P, 'P', 'p'},
    {MS7004_KEY_Q, 'Q', 'q'}, {MS7004_KEY_R, 'R', 'r'},
    {MS7004_KEY_S, 'S', 's'}, {MS7004_KEY_T, 'T', 't'},
    {MS7004_KEY_U, 'U', 'u'}, {MS7004_KEY_V, 'V', 'v'},
    {MS7004_KEY_W, 'W', 'w'}, {MS7004_KEY_X, 'X', 'x'},
    {MS7004_KEY_Y, 'Y', 'y'}, {MS7004_KEY_Z, 'Z', 'z'},
};

/*
 * Typing-test fixture: boots OSA to the prompt, returns an emulator
 * and screen reader ready for keystroke injection.  Encapsulating the
 * setup here keeps the actual scenarios short.
 */
struct TypingFixture {
    ms0515::Emulator     emu;
    ms0515::ScreenReader sr;
    TempDisk             disk;

    TypingFixture()
        : disk{kDiskFile}
    {
        REQUIRE(emu.loadRomFile(kRomFile));
        sr.buildFont({emu.board().mem.rom, MEM_ROM_SIZE});
        REQUIRE(emu.mountDisk(0, disk.path().string()));
        emu.reset();
        REQUIRE(bootToPrompt(emu, sr));
    }
};

/*
 * After bootToPrompt the cursor sits at row 4, column 1 (right after
 * the '.').  Each tapped character lands in the next column.  Read
 * the cell directly so we get a clean per-letter result.
 */
[[nodiscard]]
static char cellAt(const ms0515::ScreenReader::Snapshot &snap, int row, int col)
{
    return static_cast<char>(snap.cells[row * ms0515::ScreenReader::kHiresCols + col]);
}

/* ── Suite ──────────────────────────────────────────────────────────────── */

TEST_SUITE("KeyboardEmulated") {

/* ── ФКС (CapsLock) behaviour ──────────────────────────────────────────── */

/*
 * Helper to toggle ФКС (CapsLock) — single tap flips state.
 * ФКС on its own emits no scancode to the OS (the model applies the
 * effect locally per the cap-spec rules, see `effective_shift` in
 * ms7004.c), so we just drive `ms7004_key()` and let `ms7004.c`
 * track the toggle internally.
 */
static void toggleCaps(ms0515::Emulator &emu, int echoFrames = kEchoFrames)
{
    emu.keyPress(MS7004_KEY_CAPS, true);
    runFrames(emu, 1);
    emu.keyPress(MS7004_KEY_CAPS, false);
    runFrames(emu, echoFrames);
}

/*
 * Cap-spec for ФКС (per user, applied to BOTH РУС and ЛАТ modes):
 *   - ФКС on, Shift not held — letter case is inverted from default
 *     (ЛАТ default is uppercase, ФКС makes lowercase; РУС default is
 *     lowercase, ФКС makes uppercase).
 *   - ФКС on, Shift held — they cancel: result is the default case,
 *     same as no Shift no ФКС.
 *   - ФКС has no effect on non-letter keys: digits, punctuation,
 *     and symbol-on-letter positions in ЛАТ (Ш→[, Щ→], Э→\\, Ч→¬).
 */

TEST_CASE("ФКС inverts letter case in LAT mode (no Shift)") {
    TypingFixture fix;
    toggleCaps(fix.emu);   /* ФКС on */

    constexpr int kPromptRow = 4;
    constexpr int kCursorCol = 1;

    /* Pick a few letters; every Latin position behaves the same way. */
    static const std::pair<ms7004_key_t, char> kCases[] = {
        {MS7004_KEY_A, 'a'}, {MS7004_KEY_M, 'm'}, {MS7004_KEY_Z, 'z'},
    };
    for (auto [key, expected] : kCases) {
        tapKey(fix.emu, key);
        auto snap = readScreen(fix.emu, fix.sr);
        const char actual = cellAt(snap, kPromptRow, kCursorCol);
        CHECK_MESSAGE(actual == expected,
                      "ФКС+'", expected, "' produced '", actual, "'");
        tapKey(fix.emu, MS7004_KEY_BS);
    }
}

TEST_CASE("ФКС + Shift cancel: LAT letter back to uppercase default") {
    TypingFixture fix;
    toggleCaps(fix.emu);

    constexpr int kPromptRow = 4;
    constexpr int kCursorCol = 1;

    static const std::pair<ms7004_key_t, char> kCases[] = {
        {MS7004_KEY_A, 'A'}, {MS7004_KEY_M, 'M'}, {MS7004_KEY_Z, 'Z'},
    };
    for (auto [key, expected] : kCases) {
        tapKey(fix.emu, key, /*shift=*/true);
        auto snap = readScreen(fix.emu, fix.sr);
        const char actual = cellAt(snap, kPromptRow, kCursorCol);
        CHECK_MESSAGE(actual == expected,
                      "ФКС+Shift+'", expected, "' produced '", actual, "'");
        tapKey(fix.emu, MS7004_KEY_BS);
    }
}

TEST_CASE("ФКС has no effect on non-letter keys in LAT") {
    TypingFixture fix;
    toggleCaps(fix.emu);

    constexpr int kPromptRow = 4;
    constexpr int kCursorCol = 1;

    /* {key, expected_no_shift, expected_shift} — ФКС state is on the
     * whole time, but the result must equal the no-ФКС behaviour
     * captured by the digit / symbol / shift-immune tests above. */
    struct NonLetter {
        ms7004_key_t key;
        char         no_shift;
        char         shifted;
    };
    constexpr NonLetter kCases[] = {
        {MS7004_KEY_1,           '1',  '!'},   /* digit */
        {MS7004_KEY_SEMI_PLUS,   ';',  '+'},   /* punctuation */
        {MS7004_KEY_LBRACKET,    '[',  '['},   /* shift-immune ШЩЭЧ */
        {MS7004_KEY_RBRACKET,    ']',  ']'},
        {MS7004_KEY_BACKSLASH,   '\\', '\\'},
    };
    for (const auto &nl : kCases) {
        tapKey(fix.emu, nl.key);
        auto snap = readScreen(fix.emu, fix.sr);
        char actual = cellAt(snap, kPromptRow, kCursorCol);
        CHECK_MESSAGE(actual == nl.no_shift,
                      "ФКС+key produced '", actual, "', expected '",
                      nl.no_shift, "'");
        tapKey(fix.emu, MS7004_KEY_BS);

        tapKey(fix.emu, nl.key, /*shift=*/true);
        snap = readScreen(fix.emu, fix.sr);
        actual = cellAt(snap, kPromptRow, kCursorCol);
        CHECK_MESSAGE(actual == nl.shifted,
                      "ФКС+Shift+key produced '", actual, "', expected '",
                      nl.shifted, "'");
        tapKey(fix.emu, MS7004_KEY_BS);
    }
}

TEST_CASE("ФКС inverts letter case in RUS mode (no Shift)") {
    TypingFixture fix;
    toggleRusLat(fix.emu);
    runFrames(fix.emu, 10);
    toggleCaps(fix.emu);

    constexpr int kPromptRow = 4;
    constexpr int kCursorCol = 1;

    /* Sample of Russian letters — RUS default is lowercase, ФКС
     * should flip to uppercase.  KOI-8 uppercase codes 0xE0..0xFF. */
    struct CyrCase {
        ms7004_key_t key;
        const char  *name;
        uint8_t      upper;   /* expected with ФКС on, no Shift */
    };
    constexpr CyrCase kCases[] = {
        {MS7004_KEY_A,         "А", 0xE1},
        {MS7004_KEY_M,         "М", 0xED},
        {MS7004_KEY_Z,         "З", 0xFA},
        {MS7004_KEY_LBRACKET,  "Ш", 0xFB},   /* symbol-on-letter is letter in RUS */
    };
    for (const auto &c : kCases) {
        tapKey(fix.emu, c.key);
        auto snap = readScreen(fix.emu, fix.sr);
        const uint8_t actual =
            snap.cells[kPromptRow * ms0515::ScreenReader::kHiresCols + kCursorCol];
        CHECK_MESSAGE(actual == c.upper,
                      "ФКС+'", c.name, "' produced 0x",
                      std::hex, static_cast<int>(actual), std::dec,
                      " (expected 0x", std::hex, static_cast<int>(c.upper),
                      std::dec, ")");
        tapKey(fix.emu, MS7004_KEY_BS);
    }
}

TEST_CASE("ФКС + Shift cancel: RUS letter back to lowercase default") {
    TypingFixture fix;
    toggleRusLat(fix.emu);
    runFrames(fix.emu, 10);
    toggleCaps(fix.emu);

    constexpr int kPromptRow = 4;
    constexpr int kCursorCol = 1;

    struct CyrCase {
        ms7004_key_t key;
        const char  *name;
        uint8_t      lower;   /* expected with ФКС on AND Shift held */
    };
    constexpr CyrCase kCases[] = {
        {MS7004_KEY_A,         "А", 0xC1},
        {MS7004_KEY_M,         "М", 0xCD},
        {MS7004_KEY_Z,         "З", 0xDA},
        {MS7004_KEY_LBRACKET,  "Ш", 0xDB},
    };
    for (const auto &c : kCases) {
        tapKey(fix.emu, c.key, /*shift=*/true);
        auto snap = readScreen(fix.emu, fix.sr);
        const uint8_t actual =
            snap.cells[kPromptRow * ms0515::ScreenReader::kHiresCols + kCursorCol];
        CHECK_MESSAGE(actual == c.lower,
                      "ФКС+Shift+'", c.name, "' produced 0x",
                      std::hex, static_cast<int>(actual), std::dec,
                      " (expected 0x", std::hex, static_cast<int>(c.lower),
                      std::dec, ")");
        tapKey(fix.emu, MS7004_KEY_BS);
    }
}

/*
 * Diagnostic only — kept skip()'d so the suite stays fast.  Re-runs
 * a brute-force scancode → screen-cell map to chase down which raw
 * byte values produce specific glyphs in OSA.  Originally used to
 * show that '@' and '_' do not have unique scancodes — the ROM font
 * stores identical 8x8 patterns for ('@', '`') and ('_', 'Ъ'), so
 * what the screen-reader returns for those cells depends on which
 * KOI-8 code we register first in `ScreenReader::buildFont`.
 */
TEST_CASE("brute-force scancode → OSA echo (diagnostic)" * doctest::skip()) {
    /* Re-boot for each scancode — slow (~12 emulated seconds per
     * iteration × ~256 = a few minutes wall time), but isolates each
     * test from any state corruption (cursor drift, sticky modifiers,
     * line wrap) the previous scancode might have caused. */

    std::fprintf(stderr,
        "\n=== brute-force scancode scan: scancode → row 4 contents ===\n");

    for (int sc = 1; sc < 256; ++sc) {
        /* Modifiers + ALL-UP do not echo a glyph and would corrupt the
         * state machine.  Skip kbd command range (>=0o375 = host->kbd). */
        if (sc >= 0256 && sc <= 0263) continue;
        if (sc >= 0375) continue;

        TypingFixture fix;
        auto &kbd = fix.emu.board().kbd;

        kbd_push_scancode(&kbd, static_cast<uint8_t>(sc));
        runFrames(fix.emu, 30);

        auto snap = readScreen(fix.emu, fix.sr);
        const std::string row = snap.row(4);

        /* The dot is the prompt; anything after it is the echo. */
        std::string echo = row.substr(0, std::min<size_t>(row.size(), 12));

        /* Convert to printable hex for clarity. */
        std::string hex;
        for (char c : echo) {
            char buf[8];
            std::snprintf(buf, sizeof buf, "%02X ", static_cast<uint8_t>(c));
            hex += buf;
        }

        if (echo != ".") {
            std::fprintf(stderr,
                "  sc 0o%03o (0x%02X) -> %s| %s\n",
                sc, sc, hex.c_str(), echo.c_str());
        }
    }
}

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

/*
 * Latin letters, no Shift: tap each MS7004 letter key in alphabetic
 * order and verify OSA echoes the corresponding *uppercase* letter
 * (default monitor casing).  Single-letter-at-a-time with Backspace
 * between taps keeps the test independent of the monitor's command-
 * line buffer length.
 */
TEST_CASE("Latin letter keys echo as uppercase at OSA prompt") {
    TypingFixture fix;

    constexpr int kPromptRow = 4;
    constexpr int kCursorCol = 1;   /* first cell after the '.' */

    for (const auto &lc : kLetters) {
        tapKey(fix.emu, lc.key);
        auto snap = readScreen(fix.emu, fix.sr);
        const char actual = cellAt(snap, kPromptRow, kCursorCol);
        CHECK_MESSAGE(actual == lc.upper,
                      "key for '", lc.upper, "' produced '", actual, "'");

        tapKey(fix.emu, MS7004_KEY_BS);   /* erase to keep cursor at col 1 */
    }
}

/*
 * Latin letters, with Shift held: same set, but each tap holds Shift.
 * In OSA the Shift modifier inverts the default casing — so we expect
 * lowercase 'a'..'z' on the screen.  This is the canonical end-to-end
 * verification that the Shift scancode (0o256) reaches the OS and
 * actually changes the keyboard handler's behaviour.
 */
TEST_CASE("Latin letter keys with Shift echo as lowercase at OSA prompt") {
    TypingFixture fix;

    constexpr int kPromptRow = 4;
    constexpr int kCursorCol = 1;

    for (const auto &lc : kLetters) {
        tapKey(fix.emu, lc.key, /*shift=*/true);
        auto snap = readScreen(fix.emu, fix.sr);
        const char actual = cellAt(snap, kPromptRow, kCursorCol);
        CHECK_MESSAGE(actual == lc.lower,
                      "Shift+'", lc.upper, "' produced '", actual,
                      "' (expected '", lc.lower, "')");

        tapKey(fix.emu, MS7004_KEY_BS);
    }
}

/* ── Digit row test data ────────────────────────────────────────────────── */

struct DigitCase {
    ms7004_key_t key;
    char         digit;     /* echo without Shift */
    char         shifted;   /* echo with Shift held — Shift+0 is unchanged */
};

/*
 * Top-row digit keys.  Without Shift each echoes its own digit;
 * Shift maps via the standard typewriter layout (1!, 2", 3#, 4$, 5%,
 * 6&, 7', 8(, 9)).  Shift+0 has no alternate symbol on the MS7004
 * cap (assets/keyboard/ms7004_layout.txt row1) and OSA echoes a
 * plain '0'.  Note: the cap shows '¤' for Shift+4 but OSA renders
 * the ASCII-compatible '$' instead — the screen-reader glyph-match
 * resolves to that code.
 */
constexpr DigitCase kDigits[] = {
    {MS7004_KEY_1, '1', '!'}, {MS7004_KEY_2, '2', '"'},
    {MS7004_KEY_3, '3', '#'}, {MS7004_KEY_4, '4', '$'},
    {MS7004_KEY_5, '5', '%'}, {MS7004_KEY_6, '6', '&'},
    {MS7004_KEY_7, '7', '\''}, {MS7004_KEY_8, '8', '('},
    {MS7004_KEY_9, '9', ')'}, {MS7004_KEY_0, '0', '0'},
};

/*
 * Top-row digit keys 1..9, 0 — without modifiers, each should echo
 * the corresponding ASCII digit straight to the prompt.
 */
TEST_CASE("Digit keys echo at OSA prompt") {
    TypingFixture fix;

    constexpr int kPromptRow = 4;
    constexpr int kCursorCol = 1;

    for (const auto &dc : kDigits) {
        tapKey(fix.emu, dc.key);
        auto snap = readScreen(fix.emu, fix.sr);
        const char actual = cellAt(snap, kPromptRow, kCursorCol);
        CHECK_MESSAGE(actual == dc.digit,
                      "key for '", dc.digit, "' produced '", actual, "'");

        tapKey(fix.emu, MS7004_KEY_BS);
    }
}

/*
 * Shift + digit keys: typewriter-style symbols.  See kDigits above
 * for the mapping rationale and the Shift+0 / Shift+4 caveats.
 */
TEST_CASE("Shift + digit keys echo typewriter symbols at OSA prompt") {
    TypingFixture fix;

    constexpr int kPromptRow = 4;
    constexpr int kCursorCol = 1;

    for (const auto &dc : kDigits) {
        tapKey(fix.emu, dc.key, /*shift=*/true);
        auto snap = readScreen(fix.emu, fix.sr);
        const char actual = cellAt(snap, kPromptRow, kCursorCol);
        CHECK_MESSAGE(actual == dc.shifted,
                      "Shift+'", dc.digit, "' produced '", actual,
                      "' (expected '", dc.shifted, "')");

        tapKey(fix.emu, MS7004_KEY_BS);
    }
}

/* ── Punctuation / symbol keys ──────────────────────────────────────────── */

struct SymbolCase {
    ms7004_key_t key;
    char         label;     /* convenience name for failure messages */
    char         unshifted;
    char         shifted;
};

/*
 * Punctuation/symbol keys whose caps in `assets/keyboard/ms7004_layout.txt`
 * are formatted "top\nbottom" — top is the unshifted glyph, bottom is
 * the Shift glyph.  Only the keys whose cap labels are both clean
 * 7-bit ASCII end up here; the rest (RBRACE_LEFTUP shows the '↖'
 * arrow on Shift, TILDE has no shift glyph, BACKSLASH/CHE rely on
 * the Russian-mode top label) are listed separately or left out.
 */
constexpr SymbolCase kSymbols[] = {
    {MS7004_KEY_SEMI_PLUS,    ';',  ';',  '+'},
    {MS7004_KEY_MINUS_EQ,     '-',  '-',  '='},
    {MS7004_KEY_COLON_STAR,   ':',  ':',  '*'},
    {MS7004_KEY_PERIOD,       '.',  '.',  '>'},
    {MS7004_KEY_COMMA,        ',',  ',',  '<'},
    {MS7004_KEY_SLASH,        '/',  '/',  '?'},
    {MS7004_KEY_LBRACE_PIPE,  '{',  '{',  '|'},
};

TEST_CASE("Punctuation keys (unshifted) echo at OSA prompt") {
    TypingFixture fix;

    constexpr int kPromptRow = 4;
    constexpr int kCursorCol = 1;

    for (const auto &sc : kSymbols) {
        tapKey(fix.emu, sc.key);
        auto snap = readScreen(fix.emu, fix.sr);
        const char actual = cellAt(snap, kPromptRow, kCursorCol);
        CHECK_MESSAGE(actual == sc.unshifted,
                      "key '", sc.label, "' produced '", actual,
                      "' (expected '", sc.unshifted, "')");
        tapKey(fix.emu, MS7004_KEY_BS);
    }
}

TEST_CASE("Shift + punctuation keys echo at OSA prompt") {
    TypingFixture fix;

    constexpr int kPromptRow = 4;
    constexpr int kCursorCol = 1;

    for (const auto &sc : kSymbols) {
        tapKey(fix.emu, sc.key, /*shift=*/true);
        auto snap = readScreen(fix.emu, fix.sr);
        const char actual = cellAt(snap, kPromptRow, kCursorCol);
        CHECK_MESSAGE(actual == sc.shifted,
                      "Shift+'", sc.label, "' produced '", actual,
                      "' (expected '", sc.shifted, "')");
        tapKey(fix.emu, MS7004_KEY_BS);
    }
}

/* ── LAT-mode letter-position keys with non-letter Latin slot ───────────── */

/*
 * These cap positions are LETTER keys whose Latin slot (bottom cap
 * label per ms7004_layout.txt) is a single punctuation glyph instead
 * of a letter — keys ШЩЧЭЮ become `[`, `]`, `\\`, `@` in LAT mode
 * (KEY_CHE → ¬ is omitted since it is non-ASCII).
 *
 * Per the layout spec, in LAT mode each key emits its single Latin
 * glyph regardless of Shift — there is no second glyph to switch to.
 * IBM-style typewriter shift ([→{, ]→}, \\→|) is anachronistic here
 * (those pairings did not exist on Soviet keyboards of the era), and
 * `ms7004.c` neutralises any host Shift make code for these keys in
 * LAT mode.
 */
struct LatSpecialCase {
    ms7004_key_t key;
    const char  *name;
    char         glyph;     /* the one Latin glyph on the cap */
};

constexpr LatSpecialCase kLatSpecials[] = {
    {MS7004_KEY_LBRACKET,    "[",   '['},
    {MS7004_KEY_RBRACKET,    "]",   ']'},
    {MS7004_KEY_BACKSLASH,   "\\",  '\\'},
    {MS7004_KEY_AT,          "@",   '@'},
};

TEST_CASE("LAT-mode special keys (unshifted) echo cap symbol at OSA prompt") {
    TypingFixture fix;

    constexpr int kPromptRow = 4;
    constexpr int kCursorCol = 1;

    for (const auto &lc : kLatSpecials) {
        tapKey(fix.emu, lc.key);
        auto snap = readScreen(fix.emu, fix.sr);
        const char actual = cellAt(snap, kPromptRow, kCursorCol);
        CHECK_MESSAGE(actual == lc.glyph,
                      "key '", lc.name, "' produced '", actual,
                      "' (expected '", lc.glyph, "')");
        tapKey(fix.emu, MS7004_KEY_BS);
    }
}

TEST_CASE("Shift + LAT-mode special keys echo same glyph as unshifted") {
    TypingFixture fix;

    constexpr int kPromptRow = 4;
    constexpr int kCursorCol = 1;

    for (const auto &lc : kLatSpecials) {
        tapKey(fix.emu, lc.key, /*shift=*/true);
        auto snap = readScreen(fix.emu, fix.sr);
        const char actual = cellAt(snap, kPromptRow, kCursorCol);
        CHECK_MESSAGE(actual == lc.glyph,
                      "Shift+'", lc.name, "' produced '", actual,
                      "' (expected '", lc.glyph, "')");
        tapKey(fix.emu, MS7004_KEY_BS);
    }
}

/* ── Single-label keys (one glyph on the cap) ───────────────────────────── */

/*
 * Per the keyboard cap layout: keys that show only one symbol must
 * echo that symbol both unshifted AND with Shift held (Shift has no
 * second glyph to switch to).  Verified against the rendered MS7004
 * keyboard image.
 *
 * `0` (no shift symbol on cap) is already covered in the digits
 * tests above.
 *
 * HARDSIGN ('Ъ') is a Russian letter; it has no Latin slot on the
 * cap.  The ROM font stores an IDENTICAL 8x8 pixel pattern for Ъ
 * (KOI-8 0xFF) and underscore '_' (KOI-8 0x5F) — the two glyphs
 * are visually indistinguishable on screen.  ScreenReader resolves
 * the ambiguity in favour of the ASCII code (0x5F), so we expect
 * '_' here even though the cap label is Ъ.
 *
 * MS7004_KEY_UNDERSCORE shares scancode 0o361 with HARDSIGN in
 * `ms7004.c` (the underscore key entry needs a different scancode
 * looked up from the MS7004 spec — for now it produces the same
 * '_' glyph as HARDSIGN, which happens to match its cap label).
 */
struct SingleLabelCase {
    ms7004_key_t key;
    const char  *name;
    uint8_t      koi8;     /* KOI-8 code expected on screen */
};

constexpr SingleLabelCase kSingleLabel[] = {
    {MS7004_KEY_TILDE,      "~",  static_cast<uint8_t>('~')},
    {MS7004_KEY_HARDSIGN,   "Ъ",  static_cast<uint8_t>('_')},  /* glyph collision */
    {MS7004_KEY_UNDERSCORE, "_",  static_cast<uint8_t>('_')},  /* same scancode as HARDSIGN */
};

/* ── RUS-mode Cyrillic letters ──────────────────────────────────────────── */

/*
 * Cyrillic letter mapping for the 31 letter-position keys (Ё and Ъ
 * are not part of this set: Ё isn't on the MS7004 main block, and Ъ
 * is single-label so it lives in `kSingleLabel`).  Each entry pairs
 * the MS7004 key with the KOI-8R codes for the Russian letter on
 * the cap top — lowercase (used without Shift) and uppercase (used
 * with Shift).  RUS mode in OSA inverts the casing rule from LAT:
 * default is lowercase, Shift produces uppercase.
 *
 * For ШЩЧЭЮ-type keys (cap shows Russian letter + Latin symbol),
 * the Russian letter is what surfaces in RUS mode and the same
 * case-inversion rule applies.
 */
struct CyrLetterCase {
    ms7004_key_t key;
    const char  *name;     /* Cyrillic letter, debug only */
    uint8_t      lower;    /* KOI-8 byte for lowercase */
    uint8_t      upper;    /* KOI-8 byte for uppercase */
};

constexpr CyrLetterCase kCyrLetters[] = {
    {MS7004_KEY_J,         "Й", 0xCA, 0xEA},
    {MS7004_KEY_C,         "Ц", 0xC3, 0xE3},
    {MS7004_KEY_U,         "У", 0xD5, 0xF5},
    {MS7004_KEY_K,         "К", 0xCB, 0xEB},
    {MS7004_KEY_E,         "Е", 0xC5, 0xE5},
    {MS7004_KEY_N,         "Н", 0xCE, 0xEE},
    {MS7004_KEY_G,         "Г", 0xC7, 0xE7},
    {MS7004_KEY_LBRACKET,  "Ш", 0xDB, 0xFB},
    {MS7004_KEY_RBRACKET,  "Щ", 0xDD, 0xFD},
    {MS7004_KEY_Z,         "З", 0xDA, 0xFA},
    {MS7004_KEY_H,         "Х", 0xC8, 0xE8},
    {MS7004_KEY_F,         "Ф", 0xC6, 0xE6},
    {MS7004_KEY_Y,         "Ы", 0xD9, 0xF9},
    {MS7004_KEY_W,         "В", 0xD7, 0xF7},
    {MS7004_KEY_A,         "А", 0xC1, 0xE1},
    {MS7004_KEY_P,         "П", 0xD0, 0xF0},
    {MS7004_KEY_R,         "Р", 0xD2, 0xF2},
    {MS7004_KEY_O,         "О", 0xCF, 0xEF},
    {MS7004_KEY_L,         "Л", 0xCC, 0xEC},
    {MS7004_KEY_D,         "Д", 0xC4, 0xE4},
    {MS7004_KEY_V,         "Ж", 0xD6, 0xF6},
    {MS7004_KEY_BACKSLASH, "Э", 0xDC, 0xFC},
    {MS7004_KEY_Q,         "Я", 0xD1, 0xF1},
    {MS7004_KEY_CHE,       "Ч", 0xDE, 0xFE},
    {MS7004_KEY_S,         "С", 0xD3, 0xF3},
    {MS7004_KEY_M,         "М", 0xCD, 0xED},
    {MS7004_KEY_I,         "И", 0xC9, 0xE9},
    {MS7004_KEY_T,         "Т", 0xD4, 0xF4},
    {MS7004_KEY_X,         "Ь", 0xD8, 0xF8},
    {MS7004_KEY_B,         "Б", 0xC2, 0xE2},
    {MS7004_KEY_AT,        "Ю", 0xC0, 0xE0},
};

TEST_CASE("RUS mode: letter keys echo lowercase Cyrillic at OSA prompt") {
    TypingFixture fix;
    toggleRusLat(fix.emu);
    runFrames(fix.emu, 10);   /* let OSA register the mode flip */

    constexpr int kPromptRow = 4;
    constexpr int kCursorCol = 1;

    for (const auto &cl : kCyrLetters) {
        tapKey(fix.emu, cl.key);
        auto snap = readScreen(fix.emu, fix.sr);
        const uint8_t actual =
            snap.cells[kPromptRow * ms0515::ScreenReader::kHiresCols + kCursorCol];
        CHECK_MESSAGE(actual == cl.lower,
                      "key '", cl.name, "' produced 0x",
                      std::hex, static_cast<int>(actual), std::dec,
                      " (expected lowercase 0x",
                      std::hex, static_cast<int>(cl.lower), std::dec, ")");
        tapKey(fix.emu, MS7004_KEY_BS);
    }
}

TEST_CASE("RUS mode: Shift + letter keys echo uppercase Cyrillic at OSA prompt") {
    TypingFixture fix;
    toggleRusLat(fix.emu);
    runFrames(fix.emu, 10);

    constexpr int kPromptRow = 4;
    constexpr int kCursorCol = 1;

    for (const auto &cl : kCyrLetters) {
        tapKey(fix.emu, cl.key, /*shift=*/true);
        auto snap = readScreen(fix.emu, fix.sr);
        const uint8_t actual =
            snap.cells[kPromptRow * ms0515::ScreenReader::kHiresCols + kCursorCol];
        CHECK_MESSAGE(actual == cl.upper,
                      "Shift+'", cl.name, "' produced 0x",
                      std::hex, static_cast<int>(actual), std::dec,
                      " (expected uppercase 0x",
                      std::hex, static_cast<int>(cl.upper), std::dec, ")");
        tapKey(fix.emu, MS7004_KEY_BS);
    }
}

TEST_CASE("Single-label keys echo the same glyph with and without Shift") {
    TypingFixture fix;

    constexpr int kPromptRow = 4;
    constexpr int kCursorCol = 1;

    for (const auto &sl : kSingleLabel) {
        tapKey(fix.emu, sl.key);
        auto snap = readScreen(fix.emu, fix.sr);
        uint8_t actual = snap.cells[kPromptRow * ms0515::ScreenReader::kHiresCols
                                     + kCursorCol];
        CHECK_MESSAGE(actual == sl.koi8,
                      "key '", sl.name, "' unshifted produced 0x",
                      std::hex, static_cast<int>(actual), std::dec,
                      " (expected 0x", std::hex, static_cast<int>(sl.koi8),
                      std::dec, ")");
        tapKey(fix.emu, MS7004_KEY_BS);

        tapKey(fix.emu, sl.key, /*shift=*/true);
        snap = readScreen(fix.emu, fix.sr);
        actual = snap.cells[kPromptRow * ms0515::ScreenReader::kHiresCols
                            + kCursorCol];
        CHECK_MESSAGE(actual == sl.koi8,
                      "Shift+'", sl.name, "' produced 0x",
                      std::hex, static_cast<int>(actual), std::dec,
                      " (expected 0x", std::hex, static_cast<int>(sl.koi8),
                      std::dec, ")");
        tapKey(fix.emu, MS7004_KEY_BS);
    }
}

}  /* TEST_SUITE */

}  /* namespace */
