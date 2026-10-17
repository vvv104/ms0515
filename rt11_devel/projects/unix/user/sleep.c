/* sleep.c - /bin/sleep N: N seconds by the clock at the least, and the end:
 * the clock counts whole seconds, so till the (N+1)th has begun. */
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
	while (time() - start <= (long)n)
		;
	return 0;
}
