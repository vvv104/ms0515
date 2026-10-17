/* ps.c - /bin/ps: the processes - pid, parent, state (R running, S asleep,
 * Z gone and not waited for), & for the background, the name (getproc). */
#include "unistd.h"

static void right(unsigned n, int width)
{
	char digits[6];
	int i = sizeof digits;

	do {
		digits[--i] = (char)('0' + n % 10);
		n /= 10;
	} while (n);
	while (width-- > (int)sizeof digits - i)
		puts_fd(1, " ");
	write(1, digits + i, (int)sizeof digits - i);
}

int main(void)
{
	struct pinfo p;
	char flags[3];
	int n;

	puts_fd(1, "  PID  PPID S  NAME\n");
	for (n = 0; getproc(n, &p) == 0; n++) {
		if (!p.state)
			continue;
		right((unsigned)p.pid, 5);
		puts_fd(1, " ");
		right((unsigned)p.ppid, 5);
		flags[0] = ' ';
		flags[1] = " RSZ"[p.state & 3];
		flags[2] = p.bg ? '&' : ' ';
		write(1, flags, 3);
		puts_fd(1, " ");
		puts_fd(1, p.name);
		puts_fd(1, "\n");
	}
	return 0;
}
