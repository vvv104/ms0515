/* clock.c - the frame interrupt as the kernel's clock.
 *
 * The timer interrupt (vector 0100, priority 6) comes once a frame while
 * dispatcher bit 9 is set: 50, 60 or 72 times a second as the jumpers have
 * it, read in register B bits 3-4.  It counts the seconds since the boot -
 * there is no calendar clock - steps the ROM's cursor blink, and every
 * SLICE frames sets resched: the process running is to give the processor
 * to the other - at once when the clock came on it in its own code
 * (trap.s), else at the end of its system call (sys.c), by preempt(). */
#include "kernel.h"
#include "rom.h"

static unsigned hz;			/* frames a second */
static volatile unsigned frames;	/* in the second in hand */
static volatile unsigned long seconds;
static volatile unsigned blink;

#define SLICE 4				/* frames a process runs before the other */

volatile int resched;

void clock_start(void)
{
	static const unsigned rates[4] = {50, 72, 60, 50};

	hz = rates[(REG_B >> 3) & 3];
	VECTOR(0100, clock_entry, 0340);
	DISP_COPY |= DISP_TIMER;
	DISPATCHER = DISP_COPY;
}

int clock_intr(void)
{
	if (++frames >= hz) {
		frames = 0;
		seconds++;
	}
	if ((++blink & 15) == 0)
		rom_blink();
	if (blink % SLICE == 0)
		resched = 1;
	return resched;
}

unsigned long clock_seconds(void)
{
	int s = spl7();
	unsigned long t = seconds;

	splx(s);
	return t;
}
