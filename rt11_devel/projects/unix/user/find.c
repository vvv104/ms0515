/* find.c - /bin/find PATH... [-name PATTERN] [-type d|f]: every path under
 * each one given, itself first, in the directories' order - those whose
 * last name fits the pattern (* any run, ? any character) and of the type
 * asked.  A path too long to make is told, not followed. */
#include "unistd.h"
#include "../kernel/fs.h"

#define DEPTH 16

static const char *pattern;
static int type;

/* Whether the name fits the pattern. */
static int fits(const char *p, const char *s)
{
	if (!*p)
		return !*s;
	if (*p == '*')
		return fits(p + 1, s) || (*s && fits(p, s + 1));
	return *s && (*p == '?' || *p == *s) && fits(p + 1, s + 1);
}

static void visit(const char *path, int depth);

/* The names of directory path, each visited. */
static void inside(const char *path, int depth)
{
	struct {
		unsigned ino;
		char name[DIRSIZ];
	} d;
	char child[PATHMAX];
	unsigned long off;
	int fd, got;

	for (off = 0;; off += sizeof d) {	/* one entry at a time: the file closed while below */
		if ((fd = open(path, 0)) < 0)
			return;
		got = lseek(fd, (long)off, 0) >= 0 && read(fd, &d, sizeof d) == sizeof d;
		close(fd);
		if (!got)
			return;
		if (!d.ino || (d.name[0] == '.' && (!d.name[1] || (d.name[1] == '.' && !d.name[2]))))
			continue;
		if (join(child, path, d.name, DIRSIZ) < 0)
			complain("find", path);
		else
			visit(child, depth + 1);
	}
}

static void visit(const char *path, int depth)
{
	struct stat st;
	const char *last = path, *s;
	int dir;

	if (stat(path, &st) < 0) {
		complain("find", path);
		return;
	}
	dir = (st.mode & S_IFMT) == S_IFDIR;
	for (s = path; *s; s++)
		if (*s == '/' && s[1])
			last = s + 1;
	if ((!pattern || fits(pattern, last)) && (!type || (type == 'd') == dir)) {
		puts_fd(1, path);
		puts_fd(1, "\n");
	}
	if (dir && depth < DEPTH)
		inside(path, depth);
}

int main(int argc, char **argv)
{
	int i, paths;

	for (paths = 1; paths < argc && argv[paths][0] != '-'; paths++)
		;
	for (i = paths; i < argc; i += 2) {
		if (i + 1 >= argc || (strcmp(argv[i], "-name") && strcmp(argv[i], "-type"))) {
			puts_fd(2, "usage: find PATH... [-name PATTERN] [-type d|f]\n");
			return 1;
		}
		if (argv[i][1] == 'n')
			pattern = argv[i + 1];
		else
			type = argv[i + 1][0];
	}
	if (paths == 1) {
		puts_fd(2, "usage: find PATH... [-name PATTERN] [-type d|f]\n");
		return 1;
	}
	for (i = 1; i < paths; i++)
		visit(argv[i], 0);
	return 0;
}
