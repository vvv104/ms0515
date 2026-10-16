/* ramdisk.c - the EX board's 512 KB RAM disk, in pages of 256 bytes.
 *
 * Not in the processor's address space: page p is set in the board's PPI,
 * its low byte in port A (177530), its high bits with START in port B
 * (177532) after a START|RESET that sets the byte counter to 0, and then
 * the page goes byte by byte through the data port 177550, the counter
 * stepping on each (docs/hardware/ramdisk.md).  A page is moved with the
 * interrupts masked: the board's DRAM wants each access within 100
 * microseconds.
 *
 * The board is found as EX.SYS finds it: two pages written with two
 * patterns and the first read back; a machine without the board answers
 * otherwise, or with a bus error, which a handler on vector 4 takes for
 * the time of the test. */
#include "high.h"
#include "ramdisk.h"

#define RD_PORT_A (*(volatile unsigned char *)0177530)
#define RD_PORT_B (*(volatile unsigned char *)0177532)
#define RD_CTRL   (*(volatile unsigned char *)0177536)
#define RD_DATA   (*(volatile unsigned char *)0177550)
#define RD_START  0200
#define RD_RESET  040

int rd_present;
extern volatile int nxm;		/* trap.s: bus errors seen */
void nxm_entry(void);

static void page(unsigned p)
{
	RD_PORT_A = (unsigned char)p;
	RD_PORT_B = RD_START | RD_RESET;
	RD_PORT_B = (unsigned char)(RD_START | ((p >> 8) & 7));
}

/* A page's bytes four a step: the loop's count and test a quarter as
 * often. */
void rd_write(unsigned p, const void *from, unsigned pages)
{
	const unsigned char *s = from;
	int ps, i;

	for (; pages; pages--, p++) {
		ps = spl7();
		page(p);
		for (i = RD_PAGE / 4; i; i--) {
			RD_DATA = *s++;
			RD_DATA = *s++;
			RD_DATA = *s++;
			RD_DATA = *s++;
		}
		splx(ps);
	}
}

void rd_read(unsigned p, void *to, unsigned pages)
{
	unsigned char *d = to;
	int ps, i;

	for (; pages; pages--, p++) {
		ps = spl7();
		page(p);
		for (i = RD_PAGE / 4; i; i--) {
			*d++ = RD_DATA;
			*d++ = RD_DATA;
			*d++ = RD_DATA;
			*d++ = RD_DATA;
		}
		splx(ps);
	}
}

/* Whether the board is there: page 0 and page 1 written, page 0 read. */
int rd_probe(void)
{
	unsigned char *a = (unsigned char *)HIGH->bufs[0].data, *b = a + RD_PAGE;	/* no block in it yet */
	unsigned old4 = vectors[2], old6 = vectors[3];
	int i;

	nxm = 0;
	VECTOR(04, nxm_entry, 0340);
	RD_CTRL = 0211;				/* mode 0: A and B out, C in */
	for (i = 0; i < RD_PAGE; i++) {
		a[i] = (unsigned char)(i * 37 + 5);
		b[i] = (unsigned char)~a[i];
	}
	rd_write(0, a, 1);
	rd_write(1, b, 1);
	memset(b, 0, RD_PAGE);
	rd_read(0, b, 1);
	vectors[2] = old4;
	vectors[3] = old6;
	if (nxm)
		return rd_present = 0;
	for (i = 0; i < RD_PAGE; i++)
		if (a[i] != b[i])
			return rd_present = 0;
	return rd_present = 1;
}
