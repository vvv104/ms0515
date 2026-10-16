/* mount.c - /bin/mount DEVICE DIR: the volume on the device into the tree at
 * the directory.  mount -a: the volumes and the swap of /etc/fstab.
 *
 * /etc/fstab is a line a volume or a swap file, its words cut at spaces and
 * tabs, '#' to the line's end a comment:
 *
 *	DEVICE	DIR	OPTIONS		a volume: rd0 is /dev/rd0; "/" is the root,
 *					the kernel's own, and passed by
 *	FILE	swap	OPTIONS		a swap file
 *
 * the options cut at commas: "mkfs" makes the volume anew first, "size=KB"
 * makes the swap file when it is not there, "optional" lets it fail without
 * a word.  The volumes are mounted first; then the swap lines are tried in
 * their order until one is had, a file under a volume that did not mount
 * passed by.  All in this one process: until swap is on there are two at
 * most, and this is the second. */
#include "unistd.h"
#include "vol.h"

#define MAXLINES 8
#define MAXFSTAB 512

struct entry {
	char *what, *where, *options;
	char done;			/* mounted, for a volume */
};

static char text[MAXFSTAB];
static struct entry entries[MAXLINES];

/* The option `name` among the entry's: its value after '=' (or "" without
 * one), 0 when it is not there. */
static const char *option(const struct entry *e, const char *name)
{
	const char *s = e->options, *n;

	while (s && *s) {
		for (n = name; *n && *s == *n; n++, s++)
			;
		if (!*n && (*s == ',' || *s == '=' || !*s))
			return *s == '=' ? s + 1 : s;
		while (*s && *s++ != ',')
			;
	}
	return 0;
}

/* /etc/fstab's entries, cut in place: how many. */
static int read_fstab(void)
{
	char *s, *word[3];
	int fd = open("/etc/fstab", 0), n, lines = 0, w;

	if (fd < 0)
		return -1;
	n = read(fd, text, sizeof text - 1);
	close(fd);
	text[n > 0 ? n : 0] = 0;
	for (s = text; *s && lines < MAXLINES;) {
		for (w = 0; *s && *s != '\n';) {
			while (*s == ' ' || *s == '\t')
				*s++ = 0;
			if (*s == '#')
				while (*s && *s != '\n')
					*s++ = 0;
			if (!*s || *s == '\n')
				break;
			if (w < 3)
				word[w++] = s;
			while (*s && *s != ' ' && *s != '\t' && *s != '\n')
				s++;
		}
		if (*s == '\n')
			*s++ = 0;
		if (w >= 2) {
			entries[lines].what = word[0];
			entries[lines].where = word[1];
			entries[lines].options = w > 2 ? word[2] : "";
			entries[lines].done = 0;
			lines++;
		}
	}
	return lines;
}

/* The device's path: rd0 is /dev/rd0. */
static const char *device(const char *what, char *path)
{
	int i = 0;

	if (*what == '/')
		return what;
	memcpy(path, "/dev/", 5);
	for (i = 5; *what && i < 30; )
		path[i++] = *what++;
	path[i] = 0;
	return path;
}

static void tell(const char *a, const char *b, const char *c)
{
	puts_fd(1, a);
	puts_fd(1, b);
	puts_fd(1, c);
}

/* The entry's volume made, when asked, and mounted; 0, or -1. */
static int volume(struct entry *e)
{
	char path[32];
	const char *dev = device(e->what, path);

	if (option(e, "mkfs") && mkfs_volume(dev, 1024) < 0)
		return -1;
	if (mount(dev, e->where) < 0)
		return -1;
	e->done = 1;
	tell("mount: ", dev, " on ");
	tell(e->where, "\n", "");
	return 0;
}

/* Whether the swap file at path lies under a volume of the table that did
 * not mount: the longest of their directories that begins its path. */
static int under_missing(const char *path, int lines)
{
	int i, best = -1, len, bestlen = 0;

	for (i = 0; i < lines; i++) {
		if (!strcmp(entries[i].where, "swap") || !strcmp(entries[i].where, "/"))
			continue;
		len = strlen(entries[i].where);
		if (len > bestlen && !memcmp(entries[i].where, path, (unsigned)len) && path[len] == '/') {
			best = i;
			bestlen = len;
		}
	}
	return best >= 0 && !entries[best].done;
}

static int all(void)
{
	int lines = read_fstab(), i, bad = 0;
	const char *size;

	if (lines < 0) {
		complain("mount", "/etc/fstab");
		return 1;
	}
	for (i = 0; i < lines; i++) {
		if (!strcmp(entries[i].where, "swap") || !strcmp(entries[i].where, "/"))
			continue;
		if (volume(&entries[i]) < 0 && !option(&entries[i], "optional")) {
			complain("mount", entries[i].what);
			bad = 1;
		}
	}
	for (i = 0; i < lines; i++) {
		if (strcmp(entries[i].where, "swap") || under_missing(entries[i].what, lines))
			continue;
		size = option(&entries[i], "size");
		if (swap_file_on(entries[i].what, size ? number(size) : 0) == 0) {
			tell("swap: ", entries[i].what, "\n");
			return bad;
		}
		if (!option(&entries[i], "optional")) {
			complain("swap", entries[i].what);
			bad = 1;
		}
	}
	puts_fd(1, "swap: none - two processes at most\n");
	return bad;
}

int main(int argc, char **argv)
{
	if (argc == 2 && !strcmp(argv[1], "-a"))
		return all();
	if (argc != 3) {
		puts_fd(2, "usage: mount DEVICE DIR, or mount -a\n");
		return 1;
	}
	if (mount(argv[1], argv[2]) < 0) {
		complain("mount", argv[1]);
		return 1;
	}
	return 0;
}
