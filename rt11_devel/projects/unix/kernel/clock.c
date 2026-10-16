/* clock.c - the frame interrupt as the kernel's clock.
 *
 * The timer interrupt (vector 0100, priority 6) comes once a frame while
 * dispatcher bit 9 is set: 50, 60 or 72 times a second as the jumpers have
 * it, read in register B bits 3-4.  It counts the seconds since the boot -
 * there is no calendar clock - and steps the ROM's cursor blink. */
#include "kernel.h"
#include "rom.h"

static unsigned hz;			/* frames a second */
static volatile unsigned frames;	/* in the second in hand */
static volatile unsigned long seconds;
static volatile unsigned blink;

void clock_start(void)
{
	static const unsigned rates[4] = {50, 72, 60, 50};

	hz = rates[(REG_B >> 3) & 3];
	VECTOR(0100, clock_entry, 0340);
	DISP_COPY |= DISP_TIMER;
	DISPATCHER = DISP_COPY;
}

void clock_intr(void)
{
	if (++frames >= hz) {
		frames = 0;
		seconds++;
	}
	if ((++blink & 15) == 0)
		rom_blink();
}

unsigned long clock_seconds(void)
{
	int s = spl7();
	unsigned long t = seconds;

	splx(s);
	return t;
}
