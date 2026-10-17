/* VARSHIFT - shifts by a count in a variable, the way GCC 15.2's pdp11
 * backend got them wrong without EIS (README.md, "A trap in the
 * compiler"): the loop that shifts a bit a step was to count down a
 * register of its own, a copy of the count; short of registers the
 * allocator gave it the count's own, and a count still wanted after - a
 * loop's index - came out of every shift as 0, the loop round it never
 * ending.  The nine bits of a mode as ls -l shows them, in a function as
 * busy as ls's, then 1 shifted left and 0100000 right, unsigned and
 * signed, by 0 to 15; the harness (../tests) knows the answers. */

#include <stdio.h>

struct status {
	unsigned dev, ino, mode, nlink;
	unsigned long size, mtime;
};

int fill(const char *name, struct status *st);	/* varshift_io.c */
void out(int fd, const char *s);

/* n in decimal, right-aligned in width. */
static void put_number(unsigned long n, int width)
{
	char digits[11], *p = digits + sizeof digits;

	*--p = 0;
	do
		*--p = (char)('0' + n % 10);
	while ((n /= 10) != 0);
	while (width-- > digits + sizeof digits - 1 - p)
		out(1, " ");
	out(1, p);
}

/* As ls's line, word for word: the mode's type and bits, the links and the
 * size after. */
static int line(const char *path, const char *name)
{
	static const char rwx[] = "rwxrwxrwx";
	char mode[11];
	struct status st;
	int i;

	if (fill(path, &st) < 0)
		return out(2, path), 1;
	mode[0] = (st.mode & 0170000) == 0040000 ? 'd' : (st.mode & 0170000) == 0060000 ? 'b' : '-';
	for (i = 0; i < 9; i++)
		mode[i + 1] = st.mode & (0400 >> i) ? rwx[i] : '-';
	mode[10] = 0;
	out(1, mode);
	out(1, " ");
	put_number(st.nlink, 2);
	out(1, " ");
	put_number(st.size, 7);
	out(1, " ");
	out(1, name);
	out(1, "\n");
	return 0;
}

int main(void)
{
	volatile unsigned one = 1, top = 0100000;
	int i;

	line("/etc/motd", "motd");
	for (i = 0; i < 16; i++)
		printf("%d %u %u %d\n", i, one << i, top >> i, (int)top >> i);
	puts("VARSHIFT DONE");
	return 0;
}
