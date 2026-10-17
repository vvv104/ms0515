/* unistd.h - the system calls a process has (sys.s), and the little
 * library about them (lib.c). */
#ifndef UNIX_UNISTD_H
#define UNIX_UNISTD_H

#include "../kernel/stat.h"

extern int errno;
#define PATHMAX 64			/* a path a call takes, its NUL too */
#define ENOENT 2
#define E2BIG 7				/* a path or the arguments too long */
#define EAGAIN 11
#define EEXIST 17
#define ENOTDIR 20
#define EISDIR 21
#define ENOTBLK 15
#define EBUSY 16
#define EINVAL 22
#define ENOSPC 28
#define ENODEV 19
#define EXDEV 18
#define EACCES 13
#define ENOTEMPTY 66

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
int creat(const char *path);
int unlink(const char *path);
int chmod(const char *path, int rights);	/* 0..7: r 4, w 2, x 1 */
int mkdir(const char *path);
int rmdir(const char *path);		/* an empty directory */
int sync(void);
int mount(const char *from, const char *dir);	/* from: a device, or a directory of a mounted volume */
int umount(const char *device);
int ioctl(int fd, int cmd, void *arg);
long lseek(int fd, long off, int whence);
int chdir(const char *dir);
int link(const char *old, const char *new_name);
int stat(const char *path, struct stat *st);
int fstat(int fd, struct stat *st);	/* whence 0 from the start, 1 from here, 2 from the end */

/* lib.c */
int strlen(const char *s);
int strcmp(const char *a, const char *b);
void puts_fd(int fd, const char *s);	/* the string as it is, no newline added */
void putnum(unsigned n);		/* in decimal, to 1 */
void complain(const char *who, const char *what);	/* "who: what: why" to 2 */
int join(char *path, const char *dir, const char *name, int n);	/* dir/name: 0, or -1 too long */
int memcmp(const void *a, const void *b, unsigned n);
void *memcpy(void *to, const void *from, unsigned n);
void *memset(void *p, int c, unsigned n);

#endif
