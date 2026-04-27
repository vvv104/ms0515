#define _CRT_SECURE_NO_WARNINGS
/*
 * test_ms7004.cpp — public API tests for the firmware-driven MS 7004
 * keyboard.  Phase 3d-final replaced the hand-rolled state machine
 * with the real firmware ROM, so tests now verify externally
 * observable behaviour (scancodes on the wire, queries from the OSK)
 * rather than internal flag state.
 */
#include <doctest/doctest.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

extern "C" {
#include <ms0515/ms7004.h>
#include <ms0515/keyboard.h>
}

#ifndef ASSETS_DIR
#error "ASSETS_DIR must be defined by the build system"
#endif

TEST_SUITE("MS7004") {

/* ── Helpers ─────────────────────────────────────────────────────────────── */

static std::vector<uint8_t> load_firmware()
{
    std::string path = std::string{ASSETS_DIR} + "/rom/mc7004_keyboard_original.rom";
    FILE *f = std::fopen(path.c_str(), "rb");
    REQUIRE(f != nullptr);
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    REQUIRE(sz == 2048);
    std::vector<uint8_t> buf((size_t)sz);
    REQUIRE(std::fread(buf.data(), 1, (size_t)sz, f) == (size_t)sz);
    std::fclose(f);
    return buf;
}

/* Wrap a kbd + uart + firmware in one fixture so tests stay terse.
 * Each test gets a fresh boot; running the firmware long enough for
 * one scan pass + bit-bang TX takes about 4 M cycles. */
struct Fixture {
    std::vector<uint8_t> rom;
    ms0515_keyboard_t    uart;
    ms7004_t             kbd;

    Fixture() : rom(load_firmware()) {
        kbd_init(&uart);
        ms7004_init(&kbd, &uart);
        ms7004_attach_firmware(&kbd, rom.data(), (uint16_t)rom.size());
    }

    /* Push wall-clock time forward in 16 ms frame-sized chunks so
     * ms7004_tick's per-tick budget cap (100 ms) doesn't artificially
     * limit how many CPU cycles we run.  This mirrors how the real
     * frontend ticks the keyboard at 60 FPS. */
    uint32_t now_ms = 0;
    void tick_ms(uint32_t delta_ms) {
        const uint32_t step = 16;
        for (uint32_t elapsed = 0; elapsed < delta_ms; elapsed += step) {
            uint32_t advance = (delta_ms - elapsed < step) ? (delta_ms - elapsed) : step;
            now_ms += advance;
            ms7004_tick(&kbd, now_ms);
        }
    }
};

/* ── Init / firmware attach ──────────────────────────────────────────────── */

TEST_CASE("ms7004_init: clean slate, matrix empty, toggles off") {
    ms0515_keyboard_t uart; kbd_init(&uart);
    ms7004_t kbd;
    ms7004_init(&kbd, &uart);

    CHECK(kbd.uart == &uart);
    CHECK(kbd.firmware_rom == nullptr);
    CHECK(kbd.firmware_rom_size == 0);
    CHECK(kbd.caps_on == false);
    CHECK(kbd.ruslat_on == false);
    for (int c = 0; c < 16; ++c) CHECK(kbd.matrix[c] == 0);
}

TEST_CASE("ms7004_attach_firmware stores the ROM and resets CPU at PC=0") {
    Fixture f;
    CHECK(f.kbd.firmware_rom == f.rom.data());
    CHECK(f.kbd.firmware_rom_size == 2048);
    CHECK(f.kbd.cpu.pc == 0);
}

TEST_CASE("ms7004_reset preserves uart pointer and firmware binding") {
    Fixture f;
    f.tick_ms(700);                            /* boot finishes */
    CHECK(f.kbd.cpu.pc != 0);                  /* CPU executed */

    ms7004_reset(&f.kbd);
    CHECK(f.kbd.uart == &f.uart);
    CHECK(f.kbd.firmware_rom == f.rom.data());
    CHECK(f.kbd.cpu.pc == 0);                  /* CPU restarted */
    for (int c = 0; c < 16; ++c) CHECK(f.kbd.matrix[c] == 0);
}

/* ── Public input → matrix bit ───────────────────────────────────────────── */

TEST_CASE("ms7004_key sets the right matrix bit, release clears it") {
    Fixture f;
    /* F1 lives at column 12 row 1 (KBD12 PORT_BIT 0x02). */
    ms7004_key(&f.kbd, MS7004_KEY_F1, true);
    CHECK((f.kbd.matrix[12] & 0x02) != 0);
    CHECK(ms7004_is_held(&f.kbd, MS7004_KEY_F1));

    ms7004_key(&f.kbd, MS7004_KEY_F1, false);
    CHECK(f.kbd.matrix[12] == 0);
    CHECK(!ms7004_is_held(&f.kbd, MS7004_KEY_F1));
}

TEST_CASE("Unmapped enum caps (HARDSIGN, CHE, etc.) silently no-op") {
    Fixture f;
    ms7004_key(&f.kbd, MS7004_KEY_HARDSIGN, true);
    ms7004_key(&f.kbd, MS7004_KEY_CHE, true);
    for (int c = 0; c < 16; ++c) CHECK(f.kbd.matrix[c] == 0);
    CHECK(!ms7004_is_held(&f.kbd, MS7004_KEY_HARDSIGN));
}

TEST_CASE("ms7004_release_all clears the entire matrix") {
    Fixture f;
    ms7004_key(&f.kbd, MS7004_KEY_F1, true);
    ms7004_key(&f.kbd, MS7004_KEY_A, true);
    ms7004_key(&f.kbd, MS7004_KEY_RETURN, true);
    ms7004_release_all(&f.kbd);
    for (int c = 0; c < 16; ++c) CHECK(f.kbd.matrix[c] == 0);
}

/* ── Firmware-driven TX: pressing keys produces scancodes ────────────────── */

TEST_CASE("Pressing F1 emits scancode 0o126 over the UART") {
    Fixture f;
    f.tick_ms(700);                            /* boot completes */
    int boot_count = f.kbd.tx_history_count;

    ms7004_key(&f.kbd, MS7004_KEY_F1, true);
    /* ~13 s of wall-clock; enough for scan pass + bit-bang. */
    for (int i = 0; i < 13; ++i) f.tick_ms(1000);

    REQUIRE(f.kbd.tx_history_count > boot_count);
    CHECK(f.kbd.tx_history[boot_count] == 0126);
}

TEST_CASE("Several letter keys emit the expected canonical scancodes") {
    /* These are the same six samples used in phase 3d-prep —
     * a regression net for the (col, row) mapping table. */
    struct Sample { ms7004_key_t key; uint8_t expected; };
    Sample samples[] = {
        { MS7004_KEY_F2,     0127 },
        { MS7004_KEY_RETURN, 0275 },
        { MS7004_KEY_SPACE,  0324 },
        { MS7004_KEY_A,      0322 },
        { MS7004_KEY_KP_5,   0232 },
    };
    for (auto &s : samples) {
        Fixture f;
        f.tick_ms(700);
        int before = f.kbd.tx_history_count;
        ms7004_key(&f.kbd, s.key, true);
        for (int i = 0; i < 13; ++i) f.tick_ms(1000);
        REQUIRE(f.kbd.tx_history_count > before);
        CHECK(f.kbd.tx_history[before] == s.expected);
    }
}

/* ── Firmware-driven RX: host commands ──────────────────────────────────── */

TEST_CASE("Host ID probe (0xAB) → keyboard responds with 0x01, 0x00") {
    Fixture f;
    f.tick_ms(700);
    int before = f.kbd.tx_history_count;

    ms7004_host_byte(&f.kbd, 0xAB);
    f.tick_ms(700);

    REQUIRE(f.kbd.tx_history_count >= before + 2);
    CHECK(f.kbd.tx_history[before + 0] == 0x01);
    CHECK(f.kbd.tx_history[before + 1] == 0x00);
}

/* ── Toggle observation: caps_on / ruslat_on ─────────────────────────────── */

TEST_CASE("Pressing the RUSLAT key flips ruslat_on") {
    Fixture f;
    f.tick_ms(700);
    CHECK(f.kbd.ruslat_on == false);

    ms7004_key(&f.kbd, MS7004_KEY_RUSLAT, true);
    for (int i = 0; i < 13; ++i) f.tick_ms(1000);
    CHECK(f.kbd.ruslat_on == true);

    ms7004_key(&f.kbd, MS7004_KEY_RUSLAT, false);
    for (int i = 0; i < 5; ++i) f.tick_ms(1000);
    /* Release alone shouldn't toggle again — only the make-code does. */
    CHECK(f.kbd.ruslat_on == true);
}

/* ── Boot is silent (no spurious bytes) ──────────────────────────────────── */

TEST_CASE("Boot emits no TX bytes (stop-bit filter rejects speaker glitch)") {
    /* The init speaker beep clobbers an uninitialised RAM-mirror that
     * shadows P1, briefly dragging bit 7 low.  The TX reassembler's
     * stop-bit validation must reject this pseudo-byte. */
    Fixture f;
    f.tick_ms(700);
    CHECK(f.kbd.tx_history_count == 0);
}

/* ── Canonical scancode lookup ───────────────────────────────────────────── */

TEST_CASE("ms7004_scancode returns 0 for NONE / out-of-range") {
    CHECK(ms7004_scancode(MS7004_KEY_NONE) == 0);
    CHECK(ms7004_scancode((ms7004_key_t)999) == 0);
    CHECK(ms7004_scancode((ms7004_key_t)-1) == 0);
}

TEST_CASE("ms7004_scancode well-known values") {
    CHECK(ms7004_scancode(MS7004_KEY_F1)     == 0126);
    CHECK(ms7004_scancode(MS7004_KEY_RETURN) == 0275);
    CHECK(ms7004_scancode(MS7004_KEY_A)      == 0322);
    CHECK(ms7004_scancode(MS7004_KEY_SPACE)  == 0324);
}

} /* TEST_SUITE */
