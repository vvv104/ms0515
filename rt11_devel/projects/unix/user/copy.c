/* copy.c - what cp and mv share (copy.h): the name a file goes to, and a
 * file copied a few blocks at a time. */
#include "unistd.h"
#include "copy.h"
#include "../kernel/fs.h"

#define CHUNK 2048

static char buf[CHUNK];

int target(const char *from, const char *to, char *path)
{
	struct stat st;
	const char *last = from, *s;
	int n = 0;

	if (stat(to, &st) < 0 || (st.mode & S_IFMT) != S_IFDIR)
		return join(path, to, "", 0);
	for (s = from; *s; s++)
		if (*s == '/' && s[1])
			last = s + 1;
	while (last[n] && last[n] != '/')
		n++;
	return join(path, to, last, n);
}

int copy(const char *who, const char *from, const char *to)
{
	int in = open(from, 0), out, n, bad = 0;

	if (in < 0) {
		complain(who, from);
		return -1;
	}
	if ((out = creat(to)) < 0) {
		complain(who, to);
		close(in);
		return -1;
	}
	while (!bad && (n = read(in, buf, CHUNK)) != 0) {
		if (n < 0) {
			complain(who, from);
			bad = 1;
		} else if (write(out, buf, n) != n) {
			complain(who, to);
			bad = 1;
		}
	}
	close(in);
	close(out);
	return bad ? -1 : 0;
}
