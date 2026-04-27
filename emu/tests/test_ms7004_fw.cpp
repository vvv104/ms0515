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
