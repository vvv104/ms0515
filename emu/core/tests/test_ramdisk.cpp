#include <doctest/doctest.h>

extern "C" {
#include <ms0515/ramdisk.h>
}

TEST_SUITE("Ramdisk") {

/* ── Helpers ─────────────────────────────────────────────────────────────── */

static ms0515_ramdisk_t make_rd()
{
    ms0515_ramdisk_t rd;
    ramdisk_init(&rd);
    ramdisk_enable(&rd);
    return rd;
}

/* Perform the standard init sequence like EX.SYS:
 *   1. Set PPI mode (all ports output)
 *   2. Write Port A (page address)
 *   3. Write Port B with START|RESET, then START (clears counter)
 */
static void setup_page(ms0515_ramdisk_t *rd, uint8_t page, uint8_t bank)
{
    /* PPI control: mode 0, all outputs = 0x80 */
    ramdisk_write(rd, RAMDISK_IO_PPI_WR_CTRL, 0x80);
    /* Port A = page address (MA08-MA15) */
    ramdisk_write(rd, RAMDISK_IO_PPI_WR_A, page);
    /* Port B = START|RESET to zero counter */
    ramdisk_write(rd, RAMDISK_IO_PPI_WR_B,
                  RAMDISK_PB_START | RAMDISK_PB_RESET | (bank & 0x07));
    /* Port B = START only (clear RESET) */
    ramdisk_write(rd, RAMDISK_IO_PPI_WR_B,
                  RAMDISK_PB_START | (bank & 0x07));
}

/* ── Lifecycle ───────────────────────────────────────────────────────────── */

TEST_CASE("ramdisk_init: disabled, no RAM allocated") {
    ms0515_ramdisk_t rd;
    ramdisk_init(&rd);
    CHECK(rd.enabled == false);
    CHECK(rd.ram == nullptr);
}

TEST_CASE("ramdisk_enable allocates 512 KB") {
    ms0515_ramdisk_t rd;
    ramdisk_init(&rd);
    ramdisk_enable(&rd);

    CHECK(rd.enabled == true);
    CHECK(rd.ram != nullptr);

    ramdisk_free(&rd);
    CHECK(rd.enabled == false);
    CHECK(rd.ram == nullptr);
}

TEST_CASE("double enable is a no-op") {
    ms0515_ramdisk_t rd;
    ramdisk_init(&rd);
    ramdisk_enable(&rd);
    uint8_t *first = rd.ram;
    ramdisk_enable(&rd);
    CHECK(rd.ram == first);
    ramdisk_free(&rd);
}

/* ── I/O address routing ─────────────────────────────────────────────────── */

TEST_CASE("ramdisk_handles recognizes PPI and data ports") {
    CHECK(ramdisk_handles(RAMDISK_IO_PPI_RD_A) == true);
    CHECK(ramdisk_handles(RAMDISK_IO_PPI_WR_A) == true);
    CHECK(ramdisk_handles(RAMDISK_IO_DATA) == true);
    CHECK(ramdisk_handles(RAMDISK_IO_DATA_ALIAS) == true);

    /* Some random I/O offset outside ramdisk range */
    CHECK(ramdisk_handles(0x00) == false);
    CHECK(ramdisk_handles(0x80) == false);
}

/* ── PPI port read/write ─────────────────────────────────────────────────── */

TEST_CASE("PPI ports are writable and readable") {
    auto rd = make_rd();

    ramdisk_write(&rd, RAMDISK_IO_PPI_WR_CTRL, 0x80);  /* mode set */
    ramdisk_write(&rd, RAMDISK_IO_PPI_WR_A, 0x42);
    ramdisk_write(&rd, RAMDISK_IO_PPI_WR_B, 0x81);
    ramdisk_write(&rd, RAMDISK_IO_PPI_WR_C, 0x33);

    CHECK(ramdisk_read(&rd, RAMDISK_IO_PPI_RD_A) == 0x42);
    CHECK(ramdisk_read(&rd, RAMDISK_IO_PPI_RD_B) == 0x81);
    CHECK(ramdisk_read(&rd, RAMDISK_IO_PPI_RD_C) == 0x33);

    ramdisk_free(&rd);
}

/* ── Data write and read with auto-increment ─────────────────────────────── */

TEST_CASE("data port writes and reads back with auto-increment") {
    auto rd = make_rd();

    setup_page(&rd, 0x00, 0x00);

    /* Write 4 bytes sequentially */
    ramdisk_write(&rd, RAMDISK_IO_DATA, 0xAA);
    ramdisk_write(&rd, RAMDISK_IO_DATA, 0xBB);
    ramdisk_write(&rd, RAMDISK_IO_DATA, 0xCC);
    ramdisk_write(&rd, RAMDISK_IO_DATA, 0xDD);

    /* Reset counter and read back */
    ramdisk_write(&rd, RAMDISK_IO_PPI_WR_B,
                  RAMDISK_PB_START | RAMDISK_PB_RESET);
    ramdisk_write(&rd, RAMDISK_IO_PPI_WR_B, RAMDISK_PB_START);

    CHECK(ramdisk_read(&rd, RAMDISK_IO_DATA) == 0xAA);
    CHECK(ramdisk_read(&rd, RAMDISK_IO_DATA) == 0xBB);
    CHECK(ramdisk_read(&rd, RAMDISK_IO_DATA) == 0xCC);
    CHECK(ramdisk_read(&rd, RAMDISK_IO_DATA) == 0xDD);

    ramdisk_free(&rd);
}

/* ── Counter wraps at 256 ────────────────────────────────────────────────── */

TEST_CASE("counter wraps from 255 to 0") {
    auto rd = make_rd();

    setup_page(&rd, 0x00, 0x00);

    /* Write 256 bytes to fill the page */
    for (int i = 0; i < 256; i++)
        ramdisk_write(&rd, RAMDISK_IO_DATA, (uint8_t)i);

    /* Counter should have wrapped — next write goes to offset 0 again */
    ramdisk_write(&rd, RAMDISK_IO_DATA, 0xFF);

    /* Reset and verify first byte was overwritten */
    ramdisk_write(&rd, RAMDISK_IO_PPI_WR_B,
                  RAMDISK_PB_START | RAMDISK_PB_RESET);
    ramdisk_write(&rd, RAMDISK_IO_PPI_WR_B, RAMDISK_PB_START);

    CHECK(ramdisk_read(&rd, RAMDISK_IO_DATA) == 0xFF);

    ramdisk_free(&rd);
}

/* ── Different pages are independent ─────────────────────────────────────── */

TEST_CASE("different pages map to different memory") {
    auto rd = make_rd();

    /* Write 0x11 to page 0 */
    setup_page(&rd, 0x00, 0x00);
    ramdisk_write(&rd, RAMDISK_IO_DATA, 0x11);

    /* Write 0x22 to page 1 */
    setup_page(&rd, 0x01, 0x00);
    ramdisk_write(&rd, RAMDISK_IO_DATA, 0x22);

    /* Read back page 0 */
    setup_page(&rd, 0x00, 0x00);
    CHECK(ramdisk_read(&rd, RAMDISK_IO_DATA) == 0x11);

    /* Read back page 1 */
    setup_page(&rd, 0x01, 0x00);
    CHECK(ramdisk_read(&rd, RAMDISK_IO_DATA) == 0x22);

    ramdisk_free(&rd);
}

/* ── Data port alias ─────────────────────────────────────────────────────── */

TEST_CASE("data port alias 0177570 mirrors 0177550") {
    auto rd = make_rd();

    setup_page(&rd, 0x00, 0x00);

    /* Write via alias */
    ramdisk_write(&rd, RAMDISK_IO_DATA_ALIAS, 0x77);

    /* Reset counter and read via primary port */
    ramdisk_write(&rd, RAMDISK_IO_PPI_WR_B,
                  RAMDISK_PB_START | RAMDISK_PB_RESET);
    ramdisk_write(&rd, RAMDISK_IO_PPI_WR_B, RAMDISK_PB_START);

    CHECK(ramdisk_read(&rd, RAMDISK_IO_DATA) == 0x77);

    ramdisk_free(&rd);
}

/* ── PPI control: bit set/reset on Port C ────────────────────────────────── */

TEST_CASE("PPI bit set/reset modifies Port C") {
    auto rd = make_rd();

    ramdisk_write(&rd, RAMDISK_IO_PPI_WR_CTRL, 0x80);  /* mode set, clear all */
    CHECK(ramdisk_read(&rd, RAMDISK_IO_PPI_RD_C) == 0x00);

    /* Set bit 3: control word bit7=0, bits3-1=011 (bit 3), bit0=1 (set) */
    ramdisk_write(&rd, RAMDISK_IO_PPI_WR_CTRL, 0x07);  /* (3<<1)|1 = 7 */
    CHECK((ramdisk_read(&rd, RAMDISK_IO_PPI_RD_C) & 0x08) != 0);

    /* Reset bit 3: bit0=0 (reset) */
    ramdisk_write(&rd, RAMDISK_IO_PPI_WR_CTRL, 0x06);  /* (3<<1)|0 = 6 */
    CHECK((ramdisk_read(&rd, RAMDISK_IO_PPI_RD_C) & 0x08) == 0);

    ramdisk_free(&rd);
}

/* ── Reset ───────────────────────────────────────────────────────────────── */

TEST_CASE("ramdisk_reset clears PPI but preserves DRAM contents") {
    auto rd = make_rd();

    setup_page(&rd, 0x00, 0x00);
    ramdisk_write(&rd, RAMDISK_IO_DATA, 0xAB);

    ramdisk_reset(&rd);

    /* PPI should be zeroed */
    CHECK(rd.ppi_a == 0);
    CHECK(rd.ppi_b == 0);
    CHECK(rd.counter == 0);

    /* But DRAM content should survive */
    setup_page(&rd, 0x00, 0x00);
    CHECK(ramdisk_read(&rd, RAMDISK_IO_DATA) == 0xAB);

    ramdisk_free(&rd);
}

} /* TEST_SUITE */
