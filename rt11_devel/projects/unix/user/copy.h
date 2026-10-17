/* copy.h - what cp and mv share (copy.c). */
#ifndef UNIX_COPY_H
#define UNIX_COPY_H

/* to, or to/the last name of from when to is a directory, into path: 0,
 * or -1 with errno E2BIG when it does not fit. */
int target(const char *from, const char *to, char *path);

/* The file at from written anew at to; 0, or -1 with the trouble told on
 * the error output as who's. */
int copy(const char *who, const char *from, const char *to);

#endif
