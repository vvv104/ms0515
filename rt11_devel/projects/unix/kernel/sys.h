/* sys.h - what the system calls' files share: sys.c's checks of a
 * process's arguments, the calls kept in files of their own - mount.c,
 * ioctl.c, own.c - and the parts of the system that are files (part.c). */
#ifndef UNIX_SYS_H
#define UNIX_SYS_H

#include "high.h"
#include "proc.h"

#define PATHMAX 64			/* a path a call is given, its NUL too */

/* A part of the system as a file, put down flat at base, room bytes at
 * most: 1 when its header holds - the magic, the version, its size the
 * file's - 0 when there is no such file, -1 when it is not right. */
int part_load(const char *path, unsigned base, unsigned room, unsigned magic, unsigned version);

/* sys.c */
int inside(unsigned p, unsigned n);
int user_string(unsigned p, unsigned max);
struct file *file_of(unsigned fd);

/* The calls of their own files, as sys.c's table takes them. */
int sys_mount(struct trapframe *tf, const unsigned *arg);
int sys_umount(struct trapframe *tf, const unsigned *arg);
int sys_ioctl(struct trapframe *tf, const unsigned *arg);

/* A program that owns the machine (own.c): back only with the error that
 * kept it from being run. */
int own_exec(struct inode *ip, unsigned argc, unsigned used);

/* fs.c and dev.c, for mount.c and ioctl.c. */
struct mount *mount_of(unsigned dev);
struct mount *covering(const struct inode *ip);
int read_super(unsigned dev, struct superblock *sb);
int fs_pivot(unsigned dev);
extern const unsigned char dev_place[];
extern int (*fd_format)(unsigned unit, unsigned track, const void *image, unsigned len);

#endif
