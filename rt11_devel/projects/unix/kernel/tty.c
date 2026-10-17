/* tty.c - the console: the keyboard by its interrupt, the screen through
 * the ROM.
 *
 * The keyboard interrupt (vector 0130, priority 5) takes every byte the
 * ROM has for it and edits the line in hand, echoing as it goes: a
 * character is put at the line's end, BS or DEL takes the last one back,
 * CR or LF ends the line, which goes, with a '\n', to the queue read()
 * takes from - so a line can be typed ahead while the process is busy.
 * Tab, while the shell asks for it (tty_tab), hands the line so far to
 * read() with a '\t' after it, the line kept in hand, and so do the
 * arrows up and down - VT52's ESC A and ESC B from the ROM - with ^P and
 * ^Q after it, for the shell's history; another escape is nothing.  Ctrl+U
 * wipes the line.  Ctrl+D ends the
 * input: the line so far handed over with no '\n' - empty, read() gives
 * 0.  Ctrl+C takes out the command in front: every process but the
 * session's shell and the background's (TTY_BG) marked to exit, those
 * waiting for a line woken; none, and the shell has an empty line.
 * Latin letters have their case turned: the ROM gives a bare key's letter
 * upper case and a Russian one lower; here both are lower bare and upper
 * with Shift.  SO and SI, which the ROM sends at RUS/LAT, are dropped: ROM-B
 * gives the Russian letters as eight-bit KOI-8 codes, and SO shown would
 * turn the console's Latin to Cyrillic glyphs.  Output turns '\n' into
 * CR LF. */
#include "high.h"
#include "rom.h"

#define line (HIGH->line)
static int len;
#define queue (HIGH->queue)
static volatile int head, tail;		/* the queue: taken at tail, put at head */
static char tabs;			/* Tab hands the line over (tty_tab) */
static char esc;			/* an ESC came: the next byte is an arrow's */

static void put(int c)
{
	int next = (head + 1) % QUEUE;

	if (next != tail) {
		queue[head] = (char)c;
		head = next;
	}
}

/* The line in hand to the queue, `end` after it - none for -1 - and the
 * readers woken. */
static void hand(int end)
{
	int i;

	for (i = 0; i < len; i++)
		put(line[i]);
	if (end >= 0)
		put(end);
	wakeup(queue);
}

/* c put into the line in hand, echoed: BS or DEL takes the last character
 * back, Ctrl+U the whole line. */
static void add(int c)
{
	do {
		if (c != 010 && c != 0177 && c != 025) {
			if (len < LINE - 1) {
				line[len++] = (char)c;
				rom_putc(c);
			}
			return;
		}
		if (!len)
			return;
		len--;
		rom_putc(010);
		rom_putc(' ');
		rom_putc(010);
	} while (c == 025);
}

/* Ctrl+C: the line dropped, the command in front marked to exit (proc.c
 * preempt) and woken to it; with none, the empty line for a new prompt. */
static void interrupt(void)
{
	struct proc *p;
	int hit = 0;

	tty_puts("^C\n");
	len = tabs = 0;
	for (p = HIGH->procs; p < HIGH->procs + NPROC; p++)
		if ((p->state == P_RUN || p->state == P_SLEEP) && p->pid != 1 && !p->bg)
			hit = p->killed = 1;
	if (hit)
		resched = 1;
	hand(hit ? -1 : '\n');
}

static void input(int c)
{
	if (esc) {				/* an arrow, its letter as the ROM gives it */
		esc = 0;
		if (tabs && (c == 'A' || c == 'B'))
			hand(c == 'A' ? 020 : 021);
		return;
	}
	if (c == 033) {
		esc = 1;
		return;
	}
	if (c >= 'A' && c <= 'Z')
		c += 'a' - 'A';
	else if (c >= 'a' && c <= 'z')
		c -= 'a' - 'A';
	switch (c) {
	case 016:			/* SO, SI: the letters come as KOI-8 already */
	case 017:
		return;
	case 003:			/* Ctrl+C */
		interrupt();
		return;
	case 004:			/* Ctrl+D: the end of the input */
		hand(004);
		len = tabs = 0;
		return;
	case '\t':
		if (tabs) {
			hand('\t');
			return;
		}
		break;
	case '\r':
	case '\n':
		rom_putc('\r');
		rom_putc('\n');
		hand('\n');
		len = tabs = 0;
		return;
	}
	add(c);
}

/* TTY_TAB (ioctl.h): s typed into the line, Tab handing it over till the
 * line ends. */
void tty_tab(const char *s)
{
	int ps = spl7();

	while (*s)
		add(*s++);
	tabs = 1;
	splx(ps);
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

	while (tail == head) {
		if (curproc->killed) {
			splx(s);
			return -EINTR;
		}
		sleep(queue);
	}
	while (got < n && tail != head) {
		c = queue[tail];
		tail = (tail + 1) % QUEUE;
		if (c == 004)			/* Ctrl+D's: what came before it, or 0 */
			break;
		buf[got++] = (char)c;
		if (c == '\n' || ((c == '\t' || c == 020 || c == 021) && tail == head))
			break;			/* a Tab's or an arrow's: the last */
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
