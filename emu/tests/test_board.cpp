#include <doctest/doctest.h>

extern "C" {
#include <ms0515/board.h>
}

#include <ms0515/Emulator.hpp>

TEST_SUITE("Board") {

/* ── Helpers ─────────────────────────────────────────────────────────────── */

/* I/O addresses (octal, as the PDP-11 sees them) */
static constexpr uint16_t IO_DISPATCHER = 0177400;
static constexpr uint16_t IO_KBD_STATUS = 0177442;
static constexpr uint16_t IO_TIMER_W0   = 0177520;
static constexpr uint16_t IO_TIMER_CTRL = 0177526;
static constexpr uint16_t IO_REG_A      = 0177600;
static constexpr uint16_t IO_REG_B      = 0177602;
static constexpr uint16_t IO_REG_C      = 0177604;
static constexpr uint16_t IO_PPI_CTRL   = 0177606;

/* ── Init / Reset ────────────────────────────────────────────────────────── */

TEST_CASE("board_init produces clean state") {
    ms0515::Emulator emu;
    const auto &b = emu.board();

    CHECK(b.reg_a == 0);
    CHECK(b.reg_c == 0);
    CHECK(b.hires_mode == false);
    CHECK(b.border_color == 0);
    CHECK(b.sound_on == false);
    CHECK(b.sound_value == 0);
}

TEST_CASE("board_reset resets CPU") {
    ms0515::Emulator emu;
    emu.reset();

    CHECK(emu.cpu().halted == false);
    CHECK(emu.cpu().waiting == false);
}

/* ── Memory dispatcher I/O ───────────────────────────────────────────────── */

TEST_CASE("memory dispatcher word read/write") {
    ms0515::Emulator emu;
    emu.reset();

    emu.writeWord(IO_DISPATCHER, 0x007F);
    CHECK(emu.readWord(IO_DISPATCHER) == 0x007F);
}

TEST_CASE("memory dispatcher byte write updates low byte only") {
    ms0515::Emulator emu;
    emu.reset();

    emu.writeWord(IO_DISPATCHER, 0x1234);
    emu.writeByte(IO_DISPATCHER, 0x56);
    CHECK(emu.readWord(IO_DISPATCHER) == 0x1256);
}

/* ── Keyboard USART I/O dispatch ─────────────────────────────────────────── */

TEST_CASE("keyboard status register is readable") {
    ms0515::Emulator emu;
    emu.reset();

    uint8_t st = emu.readByte(IO_KBD_STATUS);
    /* TX ready should be set after init */
    CHECK((st & KBD_STATUS_TXRDY) != 0);
    /* RX ready should be clear (no key pressed) */
    CHECK((st & KBD_STATUS_RXRDY) == 0);
}

/* ── System Register A: ROM extended flag ────────────────────────────────── */

TEST_CASE("Reg A bit 7 controls extended ROM visibility") {
    ms0515::Emulator emu;
    emu.reset();

    CHECK(emu.board().mem.rom_extended == false);

    emu.writeByte(IO_REG_A, 0x80);
    CHECK(emu.board().mem.rom_extended == true);

    emu.writeByte(IO_REG_A, 0x00);
    CHECK(emu.board().mem.rom_extended == false);
}

/* ── System Register B: status input ─────────────────────────────────────── */

TEST_CASE("Reg B reflects DIP switch setting") {
    ms0515::Emulator emu;
    emu.reset();

    /* Default dip_refresh = 0 → bits 4:3 = 00 */
    uint8_t val = emu.readByte(IO_REG_B);
    CHECK(((val >> 3) & 0x03) == 0);

    /* Change dip to 72 Hz */
    emu.board().dip_refresh = 1;
    val = emu.readByte(IO_REG_B);
    CHECK(((val >> 3) & 0x03) == 1);
}

/* ── System Register C: video and sound ──────────────────────────────────── */

TEST_CASE("Reg C bits 0-2 set border color") {
    ms0515::Emulator emu;
    emu.reset();

    emu.writeByte(IO_REG_C, 0x05);
    CHECK(emu.board().border_color == 5);
}

TEST_CASE("Reg C bit 3 controls hires mode") {
    ms0515::Emulator emu;
    emu.reset();

    emu.writeByte(IO_REG_C, 0x08);
    CHECK(emu.board().hires_mode == true);

    emu.writeByte(IO_REG_C, 0x00);
    CHECK(emu.board().hires_mode == false);
}

/* ── PPI bit set/reset ───────────────────────────────────────────────────── */

TEST_CASE("PPI control bit-set modifies Reg C") {
    ms0515::Emulator emu;
    emu.reset();
    emu.writeByte(IO_REG_C, 0x00);

    /* Set bit 3 (hires): bit7=0, bits 3-1 = 011 (bit 3), bit0=1 (set)
     * → control word = 0x07 */
    emu.writeByte(IO_PPI_CTRL, 0x07);
    CHECK((emu.readByte(IO_REG_C) & 0x08) != 0);
    CHECK(emu.board().hires_mode == true);

    /* Reset bit 3: bit0=0 → 0x06 */
    emu.writeByte(IO_PPI_CTRL, 0x06);
    CHECK((emu.readByte(IO_REG_C) & 0x08) == 0);
    CHECK(emu.board().hires_mode == false);
}

/* ── Timer I/O routing ───────────────────────────────────────────────────── */

TEST_CASE("timer control and read through board I/O") {
    ms0515::Emulator emu;
    emu.reset();

    /* Program channel 0 in mode 0, LSB+MSB */
    uint8_t ctrl = (0 << 6) | (3 << 4) | (0 << 1);  /* ch=0, rw=3, mode=0 */
    emu.writeByte(IO_TIMER_CTRL, ctrl);

    /* Load count = 100 (LSB then MSB) */
    emu.writeByte(IO_TIMER_W0, 100);
    emu.writeByte(IO_TIMER_W0, 0);

    /* Timer channel 0 OUT should go low after loading in mode 0 */
    CHECK(timer_get_out(&emu.board().timer, 0) == false);
}

/* ── RAM bus: read/write round-trip ──────────────────────────────────────── */

TEST_CASE("board_read/write_word round-trips through RAM") {
    ms0515::Emulator emu;
    emu.reset();

    /* Ensure bank mapping allows access */
    emu.writeWord(IO_DISPATCHER, 0x007F);

    uint16_t addr = 0x2000;
    emu.writeWord(addr, 0xDEAD);
    CHECK(emu.readWord(addr) == 0xDEAD);
}

TEST_CASE("board_read/write_byte round-trips") {
    ms0515::Emulator emu;
    emu.reset();
    emu.writeWord(IO_DISPATCHER, 0x007F);

    uint16_t addr = 0x2000;
    emu.writeByte(addr, 0x42);
    CHECK(emu.readByte(addr) == 0x42);
}

/* ── VRAM access ─────────────────────────────────────────────────────────── */

TEST_CASE("board_get_vram returns non-null") {
    ms0515::Emulator emu;
    emu.reset();

    CHECK(board_get_vram(&emu.board()) != nullptr);
}

TEST_CASE("board_is_hires reflects Reg C") {
    ms0515::Emulator emu;
    emu.reset();

    CHECK(board_is_hires(&emu.board()) == false);

    emu.writeByte(IO_REG_C, 0x08);
    CHECK(board_is_hires(&emu.board()) == true);
}

TEST_CASE("board_get_border_color reflects Reg C") {
    ms0515::Emulator emu;
    emu.reset();

    emu.writeByte(IO_REG_C, 0x03);
    CHECK(board_get_border_color(&emu.board()) == 3);
}

/* ── Sound callback ──────────────────────────────────────────────────────── */

TEST_CASE("sound callback fires on Reg C change") {
    ms0515::Emulator emu;
    emu.reset();

    int last_sound = -1;
    emu.setSoundCallback([&](int v) { last_sound = v; });

    /* Program timer channel 2 as mode 3, count=2 for fast toggling */
    uint8_t ctrl_ch2 = (2 << 6) | (3 << 4) | (3 << 1);
    emu.writeByte(IO_TIMER_CTRL, ctrl_ch2);
    emu.writeByte(IO_TIMER_W0 + 4, 2);   /* ch2 = TIMER_W_BASE + 2*2 */
    emu.writeByte(IO_TIMER_W0 + 4, 0);

    /* Enable sound: bit 6 = enable, bit 7 = gate for ch2 */
    emu.writeByte(IO_REG_C, 0xC0);

    /* Timer ch2 OUT should now be driving sound — callback should have fired */
    CHECK(last_sound >= 0);
}

/* ── RAM disk integration ────────────────────────────────────────────────── */

TEST_CASE("ramdisk enable/free through board API") {
    ms0515::Emulator emu;
    emu.reset();

    CHECK(emu.board().ramdisk.enabled == false);

    board_ramdisk_enable(&emu.board());
    CHECK(emu.board().ramdisk.enabled == true);
    CHECK(emu.board().ramdisk.ram != nullptr);

    board_ramdisk_free(&emu.board());
    CHECK(emu.board().ramdisk.enabled == false);
}

} /* TEST_SUITE */
