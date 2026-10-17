/* when.h - seconds since 1970 to the calendar and back (when.c), for date
 * and ls. */
#ifndef UNIX_WHEN_H
#define UNIX_WHEN_H

struct date {
	int year, mon, mday;		/* mon 0..11, mday 1..31 */
	int hour, min, sec, wday;	/* wday 0 Sunday */
};

void when(unsigned long t, struct date *d);

/* The seconds of the date, -1 when it is no date (from 1970 on). */
long since(const struct date *d);

/* The date as text into out: 0 "Sat Oct 17 16:42:05 2026", 25 bytes with
 * the NUL; 1 "Oct 17 2026 16:42", 18. */
void date_text(unsigned long t, int form, char *out);

#endif
