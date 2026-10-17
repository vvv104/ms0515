/* rt11.c - /bin/rt11 PROGRAM.SAV: an RT-11 program run on the machine
 * (SPEC.md stage 7), the system gone; its end is a reset.
 *
 * A program that owns the machine (user/own/own.h).  It carries the little
 * monitor, mon.c, as a flat image built for 140000 (monbin.s), puts it
 * there - where RT-11's RMON is, above what the RT-11 program gets - and
 * hands it the path and the volume it came from; the monitor loads the
 * .SAV over this program and serves its requests. */
#include "../own/own.h"

#define MON 0140000u

extern const char mon_image[], mon_end[];	/* monbin.s */

int main(int argc, char **argv)
{
	char *to = (char *)MON;
	const char *from;

	if (argc != 2) {
		own_puts("usage: rt11 PROGRAM.SAV\r\n");
		while (own_getc() < 0)
			;
		return 0;
	}
	for (from = mon_image; from < mon_end; )
		*to++ = *from++;
	((void (*)(unsigned, unsigned, const char *))*(const unsigned *)MON)(own_unit, own_both, argv[1]);
	return 0;
}
