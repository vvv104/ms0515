/* pwd.c - /bin/pwd: the process's own directory, its path from the root.
 *
 * Up by .. a level at a time: the directory is the entry of its parent
 * whose device and inode are its own - on one volume the entry's inode
 * number says; where a volume is mounted the parent's entry is the
 * directory it covers, so each name there is asked by stat, which goes
 * into the volume as a path does.  The root is where .. is the same
 * directory. */
#include "unistd.h"

#define PATHMAX 64
#define DEPTH 16
#define DIRSIZ 14

struct entry {
	unsigned ino;
	char name[DIRSIZ];
};

static char names[DEPTH][DIRSIZ + 1];

/* The entry of directory `up` (at path, its stat in *ups) that is *me:
 * its name into name; 0, or -1 when there is none. */
static int find(const char *up, const struct stat *ups, const struct stat *me, char *name)
{
	char path[PATHMAX];
	struct entry e;
	struct stat st;
	int fd = open(up, 0), n, i, found = -1;

	if (fd < 0)
		return -1;
	while (found < 0 && read(fd, &e, sizeof e) == sizeof e) {
		if (!e.ino || (e.name[0] == '.' && (!e.name[1] || (e.name[1] == '.' && !e.name[2]))))
			continue;
		if (ups->dev == me->dev) {
			if (e.ino != me->ino)
				continue;
		} else {
			for (n = 0; up[n]; n++)
				path[n] = up[n];
			path[n++] = '/';
			for (i = 0; i < DIRSIZ && e.name[i] && n < PATHMAX - 1; i++)
				path[n++] = e.name[i];
			path[n] = 0;
			if (stat(path, &st) < 0 || st.dev != me->dev || st.ino != me->ino)
				continue;
		}
		for (i = 0; i < DIRSIZ && e.name[i]; i++)
			name[i] = e.name[i];
		name[i] = 0;
		found = 0;
	}
	close(fd);
	return found;
}

int main(void)
{
	char up[PATHMAX];
	struct stat me, ups;
	int depth = 0, n = 2;

	up[0] = up[1] = '.';
	up[2] = 0;
	if (stat(".", &me) < 0) {
		complain("pwd", ".");
		return 1;
	}
	for (;;) {
		if (stat(up, &ups) < 0) {
			complain("pwd", up);
			return 1;
		}
		if (ups.dev == me.dev && ups.ino == me.ino)
			break;				/* the root */
		if (depth == DEPTH || n + 3 >= PATHMAX || find(up, &ups, &me, names[depth]) < 0) {
			puts_fd(2, "pwd: lost on the way up\n");
			return 1;
		}
		depth++;
		me = ups;
		up[n++] = '/';				/* one more ../ */
		up[n++] = '.';
		up[n++] = '.';
		up[n] = 0;
	}
	if (!depth)
		puts_fd(1, "/");
	while (depth--) {
		puts_fd(1, "/");
		puts_fd(1, names[depth]);
	}
	puts_fd(1, "\n");
	return 0;
}
