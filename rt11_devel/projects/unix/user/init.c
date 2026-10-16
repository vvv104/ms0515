/* init.c - the first process: it asks for a name, greets it, and gives its
 * place to the shell, /bin/sh.  When the shell exits, the kernel runs init
 * again, and the name is asked for anew.
 *
 * init does not stay as a process of its own: the machine keeps two
 * processes at once, and the shell needs the other slot for its commands. */
#include "unistd.h"

int main(void)
{
	char line[64];
	char *argv[] = {"sh", 0};
	int n;

	puts_fd(1, "\n\nlogin: ");
	n = read(0, line, sizeof line - 1);
	if (n < 0)
		n = 0;
	if (n > 0 && line[n - 1] == '\n')
		n--;
	line[n] = 0;
	puts_fd(1, "Hello, ");
	puts_fd(1, line);
	puts_fd(1, "\n");
	execv("/bin/sh", argv);
	complain("init", "/bin/sh");
	return 1;
}
