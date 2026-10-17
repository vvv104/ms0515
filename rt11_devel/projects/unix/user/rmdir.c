/* rmdir.c - /bin/rmdir DIR...: each directory taken away, when it is empty
 * and nobody is in it (the kernel's rmdir). */
#include "unistd.h"

int main(int argc, char **argv)
{
	int i, bad = 0;

	if (argc < 2) {
		puts_fd(2, "usage: rmdir DIR...\n");
		return 1;
	}
	for (i = 1; i < argc; i++)
		if (rmdir(argv[i]) < 0) {
			complain("rmdir", argv[i]);
			bad = 1;
		}
	return bad;
}
