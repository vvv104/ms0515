/* ln.c - /bin/ln OLD NEW: the file at OLD named NEW too, on its volume. */
#include "unistd.h"

int main(int argc, char **argv)
{
	if (argc != 3) {
		puts_fd(2, "usage: ln OLD NEW\n");
		return 1;
	}
	if (link(argv[1], argv[2]) < 0) {
		complain("ln", errno == EISDIR || errno == ENOENT ? argv[1] : argv[2]);
		return 1;
	}
	return 0;
}
