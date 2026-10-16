/* bigmem.c - /bin/bigmem, a program that owns the machine (own.h), and the
 * proof of it: a pattern written over all of its room past its own image,
 * up to 150000 - the kernel's code, the processes' slots, the kernel's
 * tables alike - and read back; the root's superblock read through the
 * ROM, its magic shown; then a key, and the end, a reset. */
#include "own.h"

#define TOP 0150000u			/* the stack's room above it */

extern char _end[];
static unsigned block[256];

static unsigned char pattern(unsigned a)
{
	return (unsigned char)(a ^ a >> 8);
}

int main(void)
{
	volatile unsigned char *p;
	unsigned bad = 0, a;

	for (a = (unsigned)_end; a < TOP; a++) {
		p = (volatile unsigned char *)a;
		*p = pattern(a);
	}
	for (a = (unsigned)_end; a < TOP; a++) {
		p = (volatile unsigned char *)a;
		bad += *p != pattern(a);
	}
	own_puts("\r\nBIGMEM ");
	own_putoct((unsigned)_end);
	own_puts("-150000 ");
	own_puts(bad ? "BAD\r\n" : "OK\r\n");
	if (own_block(0, 1, block, 0) == 0) {
		own_puts("SUPER ");
		own_putoct(block[0]);
		own_puts("\r\n");
	} else {
		own_puts("SUPER UNREAD\r\n");
	}
	own_puts("PRESS A KEY\r\n");
	while (own_getc() < 0)
		;
	return 0;
}
