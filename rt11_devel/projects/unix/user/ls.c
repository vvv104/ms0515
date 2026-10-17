/* ls.c - /bin/ls [-l] [-a] [PATH...]: the names in each directory given,
 * the process's own by default, on a line; with -l a line a name - its
 * type (d, b or -) and its one rwx, its links, its size in bytes - and a
 * file named
 * is its own line; the names beginning with a dot shown with -a only. */
#include "unistd.h"
#include "../kernel/fs.h"

#define PATHMAX 64

static int longform, all;

/* n in decimal, right-aligned in width. */
static void put_number(unsigned long n, int width)
{
	char digits[11], *p = digits + sizeof digits;

	*--p = 0;
	do
		*--p = (char)('0' + n % 10);
	while ((n /= 10) != 0);
	while (width-- > digits + sizeof digits - 1 - p)
		puts_fd(1, " ");
	puts_fd(1, p);
}

/* The -l line of the file at path, shown as name. */
static int line(const char *path, const char *name)
{
	char mode[6];
	struct stat st;

	if (stat(path, &st) < 0) {
		complain("ls", path);
		return 1;
	}
	mode[0] = (st.mode & S_IFMT) == S_IFDIR ? 'd' : (st.mode & S_IFMT) == S_IFBLK ? 'b' : '-';
	mode[1] = st.mode & S_R ? 'r' : '-';
	mode[2] = st.mode & S_W ? 'w' : '-';
	mode[3] = st.mode & S_X ? 'x' : '-';
	mode[4] = ' ';
	mode[5] = 0;
	puts_fd(1, mode);
	put_number(st.nlink, 2);
	puts_fd(1, " ");
	put_number(st.size, 7);
	puts_fd(1, " ");
	puts_fd(1, name);
	puts_fd(1, "\n");
	return 0;
}

static int list(const char *dir)
{
	struct {
		unsigned ino;
		char name[14];
	} d;
	char name[15], path[PATHMAX];
	struct stat st;
	int fd, first = 1, n, i, bad = 0;

	if (stat(dir, &st) < 0) {
		complain("ls", dir);
		return 1;
	}
	if ((st.mode & S_IFMT) != S_IFDIR) {	/* a file named: itself */
		if (longform)
			return line(dir, dir);
		puts_fd(1, dir);
		puts_fd(1, "\n");
		return 0;
	}
	if ((fd = open(dir, 0)) < 0) {
		complain("ls", dir);
		return 1;
	}
	while (read(fd, &d, sizeof d) == sizeof d) {
		if (!d.ino || (d.name[0] == '.' && !all))
			continue;
		memcpy(name, d.name, 14);
		name[14] = 0;
		if (longform) {
			for (n = 0; dir[n] && n < PATHMAX - 16; n++)
				path[n] = dir[n];
			path[n++] = '/';
			for (i = 0; name[i]; i++)
				path[n++] = name[i];
			path[n] = 0;
			bad |= line(path, name);
			continue;
		}
		if (!first)
			puts_fd(1, "  ");
		puts_fd(1, name);
		first = 0;
	}
	if (!longform)
		puts_fd(1, "\n");
	close(fd);
	return bad;
}

int main(int argc, char **argv)
{
	int i, bad = 0, named = 0;
	const char *s;

	for (i = 1; i < argc && argv[i][0] == '-' && argv[i][1]; i++)
		for (s = argv[i] + 1; *s; s++) {
			if (*s == 'l')
				longform = 1;
			else if (*s == 'a')
				all = 1;
			else {
				puts_fd(2, "usage: ls [-l] [-a] [PATH...]\n");
				return 1;
			}
		}
	for (; i < argc; i++, named++)
		bad |= list(argv[i]);
	return named ? bad : list(".");
}
