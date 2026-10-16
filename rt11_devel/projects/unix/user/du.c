/* du.c - /bin/du [-s] [PATH...]: the blocks under each directory - its
 * own, its files' and the directories' under it - a line "blocks path"
 * for each, or with -s for each path given alone; the process's own
 * directory by default.  A file's blocks are its size's, whole; a device
 * holds none.  The directory is opened anew for each entry, so that no
 * open file is held down the tree. */
#include "unistd.h"
#include "../kernel/fs.h"

#define PATHMAX 64

static int summary;

static void put_number(unsigned long n)
{
	char digits[11], *p = digits + sizeof digits;

	*--p = 0;
	do
		*--p = (char)('0' + n % 10);
	while ((n /= 10) != 0);
	puts_fd(1, p);
}

static void say(unsigned long blocks, const char *path)
{
	put_number(blocks);
	puts_fd(1, " ");
	puts_fd(1, path);
	puts_fd(1, "\n");
}

/* The blocks under path; top for a path given. */
static unsigned long du(const char *path, int top)
{
	struct {
		unsigned ino;
		char name[14];
	} d;
	char child[PATHMAX];
	struct stat st;
	unsigned long blocks, off;
	int fd, n, i, got;

	if (stat(path, &st) < 0) {
		complain("du", path);
		return 0;
	}
	if ((st.mode & S_IFMT) != S_IFDIR && (st.mode & S_IFMT) != S_IFREG)
		return 0;
	blocks = (st.size + 511) / 512;
	if ((st.mode & S_IFMT) == S_IFDIR) {
		for (off = 0;; off += sizeof d) {
			if ((fd = open(path, 0)) < 0)
				break;
			got = lseek(fd, (long)off, 0) >= 0 && read(fd, &d, sizeof d) == sizeof d;
			close(fd);
			if (!got)
				break;
			if (!d.ino || (d.name[0] == '.' && (!d.name[1] || (d.name[1] == '.' && !d.name[2]))))
				continue;
			for (n = 0; path[n] && n < PATHMAX - 16; n++)
				child[n] = path[n];
			if (n && child[n - 1] != '/')
				child[n++] = '/';
			for (i = 0; i < 14 && d.name[i]; i++)
				child[n++] = d.name[i];
			child[n] = 0;
			blocks += du(child, 0);
		}
		if (!summary || top)
			say(blocks, path);
	} else if (top) {
		say(blocks, path);
	}
	return blocks;
}

int main(int argc, char **argv)
{
	int i = 1;

	if (i < argc && !strcmp(argv[i], "-s")) {
		summary = 1;
		i++;
	}
	if (i == argc)
		du(".", 1);
	for (; i < argc; i++)
		du(argv[i], 1);
	return 0;
}
