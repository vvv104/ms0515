/*
 * DeviceNames.cpp - see DeviceNames.hpp.
 */

#include "DeviceNames.hpp"

#include "Starter.hpp"

namespace ms0515::run {

namespace {

constexpr uint16_t kPnamePointer = 0404;    /* RMON + this: offset of $PNAME */
constexpr uint16_t kTablesEnd    = 0177777; /* ends $ENTRY                 */
constexpr int      kMostSlots    = 64;      /* no monitor has more devices */
constexpr uint16_t kUnitZero     = 30;      /* RAD50 "  0"                 */

} /* namespace */

uint16_t DeviceNames::word(uint16_t address)
{
    return emu_.readWord(address);
}

DeviceNames::DeviceNames(ms0515::Emulator &emu) : emu_(emu)
{
    const uint16_t rmon = word(kRmonPointer);
    pname_ = static_cast<uint16_t>(rmon + word(static_cast<uint16_t>(rmon + kPnamePointer)));

    /* $PNAME and $ENTRY, one after the other and of one length, end at
     * the -1 after $ENTRY. */
    int words = 0;
    while (words <= 2 * kMostSlots &&
           word(static_cast<uint16_t>(pname_ + 2 * words)) != kTablesEnd)
        ++words;
    if (words == 0 || words > 2 * kMostSlots || words % 2 != 0)
        return;
    slots_ = words / 2;
    const auto userTable = static_cast<uint16_t>(2 * (slots_ + 2));
    unam2_ = static_cast<uint16_t>(pname_ - userTable);
    unam1_ = static_cast<uint16_t>(unam2_ - userTable);
}

bool DeviceNames::known(uint16_t name)
{
    if (name == 0 || !valid())
        return true;
    for (int i = 0; i < slots_ + 2; ++i)
        if (word(static_cast<uint16_t>(unam2_ + 2 * i)) == name)
            return true;
    for (int i = 0; i < slots_; ++i) {
        const uint16_t permanent = word(static_cast<uint16_t>(pname_ + 2 * i));
        if (permanent == 0 || name < permanent)
            continue;
        const uint16_t rest = static_cast<uint16_t>(name - permanent);
        if (rest == 0 || (rest >= kUnitZero && rest <= kUnitZero + 7))
            return true;
    }
    return false;
}

bool DeviceNames::assign(uint16_t name, uint16_t physical)
{
    if (!valid() || name == 0)
        return false;
    int slot = -1;
    for (int i = 0; i < slots_; ++i) {
        const uint16_t user = word(static_cast<uint16_t>(unam2_ + 2 * i));
        if (user == name) { slot = i; break; }
        if (user == 0 && slot < 0) slot = i;
    }
    if (slot < 0)
        return false;
    emu_.writeWord(static_cast<uint16_t>(unam2_ + 2 * slot), name);
    emu_.writeWord(static_cast<uint16_t>(unam1_ + 2 * slot), physical);
    return true;
}

} /* namespace ms0515::run */
