/* LIBC2 - the rest of the C library put to the test: the string
 * functions LIBC left out, the long conversions, printf's corners.  A
 * line a group, each known to the harness (../tests) word for word. */

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
	char b[16];
	const char *h = "hello world";
	const char *p;
	char *end;
	int n;

	strncpy(b, "abc", 6);		/* the rest zeroed */
	printf("%s %d %d", b, b[3], b[5]);
	strncpy(b, "abcdef", 3);	/* cut, not terminated */
	b[3] = 0;
	printf(" %s", b);
	strcpy(b, "ab");
	strncat(b, "cdefgh", 3);
	printf(" %s", b);
	p = strstr(h, "o w");
	printf(" %d %d %d", (int)(p - h), strstr(h, "xyz") == 0, (int)(strstr(h, "") - h));
	p = memchr(h, 'w', 11);
	printf(" %d %d\n", (int)(p - h), memchr(h, 'z', 11) == 0);

	printf("%ld %ld %ld", labs(-123456L), atol("-70000"), atol("  12abc"));
	printf(" %lu %lu", strtoul("65535", 0, 10), strtoul("0xffff", 0, 0));
	printf(" %ld %ld", strtol("-0x10", 0, 0), strtol("017", 0, 0));
	n = (int)strtol("12z", &end, 10);	/* before end is read: the arguments' order is the compiler's */
	printf(" %d %s\n", n, end);

	n = snprintf(b, 0, "%d", 12345);		/* nothing written */
	strcpy(b, "keep");
	printf("%d %s", n, b);
	n = snprintf(b, 1, "%d", 12345);		/* the 0 alone */
	printf(" %d [%s]", n, b);
	printf(" [%5c][%-5c][%3s][%-7d][%07d]", 'x', 'y', "toolong", -42, -42);
	printf(" [%lx][%lo][%5lu]\n", -1L, 8L, 42UL);

	printf("%d %d %u %ld\n", INT_MIN / 2, -7 / 2, 7u % 3u, -7L % 3L);
	puts("LIBC2 DONE");
	return 0;
}
