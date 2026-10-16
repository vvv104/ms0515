/* mount.c - /boot/hi: mount(device, dir) and umount(device). */
#include "../hi.h"

#define mounts (HIGH->mounts)
#define itab (HIGH->itab)

/* The block device at path: its number, or -1 and the error. */
static int device_at(const char *path, int *error)
{
	struct inode *ip = namei(path, error);
	int dev;

	if (!ip)
		return -1;
	dev = (ip->d.mode & S_IFMT) == S_IFBLK ? (int)ip->d.ext[0].start : -1;
	iput(ip);
	if (dev < 0)
		*error = ENOTBLK;
	return dev;
}

/* The volume on the device into the tree at dir. */
static int mount_on(const char *device, const char *dir)
{
	struct mount *m = 0;
	struct inode *dp;
	int dev, error = 0, i;

	if ((dev = device_at(device, &error)) < 0)
		return error;
	if (fs_mounted((unsigned)dev))
		return EBUSY;
	for (i = 1; i < NMOUNT && !m; i++)
		if (!mounts[i].used)
			m = &mounts[i];
	if (!m)
		return ENFILE;
	if (!(dp = namei(dir, &error)))
		return error;
	if ((dp->d.mode & S_IFMT) != S_IFDIR || dp->ino == ROOT_INO || covering(dp)) {
		iput(dp);
		return (dp->d.mode & S_IFMT) != S_IFDIR ? ENOTDIR : EBUSY;
	}
	if ((error = read_super((unsigned)dev, &m->sb)) != 0) {
		iput(dp);
		return error;
	}
	m->dev = (unsigned)dev;
	m->on = dp;				/* held while it is covered */
	m->used = 1;
	return 0;
}

/* The volume taken out, when nothing on it is held. */
static int umount(const char *device)
{
	struct mount *m;
	struct inode *ip;
	int dev, error = 0;

	if ((dev = device_at(device, &error)) < 0)
		return error;
	if (!(m = mount_of((unsigned)dev)) || m == &mounts[0])
		return EINVAL;
	for (ip = itab; ip < itab + NINODE; ip++)
		if (ip->count && ip->dev == (unsigned)dev)
			return EBUSY;
	iput(m->on);
	m->used = 0;
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
