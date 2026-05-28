/*
 * test_vram_write_hook.cpp — VRAM write observation hook.
 *
 * The hook is the foundation of VramMirror (which lives in the C++
 * lib): it lets a host-side observer see every byte the CPU stores
 * into video RAM, regardless of what window the CPU was using or
 * which OS-level path produced the write.  Word writes go through
 * mem_write_byte twice (low then high), so the hook fires twice and
 * the observer doesn't need a separate word path.
 */

#include <doctest/doctest.h>

extern "C" {
#include <ms0515/core/memory.h>
}

#include <vector>

namespace {

struct HookRecord {
    uint16_t offset;
    uint8_t  value;
};

struct Capture {
    std::vector<HookRecord> events;
    int                     other_ud_count = 0;
};

extern "C" void capture_hook(uint16_t offset, uint8_t value, void *ud)
{
    auto *cap = static_cast<Capture *>(ud);
    cap->events.push_back({offset, value});
}

extern "C" void other_hook(uint16_t /*offset*/, uint8_t /*value*/, void *ud)
{
    auto *cap = static_cast<Capture *>(ud);
    cap->other_ud_count++;
}

/* Build a fresh memory + a translation pointing at VRAM offset `off`. */
mem_translation_t vram_tr(uint32_t off)
{
    return mem_translation_t{ADDR_TYPE_VRAM, off};
}

mem_translation_t ram_tr(uint32_t off)
{
    return mem_translation_t{ADDR_TYPE_RAM, off};
}

mem_translation_t rom_tr(uint32_t off)
{
    return mem_translation_t{ADDR_TYPE_ROM, off};
}

}  // namespace

TEST_SUITE("mem VRAM write hook") {

TEST_CASE("no hook installed → mem_write_byte to VRAM is a quiet store") {
    ms0515_memory_t mem{};
    mem_init(&mem);
    /* No hook installed.  The write must land in vram[] without
     * crashing — this is the default-state guarantee for the system
     * emulator and the existing tests. */
    mem_write_byte(&mem, vram_tr(0x123u), 0x5Au);
    CHECK(mem.vram[0x123u] == 0x5Au);
}

TEST_CASE("hook fires for VRAM byte writes with the right offset/value") {
    ms0515_memory_t mem{};
    mem_init(&mem);
    Capture cap;
    mem_set_vram_write_hook(&mem, capture_hook, &cap);

    mem_write_byte(&mem, vram_tr(0u), 0x11u);
    mem_write_byte(&mem, vram_tr(0x3FFFu), 0x22u);  /* last byte of VRAM */
    mem_write_byte(&mem, vram_tr(0x80u), 0x00u);    /* zero is still an event */

    REQUIRE(cap.events.size() == 3);
    CHECK(cap.events[0].offset == 0x0000u);
    CHECK(cap.events[0].value  == 0x11u);
    CHECK(cap.events[1].offset == 0x3FFFu);
    CHECK(cap.events[1].value  == 0x22u);
    CHECK(cap.events[2].offset == 0x0080u);
    CHECK(cap.events[2].value  == 0x00u);

    /* Side-effect on the array still happens — the hook is observation
     * only, it doesn't replace the write. */
    CHECK(mem.vram[0x0000u] == 0x11u);
    CHECK(mem.vram[0x3FFFu] == 0x22u);
    CHECK(mem.vram[0x0080u] == 0x00u);
}

TEST_CASE("hook does NOT fire for RAM or ROM writes") {
    ms0515_memory_t mem{};
    mem_init(&mem);
    Capture cap;
    mem_set_vram_write_hook(&mem, capture_hook, &cap);

    mem_write_byte(&mem, ram_tr(0x1000u), 0xAA);
    mem_write_byte(&mem, rom_tr(0x0200u), 0xBB);  /* ROM write is a no-op anyway */

    CHECK(cap.events.empty());
}

TEST_CASE("mem_write_word fires the hook twice — low byte then high byte") {
    ms0515_memory_t mem{};
    mem_init(&mem);
    Capture cap;
    mem_set_vram_write_hook(&mem, capture_hook, &cap);

    /* PDP-11 little-endian: byte 0 = low (0x34), byte 1 = high (0x12). */
    mem_write_word(&mem, vram_tr(0x100u), 0x1234u);

    REQUIRE(cap.events.size() == 2);
    CHECK(cap.events[0].offset == 0x100u);
    CHECK(cap.events[0].value  == 0x34u);
    CHECK(cap.events[1].offset == 0x101u);
    CHECK(cap.events[1].value  == 0x12u);
}

TEST_CASE("user-data pointer is delivered untouched on every event") {
    ms0515_memory_t mem{};
    mem_init(&mem);
    Capture cap_a;
    Capture cap_b;
    mem_set_vram_write_hook(&mem, capture_hook, &cap_a);

    mem_write_byte(&mem, vram_tr(0u), 0x11u);
    REQUIRE(cap_a.events.size() == 1);
    CHECK(cap_b.events.empty());

    /* Re-point hook at a different observer — events now route there. */
    mem_set_vram_write_hook(&mem, other_hook, &cap_b);
    mem_write_byte(&mem, vram_tr(0u), 0x22u);
    CHECK(cap_a.events.size() == 1);     /* unchanged */
    CHECK(cap_b.other_ud_count == 1);    /* new event landed via other ud */
}

TEST_CASE("setting hook back to NULL stops firing — subsequent writes are silent") {
    ms0515_memory_t mem{};
    mem_init(&mem);
    Capture cap;
    mem_set_vram_write_hook(&mem, capture_hook, &cap);
    mem_write_byte(&mem, vram_tr(0u), 0x01u);
    REQUIRE(cap.events.size() == 1);

    mem_set_vram_write_hook(&mem, nullptr, nullptr);
    mem_write_byte(&mem, vram_tr(0u), 0x02u);
    mem_write_byte(&mem, vram_tr(1u), 0x03u);
    CHECK(cap.events.size() == 1);            /* still 1 — silent after detach */
    CHECK(mem.vram[0u] == 0x02u);             /* but the writes still landed   */
    CHECK(mem.vram[1u] == 0x03u);
}

}  // TEST_SUITE
