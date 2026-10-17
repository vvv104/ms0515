/* mv.c - /bin/mv FROM TO: the name FROM moved to TO, or into TO under its
 * own name when TO is a directory.  On the same volume the file is linked
 * at its new name and the old one removed; to another volume it is copied
 * and then removed.  A file without w, the system's, is not moved. */
#include "unistd.h"
#include "copy.h"
#include "../kernel/fs.h"

int main(int argc, char **argv)
{
	char to[PATHMAX];
	struct stat st;

	if (argc != 3) {
		puts_fd(2, "usage: mv FROM TO\n");
		return 1;
	}
	if (stat(argv[1], &st) < 0) {
		complain("mv", argv[1]);
		return 1;
	}
	if (!(st.mode & S_W)) {
		errno = EACCES;
		complain("mv", argv[1]);
		return 1;
	}
	if (target(argv[1], argv[2], to) < 0) {
		complain("mv", argv[2]);
		return 1;
	}
	if (link(argv[1], to) < 0) {
		if (errno != EXDEV) {
			complain("mv", errno == EEXIST ? to : argv[1]);
			return 1;
		}
		if (copy("mv", argv[1], to) < 0)
			return 1;
	}
	if (unlink(argv[1]) < 0) {
		complain("mv", argv[1]);
		return 1;
	}
	return 0;
}
