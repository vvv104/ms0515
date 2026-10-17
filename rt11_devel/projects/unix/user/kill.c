/* kill.c - /bin/kill PID...: each process taken out, as Ctrl+C takes out the
 * command in front; not the session's shell. */
#include "unistd.h"

int main(int argc, char **argv)
{
	int i, pid, bad = 0;
	const char *s;

	for (i = 1; i < argc; i++)
		for (s = argv[i]; *s; s++)
			if (*s < '0' || *s > '9')
				argc = 0;
	if (argc < 2) {
		puts_fd(2, "usage: kill PID...\n");
		return 1;
	}
	for (i = 1; i < argc; i++) {
		for (pid = 0, s = argv[i]; *s; s++)
			pid = pid * 10 + *s - '0';
		if (kill(pid, 15) < 0) {
			complain("kill", argv[i]);
			bad = 1;
		}
	}
	return bad;
}
