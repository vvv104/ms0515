/*
 * test_io_decode.cpp - how the board decodes its I/O registers
 * (docs/hardware/board.md, "The register decoding").
 *
 * On the schematic (3.858.420 Э3, sheet 2) the register decoder D80
 * (К555ИД7) picks a peripheral's 040-byte block by address bits 5-7
 * alone, and the chips' RD and WR pins take the bus strobes: WR is ДЗП
 * at every address, so a write with bit 4 set or clear is the same
 * write; the ВВ51s' and the ВИ53's RD is ЧТ51 = ДЧТ gated by АД04 (D79.4,
 * К555ЛЛ1), so those chips are read only with bit 4 clear - the T-11's
 * read before every write finds no chip at the "+020" write addresses.
 * The ВВ55s and the ВГ93 take ДЧТ itself and are read at both.
 *
 * The owner's machine - the one with the memory-expansion module, whose
 * RAM disk the emulator models - had a factory wire from АД03 into D80's
 * G2B: an address with bit 3 set is not the board's at all, it is the
 * module's (ms0515-fpga, docs/findings.md, "The green wire").
 *
 * The ROM relies on the first of these: it programs the keyboard's and
 * the serial port's baud clocks, counters 0 and 1, through 177506,
 * 177500 and 177502 (ROM-B at 160070), and the speaker's counter 2
 * through 177526 and 177524.
 */

#include <doctest/doctest.h>

extern "C" {
#include <ms0515/core/board.h>
}

namespace {

constexpr uint16_t KBD_DATA     = 0177440;
constexpr uint16_t KBD_CTRL     = 0177442;
constexpr uint16_t KBD_DATA_W   = 0177460;
constexpr uint16_t KBD_CTRL_W   = 0177462;
constexpr uint16_t TIMER_R      = 0177500;
constexpr uint16_t TIMER_W      = 0177520;
constexpr uint16_t MODULE_PPI_R = 0177510;
constexpr uint16_t MODULE_PPI_W = 0177530;
constexpr uint16_t PPI2_B       = 0177542;
constexpr uint16_t REG_A        = 0177600;
constexpr uint16_t REG_B        = 0177602;
constexpr uint16_t REG_C        = 0177604;
constexpr uint16_t FDC_STATUS   = 0177640;
constexpr uint16_t SERIAL_DATA  = 0177700;
constexpr uint16_t SERIAL_DATA_W = 0177720;

constexpr uint8_t CMD_RXEN = 0x04;

bool sent(void *userdata, uint8_t value)
{
    *static_cast<int *>(userdata) = value;
    return true;
}

struct Board {
    ms0515_board_t b;
    Board() { board_init(&b); }
    void    w(uint16_t a, uint8_t v) { board_write_byte(&b, a, v); }
    uint8_t r(uint16_t a)            { return board_read_byte(&b, a); }
};

/* Counter `ch` programmed LSB only, mode 3, with the given divisor. */
void program(Board &m, uint16_t ctrl, uint16_t data, int ch, uint8_t divisor)
{
    m.w(ctrl, (uint8_t)((ch << 6) | 0x16));
    m.w(data, divisor);
}

bool same(const timer_channel_t &a, const timer_channel_t &b)
{
    return a.mode == b.mode && a.rw_mode == b.rw_mode && a.reload == b.reload &&
           a.bcd == b.bcd && a.loaded == b.loaded && a.counting == b.counting;
}

} /* namespace */

TEST_CASE("a write to the timer at 177500-506 programs it as at 177520-526")
{
    /* The ROM's own boot: counter 0 through the low addresses. */
    Board low, high;
    program(low,  TIMER_R + 6, TIMER_R,     0, 26);
    program(high, TIMER_W + 6, TIMER_W,     0, 26);
    CHECK(same(low.b.timer.ch[0], high.b.timer.ch[0]));
    CHECK(low.b.timer.ch[0].mode   == 3);
    CHECK(low.b.timer.ch[0].reload == 26);

    /* Counter 2 the other way round. */
    program(low,  TIMER_R + 6, TIMER_R + 4, 2, 100);
    program(high, TIMER_W + 6, TIMER_W + 4, 2, 100);
    CHECK(same(low.b.timer.ch[2], high.b.timer.ch[2]));
    CHECK(low.b.timer.ch[2].reload == 100);
}

TEST_CASE("the timer is read at 177500-506 only: a read at 177520-526 finds no chip")
{
    Board m;
    program(m, TIMER_W + 6, TIMER_W + 4, 2, 100);
    m.w(TIMER_W + 6, 0x80);                     /* latch counter 2 */
    REQUIRE(m.b.timer.ch[2].latched);

    /* The T-11's read before a write to 177524 reaches nothing: the
     * latch stands. */
    (void)m.r(TIMER_W + 4);
    CHECK(m.b.timer.ch[2].latched);

    /* The read at 177504 is the chip's: the latched count, released. */
    CHECK(m.r(TIMER_R + 4) == 100);
    CHECK(!m.b.timer.ch[2].latched);
}

TEST_CASE("the keyboard USART is written at 177440/442 as at 177460/462, read at 177440/442 only")
{
    Board low, high;
    low.w(KBD_CTRL,    0x4E);  high.w(KBD_CTRL_W, 0x4E);     /* mode: x16, 8 bits, 1 stop */
    low.w(KBD_CTRL,    0x05);  high.w(KBD_CTRL_W, 0x05);     /* command: TxEN, RxEN */
    CHECK(low.b.kbd.command == high.b.kbd.command);
    CHECK((low.b.kbd.command & CMD_RXEN) != 0);

    /* A byte from the keyboard: reading the data register at 177460
     * (the T-11's read before a write there) does not take it. */
    board_key_event(&low.b, 0x42);
    for (int i = 0; i < 20; ++i) kbd_tick(&low.b.kbd);
    REQUIRE(low.b.kbd.rx_ready);
    (void)low.r(KBD_DATA_W);
    CHECK(low.b.kbd.rx_ready);
    CHECK(low.r(KBD_DATA) == 0x42);
    CHECK(!low.b.kbd.rx_ready);

    /* The status is at 177442; 177462 reads no chip. */
    CHECK(low.r(KBD_CTRL) == low.b.kbd.status);
    CHECK(low.r(KBD_CTRL_W) != low.b.kbd.status);
}

TEST_CASE("the serial USART's data register is written at 177700 as at 177720")
{
    Board m;
    int got = -1;
    board_set_serial_callbacks(&m.b, nullptr, sent, &got);
    m.w(SERIAL_DATA_W, 0x31);
    CHECK(got == 0x31);
    m.w(SERIAL_DATA, 0x32);
    CHECK(got == 0x32);
}

TEST_CASE("an address with bit 3 set is the module's, not the board's")
{
    /* EX.SYS's installation check writes the module's PPI at 177536,
     * 177530 and 177532; with D80 as drawn counter 0 would take the 1
     * and the keyboard's clock would stop.  With the wire from АД03 the
     * board's chips never see those addresses - nor 177510-516. */
    Board m;
    program(m, TIMER_R + 6, TIMER_R, 0, 26);
    const timer_channel_t before = m.b.timer.ch[0];
    m.w(MODULE_PPI_W + 6, 0x80);
    m.w(MODULE_PPI_W,     1);
    m.w(MODULE_PPI_W + 2, 0);
    m.w(MODULE_PPI_R + 6, 0x80);
    m.w(MODULE_PPI_R,     1);
    CHECK(same(m.b.timer.ch[0], before));
    CHECK(m.b.timer.ch[0].reload == 26);

    /* Nor does a write at 177450/452 reach the keyboard USART. */
    m.w(KBD_CTRL, 0x4E);
    m.w(KBD_CTRL, 0x05);
    m.w(KBD_CTRL + 010, 0x00);
    CHECK(m.b.kbd.command == 0x05);
}

TEST_CASE("the PPIs and the FDC answer at both halves of their block")
{
    /* Their RD is ДЧТ itself: reads and writes alike ignore bit 4. */
    Board m;
    m.w(REG_A + 020, 0x83);
    CHECK(m.b.reg_a == 0x83);
    CHECK(m.r(REG_A + 020) == m.r(REG_A));
    m.w(REG_C + 020, 0x05);
    CHECK(board_get_border_color(&m.b) == 5);
    CHECK(m.r(REG_B + 020) == m.r(REG_B));

    board_set_joystick(&m.b, 0x11);
    CHECK(m.r(PPI2_B + 020) == m.r(PPI2_B));
    CHECK((m.r(PPI2_B) & 0x1F) == 0x0E);

    CHECK(m.r(FDC_STATUS + 020) == m.r(FDC_STATUS));
}
