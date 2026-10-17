/* when.c - seconds since 1970 to the calendar and back (when.h): the
 * Gregorian calendar, no zones, 1970-01-01 a Thursday. */
#include "unistd.h"
#include "when.h"

#define DAY 86400ul

static const char days[] = "SunMonTueWedThuFriSat";
static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";

static int leap(int y)
{
	return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

static int mlen(int y, int m)
{
	static const char len[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

	return len[m] + (m == 1 && leap(y));
}

void when(unsigned long t, struct date *d)
{
	unsigned long n = t / DAY, rest = t % DAY;

	d->hour = (int)(rest / 3600);
	d->min = (int)(rest / 60 % 60);
	d->sec = (int)(rest % 60);
	d->wday = (int)((n + 4) % 7);
	for (d->year = 1970; n >= (unsigned long)(365 + leap(d->year)); d->year++)
		n -= 365 + leap(d->year);
	for (d->mon = 0; n >= (unsigned long)mlen(d->year, d->mon); d->mon++)
		n -= mlen(d->year, d->mon);
	d->mday = (int)n + 1;
}

long since(const struct date *d)
{
	unsigned long n = 0;
	int y, m;

	if (d->year < 1970 || d->year > 2105 || d->mon < 0 || d->mon > 11 || d->mday < 1 ||
	    d->mday > mlen(d->year, d->mon) || d->hour > 23 || d->min > 59 || d->sec > 59 ||
	    d->hour < 0 || d->min < 0 || d->sec < 0)
		return -1;
	for (y = 1970; y < d->year; y++)
		n += 365 + leap(y);
	for (m = 0; m < d->mon; m++)
		n += mlen(d->year, m);
	n += d->mday - 1;
	return (long)(n * DAY + d->hour * 3600ul + d->min * 60ul + d->sec);
}

/* n as two digits, a space for the tens' 0 with pad. */
static char *two(char *o, int n, int pad)
{
	*o++ = n / 10 ? (char)('0' + n / 10) : pad ? ' ' : '0';
	*o++ = (char)('0' + n % 10);
	return o;
}

static char *word(char *o, const char *s)
{
	*o++ = s[0];
	*o++ = s[1];
	*o++ = s[2];
	return o;
}

void date_text(unsigned long t, int form, char *out)
{
	struct date d;
	char *o = out;

	when(t, &d);
	if (form == 0) {
		o = word(o, days + 3 * d.wday);
		*o++ = ' ';
	}
	o = word(o, months + 3 * d.mon);
	*o++ = ' ';
	o = two(o, d.mday, 1);
	*o++ = ' ';
	if (form) {
		o = two(two(o, d.year / 100, 0), d.year % 100, 0);
		*o++ = ' ';
	}
	o = two(o, d.hour, 0);
	*o++ = ':';
	o = two(o, d.min, 0);
	if (!form) {
		*o++ = ':';
		o = two(o, d.sec, 0);
		*o++ = ' ';
		o = two(two(o, d.year / 100, 0), d.year % 100, 0);
	}
	*o = 0;
}
