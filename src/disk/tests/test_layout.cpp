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

TEST_CASE("size classifies single- vs double-sided") {
    CHECK(isDoubleSidedSize(kSideSize)   == false);  /* 409600 */
    CHECK(isDoubleSidedSize(kDoubleSize) == true);   /* 819200 */
    CHECK(isDoubleSidedSize(12345)       == false);
}

} /* TEST_SUITE */
