/* tail.c - /bin/tail [-N] [FILE]: the last N lines of the file, or of the
 * input with none; ten without -N.  The input goes through once, its last
 * RING bytes kept: lines beyond them are not to be had. */
#include "unistd.h"

#define RING 2048

static char ring[RING], out[RING];

int main(int argc, char **argv)
{
	char buf[256];
	unsigned lines = 10, at = 0, kept = 0, from;
	int fd = 0, n, i;

	if (argc > 1 && argv[1][0] == '-') {
		lines = 0;
		for (i = 1; argv[1][i] >= '0' && argv[1][i] <= '9'; i++)
			lines = lines * 10 + (unsigned)(argv[1][i] - '0');
		argc--;
		argv++;
	}
	if (argc > 1 && (fd = open(argv[1], 0)) < 0) {
		complain("tail", argv[1]);
		return 1;
	}
	if (!lines)
		return 0;
	while ((n = read(fd, buf, sizeof buf)) > 0)
		for (i = 0; i < n; i++) {
			ring[at] = buf[i];
			at = (at + 1) % RING;
			if (kept < RING)
				kept++;
		}
	for (from = 0; from < kept; from++)	/* the ring in order, the oldest first */
		out[from] = ring[(at + RING - kept + from) % RING];
	/* Back from the end to the Nth line end before it - the last byte's
	 * not counted - the lines after it the last N. */
	for (from = kept; from > 0; from--)
		if (out[from - 1] == '\n' && from < kept && --lines == 0)
			break;
	write(1, out + from, (int)(kept - from));
	return 0;
}
