/*
 * cassette.c — MS0515 cassette tape interface (faithful no-tape model).
 *
 * See cassette.h for the hardware contract.  This file currently
 * implements only the "no cassette inserted" case: writes are
 * absorbed silently and reads return 0.  Adding playback (from a
 * host-side .wav or synthesised tape image) and record-to-host
 * support will plug into the same set/get hooks.
 */

#include <ms0515/core/cassette.h>
#include <string.h>

void cassette_init(ms0515_cassette_t *c)
{
    memset(c, 0, sizeof *c);
}

void cassette_set_output(ms0515_cassette_t *c, bool bit)
{
    c->output_bit = bit;
}

bool cassette_get_input(const ms0515_cassette_t *c)
{
    /* No tape inserted → comparator input has nothing to track,
     * line reads as 0.  Faithful to "silent / unplugged" hardware. */
    (void)c;
    return false;
}
