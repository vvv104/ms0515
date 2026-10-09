/* internal.h - what the machine library's files share: the shadows of
 * the two write-only registers, and the vectors. */

#ifndef MS0515_INTERNAL_H
#define MS0515_INTERNAL_H

/* Register C (177604) is write-only: its last value.  040 is 320x200
 * colour, black border, speaker low, the timer gate off; 010 the
 * console's 640x200. */
extern unsigned char ms_regc;

/* The dispatcher (177400): bits 0-6 the banks, 7 the VRAM window on, 9
 * the frame interrupt on, 10-11 the window's place.  02177 as the ROM
 * leaves it - banks primary, the window off with its place bits saying
 * 040000, the frame interrupt off; 03177 with the interrupt on, 07377
 * with the window on at 0100000 as well (MANICM's DSPROM/DSPOFF/DSPON). */
extern unsigned ms_dispatcher;
#define MS_DISPATCHER_ROM   02177
#define MS_DISPATCHER_CLOCK 01000
#define MS_DISPATCHER_VRAM  04200		/* on, and at 0100000 */

void ms_dispatcher_set(unsigned bits);
void ms_dispatcher_clear(unsigned bits);

/* An interrupt vector: its two words taken for a handler, with the
 * interrupts off round the swap, and given back. */
struct ms_vector {
	unsigned handler, psw;
};
void ms_vector_take(unsigned address, void (*handler)(void), struct ms_vector *saved);
void ms_vector_give(unsigned address, const struct ms_vector *saved);

#endif
