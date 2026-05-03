#include <doctest/doctest.h>
#include <cstring>

extern "C" {
#include <ms0515/core/memory.h>
}

TEST_SUITE("Memory") {

/* ── Helpers ─────────────────────────────────────────────────────────────── */

static ms0515_memory_t make_mem()
{
    ms0515_memory_t mem;
    mem_init(&mem);
    return mem;
}

/* ── Init ────────────────────────────────────────────────────────────────── */

TEST_CASE("mem_init clears RAM, VRAM, sets default dispatcher") {
    ms0515_memory_t mem;
    std::memset(&mem, 0xFF, sizeof(mem));
    mem_init(&mem);

    CHECK(mem.dispatcher == 0x007F);  /* all primary banks selected */
    CHECK(mem.rom_extended == false);

    /* Spot-check a few bytes */
    CHECK(mem.ram[0] == 0);
    CHECK(mem.ram[MEM_RAM_SIZE - 1] == 0);
    CHECK(mem.vram[0] == 0);
    CHECK(mem.vram[MEM_VRAM_SIZE - 1] == 0);
}

/* ── ROM loading ─────────────────────────────────────────────────────────── */

TEST_CASE("mem_load_rom copies data into ROM area") {
    auto mem = make_mem();
    uint8_t rom[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    mem_load_rom(&mem, rom, sizeof(rom));

    CHECK(mem.rom[0] == 0xDE);
    CHECK(mem.rom[1] == 0xAD);
    CHECK(mem.rom[2] == 0xBE);
    CHECK(mem.rom[3] == 0xEF);
}

/* ── Address translation ─────────────────────────────────────────────────── */

TEST_CASE("low RAM addresses translate to ADDR_TYPE_RAM") {
    auto mem = make_mem();
    mem.dispatcher = 0x007F;  /* all banks primary */

    auto tr = mem_translate(&mem, 0x0000);
    CHECK(tr.type == ADDR_TYPE_RAM);

    tr = mem_translate(&mem, 0x1000);
    CHECK(tr.type == ADDR_TYPE_RAM);
}

TEST_CASE("I/O space 0177400-0177776 translates to ADDR_TYPE_IO") {
    auto mem = make_mem();

    auto tr = mem_translate(&mem, 0177400);
    CHECK(tr.type == ADDR_TYPE_IO);
    CHECK(tr.offset == 0);

    tr = mem_translate(&mem, 0177776);
    CHECK(tr.type == ADDR_TYPE_IO);
}

TEST_CASE("ROM area translates to ADDR_TYPE_ROM") {
    auto mem = make_mem();

    /* ROM occupies 0160000-0177377 (basic 4 KB visible, or full 16 KB) */
    auto tr = mem_translate(&mem, 0176000);
    CHECK(tr.type == ADDR_TYPE_ROM);
}

/* ── Bank switching ──────────────────────────────────────────────────────── */

TEST_CASE("dispatcher bit selects primary vs extended bank") {
    auto mem = make_mem();

    /* Write to bank 0 (address 0x0000) with primary selected */
    mem.dispatcher = 0x007F;  /* all primary */
    auto tr = mem_translate(&mem, 0x0100);
    CHECK(tr.type == ADDR_TYPE_RAM);
    uint32_t primary_offset = tr.offset;

    /* Clear bit 0 → bank 0 is now extended */
    mem.dispatcher = 0x007E;
    tr = mem_translate(&mem, 0x0100);
    CHECK(tr.type == ADDR_TYPE_RAM);
    uint32_t extended_offset = tr.offset;

    /* Extended should map to a different physical region */
    CHECK(primary_offset != extended_offset);
}

/* ── Read / write round-trips ────────────────────────────────────────────── */

TEST_CASE("byte read/write round-trip through RAM") {
    auto mem = make_mem();
    mem.dispatcher = 0x007F;

    auto tr = mem_translate(&mem, 0x2000);
    mem_write_byte(&mem, tr, 0xAB);

    uint8_t val = mem_read_byte(&mem, tr);
    CHECK(val == 0xAB);
}

TEST_CASE("word read/write is little-endian") {
    auto mem = make_mem();
    mem.dispatcher = 0x007F;

    auto tr = mem_translate(&mem, 0x2000);
    mem_write_word(&mem, tr, 0x1234);

    /* Read back as word */
    CHECK(mem_read_word(&mem, tr) == 0x1234);

    /* Read individual bytes — little-endian */
    auto lo_tr = mem_translate(&mem, 0x2000);
    auto hi_tr = mem_translate(&mem, 0x2001);
    CHECK(mem_read_byte(&mem, lo_tr) == 0x34);
    CHECK(mem_read_byte(&mem, hi_tr) == 0x12);
}

/* ── VRAM ────────────────────────────────────────────────────────────────── */

TEST_CASE("VRAM window maps to video RAM when enabled") {
    auto mem = make_mem();

    /* Enable VRAM access and select window 0 */
    mem.dispatcher = 0x007F | MEM_DISP_VRAM_EN;

    /* VRAM window 0 overlays bank 1 (0020000-0037777) */
    auto tr = mem_translate(&mem, 0020000);
    CHECK(tr.type == ADDR_TYPE_VRAM);
}

TEST_CASE("mem_get_vram returns pointer to VRAM array") {
    auto mem = make_mem();
    mem.vram[0] = 0x42;

    const uint8_t *vp = mem_get_vram(&mem);
    CHECK(vp[0] == 0x42);
}

/* ── ROM write protection ────────────────────────────────────────────────── */

TEST_CASE("writes to ROM area are ignored") {
    auto mem = make_mem();

    /* Fill ROM with a known pattern.  In default (non-extended) mode,
     * address 0160000 maps to rom[MEM_BANK_SIZE] (upper 8 KB of 16 KB ROM). */
    uint8_t rom[MEM_ROM_SIZE];
    std::memset(rom, 0, sizeof(rom));
    rom[MEM_BANK_SIZE] = 0x11;
    mem_load_rom(&mem, rom, sizeof(rom));

    auto tr = mem_translate(&mem, 0160000);
    CHECK(tr.type == ADDR_TYPE_ROM);

    /* Try to write — ROM content should not change */
    mem_write_byte(&mem, tr, 0xFF);
    CHECK(mem_read_byte(&mem, tr) == 0x11);
}

} /* TEST_SUITE */
