/* unistd.h - the system calls a process has (sys.s), and the little
 * library about them (lib.c). */
#ifndef UNIX_UNISTD_H
#define UNIX_UNISTD_H

extern int errno;
#define ENOENT 2
#define ENOTDIR 20

void exit(int status);
int read(int fd, void *buf, int n);
int write(int fd, const void *buf, int n);
int open(const char *path, int mode);	/* mode 0: to read */
int close(int fd);
int execv(const char *path, char *const argv[]);
long time(void);			/* the seconds since the boot */

/* lib.c */
int strlen(const char *s);
int strcmp(const char *a, const char *b);
void puts_fd(int fd, const char *s);	/* the string as it is, no newline added */
void putnum(unsigned n);		/* in decimal, to 1 */
void *memcpy(void *to, const void *from, unsigned n);
void *memset(void *p, int c, unsigned n);

#endif
