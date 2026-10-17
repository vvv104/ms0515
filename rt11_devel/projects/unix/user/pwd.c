/* pwd.c - /bin/pwd: the process's own directory, its path from the root
 * (cwd.c). */
#include "unistd.h"
#include "cwd.h"

int main(void)
{
	char path[PATHMAX];

	if (getcwd(path, sizeof path) < 0) {	/* deeper than a path names, or lost */
		complain("pwd", ".");
		return 1;
	}
	puts_fd(1, path);
	puts_fd(1, "\n");
	return 0;
}
