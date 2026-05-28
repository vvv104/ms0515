#include <doctest/doctest.h>

#include <ms0515/Disassembler.hpp>

#include <cstdint>
#include <vector>

TEST_SUITE("Disassembler") {

TEST_CASE("HALT") {
    std::vector<uint8_t> buf = {0x00, 0x00};
    auto inst = ms0515::Disassembler::decode(0, buf, 0);

    CHECK(inst.mnemonic == "HALT");
    CHECK(inst.operands.empty());
    CHECK(inst.length == 2);
}

TEST_CASE("MOV R1, R2") {
    /* MOV R1, R2 = 010102 octal = 0x1042 */
    std::vector<uint8_t> buf = {0x42, 0x10};
    auto inst = ms0515::Disassembler::decode(0, buf, 0);

    CHECK(inst.mnemonic == "MOV");
    CHECK(inst.operands == "R1, R2");
    CHECK(inst.length == 2);
}

TEST_CASE("CLR R0") {
    /* CLR R0 = 005000 octal = 0x0A00 */
    std::vector<uint8_t> buf = {0x00, 0x0A};
    auto inst = ms0515::Disassembler::decode(0, buf, 0);

    CHECK(inst.mnemonic == "CLR");
    CHECK(inst.operands == "R0");
    CHECK(inst.length == 2);
}

TEST_CASE("BR with forward offset") {
    /* BR .+4 = 000401 octal = 0x0101 */
    std::vector<uint8_t> buf = {0x01, 0x01};
    auto inst = ms0515::Disassembler::decode(0, buf, 0);

    CHECK(inst.mnemonic == "BR");
    CHECK(inst.length == 2);
}

} /* TEST_SUITE */
