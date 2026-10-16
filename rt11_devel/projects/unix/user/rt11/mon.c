/* mon.c - the little RT-11 monitor of /bin/rt11 (SPEC.md stage 7): an
 * RT-11 program run on the machine, the system gone.
 *
 * It sits at 140000, where RT-11's RMON does, and gives the program what
 * RT-11 would below it: 001000-137776.  mon_main loads the .SAV as RT-11's
 * RUN does - block 0's job words (040..057, and 0400..0777 for the stack),
 * the blocks from 1 up to its high limit at 001000 and up - points the
 * EMT vector at the monitor and @#54 at a zeroed RMON's place, and enters
 * it at its start address, SP where block 0 says, at priority 0.
 *
 * The requests it serves (emt): .TTYIN and .TTYOUT, .PRINT, .EXIT - the
 * program's last words left on the screen until a key, then a reset, the
 * system booted again - .SETTOP, .LOOKUP, .READ and .READW,
 * .CLOSE, .PURGE, .WAIT, .GTIM and .DATE (nothing to tell: 0).  The files
 * are the .SAV's own directory's, of the system's file system, read
 * through the ROM (user/own/files.c): every disk device a program names -
 * DK:, SY:, DZ0: - is that directory, the name in lower case.  The rest of
 * RT-11 is not here - no USR, KMON, chaining, overlays or handlers; such a
 * request fails, C set and the error byte 0.  The keyboard is the system's:
 * ROM-B gives a bare Latin key as a capital, and the case of the Latin
 * letters is turned round, as the kernel's console does. */
#include "../own/own.h"

#define USRTOP 0137776u			/* the program's highest address */
#define NCHAN 8
#define SAVSIZ 64

#define C_BIT 1
/* The memory as C sees it (mon.s): an array at address 0, which the
 * compiler cannot take for a null pointer's target. */
extern volatile unsigned mem[];
#define WORD(a) (mem[(a) / 2])
#define BYTE(a) (((volatile unsigned char *)mem)[a])
#define ERRBYT BYTE(052)
#define TCBIT 0100			/* JSW: .TTYIN does not wait */
#define TTSPC 010000			/* JSW: the console character by character, no echo */

struct frame {
	unsigned r5, r4, r3, r2, r1, r0, pc, ps;
};

void mon_go(unsigned pc, unsigned sp) __attribute__((noreturn));	/* mon.s */
void emt_entry(void), mon_rti(void), mon_trap4(void), mon_trap10(void);

unsigned own_unit, own_both;		/* files.c's volume */

static char sav[SAVSIZ];		/* the .SAV's path; its directory is DK: */
static unsigned dirlen;
static unsigned char blk[512];
static unsigned rmon[0210];		/* RMON's fixed words, all 0 */
static struct own_file chan[NCHAN];
static char isopen[NCHAN];
static char line[82];			/* .TTYIN's line, typed with an echo */
static unsigned linepos, linelen, lineready;	/* lineready: up to its CR */

static void fail(const char *why)
{
	own_puts("rt11: ");
	own_puts(sav);
	own_puts(why);
	while (own_getc() < 0)
		;
	own_exit();
}

/* The .SAV laid down under the monitor and entered. */
void mon_main(unsigned unit, unsigned both, const char *path)
{
	struct own_file f;
	unsigned b, i, high, top;

	own_unit = unit;
	own_both = both;
	for (i = 0; path[i] && i < SAVSIZ - 1; i++) {
		sav[i] = path[i];
		if (path[i] == '/')
			dirlen = i;
	}
	if (own_open(sav, &f) != 0 || own_read(&f, 0, blk) != 0)
		fail(": not found\r\n");
	high = blk[050] | blk[051] << 8;
	if (high > USRTOP)
		fail(": too big\r\n");
	for (i = 0; i < 0400; i += 4)		/* the vectors the monitor's, not /bin/rt11's - */
		if (i < 040 || i >= 060) {	/* but for 040..057, the job's words */
			WORD(i) = (unsigned)mon_rti;
			WORD(i + 2) = 0340;
		}
	for (i = 040; i < 060; i++)		/* the job's words: start, stack, JSW, high */
		BYTE(i) = blk[i];
	for (i = 0400; i < 01000; i++)
		BYTE(i) = blk[i];
	top = high / 512;
	for (b = 1; b <= top && own_read(&f, b, (void *)&BYTE(b * 512)) == 0; b++)
		;
	WORD(004) = (unsigned)mon_trap4;
	WORD(010) = (unsigned)mon_trap10;
	WORD(030) = (unsigned)emt_entry;
	WORD(054) = (unsigned)rmon;
	ERRBYT = 0;
	mon_go(WORD(040), WORD(042) ? WORD(042) : 01000);
}

/* A RAD50 word into three characters at s, lower case, spaces kept. */
static void rad50(unsigned w, char *s)
{
	static const char set[] = " abcdefghijklmnopqrstuvwxyz$.%0123456789";

	s[2] = set[w % 40];
	w /= 40;
	s[1] = set[w % 40];
	s[0] = set[w / 40 % 40];
}

/* .LOOKUP's file at dblk (the device, the name in two words, the type) in
 * the .SAV's directory: 0 and the channel open, or -1. */
static int lookup(unsigned c, const unsigned *dblk)
{
	char name[12], path[SAVSIZ + 12];
	unsigned i, n = 0;

	rad50(dblk[1], name);
	rad50(dblk[2], name + 3);
	rad50(dblk[3], name + 7);
	name[6] = '.';
	if (name[0] == ' ')
		return -1;			/* a whole device: not here */
	for (i = 0; i < dirlen; i++)
		path[n++] = sav[i];
	path[n++] = '/';
	for (i = 0; i < 10; i++)
		if (name[i] != ' ' && !(i == 6 && name[7] == ' '))
			path[n++] = name[i];
	path[n] = 0;
	if (own_open(path, &chan[c]) != 0)
		return -1;
	isopen[c] = 1;
	return 0;
}

/* .READW: wcnt words of the channel's file from block b into buf; how many,
 * or -1 past its end. */
static int readw(unsigned c, unsigned b, unsigned char *buf, unsigned wcnt)
{
	unsigned long words = (chan[c].size + 1) / 2, at = (unsigned long)b * 256;
	unsigned n, i, k;

	if (at >= words)
		return -1;
	n = words - at < wcnt ? (unsigned)(words - at) : wcnt;
	for (i = 0; i < n; i += 256, b++) {
		if (own_read(&chan[c], b, blk) != 0)
			return -1;
		for (k = 0; k < 512 && i * 2 + k < n * 2; k++)
			buf[i * 2 + k] = blk[k];
	}
	return (int)n;
}

/* EMT 375: the request's block at r0, its code in the first word's high
 * byte, the channel in its low. */
static int request(struct frame *f)
{
	unsigned *area = (unsigned *)f->r0, c = area[0] & 0377;
	int n;

	switch (area[0] >> 8) {
	case 1:					/* .LOOKUP */
		if (c >= NCHAN || isopen[c]) {
			ERRBYT = 0;
			return -1;
		}
		if (lookup(c, (const unsigned *)area[1]) != 0) {
			ERRBYT = 1;
			return -1;
		}
		f->r0 = (unsigned)((chan[c].size + 511) / 512);
		return 0;
	case 010:				/* .READ, .READW */
		if (c >= NCHAN || !isopen[c]) {
			ERRBYT = 2;
			return -1;
		}
		if ((n = readw(c, area[1], (unsigned char *)area[2], area[3])) < 0) {
			ERRBYT = 0;
			return -1;
		}
		f->r0 = (unsigned)n;
		return 0;
	case 021:				/* .GTIM */
		WORD(area[1]) = 0;
		WORD(area[1] + 2) = 0;
		return 0;
	}
	ERRBYT = 0;
	return -1;
}

/* EMT 374: the code in r0's high byte, the channel in its low. */
static int request374(struct frame *f)
{
	unsigned c = f->r0 & 0377;

	switch (f->r0 >> 8) {
	case 0:					/* .WAIT */
		return 0;
	case 3:					/* .PURGE */
	case 6:					/* .CLOSE */
		if (c < NCHAN)
			isopen[c] = 0;
		return 0;
	case 012:				/* .DATE */
		f->r0 = 0;
		return 0;
	}
	ERRBYT = 0;
	return -1;
}

/* A key, -1 when there is none: the Latin letters' case turned round. */
static int key(void)
{
	int c = own_getc();

	if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))
		c ^= 040;
	return c;
}

/* .EXIT: the screen kept until a key, then the reset. */
static void the_end(void) __attribute__((noreturn));
static void the_end(void)
{
	own_puts("\r\nrt11: the end - a key brings the system back\r\n");
	while (own_getc() < 0)
		;
	own_exit();
}

/* A trap to 4 or 10 in the program: told as RT-11 tells it, and the end. */
void trap(unsigned vector, unsigned pc) __attribute__((noreturn));
void trap(unsigned vector, unsigned pc)
{
	own_puts("\r\n?MON-F-Trap to ");
	own_putoct(vector);
	own_puts(" ");
	own_putoct(pc);
	the_end();
}

/* .TTYIN: the next character - raw in the special mode, else of a line
 * typed with its echo, ended by CR LF; -1 when none and JSW says not to
 * wait. */
static int ttyin(void)
{
	int c;

	if (WORD(044) & TTSPC) {
		while ((c = key()) < 0)
			if (WORD(044) & TCBIT)
				return -1;
		return c;
	}
	while (!lineready) {
		if ((c = key()) < 0) {
			if (WORD(044) & TCBIT)
				return -1;
			continue;
		}
		if (c == '\r' || c == '\n') {
			line[linelen++] = '\r';
			line[linelen++] = '\n';
			own_puts("\r\n");
			linepos = 0;
			lineready = 1;
		} else if ((c == 0177 || c == 010) && linelen) {
			linelen--;
			own_puts("\010 \010");
		} else if (c >= ' ' && linelen < sizeof line - 2) {
			line[linelen++] = (char)c;
			own_putc(c);
		}
	}
	c = (unsigned char)line[linepos++];
	if (linepos == linelen)
		linepos = linelen = lineready = 0;
	return c;
}

/* .PRINT: up to a 0 (and CR LF after) or a 0200 (nothing after). */
static void print(const unsigned char *s)
{
	for (; *s && *s != 0200; s++)
		own_putc(*s);
	if (!*s)
		own_puts("\r\n");
}

/* An EMT of the program: the request done, its C bit set when it failed. */
void emt(struct frame *f)
{
	unsigned code = *(unsigned *)(f->pc - 2) & 0377;
	int c, failed = 0;

	switch (code) {
	case 0340:				/* .TTYIN */
		if ((c = ttyin()) < 0)
			failed = 1;
		else
			f->r0 = (unsigned)c;
		break;
	case 0341:				/* .TTYOUT */
		own_putc(f->r0 & 0377);
		break;
	case 0350:				/* .EXIT */
		the_end();
	case 0351:				/* .PRINT */
		print((const unsigned char *)f->r0);
		break;
	case 0354:				/* .SETTOP */
		if (f->r0 > USRTOP)
			f->r0 = USRTOP;
		WORD(050) = f->r0;
		break;
	case 0374:
		failed = request374(f) != 0;
		break;
	case 0375:
		failed = request(f) != 0;
		break;
	default:
		ERRBYT = 0;
		failed = 1;
	}
	if (failed)
		f->ps |= C_BIT;
	else
		f->ps &= ~C_BIT;
}
