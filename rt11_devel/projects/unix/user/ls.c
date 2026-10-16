/* ls.c - /bin/ls: the names in each directory given, the process's own by
 * default, on a line; the entries "." and ".." left out. */
#include "unistd.h"

static int list(const char *dir)
{
	struct {
		unsigned ino;
		char name[14];
	} d;
	char name[15];
	int fd = open(dir, 0), first = 1;

	if (fd < 0) {
		complain("ls", dir);
		return 1;
	}
	while (read(fd, &d, sizeof d) == sizeof d) {
		if (!d.ino || d.name[0] == '.')
			continue;
		memcpy(name, d.name, 14);
		name[14] = 0;
		if (!first)
			puts_fd(1, "  ");
		puts_fd(1, name);
		first = 0;
	}
	puts_fd(1, "\n");
	close(fd);
	return 0;
}

int main(int argc, char **argv)
{
	int i, bad = 0;

	if (argc < 2)
		return list(".");
	for (i = 1; i < argc; i++)
		bad |= list(argv[i]);
	return bad;
}
