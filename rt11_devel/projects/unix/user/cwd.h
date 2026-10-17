/* cwd.h - the process's directory as a path (cwd.c). */
#ifndef UNIX_CWD_H
#define UNIX_CWD_H

/* Its path from the root into buf, size bytes at most with the NUL: 0, or
 * -1 when it is lost on the way up or longer. */
int getcwd(char *buf, int size);

#endif
