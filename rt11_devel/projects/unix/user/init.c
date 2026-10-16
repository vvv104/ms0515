/* init.c - the first process: it gives its place to the shell running
 * /etc/rc - the volumes and the swap, then getty - or, without /etc/rc,
 * straight to /etc/getty.
 *
 * init does not stay as a process of its own: until /etc/rc has turned swap
 * on the machine keeps two processes at once, and the script needs the
 * other slot for its commands. */
#include "unistd.h"

int main(void)
{
	char *rc[] = {"sh", "/etc/rc", 0};
	char *getty[] = {"getty", 0};
	int fd = open("/etc/rc", 0);

	if (fd >= 0) {
		close(fd);
		execv("/bin/sh", rc);
		complain("init", "/bin/sh");
	}
	execv("/etc/getty", getty);
	complain("init", "/etc/getty");
	return 1;
}
