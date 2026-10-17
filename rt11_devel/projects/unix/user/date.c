/* date.c - /bin/date [YYMMDDhhmm[.ss]]: the date and the time; with the
 * argument they are set first (stime) - years 70..99 the 1900s, the rest
 * the 2000s.  The machine has no calendar clock: after the boot the
 * kernel counts from 1970 until the date is set. */
#include "unistd.h"
#include "when.h"

static int usage(void)
{
	puts_fd(2, "usage: date [YYMMDDhhmm[.ss]]\n");
	return 1;
}

/* Two digits at s: their value, -1 for none. */
static int two(const char *s)
{
	if (s[0] < '0' || s[0] > '9' || s[1] < '0' || s[1] > '9')
		return -1;
	return (s[0] - '0') * 10 + s[1] - '0';
}

int main(int argc, char **argv)
{
	char text[25];
	struct date d;
	const char *s;
	long t;

	if (argc > 2)
		return usage();
	if (argc == 2) {
		s = argv[1];
		if (strlen(s) != 10 && (strlen(s) != 13 || s[10] != '.'))
			return usage();
		d.year = two(s);
		d.year += d.year < 70 ? 2000 : 1900;
		d.mon = two(s + 2) - 1;
		d.mday = two(s + 4);
		d.hour = two(s + 6);
		d.min = two(s + 8);
		d.sec = strlen(s) == 13 ? two(s + 11) : 0;
		if ((t = since(&d)) < 0 || stime(t) < 0)
			return usage();
	}
	date_text((unsigned long)time(), 0, text);
	puts_fd(1, text);
	puts_fd(1, "\n");
	return 0;
}
