/* init.c - the first process: it gives its place to the shell running
 * /etc/rc - the volumes, then the shell for the console - or, without
 * /etc/rc, straight to the shell.
 *
 * init does not stay as a process of its own: the machine keeps two
 * processes at once, and the script needs the other slot for its
 * commands. */
#include "unistd.h"

int main(void)
{
	char *rc[] = {"sh", "/etc/rc", 0};
	char *sh[] = {"sh", 0};
	int fd = open("/etc/rc", 0);

	if (fd >= 0) {
		close(fd);
		puts_fd(1, "init: /etc/rc\n");	/* the boot's log */
		execv("/bin/sh", rc);
		complain("init", "/bin/sh");
	}
	execv("/bin/sh", sh);
	complain("init", "/bin/sh");
	return 1;
}
