/* copy.h - what cp and mv share (copy.c). */
#ifndef UNIX_COPY_H
#define UNIX_COPY_H

#define PATHMAX 64

/* to, or to/the last name of from when to is a directory, into path. */
void target(const char *from, const char *to, char *path);

/* The file at from written anew at to; 0, or -1 with the trouble told on
 * the error output as who's. */
int copy(const char *who, const char *from, const char *to);

#endif
