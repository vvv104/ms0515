/*
 * test_layout.cpp — LBN→byte mappings, checked against the worked
 * examples in docs/hardware/filesystem.md plus computed DS-spanning
 * points validated against the vvv104 disk family.
 */

#include <doctest/doctest.h>
#include <ms0515/disk/Layout.hpp>

#include <ostream>
#include <string>

using namespace ms0515::disk;

TEST_SUITE("Layout") {

TEST_CASE("ss-canonical matches filesystem.md example table") {
    /* | Block | Byte offset | */
    CHECK(lbnToByte(Layout::SsCanonical, 0)   == 5120);   /* boot      */
    CHECK(lbnToByte(Layout::SsCanonical, 1)   == 6144);   /* home      */
    CHECK(lbnToByte(Layout::SsCanonical, 6)   == 6656);   /* directory */
    CHECK(lbnToByte(Layout::SsCanonical, 10)  == 10240);  /* first data */
    CHECK(lbnToByte(Layout::SsCanonical, 790) == 0);      /* track-0 wrap */
    CHECK(lbnToByte(Layout::SsCanonical, 799) == 4608);   /* last block */
}

TEST_CASE("ss-osa-skew diverges from canonical at track 2+") {
    CHECK(lbnToByte(Layout::SsOsaSkew, 10) == 11264);
    CHECK(lbnToByte(Layout::SsOsaSkew, 20) == 17408);
    CHECK(lbnToByte(Layout::SsOsaSkew, 30) == 23552);
    /* LBNs 0..6 (track 1, skew 0) coincide with canonical so metadata
     * parses identically. */
    CHECK(lbnToByte(Layout::SsOsaSkew, 0) == lbnToByte(Layout::SsCanonical, 0));
    CHECK(lbnToByte(Layout::SsOsaSkew, 6) == lbnToByte(Layout::SsCanonical, 6));
}

TEST_CASE("ss-cyl0last-noil: cyl-0-last, no interleave") {
    CHECK(lbnToByte(Layout::SsCyl0LastNoIl, 0) == 5120);
    CHECK(lbnToByte(Layout::SsCyl0LastNoIl, 1) == 5632);
    CHECK(lbnToByte(Layout::SsCyl0LastNoIl, 6) == 8192);
    CHECK(lbnToByte(Layout::SsCyl0LastNoIl, 790) == 0);
}

TEST_CASE("ss-cyl0first-noil: no rotation, no interleave") {
    CHECK(lbnToByte(Layout::SsCyl0FirstNoIl, 0)  == 0);
    CHECK(lbnToByte(Layout::SsCyl0FirstNoIl, 6)  == 3072);
    CHECK(lbnToByte(Layout::SsCyl0FirstNoIl, 10) == 5120);
}

TEST_CASE("ss-lbn-linear: block N at byte N*512") {
    CHECK(lbnToByte(Layout::SsLbnLinear, 0)   == 0);
    CHECK(lbnToByte(Layout::SsLbnLinear, 6)   == 3072);
    CHECK(lbnToByte(Layout::SsLbnLinear, 100) == 51200);
}

TEST_CASE("ds-cyl0last-noil: double-sided spanning volume") {
    /* cyl=(N/20+1)%80, head=(N/10)%2, sec=N%10,
     * byte=(cyl*2+head)*5120 + sec*512 */
    CHECK(lbnToByte(Layout::DsCyl0LastNoIl, 0)    == 10240);  /* cyl1 head0 */
    CHECK(lbnToByte(Layout::DsCyl0LastNoIl, 8)    == 14336);  /* vvv104 data_start */
    CHECK(lbnToByte(Layout::DsCyl0LastNoIl, 10)   == 15360);  /* cyl1 head1 */
    CHECK(lbnToByte(Layout::DsCyl0LastNoIl, 1580) == 0);      /* track-0 wrap */
}

TEST_CASE("volume metadata") {
    CHECK(isDoubleSided(Layout::SsCanonical)   == false);
    CHECK(isDoubleSided(Layout::DsCyl0LastNoIl) == true);
    CHECK(volumeBlocks(Layout::SsCanonical)   == 800);
    CHECK(volumeBlocks(Layout::DsCyl0LastNoIl) == 1600);
    CHECK(layoutTag(Layout::SsCanonical)    == "ss-canonical");
    CHECK(layoutTag(Layout::DsCyl0LastNoIl) == "ds-cyl0last-noil");
}

TEST_CASE("lbn wraps modulo volume size") {
    /* block N and N+volumeBlocks land at the same byte */
    CHECK(lbnToByte(Layout::SsCanonical, 5) ==
          lbnToByte(Layout::SsCanonical, 5 + kSsBlocks));
    CHECK(lbnToByte(Layout::DsCyl0LastNoIl, 5) ==
          lbnToByte(Layout::DsCyl0LastNoIl, 5 + kDsBlocks));
}

} /* TEST_SUITE */
