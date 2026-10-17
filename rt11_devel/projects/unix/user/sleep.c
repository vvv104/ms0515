/* sleep.c - /bin/sleep N: N seconds by the clock, and the end. */
#include "unistd.h"

int main(int argc, char **argv)
{
	long start = time();
	unsigned n = 0;
	const char *s;

	if (argc != 2) {
		puts_fd(2, "usage: sleep SECONDS\n");
		return 1;
	}
	for (s = argv[1]; *s >= '0' && *s <= '9'; s++)
		n = n * 10 + (unsigned)(*s - '0');
	while (time() - start < (long)n)
		;
	return 0;
}
