#include <doctest/doctest.h>
#include <vector>

extern "C" {
#include <ms0515/core/hd.h>
#include <ms0515/core/memory.h>
}

TEST_SUITE("HD") {

/* ── Helpers ─────────────────────────────────────────────────────────────── */

/* Issue a command the way HD.SYS does: argument to HDDAT first, then the
 * command code to HDCSR (the CSR write executes it). */
static void hd_cmd(ms0515_hd_t *hd, ms0515_memory_t *mem,
                   uint16_t arg, uint16_t cmd)
{
    hd_write_word(hd, mem, HD_IO_DATA, arg);
    hd_write_word(hd, mem, HD_IO_CSR, cmd);
}

static uint16_t mem_word(ms0515_memory_t *mem, uint16_t addr)
{
    return mem_read_word(mem, mem_translate(mem, addr));
}

static void set_mem_word(ms0515_memory_t *mem, uint16_t addr, uint16_t val)
{
    mem_write_word(mem, mem_translate(mem, addr), val);
}

/* ── Lifecycle ───────────────────────────────────────────────────────────── */

TEST_CASE("hd_init: disabled, no image") {
    ms0515_hd_t hd;
    hd_init(&hd);
    CHECK(hd.enabled == false);
    CHECK(hd.image == nullptr);
    CHECK(hd.image_size == 0);
}

TEST_CASE("hd_mount allocates and copies, hd_unmount ejects media") {
    ms0515_hd_t hd;
    hd_init(&hd);

    std::vector<uint8_t> img(2 * HD_BLOCK_SIZE);
    for (size_t i = 0; i < img.size(); ++i)
        img[i] = (uint8_t)(i & 0xFF);

    CHECK(hd_mount(&hd, img.data(), (uint32_t)img.size()) == true);
    CHECK(hd.enabled == true);              /* mounting enables the card */
    CHECK(hd.image != nullptr);
    CHECK(hd.image_size == img.size());
    CHECK(hd.image[5] == 5);

    hd_unmount(&hd);
    CHECK(hd.image == nullptr);             /* media ejected ... */
    CHECK(hd.enabled == true);              /* ... but the card stays present */
}

TEST_CASE("controller presence is independent of mounted media") {
    ms0515_hd_t hd;
    hd_init(&hd);
    CHECK(hd.enabled == false);

    /* Enable the controller with no media — an offline drive. */
    hd_set_enabled(&hd, true);
    CHECK(hd.enabled == true);
    CHECK(hd.image == nullptr);

    hd_cmd(&hd, nullptr, 0, HD_CMD_SET_UNIT);
    hd_cmd(&hd, nullptr, 0, HD_CMD_GET_SIZE);
    CHECK(hd_read_word(&hd, HD_IO_DATA) == 0);   /* offline → size 0 */

    /* Disable returns the bus to the serial port; media untouched. */
    hd_set_enabled(&hd, false);
    CHECK(hd.enabled == false);
}

TEST_CASE("hd_mount rejects sizes that are not a positive block multiple") {
    ms0515_hd_t hd;
    hd_init(&hd);
    CHECK(hd_mount(&hd, nullptr, 0) == false);
    CHECK(hd_mount(&hd, nullptr, HD_BLOCK_SIZE - 1) == false);
    CHECK(hd_mount(&hd, nullptr, HD_BLOCK_SIZE + 1) == false);
    CHECK(hd.enabled == false);
}

/* ── I/O address routing ─────────────────────────────────────────────────── */

TEST_CASE("hd_handles recognizes only the three HD registers") {
    CHECK(hd_handles(HD_IO_CSR) == true);
    CHECK(hd_handles(HD_IO_CSR_HI) == true);
    CHECK(hd_handles(HD_IO_DATA) == true);
    CHECK(hd_handles(HD_IO_DATA + 1) == true);   /* HDDAT high byte */
    CHECK(hd_handles(0x00) == false);
    CHECK(hd_handles(0xA0) == false);            /* FDC base */
}

/* ── Controller-type identification ──────────────────────────────────────── */

TEST_CASE("at-rest CSR identifies a t2 controller") {
    ms0515_hd_t hd;
    hd_init(&hd);
    std::vector<uint8_t> img(HD_BLOCK_SIZE);
    REQUIRE(hd_mount(&hd, img.data(), (uint32_t)img.size()));

    uint16_t csr = hd_read_word(&hd, HD_IO_CSR);
    CHECK((csr & HD_CS_READY) != 0);     /* low-byte bit 7 set → not t1   */
    CHECK((csr & HD_CS_DMA)   == 0);     /* bit 14 clear        → not t4/t5 */
    CHECK((csr & HD_CS_INT)   == 0);     /* bit 6 clear         → not t3/t5 */
    CHECK((csr & HD_CS_ERROR) == 0);     /* sign bit clear      → OK        */

    /* The driver's install check uses a byte read (TstB) — must be negative. */
    CHECK((hd_read_byte(&hd, HD_IO_CSR) & 0x80) != 0);

    hd_unmount(&hd);
}

/* ── GetSize ─────────────────────────────────────────────────────────────── */

TEST_CASE("GetSize reports the backed unit's block count") {
    ms0515_hd_t hd;
    hd_init(&hd);
    std::vector<uint8_t> img(40 * HD_BLOCK_SIZE);
    REQUIRE(hd_mount(&hd, img.data(), (uint32_t)img.size()));

    hd_cmd(&hd, nullptr, 0, HD_CMD_SET_UNIT);
    hd_cmd(&hd, nullptr, 0, HD_CMD_GET_SIZE);
    CHECK(hd_read_word(&hd, HD_IO_DATA) == 40);

    /* An unbacked unit is offline → size 0 (driver treats 0 as error). */
    hd_cmd(&hd, nullptr, 3, HD_CMD_SET_UNIT);
    hd_cmd(&hd, nullptr, 0, HD_CMD_GET_SIZE);
    CHECK(hd_read_word(&hd, HD_IO_DATA) == 0);

    hd_unmount(&hd);
}

/* ── DMA read ────────────────────────────────────────────────────────────── */

TEST_CASE("CmdRead DMAs image blocks into memory") {
    ms0515_memory_t mem;
    mem_init(&mem);

    ms0515_hd_t hd;
    hd_init(&hd);
    std::vector<uint8_t> img(4 * HD_BLOCK_SIZE);
    for (size_t i = 0; i < img.size(); ++i)
        img[i] = (uint8_t)(i & 0xFF);
    REQUIRE(hd_mount(&hd, img.data(), (uint32_t)img.size()));

    const uint16_t buf = 0x2000;     /* lands in RAM under default banking */
    hd_cmd(&hd, &mem, 1, HD_CMD_SET_BLOCK);   /* block 1 → byte offset 512  */
    hd_cmd(&hd, &mem, buf, HD_CMD_SET_BUF);
    hd_cmd(&hd, &mem, 4, HD_CMD_SET_WCNT);    /* 4 words = 8 bytes          */
    hd_cmd(&hd, &mem, 0, HD_CMD_READ);

    CHECK((hd_read_word(&hd, HD_IO_CSR) & HD_CS_ERROR) == 0);
    for (int w = 0; w < 4; ++w) {
        uint8_t lo = (uint8_t)((512 + w * 2) & 0xFF);
        uint8_t hi = (uint8_t)((512 + w * 2 + 1) & 0xFF);
        CHECK(mem_word(&mem, (uint16_t)(buf + w * 2)) ==
              (uint16_t)(lo | (hi << 8)));
    }

    hd_unmount(&hd);
}

/* ── DMA write ───────────────────────────────────────────────────────────── */

TEST_CASE("CmdWrite DMAs memory into image and marks it dirty") {
    ms0515_memory_t mem;
    mem_init(&mem);

    ms0515_hd_t hd;
    hd_init(&hd);
    std::vector<uint8_t> img(4 * HD_BLOCK_SIZE);
    REQUIRE(hd_mount(&hd, img.data(), (uint32_t)img.size()));
    CHECK(hd.dirty == false);

    const uint16_t buf = 0x2000;
    for (int w = 0; w < 4; ++w)
        set_mem_word(&mem, (uint16_t)(buf + w * 2), (uint16_t)(0xA000 + w));

    hd_cmd(&hd, &mem, 2, HD_CMD_SET_BLOCK);   /* block 2 → byte offset 1024 */
    hd_cmd(&hd, &mem, buf, HD_CMD_SET_BUF);
    hd_cmd(&hd, &mem, 4, HD_CMD_SET_WCNT);
    hd_cmd(&hd, &mem, 0, HD_CMD_WRITE);

    CHECK((hd_read_word(&hd, HD_IO_CSR) & HD_CS_ERROR) == 0);
    CHECK(hd.dirty == true);
    for (int w = 0; w < 4; ++w) {
        uint32_t off = 1024 + w * 2;
        uint16_t got = (uint16_t)(hd.image[off] | (hd.image[off + 1] << 8));
        CHECK(got == (uint16_t)(0xA000 + w));
    }

    hd_unmount(&hd);
}

/* ── Bounds checking ─────────────────────────────────────────────────────── */

TEST_CASE("a transfer past the end of the image raises the error bit") {
    ms0515_memory_t mem;
    mem_init(&mem);

    ms0515_hd_t hd;
    hd_init(&hd);
    std::vector<uint8_t> img(2 * HD_BLOCK_SIZE);
    REQUIRE(hd_mount(&hd, img.data(), (uint32_t)img.size()));

    hd_cmd(&hd, &mem, 1, HD_CMD_SET_BLOCK);
    hd_cmd(&hd, &mem, 0x2000, HD_CMD_SET_BUF);
    hd_cmd(&hd, &mem, 512, HD_CMD_SET_WCNT);   /* 1024 bytes from block 1 → overruns */
    hd_cmd(&hd, &mem, 0, HD_CMD_READ);

    CHECK((hd_read_word(&hd, HD_IO_CSR) & HD_CS_ERROR) != 0);

    hd_unmount(&hd);
}

/* ── Activity LED ────────────────────────────────────────────────────────── */

TEST_CASE("a transfer lights the activity timer; hd_tick decays it") {
    ms0515_memory_t mem;
    mem_init(&mem);

    ms0515_hd_t hd;
    hd_init(&hd);
    std::vector<uint8_t> img(2 * HD_BLOCK_SIZE);
    REQUIRE(hd_mount(&hd, img.data(), (uint32_t)img.size()));

    CHECK(hd.activity_remaining == 0);
    hd_cmd(&hd, &mem, 0, HD_CMD_SET_BLOCK);
    hd_cmd(&hd, &mem, 0x2000, HD_CMD_SET_BUF);
    hd_cmd(&hd, &mem, 4, HD_CMD_SET_WCNT);
    hd_cmd(&hd, &mem, 0, HD_CMD_READ);
    CHECK(hd.activity_remaining > 0);

    int before = hd.activity_remaining;
    hd_tick(&hd, 1000);
    CHECK(hd.activity_remaining == before - 1000);
    hd_tick(&hd, before);            /* decays to floor, never negative */
    CHECK(hd.activity_remaining == 0);

    hd_unmount(&hd);
}

} /* TEST_SUITE("HD") */
