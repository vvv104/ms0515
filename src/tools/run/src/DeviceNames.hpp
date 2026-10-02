/*
 * DeviceNames.hpp - the device names the monitor knows, and one more.
 *
 * RT-11 names a device by up to three characters.  Some names are
 * permanent - a handler's, with a unit digit or without (DZ, DZ1, HD);
 * others are given with ASSIGN and stand for a permanent one (DK for HD
 * in the saved state).  A program built around a name of its own - DECUS
 * C reads its headers from C: - expects somebody to have typed
 * `ASSIGN dev C` first, and ms0515-run has nobody to type it: the monitor
 * answers ?MON-F-No device.
 *
 * So a name the monitor does not know is made to stand for the program's
 * folder, the way ASSIGN would do it, at the moment a program names it.
 * No name is known to the tool beforehand; a name the monitor does know
 * is left alone.
 *
 * The tables are the monitor's own ($PNAME, $UNAM1, $UNAM2 in
 * RMONSJ.MAC), found at run time the way DEC's FILEX finds them (TRNLOG):
 * $PNAME through the pointer at RMON+404, the tables' length by the -1
 * that ends $ENTRY, the two user tables below $PNAME, each two words
 * longer than it.
 */

#ifndef MS0515_RUN_DEVICENAMES_HPP
#define MS0515_RUN_DEVICENAMES_HPP

#include <ms0515/Emulator.hpp>

#include <cstdint>

namespace ms0515::run {

class DeviceNames {
public:
    /* Reads where the tables are; valid() is false when the monitor's
     * memory does not hold them as expected. */
    explicit DeviceNames(ms0515::Emulator &emu);

    [[nodiscard]] bool valid() const noexcept { return slots_ > 0; }

    /* The monitor would find `name` (a RAD50 word): no name at all (the
     * default device), an assigned name, a permanent one or a permanent
     * one with a unit digit. */
    [[nodiscard]] bool known(uint16_t name);

    /* ASSIGN `physical` `name`: the name stands for the permanent one
     * from now on.  False when the table of assigned names is full. */
    bool assign(uint16_t name, uint16_t physical);

private:
    [[nodiscard]] uint16_t word(uint16_t address);

    ms0515::Emulator &emu_;
    uint16_t pname_ = 0, unam1_ = 0, unam2_ = 0;
    int      slots_ = 0;
};

} /* namespace ms0515::run */

#endif /* MS0515_RUN_DEVICENAMES_HPP */
