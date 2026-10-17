/* mkdir.c - /bin/mkdir: each directory given made, with its "." and "..". */
#include "unistd.h"

int main(int argc, char **argv)
{
	int i, bad = 0;

	for (i = 1; i < argc; i++)
		if (mkdir(argv[i]) < 0) {
			complain("mkdir", argv[i]);
			bad = 1;
		}
	return bad;
}
