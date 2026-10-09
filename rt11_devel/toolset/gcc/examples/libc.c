/* LIBC - the C library put to the test: one line a group, each line
 * known to the harness (../tests/test_programs.cpp) word for word. */

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void strings(void)
{
	char b[16];
	const char *h = "hello";

	strcpy(b, "foo");
	strcat(b, "bar");
	printf("%d %d %s %d %d %d", (int)strlen(h), strcmp("a", "b") < 0 ? -1 : 1, b,
	       (int)(strchr(h, 'l') - h), (int)(strrchr(h, 'l') - h), strncmp("abc", "abd", 2));
	strcpy(b, "abcdef");
	memmove(b + 1, b, 5);
	b[6] = 0;
	printf(" %s", b);
	memset(b, 'x', 3);
	memcpy(b + 3, "yz", 3);
	printf(" %s %d %d\n", b, memcmp("ab", "ac", 2) < 0 ? -1 : 1, memcmp("ab", "ab", 2));
}

static void heap(void)
{
	char local;
	char *p = malloc(100);
	char *q = malloc(200);
	char *r;
	int *z;
	int ok = 1;

	free(p);
	r = malloc(50);
	z = calloc(8, sizeof *z);
	for (int i = 0; i < 8; i++)
		ok = ok && z[i] == 0;
	strcpy(q, "keep me");
	q = realloc(q, 400);
	printf("%d %d %d %d\n", r == p, ok, strcmp(q, "keep me") == 0, (unsigned)p > (unsigned)&local);
	free(q);
	free(r);
	free(z);
}

int main(void)
{
	char buf[16];
	int n;
	char *end;

	printf("%d|%i|%u|%x|%X|%o|%c|%s|%%\n", 42, -42, 65535u, 255, 255, 8, 'c', "str");
	printf("[%5d][%-5d][%05d][%.2s][%5s][%-5s]\n", 42, 42, 42, "abc", "ab", "ab");
	printf("%ld|%ld|%lu|%lx\n", 123456L, -123456L, 4294967295UL, 0xABCDEFL);
	printf("%d|%d|%u\n", INT_MIN, INT_MAX, UINT_MAX);
	printf("%ld %ld %lu %ld\n", 123456L * 7L, -100000L * 3L, 65535UL * 65535UL, 1000000L / 7L);
	sprintf(buf, "%d-%s", 7, "x");
	puts(buf);
	n = snprintf(buf, 4, "%s", "abcdef");
	printf("%s %d\n", buf, n);
	strings();
	printf("%d %d %c %c %d\n", isdigit('5') != 0, isalpha('x') != 0, toupper('a'), tolower('Q'), isspace(' ') != 0);
	printf("%d %d %d %ld %ld [%s]\n", atoi("-123"), atoi("  42abc"), abs(-5),
	       strtol("777", 0, 8), strtol("-1F", &end, 16), end);
	heap();
	srand(1);
	n = rand();
	srand(1);
	printf("%d %d\n", rand() == n, n >= 0);
	puts("LIBC DONE");
	return 0;
}
