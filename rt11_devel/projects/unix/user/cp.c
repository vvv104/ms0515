/* cp.c - /bin/cp FROM TO: the file copied to TO, or into TO under its own
 * name when TO is a directory. */
#include "unistd.h"
#include "copy.h"

int main(int argc, char **argv)
{
	char to[PATHMAX];

	if (argc != 3) {
		puts_fd(2, "usage: cp FROM TO\n");
		return 1;
	}
	target(argv[1], argv[2], to);
	return copy("cp", argv[1], to) < 0 ? 1 : 0;
}
