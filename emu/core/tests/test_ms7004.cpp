#include <doctest/doctest.h>
#include <cstring>

extern "C" {
#include <ms0515/ms7004.h>
#include <ms0515/keyboard.h>
}

TEST_SUITE("MS7004") {

/* ── Helpers ─────────────────────────────────────────────────────────────── */

/* Well-known scancodes from LK201 protocol. */
static constexpr uint8_t SC_ALLUP = 0263;

static ms0515_keyboard_t make_uart()
{
    ms0515_keyboard_t uart;
    kbd_init(&uart);
    return uart;
}

static ms7004_t make_kbd(ms0515_keyboard_t *uart)
{
    ms7004_t kbd;
    ms7004_init(&kbd, uart);
    return kbd;
}

/* Drain all bytes from UART FIFO into a buffer.  Returns count. */
static int drain_fifo(ms0515_keyboard_t *uart, uint8_t *buf, int max)
{
    int n = 0;
    while (n < max && uart->fifo_count > 0) {
        /* Tick enough to transfer one byte from FIFO to rx_data. */
        for (int i = 0; i < 20; i++) kbd_tick(uart);
        if (uart->rx_ready) {
            buf[n++] = kbd_read(uart, 0);
        } else {
            break;
        }
    }
    return n;
}

/* ── Init / Reset ────────────────────────────────────────────────────────── */

TEST_CASE("ms7004_init: all keys released, toggles off") {
    auto uart = make_uart();
    auto kbd  = make_kbd(&uart);

    CHECK(kbd.held_count == 0);
    CHECK(kbd.caps_on == false);
    CHECK(kbd.ruslat_on == false);
    CHECK(kbd.repeat_key == MS7004_KEY_NONE);
    CHECK(kbd.repeat_enabled == false);
    CHECK(kbd.data_enabled == true);
}

TEST_CASE("ms7004_reset preserves uart pointer") {
    auto uart = make_uart();
    auto kbd  = make_kbd(&uart);

    ms7004_key(&kbd, MS7004_KEY_A, true);
    ms7004_reset(&kbd);

    CHECK(kbd.uart == &uart);
    CHECK(kbd.held_count == 0);
    CHECK(kbd.caps_on == false);
}

/* ── Scancode lookup ─────────────────────────────────────────────────────── */

TEST_CASE("ms7004_scancode returns 0 for NONE and out-of-range") {
    CHECK(ms7004_scancode(MS7004_KEY_NONE) == 0);
    CHECK(ms7004_scancode(MS7004_KEY__COUNT) == 0);
    CHECK(ms7004_scancode((ms7004_key_t)255) == 0);
}

TEST_CASE("ms7004_scancode returns non-zero for valid keys") {
    CHECK(ms7004_scancode(MS7004_KEY_A) != 0);
    CHECK(ms7004_scancode(MS7004_KEY_SPACE) != 0);
    CHECK(ms7004_scancode(MS7004_KEY_RETURN) != 0);
    CHECK(ms7004_scancode(MS7004_KEY_F1) != 0);
}

TEST_CASE("well-known scancodes match expected values") {
    CHECK(ms7004_scancode(MS7004_KEY_SHIFT_L) == 0256);
    CHECK(ms7004_scancode(MS7004_KEY_SHIFT_R) == 0256);  /* same as left */
    CHECK(ms7004_scancode(MS7004_KEY_CTRL)    == 0257);
    CHECK(ms7004_scancode(MS7004_KEY_CAPS)    == 0260);
    CHECK(ms7004_scancode(MS7004_KEY_COMPOSE) == 0261);
    CHECK(ms7004_scancode(MS7004_KEY_RUSLAT)  == 0262);
}

/* ── Regular key press/release ───────────────────────────────────────────── */

TEST_CASE("regular key press emits scancode, release without modifier emits no ALL-UP") {
    auto uart = make_uart();
    auto kbd  = make_kbd(&uart);

    uint8_t expected_sc = ms7004_scancode(MS7004_KEY_A);
    ms7004_key(&kbd, MS7004_KEY_A, true);

    uint8_t buf[8];
    int n = drain_fifo(&uart, buf, 8);
    REQUIRE(n == 1);
    CHECK(buf[0] == expected_sc);

    /* Release — pure-regular session, NO ALL-UP (sidesteps a host-side
     * R0-leak bug that crashes some games when ALL-UP fires often). */
    ms7004_key(&kbd, MS7004_KEY_A, false);
    n = drain_fifo(&uart, buf, 8);
    CHECK(n == 0);
}

TEST_CASE("double press is idempotent") {
    auto uart = make_uart();
    auto kbd  = make_kbd(&uart);

    ms7004_key(&kbd, MS7004_KEY_A, true);
    ms7004_key(&kbd, MS7004_KEY_A, true);  /* duplicate — no-op */

    uint8_t buf[8];
    int n = drain_fifo(&uart, buf, 8);
    CHECK(n == 1);  /* only one scancode emitted */
}

TEST_CASE("double release is idempotent") {
    auto uart = make_uart();
    auto kbd  = make_kbd(&uart);

    /* Use Shift so we get a deterministic ALL-UP on full release. */
    ms7004_key(&kbd, MS7004_KEY_SHIFT_L, true);
    drain_fifo(&uart, nullptr, 0);  /* consume make code */
    kbd_flush_fifo(&uart);

    ms7004_key(&kbd, MS7004_KEY_SHIFT_L, false);
    ms7004_key(&kbd, MS7004_KEY_SHIFT_L, false);  /* duplicate — no-op */

    uint8_t buf[8];
    int n = drain_fifo(&uart, buf, 8);
    CHECK(n == 1);  /* only one ALL-UP */
    CHECK(buf[0] == SC_ALLUP);
}

/* Modifier release (after press alone) emits ALL-UP. */
TEST_CASE("modifier press/release emits ALL-UP") {
    auto uart = make_uart();
    auto kbd  = make_kbd(&uart);

    ms7004_key(&kbd, MS7004_KEY_SHIFT_L, true);
    kbd_flush_fifo(&uart);

    ms7004_key(&kbd, MS7004_KEY_SHIFT_L, false);

    uint8_t buf[8];
    int n = drain_fifo(&uart, buf, 8);
    REQUIRE(n == 1);
    CHECK(buf[0] == SC_ALLUP);
}

/* Modifier+letter session emits ALL-UP on full release.  Confirms the
 * "sticky Shift" path stays unbroken even though modifier-free sessions
 * no longer emit ALL-UP. */
TEST_CASE("Shift+letter session emits ALL-UP after both released") {
    auto uart = make_uart();
    auto kbd  = make_kbd(&uart);

    ms7004_key(&kbd, MS7004_KEY_SHIFT_L, true);
    ms7004_key(&kbd, MS7004_KEY_A, true);
    ms7004_key(&kbd, MS7004_KEY_A, false);
    ms7004_key(&kbd, MS7004_KEY_SHIFT_L, false);

    /* Drain everything and check the LAST byte is ALL-UP. */
    uint8_t buf[8];
    int n = drain_fifo(&uart, buf, 8);
    REQUIRE(n >= 1);
    CHECK(buf[n - 1] == SC_ALLUP);
}

/* After a modifier-bearing session ends with ALL-UP, a fresh
 * modifier-free session should NOT emit a second ALL-UP. */
TEST_CASE("session flag resets after ALL-UP — next regular keypress is silent") {
    auto uart = make_uart();
    auto kbd  = make_kbd(&uart);

    ms7004_key(&kbd, MS7004_KEY_SHIFT_L, true);
    ms7004_key(&kbd, MS7004_KEY_SHIFT_L, false);  /* emits ALL-UP */
    kbd_flush_fifo(&uart);

    ms7004_key(&kbd, MS7004_KEY_A, true);
    ms7004_key(&kbd, MS7004_KEY_A, false);

    uint8_t buf[8];
    int n = drain_fifo(&uart, buf, 8);
    REQUIRE(n == 1);
    CHECK(buf[0] == ms7004_scancode(MS7004_KEY_A));
}

/* ── Held modifier + regular key: ALL-UP on last release ─────────────────── */

TEST_CASE("ALL-UP only when last held key is released") {
    auto uart = make_uart();
    auto kbd  = make_kbd(&uart);

    ms7004_key(&kbd, MS7004_KEY_SHIFT_L, true);
    ms7004_key(&kbd, MS7004_KEY_A, true);
    kbd_flush_fifo(&uart);

    /* Release A — shift still held, no ALL-UP */
    ms7004_key(&kbd, MS7004_KEY_A, false);
    CHECK(uart.fifo_count == 0);
    CHECK(kbd.held_count == 1);

    /* Release shift — last key, ALL-UP emitted */
    ms7004_key(&kbd, MS7004_KEY_SHIFT_L, false);
    uint8_t buf[8];
    int n = drain_fifo(&uart, buf, 8);
    REQUIRE(n == 1);
    CHECK(buf[0] == SC_ALLUP);
}

/* ── Toggle keys ─────────────────────────────────────────────────────────── */

TEST_CASE("CAPS toggle flips on press, release is no-op") {
    auto uart = make_uart();
    auto kbd  = make_kbd(&uart);

    CHECK(ms7004_caps_on(&kbd) == false);

    ms7004_key(&kbd, MS7004_KEY_CAPS, true);
    CHECK(ms7004_caps_on(&kbd) == true);

    /* Toggle keys don't participate in held_count */
    CHECK(kbd.held_count == 0);

    /*
     * No scancode is emitted on a ФКС tap — the model intercepts the
     * toggle and applies the case-flip locally per the cap-spec rules
     * (see `effective_shift` in ms7004.c).  The OS therefore never
     * sees SC_CAPS, which prevents it from double-applying the toggle
     * (its own CapsLock semantics differ from what the cap demands).
     */
    CHECK(uart.fifo_count == 0);

    /* Release — no-op, no ALL-UP */
    ms7004_key(&kbd, MS7004_KEY_CAPS, false);
    CHECK(ms7004_caps_on(&kbd) == true);
    CHECK(uart.fifo_count == 0);

    /* Second press flips back */
    ms7004_key(&kbd, MS7004_KEY_CAPS, true);
    CHECK(ms7004_caps_on(&kbd) == false);
}

TEST_CASE("RUSLAT toggle") {
    auto uart = make_uart();
    auto kbd  = make_kbd(&uart);

    CHECK(ms7004_ruslat_on(&kbd) == false);

    ms7004_key(&kbd, MS7004_KEY_RUSLAT, true);
    CHECK(ms7004_ruslat_on(&kbd) == true);

    ms7004_key(&kbd, MS7004_KEY_RUSLAT, true);
    CHECK(ms7004_ruslat_on(&kbd) == false);
}

/* ── is_held query ───────────────────────────────────────────────────────── */

TEST_CASE("ms7004_is_held tracks regular key state") {
    auto uart = make_uart();
    auto kbd  = make_kbd(&uart);

    CHECK(ms7004_is_held(&kbd, MS7004_KEY_A) == false);

    ms7004_key(&kbd, MS7004_KEY_A, true);
    CHECK(ms7004_is_held(&kbd, MS7004_KEY_A) == true);

    ms7004_key(&kbd, MS7004_KEY_A, false);
    CHECK(ms7004_is_held(&kbd, MS7004_KEY_A) == false);
}

TEST_CASE("ms7004_is_held returns false for invalid keys") {
    auto uart = make_uart();
    auto kbd  = make_kbd(&uart);

    CHECK(ms7004_is_held(&kbd, MS7004_KEY_NONE) == false);
    CHECK(ms7004_is_held(&kbd, MS7004_KEY__COUNT) == false);
}

/* ── release_all ─────────────────────────────────────────────────────────── */

TEST_CASE("ms7004_release_all with modifier in session emits ALL-UP") {
    auto uart = make_uart();
    auto kbd  = make_kbd(&uart);

    ms7004_key(&kbd, MS7004_KEY_SHIFT_L, true);
    ms7004_key(&kbd, MS7004_KEY_A, true);
    ms7004_key(&kbd, MS7004_KEY_B, true);
    kbd_flush_fifo(&uart);

    ms7004_release_all(&kbd);

    CHECK(kbd.held_count == 0);
    CHECK(ms7004_is_held(&kbd, MS7004_KEY_A) == false);
    CHECK(ms7004_is_held(&kbd, MS7004_KEY_SHIFT_L) == false);

    uint8_t buf[4];
    int n = drain_fifo(&uart, buf, 4);
    REQUIRE(n == 1);
    CHECK(buf[0] == SC_ALLUP);
}

TEST_CASE("ms7004_release_all without modifier emits no ALL-UP") {
    auto uart = make_uart();
    auto kbd  = make_kbd(&uart);

    ms7004_key(&kbd, MS7004_KEY_A, true);
    ms7004_key(&kbd, MS7004_KEY_B, true);
    kbd_flush_fifo(&uart);

    ms7004_release_all(&kbd);

    CHECK(kbd.held_count == 0);
    CHECK(uart.fifo_count == 0);
}

TEST_CASE("ms7004_release_all with no held keys emits nothing") {
    auto uart = make_uart();
    auto kbd  = make_kbd(&uart);

    ms7004_release_all(&kbd);
    CHECK(uart.fifo_count == 0);
}

/* ── Auto-repeat ─────────────────────────────────────────────────────────── */

TEST_CASE("auto-repeat disabled by default, no repeat scancodes") {
    auto uart = make_uart();
    auto kbd  = make_kbd(&uart);

    ms7004_key(&kbd, MS7004_KEY_A, true);
    kbd_flush_fifo(&uart);

    /* Advance time well past the delay */
    ms7004_tick(&kbd, 1000);
    CHECK(uart.fifo_count == 0);
}

TEST_CASE("auto-repeat emits scancodes when enabled") {
    auto uart = make_uart();
    auto kbd  = make_kbd(&uart);
    kbd.repeat_enabled = true;

    ms7004_key(&kbd, MS7004_KEY_A, true);
    kbd_flush_fifo(&uart);

    /* Advance past initial delay (500 ms) */
    ms7004_tick(&kbd, 600);

    CHECK(uart.fifo_count > 0);

    uint8_t buf[16];
    int n = drain_fifo(&uart, buf, 16);
    CHECK(n > 0);
    /* All repeat codes should be the key's scancode */
    for (int i = 0; i < n; i++)
        CHECK(buf[i] == ms7004_scancode(MS7004_KEY_A));
}

TEST_CASE("auto-repeat stops when key is released") {
    auto uart = make_uart();
    auto kbd  = make_kbd(&uart);
    kbd.repeat_enabled = true;

    ms7004_key(&kbd, MS7004_KEY_A, true);
    ms7004_tick(&kbd, 100);
    kbd_flush_fifo(&uart);

    ms7004_key(&kbd, MS7004_KEY_A, false);
    kbd_flush_fifo(&uart);

    /* Further ticks should produce nothing */
    ms7004_tick(&kbd, 2000);
    CHECK(uart.fifo_count == 0);
}

TEST_CASE("auto-repeat eligibility matches MS-7004 firmware") {
    /* Verified against the 8035 ROM disassembly (routine L_29B):
     *   - scancode 0xBD (ВК) does NOT repeat
     *   - scancode  < 0x80 (entire top function row) does NOT repeat
     *   - scancode in 0x80..0x8F (Ф17–Ф20 + 6-key edit block) does NOT repeat
     *   - everything else (PF1–PF4, arrows, KP, ВВОД, letters …) DOES repeat */
    struct Spec { ms7004_key_t key; bool should_repeat; const char *label; };
    static const Spec kCases[] = {
        /* No-repeat — top row */
        { MS7004_KEY_F1,     false, "F1"     },
        { MS7004_KEY_F20,    false, "F20"    },
        /* No-repeat — 6-key editing block */
        { MS7004_KEY_FIND,   false, "НТ"     },
        { MS7004_KEY_INSERT, false, "ВСТ"    },
        { MS7004_KEY_REMOVE, false, "УДАЛ"   },
        { MS7004_KEY_SELECT, false, "ВЫБР"   },
        { MS7004_KEY_PREV,   false, "ПРЕД"   },
        { MS7004_KEY_NEXT,   false, "СЛЕД"   },
        /* No-repeat — ВК */
        { MS7004_KEY_RETURN, false, "ВК"     },
        /* Repeat — PF block, arrows, KP Enter, regular letters */
        { MS7004_KEY_PF1,    true,  "ПФ1"    },
        { MS7004_KEY_PF4,    true,  "ПФ4"    },
        { MS7004_KEY_UP,     true,  "Up"     },
        { MS7004_KEY_LEFT,   true,  "Left"   },
        { MS7004_KEY_KP_ENTER, true,"ВВОД"   },
        { MS7004_KEY_A,      true,  "A"      },
    };

    for (const auto &c : kCases) {
        auto uart = make_uart();
        auto kbd  = make_kbd(&uart);
        kbd.repeat_enabled = true;

        ms7004_key(&kbd, c.key, true);
        kbd_flush_fifo(&uart);            /* drain the press scancode */

        ms7004_tick(&kbd, 2000);          /* well past the 500 ms initial delay */

        if (c.should_repeat) {
            CHECK_MESSAGE(uart.fifo_count > 0,
                          c.label, " (sc=", (int)ms7004_scancode(c.key),
                          ") should auto-repeat but didn't");
        } else {
            CHECK_MESSAGE(uart.fifo_count == 0,
                          c.label, " (sc=", (int)ms7004_scancode(c.key),
                          ") repeated when it shouldn't");
        }
    }
}

TEST_CASE("modifier press cancels pending auto-repeat") {
    auto uart = make_uart();
    auto kbd  = make_kbd(&uart);
    kbd.repeat_enabled = true;

    ms7004_key(&kbd, MS7004_KEY_A, true);
    ms7004_tick(&kbd, 100);
    kbd_flush_fifo(&uart);

    /* Press modifier — cancels repeat */
    ms7004_key(&kbd, MS7004_KEY_CTRL, true);
    kbd_flush_fifo(&uart);

    /* Advance past delay — no repeat since modifier cancelled it */
    ms7004_tick(&kbd, 2000);
    CHECK(uart.fifo_count == 0);
}

/* ── Key stack fallback ──────────────────────────────────────────────────── */

TEST_CASE("releasing top key falls back repeat to previous") {
    auto uart = make_uart();
    auto kbd  = make_kbd(&uart);
    kbd.repeat_enabled = true;

    ms7004_key(&kbd, MS7004_KEY_A, true);
    ms7004_tick(&kbd, 50);
    ms7004_key(&kbd, MS7004_KEY_B, true);
    kbd_flush_fifo(&uart);

    /* B is the repeat key now */
    CHECK(kbd.repeat_key == MS7004_KEY_B);

    /* Release B — should fall back to A */
    ms7004_key(&kbd, MS7004_KEY_B, false);
    CHECK(kbd.repeat_key == MS7004_KEY_A);
}

/* ── Host commands ───────────────────────────────────────────────────────── */

TEST_CASE("ID probe (0xAB) responds with 0x01, 0x00") {
    auto uart = make_uart();
    auto kbd  = make_kbd(&uart);

    ms7004_host_byte(&kbd, 0xAB);

    uint8_t buf[8];
    int n = drain_fifo(&uart, buf, 8);
    REQUIRE(n == 2);
    CHECK(buf[0] == 0x01);
    CHECK(buf[1] == 0x00);
}

TEST_CASE("power-up reset (0xFD) responds with 4 bytes and resets state") {
    auto uart = make_uart();
    auto kbd  = make_kbd(&uart);
    kbd.repeat_enabled = true;

    ms7004_host_byte(&kbd, 0xFD);

    CHECK(kbd.repeat_enabled == false);

    uint8_t buf[8];
    int n = drain_fifo(&uart, buf, 8);
    REQUIRE(n == 4);
    CHECK(buf[0] == 0x01);
    CHECK(buf[1] == 0x00);
    CHECK(buf[2] == 0x00);
    CHECK(buf[3] == 0x00);
}

TEST_CASE("auto-repeat enable/disable commands") {
    auto uart = make_uart();
    auto kbd  = make_kbd(&uart);

    CHECK(kbd.repeat_enabled == false);

    ms7004_host_byte(&kbd, 0x90);   /* enable */
    CHECK(kbd.repeat_enabled == true);

    ms7004_host_byte(&kbd, 0xE1);   /* disable */
    CHECK(kbd.repeat_enabled == false);

    ms7004_host_byte(&kbd, 0xE3);   /* enable (alternate) */
    CHECK(kbd.repeat_enabled == true);

    ms7004_host_byte(&kbd, 0xD9);   /* disable (alternate) */
    CHECK(kbd.repeat_enabled == false);
}

TEST_CASE("data output enable/disable commands") {
    auto uart = make_uart();
    auto kbd  = make_kbd(&uart);

    CHECK(kbd.data_enabled == true);

    ms7004_host_byte(&kbd, 0x88);   /* disable */
    CHECK(kbd.data_enabled == false);

    ms7004_host_byte(&kbd, 0x8B);   /* enable */
    CHECK(kbd.data_enabled == true);
}

TEST_CASE("keyclick disable does NOT touch auto-repeat") {
    /* Per the 8035 firmware (routine L_44E in the ROM disasm),
     * 0x99 only clears bit 3 (click) in the parameters byte; bit 5
     * (auto-repeat) is independent and stays as-is.  Verifies our
     * earlier mis-interpretation that "0x99 also kills AR" is fixed —
     * SABOT2 needs AR to remain on after this command. */
    auto uart = make_uart();
    auto kbd  = make_kbd(&uart);
    kbd.repeat_enabled = true;

    ms7004_host_byte(&kbd, 0x99);   /* keyclick disabled */
    CHECK(kbd.click_enabled == false);
    CHECK(kbd.repeat_enabled == true);   /* must stay on */
}

/* ── Auto game-mode heuristic ───────────────────────────────────────────── */

TEST_CASE("auto game-mode: 0x99 swaps to game preset, click-on swaps back") {
    auto uart = make_uart();
    auto kbd  = make_kbd(&uart);
    REQUIRE(kbd.auto_game_mode == true);
    REQUIRE(kbd.in_game_mode == false);
    REQUIRE(kbd.repeat_delay_ms == kbd.repeat_typing_delay_ms);

    /* Game start: keyclick off → enter game mode. */
    ms7004_host_byte(&kbd, 0x99);
    CHECK(kbd.in_game_mode == true);
    CHECK(kbd.repeat_delay_ms == kbd.repeat_game_delay_ms);
    CHECK(kbd.repeat_period_ms == kbd.repeat_game_period_ms);

    /* OS shell returns: produce-click (0x9F) → leave game mode. */
    ms7004_host_byte(&kbd, 0x9F);
    CHECK(kbd.in_game_mode == false);
    CHECK(kbd.repeat_delay_ms == kbd.repeat_typing_delay_ms);
    CHECK(kbd.repeat_period_ms == kbd.repeat_typing_period_ms);
}

TEST_CASE("auto game-mode: BIOS ID probe (0xAB) ends game mode after Ctrl-C reboot") {
    /* SABOT2 → Ctrl-C → POST.  POST does NOT send 0xFD or 0x9F, but
     * always sends 0xAB at startup.  The heuristic uses that to leave
     * game mode. */
    auto uart = make_uart();
    auto kbd  = make_kbd(&uart);

    ms7004_host_byte(&kbd, 0x99);
    REQUIRE(kbd.in_game_mode == true);

    ms7004_host_byte(&kbd, 0xAB);   /* BIOS POST init */
    CHECK(kbd.in_game_mode == false);
    CHECK(kbd.repeat_delay_ms == kbd.repeat_typing_delay_ms);
}

TEST_CASE("auto game-mode: keyclick-enable 2-byte (0x1B X) ends game mode") {
    auto uart = make_uart();
    auto kbd  = make_kbd(&uart);

    ms7004_host_byte(&kbd, 0x99);
    REQUIRE(kbd.in_game_mode == true);

    ms7004_host_byte(&kbd, 0x1B);
    ms7004_host_byte(&kbd, 0x80);   /* volume byte completes the command */
    CHECK(kbd.in_game_mode == false);
    CHECK(kbd.click_enabled == true);
}

TEST_CASE("auto game-mode: power reset (0xFD) returns to typing preset") {
    auto uart = make_uart();
    auto kbd  = make_kbd(&uart);

    ms7004_host_byte(&kbd, 0x99);
    REQUIRE(kbd.in_game_mode == true);

    ms7004_host_byte(&kbd, 0xFD);   /* full keyboard reset */
    CHECK(kbd.in_game_mode == false);
    CHECK(kbd.repeat_delay_ms == kbd.repeat_typing_delay_ms);
    CHECK(kbd.repeat_period_ms == kbd.repeat_typing_period_ms);
}

TEST_CASE("auto game-mode: heuristic state latches even when toggle is off") {
    /* When the user disables auto-game-mode, live values revert to
     * typing — but the heuristic keeps tracking `in_game_mode` based on
     * commands.  Re-enabling auto-game-mode restores game preset
     * without needing the game to re-send 0x99 (which it won't). */
    auto uart = make_uart();
    auto kbd  = make_kbd(&uart);
    kbd.auto_game_mode = false;

    ms7004_host_byte(&kbd, 0x99);
    /* Heuristic state advances silently; live stays in typing preset. */
    CHECK(kbd.in_game_mode == true);
    CHECK(kbd.click_enabled == false);
    CHECK(kbd.repeat_delay_ms == kbd.repeat_typing_delay_ms);

    /* User re-enables the toggle (e.g. after typing a name).  Without
     * any further game command, live should snap to the game preset. */
    kbd.auto_game_mode = true;
    ms7004_recompute_live_repeat(&kbd);
    CHECK(kbd.repeat_delay_ms  == kbd.repeat_game_delay_ms);
    CHECK(kbd.repeat_period_ms == kbd.repeat_game_period_ms);
}

TEST_CASE("auto game-mode: toggle off-then-on round-trip preserves game state") {
    /* Real-world scenario the user reported: SABOT2 → enter game mode
     * via 0x99 → user toggles auto off to type their high-score name
     * with normal typing delay → toggles back on to keep playing.  The
     * bug being guarded against was: toggling off cleared in_game_mode,
     * so flipping on landed in typing-mode permanently. */
    auto uart = make_uart();
    auto kbd  = make_kbd(&uart);

    ms7004_host_byte(&kbd, 0x99);                  /* game start */
    REQUIRE(kbd.in_game_mode == true);
    REQUIRE(kbd.repeat_delay_ms == kbd.repeat_game_delay_ms);

    /* User flips auto off.  Live drops to typing preset, but the
     * heuristic remembers the game session. */
    kbd.auto_game_mode = false;
    ms7004_recompute_live_repeat(&kbd);
    CHECK(kbd.in_game_mode == true);
    CHECK(kbd.repeat_delay_ms == kbd.repeat_typing_delay_ms);

    /* User flips auto back on.  Live should resume the game preset. */
    kbd.auto_game_mode = true;
    ms7004_recompute_live_repeat(&kbd);
    CHECK(kbd.in_game_mode == true);
    CHECK(kbd.repeat_delay_ms == kbd.repeat_game_delay_ms);
}

TEST_CASE("sound disable command") {
    auto uart = make_uart();
    auto kbd  = make_kbd(&uart);

    CHECK(kbd.sound_enabled == true);

    ms7004_host_byte(&kbd, 0xA1);   /* sound disabled */
    CHECK(kbd.sound_enabled == false);
}

/* ── 2-byte commands ─────────────────────────────────────────────────────── */

TEST_CASE("sound enable 2-byte command (0x23 + volume)") {
    auto uart = make_uart();
    auto kbd  = make_kbd(&uart);
    kbd.sound_enabled = false;

    ms7004_host_byte(&kbd, 0x23);
    CHECK(kbd.cmd_pending == 0x23);

    ms7004_host_byte(&kbd, 0x80);  /* volume byte */
    CHECK(kbd.cmd_pending == 0);
    CHECK(kbd.sound_enabled == true);
}

TEST_CASE("keyclick enable 2-byte command (0x1B + volume)") {
    auto uart = make_uart();
    auto kbd  = make_kbd(&uart);
    kbd.click_enabled = false;

    ms7004_host_byte(&kbd, 0x1B);
    ms7004_host_byte(&kbd, 0x80);
    CHECK(kbd.click_enabled == true);
}

TEST_CASE("LED control 2-byte command (0x13 + mask)") {
    auto uart = make_uart();
    auto kbd  = make_kbd(&uart);

    /* LED ON mode + valid mask */
    ms7004_host_byte(&kbd, 0x13);
    ms7004_host_byte(&kbd, 0x84);  /* CapsLock LED */
    CHECK(kbd.cmd_pending == 0);
}

TEST_CASE("LED mode without valid mask acts as Latin indicator") {
    auto uart = make_uart();
    auto kbd  = make_kbd(&uart);

    /* 0x11 without LED mask = Latin indicator ON */
    ms7004_host_byte(&kbd, 0x11);
    ms7004_host_byte(&kbd, 0x90);  /* invalid LED mask, 0x90 > 0x8F */
    CHECK(kbd.latin_indicator == true);

    /* 0x13 without LED mask = Latin indicator OFF */
    ms7004_host_byte(&kbd, 0x13);
    ms7004_host_byte(&kbd, 0x90);
    CHECK(kbd.latin_indicator == false);
}

/* ── Invalid key ─────────────────────────────────────────────────────────── */

TEST_CASE("invalid key press/release is no-op") {
    auto uart = make_uart();
    auto kbd  = make_kbd(&uart);

    ms7004_key(&kbd, MS7004_KEY_NONE, true);
    ms7004_key(&kbd, MS7004_KEY__COUNT, true);
    CHECK(kbd.held_count == 0);
    CHECK(uart.fifo_count == 0);
}

/* ── NULL uart ───────────────────────────────────────────────────────────── */

TEST_CASE("ms7004 works without uart (NULL)") {
    ms7004_t kbd;
    ms7004_init(&kbd, nullptr);

    /* Should not crash */
    ms7004_key(&kbd, MS7004_KEY_A, true);
    CHECK(kbd.held_count == 1);

    ms7004_key(&kbd, MS7004_KEY_A, false);
    CHECK(kbd.held_count == 0);
}

} /* TEST_SUITE */
