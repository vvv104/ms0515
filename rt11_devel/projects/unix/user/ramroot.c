/* ramroot.c - /bin/ramroot [DIR [PATH]]: the system into the RAM disk and
 * the root there, run by /etc/rc first.
 *
 * The boot diskette's volume is copied to /dev/rd0 block for block, the
 * system alone - /home and /tmp left empty (clone.c); mount(rd0, "/")
 * makes the copy the root.  Then PATH of the diskette's volume - its root
 * without one - is mounted at DIR: /etc/rc's "ramroot /home /home" keeps
 * the user's files on the diskette, the system in memory.  With no RAM
 * disk nothing is done and nothing said; with a root that is not a
 * diskette's - ramroot run again - neither. */
#include "unistd.h"
#include "clone.h"

#define RD "/dev/rd0"
#define RD_DEV 6

static void say(const char *s)
{
	puts_fd(1, s);
}

int main(int argc, char **argv)
{
	const char *dir = argc > 1 ? argv[1] : 0, *path = argc > 2 ? argv[2] : "/", *disk;
	struct stat st;
	unsigned top = 0;
	int from, to, bad;

	if (stat("/", &st) < 0 || st.dev >= RD_DEV)
		return 0;			/* the root not a diskette's: done already */
	disk = device_path(st.dev);
	if (dir) {				/* PATH found while the diskette is the root */
		if (stat(path, &st) < 0) {
			complain("ramroot", path);
			return 1;
		}
		if ((st.mode & 0170000) != 0040000) {
			errno = ENOTDIR;
			complain("ramroot", path);
			return 1;
		}
		top = st.ino;
	}
	if ((to = open(RD, 2)) < 0)
		return 0;
	if (device_blocks(to) != 1024) {	/* no RAM disk */
		close(to);
		return 0;
	}
	if ((from = open(disk, 0)) < 0) {
		complain("ramroot", disk);
		close(to);
		return 1;
	}
	bad = clone_volume(from, to, 1024, "RAMDISK", 1);
	close(from);
	close(to);
	if (bad < 0 || mount(RD, "/", 0) < 0) {
		complain("ramroot", RD);
		return 1;
	}
	say("ramroot: the root in " RD);
	if (dir) {
		if (mount(disk, dir, top) < 0) {
			say("\n");
			complain("ramroot", dir);
			return 1;
		}
		say(", ");
		say(path);
		say(" of ");
		say(disk);
		say(" at ");
		say(dir);
	}
	say("\n");
	return 0;
}
