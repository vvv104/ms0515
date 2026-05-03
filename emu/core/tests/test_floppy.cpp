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
 * with its own (side, track, sector) signature so a misrouted read
 * is obvious.  Side is encoded as 0x00 or 0x80 in the first
 * signature byte so we can tell sides apart at a glance.
 *
 * Layouts:
 *   - bytes == FDC_DISK_SIZE   → single-side (80 tracks × 10 sectors)
 *   - bytes == 2*FDC_DISK_SIZE → track-interleaved DS:
 *       T0S0(10 secs) T0S1(10) T1S0(10) T1S1(10) ... T79S0 T79S1
 * The track-interleaved layout matches what raw MS0515 hardware
 * dumps look like and is the only DS format the FDC supports. */
fs::path makeImage(std::size_t bytes)
{
    std::random_device rd;
    fs::path path = fs::temp_directory_path()
                  / fs::path{"ms0515_floppy_test_" + std::to_string(rd()) + ".dsk"};
    std::FILE *f = std::fopen(path.string().c_str(), "wb");
    REQUIRE(f);

    const bool isDs = (bytes == 2 * FDC_DISK_SIZE);
    const int  sides = isDs ? 2 : 1;

    std::array<uint8_t, FDC_SECTOR_SIZE> sec{};
    for (int track = 0; track < FDC_TRACKS; ++track) {
        for (int side = 0; side < sides; ++side) {
            for (int sector = 1; sector <= FDC_SECTORS; ++sector) {
                sec.fill(0);
                sec[0] = static_cast<uint8_t>(side ? 0x80 : 0x00);
                sec[1] = static_cast<uint8_t>(track);
                sec[2] = static_cast<uint8_t>(sector);
                REQUIRE(std::fwrite(sec.data(), 1, sec.size(), f) == sec.size());
            }
        }
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

TEST_CASE("Single-side image: image_offset 0 / track_stride FDC_TRACK_SIZE on every unit") {
    auto path = makeImage(FDC_DISK_SIZE);

    ms0515_floppy_t fdc;
    fdc_init(&fdc);

    REQUIRE(fdc_attach(&fdc, 0, path.string().c_str(), false));
    CHECK(fdc.drives[0].image_offset == 0);
    CHECK(fdc.drives[0].track_stride == FDC_TRACK_SIZE);

    /* Even when attached to a side-1 unit, an SS image still has
     * offset 0 / single-track stride — there is no side-1 data on
     * this disk, the FDC will read whatever's at offset 0 (the SS
     * layout). */
    REQUIRE(fdc_attach(&fdc, 2, path.string().c_str(), false));
    CHECK(fdc.drives[2].image_offset == 0);
    CHECK(fdc.drives[2].track_stride == FDC_TRACK_SIZE);

    fdc_detach(&fdc, 0);
    fdc_detach(&fdc, 2);
    std::error_code ec;
    fs::remove(path, ec);
}

TEST_CASE("Track-interleaved DS image: per-side image_offset, doubled track_stride") {
    auto path = makeImage(FDC_DISK_SIZE * 2);

    ms0515_floppy_t fdc;
    fdc_init(&fdc);

    /* FD0 + FD1 are the side-0 units; FD2 + FD3 are side-1. */
    REQUIRE(fdc_attach(&fdc, 0, path.string().c_str(), false));
    REQUIRE(fdc_attach(&fdc, 1, path.string().c_str(), false));
    REQUIRE(fdc_attach(&fdc, 2, path.string().c_str(), false));
    REQUIRE(fdc_attach(&fdc, 3, path.string().c_str(), false));

    /* Side-0 units start at the very first byte of each track slot. */
    CHECK(fdc.drives[0].image_offset == 0);
    CHECK(fdc.drives[1].image_offset == 0);
    /* Side-1 units start FDC_TRACK_SIZE bytes into the slot
     * (after the side-0 sectors of that track). */
    CHECK(fdc.drives[2].image_offset == FDC_TRACK_SIZE);
    CHECK(fdc.drives[3].image_offset == FDC_TRACK_SIZE);
    /* All four DS units use the doubled stride: each "track" in the
     * file covers both sides. */
    for (int u = 0; u < 4; ++u)
        CHECK(fdc.drives[u].track_stride == 2 * FDC_TRACK_SIZE);

    /* Verify the file really is track-interleaved: signature at
     * offset 0 = (side 0, track 0, sector 1); at offset FDC_TRACK_SIZE
     * = (side 1, track 0, sector 1); at offset 2*FDC_TRACK_SIZE
     * = (side 0, track 1, sector 1). */
    auto sig00 = sniffAt(path, 0);
    auto sig01 = sniffAt(path, FDC_TRACK_SIZE);
    auto sig10 = sniffAt(path, 2 * FDC_TRACK_SIZE);
    CHECK(sig00[0] == 0x00); CHECK(sig00[1] == 0); CHECK(sig00[2] == 1);
    CHECK(sig01[0] == 0x80); CHECK(sig01[1] == 0); CHECK(sig01[2] == 1);
    CHECK(sig10[0] == 0x00); CHECK(sig10[1] == 1); CHECK(sig10[2] == 1);

    fdc_detach(&fdc, 0);
    fdc_detach(&fdc, 1);
    fdc_detach(&fdc, 2);
    fdc_detach(&fdc, 3);
    std::error_code ec;
    fs::remove(path, ec);
}

TEST_CASE("Detach restores image_offset / track_stride defaults") {
    auto path = makeImage(FDC_DISK_SIZE * 2);

    ms0515_floppy_t fdc;
    fdc_init(&fdc);

    REQUIRE(fdc_attach(&fdc, 2, path.string().c_str(), false));
    CHECK(fdc.drives[2].image_offset == FDC_TRACK_SIZE);
    CHECK(fdc.drives[2].track_stride == 2 * FDC_TRACK_SIZE);

    fdc_detach(&fdc, 2);
    CHECK(fdc.drives[2].image_offset == 0);
    CHECK(fdc.drives[2].track_stride == FDC_TRACK_SIZE);

    std::error_code ec;
    fs::remove(path, ec);
}

}  /* TEST_SUITE */
