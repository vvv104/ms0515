/* ports.c - the joystick and the speaker.
 *
 * docs/programming.md: the joystick is port B of the MS7007 PPI,
 * 177542, bits 0 right, 1 left, 2 down, 3 up, 4 fire, low when pressed,
 * the Kempston order (SABOT2, FIST and MANICM read it).  The speaker
 * is one bit, register C bit 6, with the timer gate (bit 7) off so the
 * line is the program's and bit 5 set as well; a MOVB of the shadow
 * with bit 6 flipped toggles it - the Spectrum's OUT (254). */

#include <ms0515.h>
#include "internal.h"

unsigned ms_joystick(void)
{
	return (unsigned)(~*MS_JOYSTICK) & 037;
}

void ms_speaker_flip(void)
{
	ms_regc ^= 0100;
	*MS_REGISTER_C = ms_regc;
}
