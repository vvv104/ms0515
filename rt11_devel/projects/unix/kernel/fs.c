/* fs.c - the file system: the superblock, inodes, the blocks of a file by
 * its extents, paths. */
#include "kernel.h"

#define NINODE 8

static struct superblock sb;
static struct inode inodes[NINODE];

int fs_mount(void)
{
	struct buf *b = bread(1);

	if (!b)
		return EIO;
	memcpy(&sb, b->data, sizeof sb);
	if (sb.magic != FS_MAGIC || sb.version != FS_VERSION)
		return EINVAL;
	return 0;
}

/* Inode n, read in if it is not in hand; 0 when there is none. */
struct inode *iget(unsigned n)
{
	struct inode *ip, *free = 0;
	struct buf *b;
	int i;

	if (n == 0 || n > sb.inodes)
		return 0;
	for (i = 0; i < NINODE; i++) {
		ip = &inodes[i];
		if (ip->count && ip->ino == n) {
			ip->count++;
			return ip;
		}
		if (!ip->count && !free)
			free = ip;
	}
	if (!free)
		return 0;
	b = bread(sb.itable + (n - 1) / (BSIZE / sizeof(struct dinode)));
	if (!b)
		return 0;
	memcpy(&free->d, b->data + (n - 1) % (BSIZE / sizeof(struct dinode)) * sizeof(struct dinode),
	       sizeof(struct dinode));
	free->ino = n;
	free->count = 1;
	return free;
}

void iput(struct inode *ip)
{
	if (ip && ip->count)
		ip->count--;
}

/* The disk's block holding block lbn of the file, 0 past its extents. */
static unsigned bmap(const struct inode *ip, unsigned lbn)
{
	int i;

	for (i = 0; i < ip->d.nextents && i < NEXTENT; i++) {
		if (lbn < ip->d.ext[i].len)
			return ip->d.ext[i].start + lbn;
		lbn -= ip->d.ext[i].len;
	}
	return 0;
}

/* Up to n bytes of the file from off into buf; how many, or -EIO. */
int readi(const struct inode *ip, unsigned long off, char *buf, unsigned n)
{
	unsigned got = 0, in, take, pbn;
	struct buf *b;

	if (off >= ip->d.size)
		return 0;
	if (n > ip->d.size - off)
		n = (unsigned)(ip->d.size - off);
	while (got < n) {
		in = (unsigned)off & (BSIZE - 1);
		take = BSIZE - in;
		if (take > n - got)
			take = n - got;
		pbn = bmap(ip, (unsigned)(off >> 9));
		if (!pbn || !(b = bread(pbn)))
			return -EIO;
		memcpy(buf + got, b->data + in, take);
		got += take;
		off += take;
	}
	return (int)got;
}

/* The entry `name` (len bytes) of directory dp: its inode, 0 when none. */
static unsigned lookup(const struct inode *dp, const char *name, int len)
{
	struct direct d;
	unsigned long off;
	int i;

	for (off = 0; readi(dp, off, (char *)&d, sizeof d) == sizeof d; off += sizeof d) {
		if (!d.ino)
			continue;
		for (i = 0; i < len && d.name[i] == name[i]; i++)
			;
		if (i == len && (len == DIRSIZ || d.name[len] == 0))
			return d.ino;
	}
	return 0;
}

/* The inode of path, from the root; 0 and *error when there is none. */
struct inode *namei(const char *path, int *error)
{
	struct inode *ip = iget(ROOT_INO), *next;
	const char *name;
	unsigned n;
	int len;

	if (!ip) {
		*error = EIO;
		return 0;
	}
	for (;;) {
		while (*path == '/')
			path++;
		if (!*path)
			return ip;
		if (!ip || (ip->d.mode & S_IFMT) != S_IFDIR) {
			iput(ip);
			*error = ENOTDIR;
			return 0;
		}
		for (name = path, len = 0; *path && *path != '/'; path++)
			len++;
		if (len > DIRSIZ) {
			iput(ip);
			*error = ENOENT;
			return 0;
		}
		n = lookup(ip, name, len);
		next = n ? iget(n) : 0;
		iput(ip);
		if (!next) {
			*error = ENOENT;
			return 0;
		}
		ip = next;
	}
}
