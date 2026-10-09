/* clock.c - the frame interrupt as the program's clock.
 *
 * docs/programming.md, "The dispatcher, the VRAM window, the interrupts":
 * RT-11 has no periodic clock on this machine; the timer interrupt on
 * vector 100, 50 Hz, fires only while dispatcher bit 9 is set.  A
 * program that wants time takes the vector with a handler that counts
 * (clock.s), sets the bit, and puts both back on exit. */

#include <ms0515.h>
#include "internal.h"

#define FRAME_VECTOR 0100

void ms_frame_isr(void);		/* clock.s: ms_frames++, RTI */

volatile unsigned ms_frames;
static struct ms_vector saved;

void ms_clock_begin(void)
{
	ms_frames = 0;
	ms_vector_take(FRAME_VECTOR, ms_frame_isr, &saved);
	ms_dispatcher_set(MS_DISPATCHER_CLOCK);
}

void ms_clock_end(void)
{
	ms_dispatcher_clear(MS_DISPATCHER_CLOCK);
	ms_vector_give(FRAME_VECTOR, &saved);
}

void ms_wait_frames(unsigned n)
{
	unsigned from = ms_frames;

	while (ms_frames - from < n)
		;
}

/* The vector's two words.  GCC takes a constant address for a thing of
 * no size and warns of every access past it; the empty asm hides the
 * constant from it. */
static volatile unsigned *vector_at(unsigned address)
{
	volatile unsigned *vector = (volatile unsigned *)address;

	__asm__("" : "+r"(vector));
	return vector;
}

void ms_vector_take(unsigned address, void (*handler)(void), struct ms_vector *s)
{
	volatile unsigned *vector = vector_at(address);
	unsigned psw = ms_interrupts_off();

	s->handler = vector[0];
	s->psw = vector[1];
	vector[0] = (unsigned)handler;
	vector[1] = 0340;		/* the handler at priority 7 */
	ms_interrupts_restore(psw);
}

void ms_vector_give(unsigned address, const struct ms_vector *s)
{
	volatile unsigned *vector = vector_at(address);
	unsigned psw = ms_interrupts_off();

	vector[0] = s->handler;
	vector[1] = s->psw;
	ms_interrupts_restore(psw);
}
