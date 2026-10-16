/* tty.c - the console: the keyboard by its interrupt, the screen through
 * the ROM.
 *
 * The keyboard interrupt (vector 0130, priority 5) takes every byte the
 * ROM has for it and edits the line in hand, echoing as it goes: a
 * character is put at the line's end, BS or DEL takes the last one back,
 * CR or LF ends the line, which goes, with a '\n', to the queue read()
 * takes from - so a line can be typed ahead while the process is busy.
 * Latin letters have their case turned: the ROM gives a bare key's letter
 * upper case and a Russian one lower; here both are lower bare and upper
 * with Shift.  SO and SI, which the ROM sends at RUS/LAT, are dropped: ROM-B
 * gives the Russian letters as eight-bit KOI-8 codes, and SO shown would
 * turn the console's Latin to Cyrillic glyphs.  Output turns '\n' into
 * CR LF. */
#include "kernel.h"
#include "proc.h"
#include "rom.h"

#define LINE 80				/* the line in hand */
#define QUEUE 256			/* the lines typed, not yet read */

static char line[LINE];
static int len;
static char queue[QUEUE];
static volatile int head, tail;		/* the queue: taken at tail, put at head */

static void put(int c)
{
	int next = (head + 1) % QUEUE;

	if (next != tail) {
		queue[head] = (char)c;
		head = next;
	}
}

static void input(int c)
{
	int i;

	if (c >= 'A' && c <= 'Z')
		c += 'a' - 'A';
	else if (c >= 'a' && c <= 'z')
		c -= 'a' - 'A';
	switch (c) {
	case 016:			/* SO, SI: the letters come as KOI-8 already */
	case 017:
		return;
	case 010:			/* BS, DEL */
	case 0177:
		if (len > 0) {
			len--;
			rom_putc(010);
			rom_putc(' ');
			rom_putc(010);
		}
		return;
	case '\r':
	case '\n':
		rom_putc('\r');
		rom_putc('\n');
		for (i = 0; i < len; i++)
			put(line[i]);
		put('\n');
		len = 0;
		wakeup(queue);
		return;
	}
	if (len < LINE - 1) {
		line[len++] = (char)c;
		rom_putc(c);
	}
}

void tty_intr(void)
{
	int c;

	while ((c = rom_getc()) >= 0)
		input(c);
	KBD_ROWS = 0;			/* the rows let go, as the DEC monitor does */
}

/* Up to n bytes of the next line, its '\n' the last; the process sleeps
 * till a line comes. */
int tty_read(char *buf, int n)
{
	int got = 0, s = spl7(), c;

	while (tail == head)
		sleep(queue);
	while (got < n && tail != head) {
		c = queue[tail];
		tail = (tail + 1) % QUEUE;
		buf[got++] = (char)c;
		if (c == '\n')
			break;
	}
	splx(s);
	return got;
}

int tty_write(const char *buf, int n)
{
	int i;

	for (i = 0; i < n; i++) {
		if (buf[i] == '\n')
			rom_putc('\r');
		rom_putc(buf[i]);
	}
	return n;
}

void tty_puts(const char *s)
{
	while (*s) {
		if (*s == '\n')
			rom_putc('\r');
		rom_putc(*s++);
	}
}
