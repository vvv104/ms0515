#define _CRT_SECURE_NO_WARNINGS
/*
 * test_floppy.cpp — FDC mount-side-offset behaviour.
 *
 * Single-side and double-side image attachment via `fdc_attach`
 * is what makes `--diskN` work in the frontend: the same image
 * file path mounted on the two side-units of one drive must read
 * and write to the right halves of the file.  These tests bypass
 * the WD1793 state machine and inspect the per-drive image_offset
 * directly to make sure the offset auto-detection picks the right
 * half of a double-sided image.
 */

#include <doctest/doctest.h>

#include <ms0515/floppy.h>

#include <array>
#include <cstdio>
#include <filesystem>
#include <random>
#include <string>

namespace fs = std::filesystem;

namespace {

/* Helper: write a temp file of `bytes` size where every sector starts
 * with its own (track, sector, side) signature so a misrouted read
 * is obvious.  Side is encoded as 0x00 or 0x80 in the first signature
 * byte so we can tell sides apart at a glance. */
fs::path makeImage(std::size_t bytes)
{
    std::random_device rd;
    fs::path path = fs::temp_directory_path()
                  / fs::path{"ms0515_floppy_test_" + std::to_string(rd()) + ".dsk"};
    std::FILE *f = std::fopen(path.string().c_str(), "wb");
    REQUIRE(f);

    constexpr int kSectorBytes = FDC_SECTOR_SIZE;
    std::array<uint8_t, kSectorBytes> sec{};
    int totalSectors = static_cast<int>(bytes / kSectorBytes);
    for (int sec_index = 0; sec_index < totalSectors; ++sec_index) {
        int side = (sec_index < FDC_TRACKS * FDC_SECTORS) ? 0 : 1;
        int local = sec_index % (FDC_TRACKS * FDC_SECTORS);
        int track = local / FDC_SECTORS;
        int sector = local % FDC_SECTORS + 1;  /* 1-based */
        sec.fill(0);
        sec[0] = static_cast<uint8_t>(side ? 0x80 : 0x00);
        sec[1] = static_cast<uint8_t>(track);
        sec[2] = static_cast<uint8_t>(sector);
        REQUIRE(std::fwrite(sec.data(), 1, sec.size(), f) == sec.size());
    }
    std::fclose(f);
    return path;
}

/* Helper: read 3-byte signature from the file at the given absolute
 * offset (bypasses the FDC entirely; used to verify writes). */
std::array<uint8_t, 3> sniffAt(const fs::path &path, long offset)
{
    std::array<uint8_t, 3> sig{};
    std::FILE *f = std::fopen(path.string().c_str(), "rb");
    REQUIRE(f);
    REQUIRE(std::fseek(f, offset, SEEK_SET) == 0);
    REQUIRE(std::fread(sig.data(), 1, sig.size(), f) == sig.size());
    std::fclose(f);
    return sig;
}

} /* namespace */

TEST_SUITE("Floppy") {

TEST_CASE("Single-side image: every unit reads from offset 0") {
    auto path = makeImage(FDC_DISK_SIZE);

    ms0515_floppy_t fdc;
    fdc_init(&fdc);

    REQUIRE(fdc_attach(&fdc, 0, path.string().c_str(), false));
    /* image_offset must be 0 for an SS image regardless of unit. */
    CHECK(fdc.drives[0].image_offset == 0);

    REQUIRE(fdc_attach(&fdc, 2, path.string().c_str(), false));
    /* Even when attached to a side-1 unit, an SS image still has
     * offset 0 — there is no side-1 data on this disk. */
    CHECK(fdc.drives[2].image_offset == 0);

    fdc_detach(&fdc, 0);
    fdc_detach(&fdc, 2);
    std::error_code ec;
    fs::remove(path, ec);
}

TEST_CASE("Double-side image: side-0 units offset 0, side-1 units offset FDC_DISK_SIZE") {
    auto path = makeImage(FDC_DISK_SIZE * 2);

    ms0515_floppy_t fdc;
    fdc_init(&fdc);

    /* FD0 + FD1 are the side-0 units; FD2 + FD3 are side-1. */
    REQUIRE(fdc_attach(&fdc, 0, path.string().c_str(), false));
    REQUIRE(fdc_attach(&fdc, 1, path.string().c_str(), false));
    REQUIRE(fdc_attach(&fdc, 2, path.string().c_str(), false));
    REQUIRE(fdc_attach(&fdc, 3, path.string().c_str(), false));

    CHECK(fdc.drives[0].image_offset == 0);
    CHECK(fdc.drives[1].image_offset == 0);
    CHECK(fdc.drives[2].image_offset == FDC_DISK_SIZE);
    CHECK(fdc.drives[3].image_offset == FDC_DISK_SIZE);

    /* The signature we wrote at file offset 0 marks "side 0, track 0,
     * sector 1"; at file offset FDC_DISK_SIZE it marks "side 1". */
    auto side0 = sniffAt(path, 0);
    auto side1 = sniffAt(path, FDC_DISK_SIZE);
    CHECK(side0[0] == 0x00);
    CHECK(side1[0] == 0x80);

    fdc_detach(&fdc, 0);
    fdc_detach(&fdc, 1);
    fdc_detach(&fdc, 2);
    fdc_detach(&fdc, 3);
    std::error_code ec;
    fs::remove(path, ec);
}

TEST_CASE("Detach clears image_offset") {
    auto path = makeImage(FDC_DISK_SIZE * 2);

    ms0515_floppy_t fdc;
    fdc_init(&fdc);

    REQUIRE(fdc_attach(&fdc, 2, path.string().c_str(), false));
    CHECK(fdc.drives[2].image_offset == FDC_DISK_SIZE);

    fdc_detach(&fdc, 2);
    CHECK(fdc.drives[2].image_offset == 0);

    std::error_code ec;
    fs::remove(path, ec);
}

}  /* TEST_SUITE */
