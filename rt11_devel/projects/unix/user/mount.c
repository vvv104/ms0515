/* mount.c - /bin/mount DEVICE DIR: the volume on the device into the tree at
 * the directory.  mount -a: the volumes of /etc/fstab.
 *
 * /etc/fstab is a line a volume, its words cut at spaces and tabs, '#' to
 * the line's end a comment:
 *
 *	DEVICE	DIR	OPTIONS		rd0 is /dev/rd0; "/" is the root, the
 *					kernel's own, and passed by
 *
 * the options cut at commas: "mkfs" makes the volume anew first,
 * "optional" lets it fail without a word. */
#include "unistd.h"
#include "vol.h"

#define MAXLINES 8
#define MAXFSTAB 1024			/* /etc/fstab's bytes at most */

struct entry {
	char *what, *where, *options;
	char done;			/* mounted, for a volume */
};

static char text[MAXFSTAB + 1];
static struct entry entries[MAXLINES];

/* Whether the option `name` is among the entry's. */
static int option(const struct entry *e, const char *name)
{
	const char *s = e->options, *n;

	while (s && *s) {
		for (n = name; *n && *s == *n; n++, s++)
			;
		if (!*n && (*s == ',' || !*s))
			return 1;
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
	n = read(fd, text, sizeof text);	/* a byte more than fits: too long */
	close(fd);
	if (n < 0 || n > MAXFSTAB)
		return -1;
	text[n] = 0;
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

static int all(void)
{
	int lines = read_fstab(), i, bad = 0;

	if (lines < 0) {
		complain("mount", "/etc/fstab");
		return 1;
	}
	for (i = 0; i < lines; i++) {
		if (!strcmp(entries[i].where, "/"))
			continue;
		if (volume(&entries[i]) < 0 && !option(&entries[i], "optional")) {
			complain("mount", entries[i].what);
			bad = 1;
		}
	}
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
