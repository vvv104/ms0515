/*
 * test_layout.cpp — LBN→byte geometry, replicating the emulator FDC
 * (core/src/floppy.c) + the OS driver's interleave/skew.  Single distinction
 * is single- vs double-sided storage; `side` picks a DS side.
 */

#include <doctest/doctest.h>
#include <ms0515/disk/Layout.hpp>

using namespace ms0515::disk;

TEST_SUITE("Layout") {

TEST_CASE("single-sided: track stride 5120, osa-skew driver") {
    /* boot/home/dir sit on track 1, where the skew term is 0 */
    CHECK(lbnToByte(0, 0, false) == 5120);   /* track1 sector0 */
    CHECK(lbnToByte(1, 0, false) == 6144);   /* home  */
    CHECK(lbnToByte(6, 0, false) == 6656);   /* dir   */
    /* track 2+ picks up the +2-sectors-per-track skew */
    CHECK(lbnToByte(10, 0, false) == 11264);
    CHECK(lbnToByte(20, 0, false) == 17408);
    CHECK(lbnToByte(30, 0, false) == 23552);
}

TEST_CASE("double-sided: track stride 10240, side offset 5120 (interleaved)") {
    /* Same (track,sector) as single-sided, but each cylinder stores side 0
     * then side 1 back to back, so the track stride doubles. */
    CHECK(lbnToByte(0, 0, true) == 10240);             /* track1 sector0, side0 */
    CHECK(lbnToByte(6, 0, true) == 10240 + 3 * 512);   /* 11776 */
    CHECK(lbnToByte(1, 0, true) == 10240 + 2 * 512);   /* 11264 home */
    /* side 1 is one track (5120) further into each cylinder */
    CHECK(lbnToByte(0, 1, true) == 10240 + 5120);
    CHECK(lbnToByte(6, 1, true) == 10240 + 5120 + 3 * 512);
}

TEST_CASE("lbn wraps modulo the 800-block side") {
    CHECK(lbnToByte(5, 0, false) == lbnToByte(5 + kSsBlocks, 0, false));
    CHECK(lbnToByte(5, 1, true)  == lbnToByte(5 + kSsBlocks, 1, true));
    /* track-0 wrap (LBN 790-799) stays in bounds with proper modulo skew */
    CHECK(lbnToByte(790, 0, false) < kSideSize);
}

TEST_CASE("dv/mz: whole-DS 1600-block volumes per the handlers' translate code") {
    /* MZ: byte-linear over the track-interleaved dump. */
    CHECK(lbnToByte(0, 0, true, Vol::mz) == 0);
    CHECK(lbnToByte(20, 0, true, Vol::mz) == 10240);        /* cyl 1 side 0 */
    CHECK(lbnToByte(110, 0, true, Vol::mz) == 110 * 512);   /* cyl 5 side 1 */
    CHECK(lbnToByte(1599, 0, true, Vol::mz) == 1599 * 512); /* cyl 79 side 1 sec 10 */
    /* DV: the same rotated 20 blocks — cylinder 0 last. */
    CHECK(lbnToByte(0, 0, true, Vol::dv) == 20 * 512);      /* cyl 1 side 0 */
    CHECK(lbnToByte(20, 0, true, Vol::dv) == 20480);        /* cyl 2 side 0 — OS oracle */
    CHECK(lbnToByte(110, 0, true, Vol::dv) == 66560);       /* cyl 6 side 1 — OS oracle */
    CHECK(lbnToByte(1580, 0, true, Vol::dv) == 0);          /* cyl 0 sector 1 */
    CHECK(lbnToByte(1599, 0, true, Vol::dv) == 19 * 512);   /* cyl 0 side 1 sec 10 */
    /* Wrap modulo the diskette, like the floppy mapping wraps its side. */
    CHECK(lbnToByte(5, 0, true, Vol::mz) == lbnToByte(5 + kDsBlocks, 0, true, Vol::mz));
    CHECK(lbnToByte(5, 0, true, Vol::dv) == lbnToByte(5 + kDsBlocks, 0, true, Vol::dv));
    /* floppy/linear dispatch to the existing mappings. */
    CHECK(lbnToByte(6, 0, false, Vol::floppy) == lbnToByte(6, 0, false));
    CHECK(lbnToByte(6, 1, true, Vol::floppy) == lbnToByte(6, 1, true));
    CHECK(lbnToByte(7, 0, false, Vol::linear) == 7 * 512);
}

TEST_CASE("size classifies single- vs double-sided") {
    CHECK(isDoubleSidedSize(kSideSize)   == false);  /* 409600 */
    CHECK(isDoubleSidedSize(kDoubleSize) == true);   /* 819200 */
    CHECK(isDoubleSidedSize(12345)       == false);
}

} /* TEST_SUITE */

TEST_CASE("lbnFromPhys inverts the OS-driver mapping for every block") {
    using namespace ms0515::disk;
    for (int lbn = 0; lbn < kSsBlocks; ++lbn) {
        const std::size_t off = lbnToByte(lbn, 0, false);
        const int track  = static_cast<int>(off / kTrackSize);
        const int sector = static_cast<int>((off % kTrackSize) / kBlock) + 1;
        CHECK(lbnFromPhys(track, sector) == lbn);
    }
}
