/* stat.h - what stat(path, &st) and fstat(fd, &st) give, for the kernel
 * and the programs alike: a file's device and inode - the two tell one
 * file from another - its type and permissions (fs.h S_IF...), its links,
 * its size and its time. */
#ifndef UNIX_STAT_H
#define UNIX_STAT_H

struct stat {
	unsigned dev, ino;
	unsigned mode, nlink;
	unsigned long size, mtime;
};

#endif
