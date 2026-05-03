/*
 * cassette.h — MS0515 cassette tape interface.
 *
 * The MS0515 has a simple cassette port wired into the system register
 * file:
 *
 *   - Reg A bit 6 (CASS, write):  cassette output, drives the audio
 *                                  signal recorded onto tape.
 *   - Reg B bit 7 (CSIN, read):   cassette input, the analog
 *                                  comparator output that recovers the
 *                                  recorded bit stream from the
 *                                  playback head.
 *
 * The CPU bit-bangs the cassette protocol entirely in software: ROM
 * routines toggle Reg A bit 6 to write and poll Reg B bit 7 to read.
 * There is no UART, no DMA, no IRQ — just two single-bit pins.
 *
 * On real hardware with no cassette plugged in, the comparator's
 * input floats and its output is undefined (typically picks up RF
 * noise).  Software that polls the line in a tight loop without
 * timeout will hang.
 *
 * Currently this module models the "no tape inserted" case
 * faithfully: writes are absorbed and reads return 0.  Tape image
 * playback / record-to-host can be added later by extending
 * cassette_get_input / cassette_set_output without changing the
 * board-level wiring.
 */

#ifndef MS0515_CASSETTE_H
#define MS0515_CASSETTE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ms0515_cassette {
    /* Last value driven onto Reg A bit 6.  Tracked so a future
     * record-mode implementation can emit it onto a host-side
     * stream; the no-tape model does nothing with it. */
    bool output_bit;
} ms0515_cassette_t;

/*
 * cassette_init — Zero-initialise the cassette device.
 */
void cassette_init(ms0515_cassette_t *c);

/*
 * cassette_set_output — Called by the board when the CPU writes a
 * new value to Reg A bit 6.
 */
void cassette_set_output(ms0515_cassette_t *c, bool bit);

/*
 * cassette_get_input — Called by the board to fetch Reg B bit 7.
 * Currently always returns false (no tape inserted).
 */
bool cassette_get_input(const ms0515_cassette_t *c);

#ifdef __cplusplus
}
#endif

#endif /* MS0515_CASSETTE_H */
