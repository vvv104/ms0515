/* unistd.h - the system calls a process has (sys.s). */
#ifndef UNIX_UNISTD_H
#define UNIX_UNISTD_H

extern int errno;

void exit(int status);
int read(int fd, void *buf, int n);
int write(int fd, const void *buf, int n);
long time(void);		/* the seconds since the boot */

#endif
