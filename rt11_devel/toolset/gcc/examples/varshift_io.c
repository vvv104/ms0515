/* varshift_io.c - VARSHIFT's calls out, in a file of their own as ls's
 * stat and puts_fd are: what they do to the registers unknown to the
 * caller's compilation, which is what the fault needed. */

#include <stdio.h>

struct status {
	unsigned dev, ino, mode, nlink;
	unsigned long size, mtime;
};

int fill(const char *name, struct status *st);
void out(int fd, const char *s);

int fill(const char *name, struct status *st)
{
	st->dev = 4;
	st->ino = (unsigned)name[0];
	st->mode = 0100644;
	st->nlink = 1;
	st->size = 1234;
	st->mtime = 0;
	return 0;
}

void out(int fd, const char *s)
{
	if (fd == 1)
		printf("%s", s);
}
