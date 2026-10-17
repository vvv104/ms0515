/* chmod.c - /bin/chmod RIGHTS FILE...: each file's one rwx (kernel/fs.h)
 * set, RIGHTS a digit 0..7 (r 4, w 2, x 1) or three letters as ls -l
 * shows them, r-x. */
#include "unistd.h"

/* RIGHTS as a number 0..7, or -1 when it is neither form. */
static int rights(const char *s)
{
	static const char letters[] = "rwx";
	int r = 0, i;

	if (s[0] >= '0' && s[0] <= '7' && !s[1])
		return s[0] - '0';
	for (i = 0; i < 3; i++) {
		if (s[i] == letters[i])
			r |= 4 >> i;
		else if (s[i] != '-')
			return -1;
	}
	return s[3] ? -1 : r;
}

int main(int argc, char **argv)
{
	int r, i, status = 0;

	if (argc < 3 || (r = rights(argv[1])) < 0) {
		puts_fd(2, "chmod: RIGHTS FILE...: RIGHTS 0..7 or rwx, r-x, rw-...\n");
		return 1;
	}
	for (i = 2; i < argc; i++)
		if (chmod(argv[i], r) < 0) {
			complain("chmod", argv[i]);
			status = 1;
		}
	return status;
}
