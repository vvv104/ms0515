/* mount.c - mount(from, dir) and umount(device).
 *
 * from is a block device - its volume put at dir whole - or a directory
 * of a volume mounted already, put at dir as well: the volume then in
 * the table twice, its root at the one place and that directory at the
 * other, the rest unseen there.  So /etc/rc has the boot diskette at
 * /mnt/floppy1 and its /home at /home.  A directory of the root's volume
 * is not put elsewhere, nor a volume on a directory of its own.  umount
 * takes every place of the device's volume out at once.
 *
 * mount(device, "/") makes the volume the root in place of the boot's
 * (fs.c fs_pivot): /bin/ramroot's, the system copied to the RAM disk. */
#include "sys.h"

#define mounts (HIGH->mounts)
#define itab (HIGH->itab)

/* What path names: a block device - its number, *top ROOT_INO - or a
 * directory of a mounted volume - its device, *top the directory's inode;
 * -1 and the error for anything else. */
static int source(const char *path, unsigned *top, int *error)
{
	struct inode *ip = namei(path, error);
	unsigned mode;
	int dev = -1;

	if (!ip)
		return -1;
	mode = ip->d.mode & S_IFMT;
	*top = ROOT_INO;
	if (mode == S_IFBLK) {
		dev = (int)ip->d.ext[0].start;
	} else if (mode == S_IFDIR && ip->dev != mounts[0].dev) {
		dev = (int)ip->dev;
		*top = ip->ino;
	}
	iput(ip);
	if (dev < 0)
		*error = mode == S_IFDIR ? EINVAL : ENOTBLK;
	return dev;
}

/* The volume of from into the tree at dir. */
static int mount_on(const char *from, const char *dir)
{
	struct mount *m = 0, *had;
	struct inode *dp;
	unsigned top;
	int dev, error = 0, i;

	if ((dev = source(from, &top, &error)) < 0)
		return error;
	had = mount_of((unsigned)dev);
	if (top == ROOT_INO ? fs_mounted((unsigned)dev) : !had)	/* a whole volume once */
		return EBUSY;
	if (dir[0] == '/' && !dir[1])
		return fs_pivot((unsigned)dev);
	for (i = 1; i < NMOUNT && !m; i++)
		if (!mounts[i].used)
			m = &mounts[i];
	if (!m)
		return ENFILE;
	if (!(dp = namei(dir, &error)))
		return error;
	if ((dp->d.mode & S_IFMT) != S_IFDIR || dp->ino == ROOT_INO || covering(dp) || dp->dev == (unsigned)dev) {
		iput(dp);
		return (dp->d.mode & S_IFMT) != S_IFDIR ? ENOTDIR : EBUSY;
	}
	if (had)
		memcpy(&m->sb, &had->sb, sizeof m->sb);
	else if ((error = read_super((unsigned)dev, &m->sb)) != 0) {
		iput(dp);
		return error;
	}
	m->dev = (unsigned)dev;
	m->on = dp;				/* held while it is covered */
	m->top = top;
	m->used = 1;
	return 0;
}

/* The device's volume taken out of every place it has, when nothing on
 * it is held. */
static int umount(const char *device)
{
	struct mount *m;
	struct inode *ip;
	unsigned top;
	int dev, error = 0;

	if ((dev = source(device, &top, &error)) < 0 || top != ROOT_INO)
		return error ? error : ENOTBLK;
	if (!(m = mount_of((unsigned)dev)) || m == &mounts[0])
		return EINVAL;
	for (ip = itab; ip < itab + NINODE; ip++)
		if (ip->count && ip->dev == (unsigned)dev)
			return EBUSY;
	for (m = mounts + 1; m < mounts + NMOUNT; m++)
		if (m->used && m->dev == (unsigned)dev) {
			iput(m->on);
			m->used = 0;
		}
	bforget((unsigned)dev);			/* the diskette may be changed now */
	return 0;
}

int sys_mount(struct trapframe *tf, const unsigned *arg)
{
	int error;

	if ((error = user_string(arg[0], PATHMAX)) != 0 || (error = user_string(arg[1], PATHMAX)) != 0)
		return error;
	if ((error = mount_on((const char *)arg[0], (const char *)arg[1])) != 0)
		return error;
	tf->r0 = 0;
	return 0;
}

int sys_umount(struct trapframe *tf, const unsigned *arg)
{
	int error;

	if ((error = user_string(arg[0], PATHMAX)) != 0)
		return error;
	if ((error = umount((const char *)arg[0])) != 0)
		return error;
	tf->r0 = 0;
	return 0;
}
