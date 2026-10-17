/* mtab.h - /etc/mtab, what is mounted where, kept by ramroot, mount and
 * umount as V7's mount kept it (mtab.c): a line a volume, its device - or
 * DEVICE:PATH for a directory of it - and where it is in the tree. */
#ifndef UNIX_MTAB_H
#define UNIX_MTAB_H

/* The table begun anew with the root's line: 0, or -1. */
int mtab_new(const char *what);

/* A line added: 0, or -1. */
int mtab_add(const char *what, const char *where);

/* The lines of the device's volume, a directory of it among them, taken
 * out. */
void mtab_drop(const char *device);

/* What a directory in the tree is as a source: DEVICE:PATH by the line
 * mounted on the longest part of it, into what; 0, or -1 for none. */
int mtab_what(const char *dir, char *what, int size);

/* The table told in two columns. */
void mtab_show(void);

#endif
