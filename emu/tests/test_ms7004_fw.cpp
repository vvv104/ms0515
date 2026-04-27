#define _CRT_SECURE_NO_WARNINGS
/*
 * test_ms7004_fw.cpp — phase 3b smoke tests for the firmware-driven
 * MS 7004 backend.
 *
 * Goal of phase 3b: prove the real keyboard firmware boots on top of
 * our i8035 + i8243 cores without crashing.  Tests load the ROM,
 * reset, run for several thousand cycles, and assert PC ends up past
 * the boot init in the main poll loop region.
 */
#include <doctest/doctest.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

extern "C" {
#include <ms0515/ms7004_fw.h>
}

#ifndef ASSETS_DIR
#error "ASSETS_DIR must be defined by the build system"
#endif

TEST_SUITE("ms7004_fw") {

/* Load the firmware ROM into a heap-owned blob; doctest-friendly
 * REQUIRE on every step so a missing asset surfaces immediately. */
static std::vector<uint8_t> load_firmware()
{
    std::string path = std::string{ASSETS_DIR} + "/rom/mc7004_keyboard_original.rom";
    FILE *f = std::fopen(path.c_str(), "rb");
    REQUIRE(f != nullptr);
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    REQUIRE(sz == 2048);                  /* exactly 2 KB */
    std::vector<uint8_t> buf((size_t)sz);
    REQUIRE(std::fread(buf.data(), 1, (size_t)sz, f) == (size_t)sz);
    std::fclose(f);
    return buf;
}

TEST_CASE("Firmware ROM is exactly 2048 bytes and loads without error") {
    auto rom = load_firmware();
    CHECK(rom.size() == 2048);
}

TEST_CASE("Reset puts CPU at PC=0 with bank 0 selected") {
    auto rom = load_firmware();
    ms7004_fw_t fw;
    ms7004_fw_init(&fw, rom.data(), (uint16_t)rom.size(), nullptr);
    CHECK(fw.cpu.pc == 0);
    CHECK((fw.cpu.psw & 0x10) == 0);      /* BS=0 → register bank 0 */
    /* No keys pressed initially. */
    for (int c = 0; c < 16; ++c) CHECK(fw.matrix[c] == 0);
    CHECK(fw.keylatch == false);
}

TEST_CASE("Firmware reaches the main poll loop without crashing") {
    /* The reset vector at 000H jumps to 133H (main entry), which sets
     * up timer interrupts, blinks LEDs, beeps the speaker twice, and
     * settles into a poll loop somewhere past 148H.  We give it a
     * generous budget — speaker beeps alone cost ~30 000 cycles each
     * via the 0E4H delay loop — and assert PC ends up past the boot
     * init prologue. */
    auto rom = load_firmware();
    ms7004_fw_t fw;
    ms7004_fw_init(&fw, rom.data(), (uint16_t)rom.size(), nullptr);

    /* 200 000 cycles ≈ 650 ms wall time on the real chip — well past
     * the two boot beeps and into the steady-state poll loop. */
    int spent = ms7004_fw_run_cycles(&fw, 200000);
    CHECK(spent >= 200000);

    /* PC must have left the first 256 bytes of ROM (the ISRs and
     * helper subroutines live there; main loop is at 133H+). */
    CHECK(fw.cpu.pc >= 0x100);
    CHECK(fw.cpu.pc < 0x800);             /* still inside ROM image */
}

TEST_CASE("Boot leaves the TX line silent (no spurious bytes)") {
    /* The init speaker beep clobbers P1 (uninitialized RAM mirror)
     * and briefly lowers P1[7].  Our TX reassembler must not mistake
     * that for a UART start bit — the stop-bit validation rejects it
     * because P1[7] stays low during the beep. */
    auto rom = load_firmware();
    ms7004_fw_t fw;
    ms7004_fw_init(&fw, rom.data(), (uint16_t)rom.size(), nullptr);
    ms7004_fw_run_cycles(&fw, 200000);
    CHECK(fw.tx_history_count == 0);
}

TEST_CASE("Pressing F1 emits a scancode byte over the TX line") {
    /* F1 lives at column 12 row 1 (KBD12 PORT_BIT 0x02 — see
     * docs/kb/MS7004_WIRING.md).  After boot we press the key,
     * give the firmware time to scan, debounce, and bit-bang the
     * scancode, then check the TX history captured a byte. */
    auto rom = load_firmware();
    ms7004_fw_t fw;
    ms7004_fw_init(&fw, rom.data(), (uint16_t)rom.size(), nullptr);

    ms7004_fw_run_cycles(&fw, 200000);
    int boot_history = fw.tx_history_count;

    /* Press F1, then run long enough for the firmware to scan, debounce,
     * and bit-bang the scancode (~5 ms at 4800 baud per byte). */
    ms7004_fw_press(&fw, 12, 1, true);
    ms7004_fw_run_cycles(&fw, 4000000);

    CHECK(fw.tx_history_count > boot_history);
    /* Non-zero scancode emitted — we deliberately don't pin the exact
     * value here.  The hand-rolled kScancode table in ms7004.c was
     * derived from an OS-side keymap, not from the firmware itself;
     * reconciliation against what the real firmware emits is phase 4. */
    if (fw.tx_history_count > boot_history) {
        CHECK(fw.tx_history[boot_history] != 0);
    }
}

TEST_CASE("Host ID probe (0xAB) elicits a 2-byte response") {
    /* Sending 0xAB to the keyboard via !INT triggers the firmware's
     * external IRQ handler at 400H, which decodes the command and
     * pushes a 2-byte ID response (0x01, 0x00) back over TX.  This
     * verifies the full RX-IRQ-process-respond loop end to end. */
    auto rom = load_firmware();
    ms7004_fw_t fw;
    ms7004_fw_init(&fw, rom.data(), (uint16_t)rom.size(), nullptr);

    ms7004_fw_run_cycles(&fw, 200000);
    int boot_history = fw.tx_history_count;

    ms7004_fw_send_host_byte(&fw, 0xAB);
    ms7004_fw_run_cycles(&fw, 200000);

    REQUIRE(fw.tx_history_count >= boot_history + 2);
    CHECK(fw.tx_history[boot_history + 0] == 0x01);
    CHECK(fw.tx_history[boot_history + 1] == 0x00);
}

TEST_CASE("Key enum maps to the right matrix coords (firmware-derived)") {
    /* Smoke-check the enum→(col,row) lookup table by pressing a few
     * representative keys via the enum and inspecting the matrix
     * bits directly. */
    auto rom = load_firmware();
    ms7004_fw_t fw;
    ms7004_fw_init(&fw, rom.data(), (uint16_t)rom.size(), nullptr);

    ms7004_fw_key(&fw, MS7004_KEY_F1, true);          /* col 12 row 1 */
    CHECK((fw.matrix[12] & 0x02) != 0);
    ms7004_fw_key(&fw, MS7004_KEY_F1, false);
    CHECK(fw.matrix[12] == 0);

    ms7004_fw_key(&fw, MS7004_KEY_RETURN, true);      /* col 5  row 5 */
    CHECK((fw.matrix[5] & 0x20) != 0);
    ms7004_fw_key(&fw, MS7004_KEY_RETURN, false);

    ms7004_fw_key(&fw, MS7004_KEY_KP_5, true);        /* col 0  row 7 */
    CHECK((fw.matrix[0] & 0x80) != 0);

    /* Unmapped keys (caps that don't exist on the matrix) are no-ops. */
    ms7004_fw_key(&fw, MS7004_KEY_HARDSIGN, true);
    /* No matrix change beyond what we already pressed: */
    CHECK((fw.matrix[0] & 0x80) != 0);
    /* Sanity: nothing in column 11 (we only touched 0, 5, 12). */
    CHECK(fw.matrix[11] == 0);
}

TEST_CASE("Authentic firmware scancodes for a few keys") {
    /* Capture what bytes the real firmware emits for a handful of
     * named keys.  These values are what the host ROM's keyboard
     * driver actually receives — phase 4 will reconcile them with
     * the existing kScancode[] table in ms7004.c. */
    struct Sample { ms7004_key_t key; const char *name; };
    Sample samples[] = {
        { MS7004_KEY_F1,     "F1"     },
        { MS7004_KEY_F2,     "F2"     },
        { MS7004_KEY_RETURN, "RETURN" },
        { MS7004_KEY_SPACE,  "SPACE"  },
        { MS7004_KEY_A,      "A"      },
        { MS7004_KEY_KP_5,   "KP_5"   },
    };
    auto rom = load_firmware();
    for (auto &s : samples) {
        ms7004_fw_t fw;
        ms7004_fw_init(&fw, rom.data(), (uint16_t)rom.size(), nullptr);
        ms7004_fw_run_cycles(&fw, 200000);
        int before = fw.tx_history_count;

        ms7004_fw_key(&fw, s.key, true);
        ms7004_fw_run_cycles(&fw, 4000000);

        REQUIRE(fw.tx_history_count > before);
        std::fprintf(stderr, "  %-8s → 0x%02X (0o%03o)\n",
                     s.name, fw.tx_history[before], fw.tx_history[before]);
        CHECK(fw.tx_history[before] != 0);
    }
}

TEST_CASE("Pressing keys updates the matrix bits") {
    /* Phase 3b only: setting the matrix is enough — the keylatch
     * recompute on every i8243 write lands in 3c with the wiring of
     * the i8243 callback. */
    auto rom = load_firmware();
    ms7004_fw_t fw;
    ms7004_fw_init(&fw, rom.data(), (uint16_t)rom.size(), nullptr);

    ms7004_fw_press(&fw, 12, 1, true);    /* col=12 row=1 → KBD12 bit 1 = F1 */
    CHECK((fw.matrix[12] & 0x02) != 0);

    ms7004_fw_press(&fw, 12, 1, false);
    CHECK((fw.matrix[12] & 0x02) == 0);

    /* Out-of-range coords are silently ignored. */
    ms7004_fw_press(&fw, 16, 0, true);
    ms7004_fw_press(&fw,  0, 8, true);
    for (int c = 0; c < 16; ++c) CHECK(fw.matrix[c] == 0);
}

} /* TEST_SUITE */
