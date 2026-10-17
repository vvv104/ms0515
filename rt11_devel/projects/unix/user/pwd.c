/* pwd.c - /bin/pwd: the process's own directory, its path from the root
 * (cwd.c). */
#include "unistd.h"
#include "cwd.h"

int main(void)
{
	char path[128];

	if (getcwd(path, sizeof path) < 0) {
		puts_fd(2, "pwd: lost on the way up\n");
		return 1;
	}
	puts_fd(1, path);
	puts_fd(1, "\n");
	return 0;
}
