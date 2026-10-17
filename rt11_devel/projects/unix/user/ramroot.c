/* ramroot.c - /bin/ramroot: the system into the RAM disk and the root
 * there, run by /etc/rc first.
 *
 * The boot diskette's volume is copied to /dev/rd0 block for block
 * (clone.c) - the blocks below the data area and those its bitmap has
 * taken, so a few hundred of the 1600; mount(rd0, "/") makes the copy the
 * root, and the diskette's volume goes to /disk - an empty directory of
 * the system's, there to be mounted on - from where a file is
 * saved and a program that owns the machine is run, and where umount lets
 * the diskette go.  With no RAM disk nothing is done and nothing said;
 * with a root that is not a diskette's - ramroot run again - neither. */
#include "unistd.h"
#include "clone.h"

#define RD "/dev/rd0"
#define RD_DEV 6
#define DISK "/disk"

int main(void)
{
	struct stat st;
	const char *disk;
	int from, to, bad;

	if (stat("/", &st) < 0 || st.dev >= RD_DEV)
		return 0;			/* the root not a diskette's: done already */
	disk = device_path(st.dev);
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
	bad = clone_volume(from, to, 1024, "RAMDISK");
	close(from);
	close(to);
	if (bad < 0 || mount(RD, "/") < 0) {
		complain("ramroot", RD);
		return 1;
	}
	if (mount(disk, DISK) < 0) {
		complain("ramroot", DISK);
		return 1;
	}
	puts_fd(1, "ramroot: the root in " RD ", the diskette at " DISK "\n");
	return 0;
}
