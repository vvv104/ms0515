/* bigmem.c - /bin/bigmem [FILE]: a program that owns the machine (own.h),
 * and the proof of it: its command line shown; a pattern written over all
 * of its room past its own image, up to 150000 - the kernel's code, the
 * processes' slots, the kernel's tables alike - and read back; the root's
 * superblock read through the ROM, its magic shown; FILE found by the
 * library's own reader and its last line shown; then a key, and the end,
 * a reset. */
#include "own.h"

#define TOP 0150000u			/* the stack's room above it */

extern char _end[];
static unsigned block[256];

static unsigned char pattern(unsigned a)
{
	return (unsigned char)(a ^ a >> 8);
}

/* The last line of the file at path, or NO FILE. */
static void last_line(const char *path)
{
	static char text[2 * 512 + 1];
	struct own_file f;
	unsigned last, n, i, from = 0;

	if (own_open(path, &f) != 0 || !f.size) {
		own_puts("NO FILE\r\n");
		return;
	}
	last = (unsigned)((f.size - 1) / 512);
	n = last ? 2 : 1;			/* the line may begin a block back */
	for (i = 0; i < n; i++)
		if (own_read(&f, last - (n - 1) + i, text + i * 512) != 0) {
			own_puts("NO FILE\r\n");
			return;
		}
	n = (unsigned)(f.size - (unsigned long)(last - (n - 1)) * 512);
	if (n && text[n - 1] == '\n')
		n--;
	text[n] = 0;
	for (i = 0; i < n; i++)
		if (text[i] == '\n')
			from = i + 1;
	own_puts("LAST ");
	own_puts(text + from);
	own_puts("\r\n");
}

int main(int argc, char **argv)
{
	volatile unsigned char *p;
	unsigned bad = 0, a;
	int i;

	for (a = (unsigned)_end; a < TOP; a++) {
		p = (volatile unsigned char *)a;
		*p = pattern(a);
	}
	for (a = (unsigned)_end; a < TOP; a++) {
		p = (volatile unsigned char *)a;
		bad += *p != pattern(a);
	}
	own_puts("\r\nARGS ");
	own_putoct((unsigned)argc);
	own_puts("\r\n");
	for (i = 0; i < argc; i++) {
		own_puts("ARG ");
		own_puts(argv[i]);
		own_puts("\r\n");
	}
	own_puts("BIGMEM ");
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
	if (argc > 1)
		last_line(argv[1]);
	own_puts("PRESS A KEY\r\n");
	while (own_getc() < 0)
		;
	return 0;
}
