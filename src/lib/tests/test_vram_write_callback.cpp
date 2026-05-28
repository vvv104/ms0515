/*
 * test_vram_write_callback.cpp — Emulator::setVramWriteCallback.
 *
 * Verifies the lib-side passthrough to the C core hook fires for
 * every VRAM byte the CPU writes — both direct memory writes via
 * the public API and CPU-issued MOV instructions, with the latter
 * the path VramMirror actually uses in production.
 */

#include <doctest/doctest.h>

#include <ms0515/Emulator.hpp>

#include "../src/EmulatorInternal.hpp"

extern "C" {
#include <ms0515/core/board.h>
#include <ms0515/core/memory.h>
}

#include <vector>

namespace {

/* Pick a VRAM-window mapping that lets the CPU address VRAM at a
 * convenient virtual range.  Dispatcher bit 7 = VRAM-enable;
 * bits 10/11 select which 16 KB virtual window maps to VRAM.
 * Setting both window bits to 0 maps VRAM at virtual 0..037777. */
void enable_vram_window(ms0515::Emulator &emu)
{
    auto &mem = ms0515::internal::board(emu).mem;
    mem.dispatcher = MEM_DISP_VRAM_EN;          /* window bits = 00 */
}

}  // namespace

TEST_SUITE("Emulator VRAM write callback") {

TEST_CASE("callback fires for every VRAM byte the CPU stores, with the right offset") {
    ms0515::Emulator emu;
    enable_vram_window(emu);

    struct Event { uint16_t offset; uint8_t value; };
    std::vector<Event> events;
    emu.setVramWriteCallback([&](uint16_t off, uint8_t val) {
        events.push_back({off, val});
    });

    /* Drive VRAM writes through the public memory-write helpers — these
     * route through mem_write_byte in the same way a CPU MOV does. */
    emu.writeByte(0x0000u, 0xABu);              /* VRAM offset 0 */
    emu.writeByte(0x0001u, 0xCDu);              /* VRAM offset 1 */

    REQUIRE(events.size() == 2);
    CHECK(events[0].offset == 0x0000u);
    CHECK(events[0].value  == 0xABu);
    CHECK(events[1].offset == 0x0001u);
    CHECK(events[1].value  == 0xCDu);
}

TEST_CASE("word write fires the callback twice — low byte then high byte") {
    ms0515::Emulator emu;
    enable_vram_window(emu);

    std::vector<std::pair<uint16_t, uint8_t>> events;
    emu.setVramWriteCallback([&](uint16_t off, uint8_t val) {
        events.emplace_back(off, val);
    });

    emu.writeWord(0x0100u, 0x1234u);

    REQUIRE(events.size() == 2);
    CHECK(events[0].first  == 0x0100u);
    CHECK(events[0].second == 0x34u);           /* low byte first */
    CHECK(events[1].first  == 0x0101u);
    CHECK(events[1].second == 0x12u);           /* then high byte */
}

TEST_CASE("RAM and ROM writes do NOT trigger the VRAM callback") {
    ms0515::Emulator emu;
    /* No VRAM window enabled — addresses route to RAM by default. */
    auto &mem = ms0515::internal::board(emu).mem;
    mem.dispatcher = 0x007F;                    /* all primary banks, VRAM off */

    int fired = 0;
    emu.setVramWriteCallback([&](uint16_t, uint8_t) { ++fired; });

    emu.writeByte(0x1000u, 0x55u);              /* lands in RAM */
    emu.writeByte(0x2000u, 0x66u);
    CHECK(fired == 0);
}

TEST_CASE("clearing the callback (empty std::function) stops firing") {
    ms0515::Emulator emu;
    enable_vram_window(emu);

    int fired = 0;
    emu.setVramWriteCallback([&](uint16_t, uint8_t) { ++fired; });
    emu.writeByte(0x0000u, 0x11u);
    REQUIRE(fired == 1);

    emu.setVramWriteCallback({});               /* detach */
    emu.writeByte(0x0001u, 0x22u);
    emu.writeByte(0x0002u, 0x33u);
    CHECK(fired == 1);                          /* still 1 — silent after detach */
}

}  // TEST_SUITE
