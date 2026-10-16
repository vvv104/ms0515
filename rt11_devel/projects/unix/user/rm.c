/* rm.c - /bin/rm: each file given, its name taken away; the file goes when
 * no name and no open descriptor is left. */
#include "unistd.h"

int main(int argc, char **argv)
{
	int i, bad = 0;

	for (i = 1; i < argc; i++)
		if (unlink(argv[i]) < 0) {
			complain("rm", argv[i]);
			bad = 1;
		}
	return bad;
}
