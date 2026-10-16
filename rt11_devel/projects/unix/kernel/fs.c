/* fs.c - the file system: the superblock, the bitmap, inodes, a file's
 * blocks by its extents, reading and writing, directories, paths.
 *
 * A file grows at its end alone: its next block is the one after its last
 * extent when that one is free, else the first free block starts a new
 * extent - five at most.  Each change goes to the disk at once, the bitmap,
 * the inode and the directory too (bio.c writes through).  One volume, the
 * root on the boot drive, until mount comes. */
#include "kernel.h"

#define NINODE 8
#define IPB (BSIZE / sizeof(struct dinode))	/* inodes a block: 16 */

static struct superblock sb;
static struct inode inodes[NINODE];
static unsigned rootdev = DEV_FD0;

int fs_mount(void)
{
	struct buf *b = bread(rootdev, 1);

	if (!b)
		return EIO;
	memcpy(&sb, b->data, sizeof sb);
	if (sb.magic != FS_MAGIC || sb.version != FS_VERSION)
		return EINVAL;
	return 0;
}

/* -- inodes ------------------------------------------------------------ */

/* Inode n of dev, read in if it is not in hand; 0 when there is none. */
struct inode *iget(unsigned dev, unsigned n)
{
	struct inode *ip, *free = 0;
	struct buf *b;
	int i;

	if (n == 0 || n > sb.inodes)
		return 0;
	for (i = 0; i < NINODE; i++) {
		ip = &inodes[i];
		if (ip->count && ip->dev == dev && ip->ino == n) {
			ip->count++;
			return ip;
		}
		if (!ip->count && !free)
			free = ip;
	}
	if (!free)
		return 0;
	b = bread(dev, sb.itable + (n - 1) / IPB);
	if (!b)
		return 0;
	memcpy(&free->d, b->data + (n - 1) % IPB * sizeof(struct dinode), sizeof(struct dinode));
	free->dev = dev;
	free->ino = n;
	free->count = 1;
	return free;
}

/* The inode written back to its table. */
int iupdate(struct inode *ip)
{
	struct buf *b = bread(ip->dev, sb.itable + (ip->ino - 1) / IPB);

	if (!b)
		return -EIO;
	memcpy(b->data + (ip->ino - 1) % IPB * sizeof(struct dinode), &ip->d, sizeof(struct dinode));
	return bwrite(b);
}

/* -- the bitmap -------------------------------------------------------- */

static int taken(unsigned dev, unsigned block)
{
	struct buf *b = bread(dev, sb.bitmap);

	return !b || (b->data[block >> 3] >> (block & 7) & 1);
}

static void mark(unsigned dev, unsigned block, int on)
{
	struct buf *b = bread(dev, sb.bitmap);

	if (!b)
		return;
	if (on)
		b->data[block >> 3] |= (char)(1 << (block & 7));
	else
		b->data[block >> 3] &= (char)~(1 << (block & 7));
	bwrite(b);
}

/* A free block taken: `want` when it is free, else the first; 0 when none. */
static unsigned balloc(unsigned dev, unsigned want)
{
	unsigned b;

	if (want >= sb.data && want < sb.blocks && !taken(dev, want)) {
		mark(dev, want, 1);
		return want;
	}
	for (b = sb.data; b < sb.blocks; b++)
		if (!taken(dev, b)) {
			mark(dev, b, 1);
			return b;
		}
	return 0;
}

/* The file's blocks given back, its length 0. */
void itrunc(struct inode *ip)
{
	unsigned i, b;

	for (i = 0; i < ip->d.nextents && i < NEXTENT; i++)
		for (b = 0; b < ip->d.ext[i].len; b++)
			mark(ip->dev, ip->d.ext[i].start + b, 0);
	memset(ip->d.ext, 0, sizeof ip->d.ext);
	ip->d.nextents = 0;
	ip->d.size = 0;
	iupdate(ip);
}

/* The inode let go; when no name and no one holds it, its blocks and
 * itself are freed. */
void iput(struct inode *ip)
{
	if (!ip || !ip->count)
		return;
	if (--ip->count == 0 && ip->d.nlink == 0 && ip->d.mode) {
		itrunc(ip);
		ip->d.mode = 0;
		iupdate(ip);
	}
}

/* A free inode of dev taken, cleared, of the mode given. */
static struct inode *ialloc(unsigned dev, unsigned mode)
{
	struct buf *b;
	struct dinode *d;
	struct inode *ip;
	unsigned n;

	for (n = 1; n <= sb.inodes; n++) {
		if (!(b = bread(dev, sb.itable + (n - 1) / IPB)))
			return 0;
		d = (struct dinode *)(b->data + (n - 1) % IPB * sizeof(struct dinode));
		if (d->mode)
			continue;
		if (!(ip = iget(dev, n)))
			return 0;
		memset(&ip->d, 0, sizeof ip->d);
		ip->d.mode = mode;
		ip->d.nlink = 1;
		iupdate(ip);
		return ip;
	}
	return 0;
}

/* -- a file's blocks --------------------------------------------------- */

static unsigned nblocks(const struct inode *ip)
{
	unsigned i, n = 0;

	for (i = 0; i < ip->d.nextents && i < NEXTENT; i++)
		n += ip->d.ext[i].len;
	return n;
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

/* One more block at the file's end: the last extent grown when the next
 * block is free, else a new extent; 0 when there is no room. */
static unsigned grow(struct inode *ip)
{
	struct extent *e = ip->d.nextents ? &ip->d.ext[ip->d.nextents - 1] : 0;
	unsigned b;

	if (e && (b = balloc(ip->dev, e->start + e->len)) != 0) {
		if (b == e->start + e->len) {
			e->len++;
			return b;
		}
		mark(ip->dev, b, 0);		/* elsewhere: a new extent's, if one is left */
	}
	if (ip->d.nextents == NEXTENT || !(b = balloc(ip->dev, 0)))
		return 0;
	e = &ip->d.ext[ip->d.nextents++];
	e->start = b;
	e->len = 1;
	return b;
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
		if (!pbn || !(b = bread(ip->dev, pbn)))
			return -EIO;
		memcpy(buf + got, b->data + in, take);
		got += take;
		off += take;
	}
	return (int)got;
}

/* n bytes of buf into the file at off, which is no further than its end;
 * how many, or the error negative. */
int writei(struct inode *ip, unsigned long off, const char *buf, unsigned n)
{
	unsigned put = 0, in, take, lbn, pbn;
	struct buf *b;

	if (off > ip->d.size)
		return -EINVAL;
	while (put < n) {
		in = (unsigned)off & (BSIZE - 1);
		take = BSIZE - in;
		if (take > n - put)
			take = n - put;
		lbn = (unsigned)(off >> 9);
		if (lbn < nblocks(ip)) {
			pbn = bmap(ip, lbn);
			b = in == 0 && take == BSIZE ? getblk(ip->dev, pbn) : bread(ip->dev, pbn);
		} else {
			if (!(pbn = grow(ip)))
				break;
			b = getblk(ip->dev, pbn);
		}
		if (!b)
			break;
		memcpy(b->data + in, buf + put, take);
		if (bwrite(b) != 0)
			break;
		put += take;
		off += take;
		if (off > ip->d.size)
			ip->d.size = off;
	}
	iupdate(ip);
	if (put == 0 && n > 0)
		return -ENOSPC;
	return (int)put;
}

/* -- directories ------------------------------------------------------- */

/* The entry `name` (len bytes) of directory dp: its inode and, at *where,
 * its offset; 0 when there is none. */
static unsigned lookup(const struct inode *dp, const char *name, int len, unsigned long *where)
{
	struct direct d;
	unsigned long off;
	int i;

	for (off = 0; readi(dp, off, (char *)&d, sizeof d) == sizeof d; off += sizeof d) {
		if (!d.ino)
			continue;
		for (i = 0; i < len && d.name[i] == name[i]; i++)
			;
		if (i == len && (len == DIRSIZ || d.name[len] == 0)) {
			if (where)
				*where = off;
			return d.ino;
		}
	}
	return 0;
}

/* An entry for inode n put in directory dp: in a free one, else at its end. */
static int dir_add(struct inode *dp, const char *name, int len, unsigned n)
{
	struct direct d;
	unsigned long off;

	for (off = 0; readi(dp, off, (char *)&d, sizeof d) == sizeof d; off += sizeof d)
		if (!d.ino)
			break;
	memset(&d, 0, sizeof d);
	d.ino = n;
	memcpy(d.name, name, (unsigned)len);
	return writei(dp, off, (const char *)&d, sizeof d) == sizeof d ? 0 : ENOSPC;
}

/* -- paths ------------------------------------------------------------- */

/* The path's walk: through each directory down to the last name, which is
 * left at *last (*len bytes, 0 for the root itself); the inode holding it -
 * a directory, if the path is right - is returned, held.  0 and *error when
 * a directory on the way is not there. */
static struct inode *walk(const char *path, const char **last, int *len, int *error)
{
	struct inode *ip = iget(rootdev, ROOT_INO), *next;
	const char *name;
	unsigned n;
	int l;

	if (!ip) {
		*error = EIO;
		return 0;
	}
	for (;;) {
		while (*path == '/')
			path++;
		for (name = path, l = 0; *path && *path != '/'; path++)
			l++;
		while (*path == '/')
			path++;
		if (!*path) {			/* the last name, or none for the root */
			*last = name;
			*len = l;
			return ip;
		}
		if ((ip->d.mode & S_IFMT) != S_IFDIR) {
			iput(ip);
			*error = ENOTDIR;
			return 0;
		}
		n = l <= DIRSIZ ? lookup(ip, name, l, 0) : 0;
		next = n ? iget(ip->dev, n) : 0;
		iput(ip);
		if (!next) {
			*error = ENOENT;
			return 0;
		}
		ip = next;
	}
}

/* The directory to hold path's last name, checked: held, the name and its
 * length at *last and *len; 0 and *error otherwise. */
static struct inode *parent(const char *path, const char **last, int *len, int *error)
{
	struct inode *dp = walk(path, last, len, error);

	if (!dp)
		return 0;
	if (*len == 0) {			/* the root itself */
		iput(dp);
		*error = EISDIR;
		return 0;
	}
	if ((dp->d.mode & S_IFMT) != S_IFDIR) {
		iput(dp);
		*error = ENOTDIR;
		return 0;
	}
	if (*len > DIRSIZ) {
		iput(dp);
		*error = ENOENT;
		return 0;
	}
	return dp;
}

/* The inode of path; 0 and *error when there is none. */
struct inode *namei(const char *path, int *error)
{
	const char *name;
	int len;
	struct inode *dp = walk(path, &name, &len, error), *ip;
	unsigned n;

	if (!dp || len == 0)
		return dp;			/* none, or the root */
	if ((dp->d.mode & S_IFMT) != S_IFDIR) {
		iput(dp);
		*error = ENOTDIR;
		return 0;
	}
	n = len <= DIRSIZ ? lookup(dp, name, len, 0) : 0;
	ip = n ? iget(dp->dev, n) : 0;
	iput(dp);
	if (!ip)
		*error = ENOENT;
	return ip;
}

/* The file at path to write from its start: made when it is not there,
 * emptied when it is.  Held; 0 and *error otherwise. */
struct inode *fs_creat(const char *path, int *error)
{
	const char *name;
	int len;
	struct inode *dp = parent(path, &name, &len, error), *ip;
	unsigned n;

	if (!dp)
		return 0;
	if ((n = lookup(dp, name, len, 0)) != 0) {
		ip = iget(dp->dev, n);
		iput(dp);
		if (!ip) {
			*error = EIO;
			return 0;
		}
		if ((ip->d.mode & S_IFMT) == S_IFDIR) {
			iput(ip);
			*error = EISDIR;
			return 0;
		}
		itrunc(ip);
		return ip;
	}
	if (!(ip = ialloc(dp->dev, S_IFREG | 0644))) {
		iput(dp);
		*error = ENOSPC;
		return 0;
	}
	if ((*error = dir_add(dp, name, len, ip->ino)) != 0) {
		ip->d.nlink = 0;
		iput(ip);
		ip = 0;
	}
	iput(dp);
	return ip;
}

int fs_mkdir(const char *path)
{
	const char *name;
	int len, error = 0;
	struct inode *dp = parent(path, &name, &len, &error), *ip;

	if (!dp)
		return error;
	if (lookup(dp, name, len, 0)) {
		iput(dp);
		return EEXIST;
	}
	if (!(ip = ialloc(dp->dev, S_IFDIR | 0755))) {
		iput(dp);
		return ENOSPC;
	}
	ip->d.nlink = 2;			/* its name and its "." */
	if ((error = dir_add(ip, ".", 1, ip->ino)) == 0 &&
	    (error = dir_add(ip, "..", 2, dp->ino)) == 0 &&
	    (error = dir_add(dp, name, len, ip->ino)) == 0) {
		dp->d.nlink++;			/* the new one's ".." */
		iupdate(dp);
	} else {
		ip->d.nlink = 0;
	}
	iput(ip);
	iput(dp);
	return error;
}

int fs_unlink(const char *path)
{
	const char *name;
	int len, error = 0;
	struct inode *dp = parent(path, &name, &len, &error), *ip;
	struct direct d;
	unsigned long where = 0;
	unsigned n;

	if (!dp)
		return error;
	if ((len == 1 && name[0] == '.') || (len == 2 && name[0] == '.' && name[1] == '.')) {
		iput(dp);
		return EINVAL;
	}
	if (!(n = lookup(dp, name, len, &where)) || !(ip = iget(dp->dev, n))) {
		iput(dp);
		return ENOENT;
	}
	if ((ip->d.mode & S_IFMT) == S_IFDIR) {
		iput(ip);
		iput(dp);
		return EISDIR;
	}
	memset(&d, 0, sizeof d);
	writei(dp, where, (const char *)&d, sizeof d);
	ip->d.nlink--;
	iupdate(ip);
	iput(ip);
	iput(dp);
	return 0;
}
