/* unistd.h - the system calls a process has (sys.s), and the little
 * library about them (lib.c). */
#ifndef UNIX_UNISTD_H
#define UNIX_UNISTD_H

extern int errno;
#define ENOENT 2
#define EAGAIN 11
#define ENOTDIR 20

void exit(int status);
int fork(void);
int read(int fd, void *buf, int n);
int write(int fd, const void *buf, int n);
int open(const char *path, int mode);	/* mode 0: to read */
int close(int fd);
int wait(int *status);
int execv(const char *path, char *const argv[]);
long time(void);			/* the seconds since the boot */
int getpid(void);
int getppid(void);
int dup(int fd);
int pipe(int fd[2]);

/* lib.c */
int strlen(const char *s);
int strcmp(const char *a, const char *b);
void puts_fd(int fd, const char *s);	/* the string as it is, no newline added */
void putnum(unsigned n);		/* in decimal, to 1 */
void complain(const char *who, const char *what);	/* "who: what: why" to 2 */
void *memcpy(void *to, const void *from, unsigned n);
void *memset(void *p, int c, unsigned n);

#endif
