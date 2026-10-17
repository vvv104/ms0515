/* clone.c - a volume read and copied by its blocks (clone.h): the system
 * into the RAM disk (ramroot), onto a diskette that boots (mkboot), a
 * directory of a volume found for mount. */
#include "unistd.h"
#include "clone.h"
#include "../kernel/fs.h"

#define ENTRIES (BSIZE / sizeof(struct direct))	/* a block's: 32 */
#define DEPTH 16

static const char *const devices[] = {
	"/dev/fd0", "/dev/fd1", "/dev/fd2", "/dev/fd3", "/dev/fdx0", "/dev/fdx1", "/dev/rd0"
};

/* The user's: their files are not the system's. */
static const char *const users[] = {"home", "tmp"};

static char block[BSIZE], map[BSIZE];
static unsigned itable;			/* the volume's inode table, a block */

const char *device_path(unsigned dev)
{
	return dev < sizeof devices / sizeof *devices ? devices[dev] : 0;
}

/* n bytes at off on the device at fd into p, or out of it: 0, or -1. */
static int io(int fd, long off, void *p, int n, int writing)
{
	if (lseek(fd, off, 0) < 0)
		return -1;
	return (writing ? write(fd, p, n) : read(fd, p, n)) == n ? 0 : -1;
}

static int at(int fd, unsigned b, int writing)
{
	return io(fd, (long)b * BSIZE, block, BSIZE, writing);
}

static long inode_at(unsigned n)
{
	return (long)itable * BSIZE + (long)(n - 1) * sizeof(struct dinode);
}

/* Where entry i of directory d is on the device; -1 past its end. */
static long entry_at(const struct dinode *d, unsigned i)
{
	unsigned b = i / ENTRIES, e;

	if ((unsigned long)i * sizeof(struct direct) >= d->size)
		return -1;
	for (e = 0; e < d->nextents && e < NEXTENT; e++) {
		if (b < d->ext[e].len)
			return (long)(d->ext[e].start + b) * BSIZE + (long)(i % ENTRIES) * sizeof(struct direct);
		b -= d->ext[e].len;
	}
	return -1;
}

static int dots(const struct direct *e)
{
	return e->name[0] == '.' && (!e->name[1] || (e->name[1] == '.' && !e->name[2]));
}

/* The entry called name (len bytes) in directory n: its inode, 0 for none. */
static unsigned lookup(int fd, unsigned n, const char *name, int len)
{
	struct dinode d;
	struct direct e;
	unsigned i;
	long off;

	if (len > DIRSIZ || io(fd, inode_at(n), &d, sizeof d, 0) < 0 || (d.mode & S_IFMT) != S_IFDIR)
		return 0;
	for (i = 0; (off = entry_at(&d, i)) >= 0; i++)
		if (io(fd, off, &e, sizeof e, 0) == 0 && e.ino && !memcmp(e.name, name, len) &&
		    (len == DIRSIZ || !e.name[len]))
			return e.ino;
	return 0;
}

/* The volume's superblock read: its inode table noted; 0, or -1. */
static int super(int fd, struct superblock *sb)
{
	if (io(fd, BSIZE, sb, sizeof *sb, 0) < 0 || sb->magic != FS_MAGIC || sb->blocks > 8 * BSIZE)
		return -1;
	itable = sb->itable;
	return 0;
}

unsigned volume_inode(int fd, const char *path)
{
	struct superblock sb;
	unsigned n = ROOT_INO;
	int len;

	if (super(fd, &sb) < 0)
		return 0;
	while (n && *path) {
		while (*path == '/')
			path++;
		for (len = 0; path[len] && path[len] != '/'; len++)
			;
		if (len)
			n = lookup(fd, n, path, len);
		path += len;
	}
	return n;
}

unsigned device_blocks(int fd)
{
	static const unsigned sizes[] = {1600, 1024, 800};
	unsigned i;

	for (i = 0; i < sizeof sizes / sizeof *sizes; i++)
		if (at(fd, sizes[i] - 1, 0) == 0)
			return sizes[i];
	return 0;
}

/* Everything under directory n let go in the copy: each file's blocks
 * taken off the map, its inode cleared on to - one with a name elsewhere
 * too only its links counted down.  0, or -1. */
static int let_go(int from, int to, unsigned n, int depth)
{
	struct dinode d, c;
	struct direct e;
	unsigned i, k, b;
	long off;

	if (depth > DEPTH || io(from, inode_at(n), &d, sizeof d, 0) < 0)
		return -1;
	for (i = 0; (off = entry_at(&d, i)) >= 0; i++) {
		if (io(from, off, &e, sizeof e, 0) < 0)
			return -1;
		if (!e.ino || dots(&e) || io(from, inode_at(e.ino), &c, sizeof c, 0) < 0)
			continue;
		if ((c.mode & S_IFMT) == S_IFDIR && let_go(from, to, e.ino, depth + 1) < 0)
			return -1;
		if ((c.mode & S_IFMT) != S_IFDIR && c.nlink > 1) {
			c.nlink--;
		} else {
			if ((c.mode & S_IFMT) != S_IFBLK)	/* a device's extent is its number */
				for (k = 0; k < c.nextents && k < NEXTENT; k++)
					for (b = c.ext[k].start; b < c.ext[k].start + c.ext[k].len; b++)
						map[b >> 3] &= (char)~(1 << (b & 7));
			memset(&c, 0, sizeof c);
		}
		if (io(to, inode_at(e.ino), &c, sizeof c, 1) < 0)
			return -1;
	}
	return 0;
}

/* Directory n left with "." and ".." alone on to, its links two. */
static int emptied(int to, unsigned n)
{
	struct dinode d;
	struct direct e;
	unsigned i;
	long off;

	if (io(to, inode_at(n), &d, sizeof d, 0) < 0)
		return -1;
	for (i = 0; (off = entry_at(&d, i)) >= 0; i++) {
		if (io(to, off, &e, sizeof e, 0) < 0)
			return -1;
		if (!e.ino || dots(&e))
			continue;
		memset(&e, 0, sizeof e);
		if (io(to, off, &e, sizeof e, 1) < 0)
			return -1;
	}
	d.nlink = 2;
	return io(to, inode_at(n), &d, sizeof d, 1);
}

/* The blocks from..to of the map's copied: 0, or -1 with errno. */
static int blocks_copied(int from, int to, unsigned first, unsigned end, unsigned blocks, int all)
{
	unsigned b;

	for (b = first; b < end; b++) {
		if (!all && !(map[b >> 3] & (1 << (b & 7))))
			continue;
		if (b >= blocks) {
			errno = ENOSPC;
			return -1;
		}
		if (at(from, b, 0) < 0 || at(to, b, 1) < 0)
			return -1;
	}
	return 0;
}

int clone_volume(int from, int to, unsigned blocks, const char *label, int system)
{
	struct superblock sb;
	unsigned i, top[sizeof users / sizeof *users] = {0, 0};

	errno = EINVAL;
	if (super(from, &sb) < 0 || at(from, sb.bitmap, 0) < 0)
		return -1;
	memcpy(map, block, BSIZE);
	/* The blocks below the data first - the inodes among them - then the
	 * user's let go, then the data the map has left. */
	if (blocks_copied(from, to, 0, sb.data, blocks, 1) < 0)
		return -1;
	for (i = 0; system && i < sizeof users / sizeof *users; i++)
		if ((top[i] = lookup(from, ROOT_INO, users[i], strlen(users[i]))) && let_go(from, to, top[i], 0) < 0)
			return -1;
	if (blocks_copied(from, to, sb.data, sb.blocks, blocks, 0) < 0)
		return -1;
	for (i = 0; system && i < sizeof users / sizeof *users; i++)
		if (top[i] && emptied(to, top[i]) < 0)
			return -1;
	memcpy(block, map, BSIZE);
	if (at(to, sb.bitmap, 1) < 0)
		return -1;
	sb.blocks = blocks;
	sb.volume = (unsigned long)time() * 40503u + (unsigned)getpid();
	memset(sb.label, 0, sizeof sb.label);
	memcpy(sb.label, label, strlen(label));
	return io(to, BSIZE, &sb, sizeof sb, 1);
}
