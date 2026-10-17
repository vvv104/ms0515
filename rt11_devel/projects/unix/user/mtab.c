/* mtab.c - /etc/mtab (mtab.h): the file read whole, 1 KB at most, and
 * written whole again. */
#include "unistd.h"
#include "mtab.h"

#define MTAB "/etc/mtab"
#define ROOM 1024
#define COLUMN 19

static char text[ROOM + 1];

static int load(void)
{
	int fd = open(MTAB, 0), n = 0, got;

	if (fd >= 0) {
		while (n < ROOM && (got = read(fd, text + n, ROOM - n)) > 0)
			n += got;
		close(fd);
	}
	text[n] = 0;
	return n;
}

static int store(int n)
{
	int fd = creat(MTAB), bad;

	if (fd < 0)
		return -1;
	bad = write(fd, text, n) != n;
	close(fd);
	return bad ? -1 : 0;
}

/* "what where\n" put at text + n: the new length, -1 with no room. */
static int line(int n, const char *what, const char *where)
{
	int a = strlen(what), b = strlen(where);

	if (n + a + b + 2 > ROOM)
		return -1;
	memcpy(text + n, what, a);
	text[n + a] = ' ';
	memcpy(text + n + a + 1, where, b);
	text[n + a + b + 1] = '\n';
	return n + a + b + 2;
}

int mtab_new(const char *what)
{
	return store(line(0, what, "/"));
}

int mtab_add(const char *what, const char *where)
{
	int n = line(load(), what, where);

	return n < 0 ? -1 : store(n);
}

void mtab_drop(const char *device)
{
	int n = load(), from, to = 0, len = strlen(device);

	for (from = 0; from < n;) {
		int end = from;

		while (end < n && text[end] != '\n')
			end++;
		end += end < n;
		if (memcmp(text + from, device, len) || (text[from + len] != ' ' && text[from + len] != ':')) {
			memcpy(text + to, text + from, end - from);
			to += end - from;
		}
		from = end;
	}
	store(to);
}

int mtab_what(const char *dir, char *what, int size)
{
	const char *rest;
	int n = load(), from = 0, best = -1, bestw = 0, bestlen = -1, w, where, end, len, i, colon = 0;

	while (from < n) {
		for (w = from; w < n && text[w] != ' '; w++)
			;
		for (where = end = w + 1; end < n && text[end] != '\n'; end++)
			;
		len = end - where;
		if (len > bestlen && !memcmp(text + where, dir, len) && (dir[len] == '/' || !dir[len] || len == 1)) {
			best = from;
			bestw = w;
			bestlen = len;
		}
		from = end + 1;
	}
	if (best < 0)
		return -1;
	for (i = 0; best < bestw && i < size - 1; best++) {
		colon |= text[best] == ':';
		what[i++] = text[best];
	}
	rest = dir + (bestlen == 1 ? 0 : bestlen);	/* below the root, the whole path */
	if (*rest && !colon && i < size - 1)
		what[i++] = ':';
	for (; *rest && i < size - 1; rest++)
		what[i++] = *rest;
	what[i] = 0;
	return 0;
}

void mtab_show(void)
{
	int n = load(), from = 0, w, end, col;

	while (from < n) {
		for (w = from; w < n && text[w] != ' '; w++)
			;
		for (end = w; end < n && text[end] != '\n'; end++)
			;
		puts_fd(1, "  ");
		write(1, text + from, w - from);
		for (col = 2 + w - from; col < COLUMN; col++)
			puts_fd(1, " ");
		if (w < end)
			write(1, text + w, end - w);	/* the space, then where */
		puts_fd(1, "\n");
		from = end + 1;
	}
}
