/* init.c - the first process: it asks for a name - again while none is
 * given, an empty line or spaces - greets it, and gives its place to the
 * shell, /bin/sh.  When the shell exits, the kernel runs init again, and
 * the name is asked for anew.
 *
 * init does not stay as a process of its own: the machine keeps two
 * processes at once, and the shell needs the other slot for its commands. */
#include "unistd.h"

/* The name typed, its spaces at either end taken off; 0 when there is none. */
static char *ask(char *line, int size)
{
	char *name;
	int n;

	puts_fd(1, "\n\nlogin: ");
	n = read(0, line, size - 1);
	if (n < 0)
		n = 0;
	while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == ' '))
		n--;
	line[n] = 0;
	for (name = line; *name == ' '; name++)
		;
	return *name ? name : 0;
}

int main(void)
{
	char line[64], *name;
	char *argv[] = {"sh", 0};

	while (!(name = ask(line, sizeof line)))
		;
	puts_fd(1, "Hello, ");
	puts_fd(1, name);
	puts_fd(1, "\n");
	execv("/bin/sh", argv);
	complain("init", "/bin/sh");
	return 1;
}
