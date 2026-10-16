/* umount.c - /bin/umount DEVICE: the volume taken out of the tree, when
 * nothing on it is in use. */
#include "unistd.h"

int main(int argc, char **argv)
{
	if (argc != 2) {
		puts_fd(2, "usage: umount DEVICE\n");
		return 1;
	}
	if (umount(argv[1]) < 0) {
		complain("umount", argv[1]);
		return 1;
	}
	return 0;
}
