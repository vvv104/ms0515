/* mount.c - /bin/mount DEVICE DIR: the volume on the device into the tree
 * at the directory. */
#include "unistd.h"

int main(int argc, char **argv)
{
	if (argc != 3) {
		puts_fd(2, "usage: mount DEVICE DIR\n");
		return 1;
	}
	if (mount(argv[1], argv[2]) < 0) {
		complain("mount", argv[1]);
		return 1;
	}
	return 0;
}
