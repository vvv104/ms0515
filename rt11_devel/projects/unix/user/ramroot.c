/* ramroot.c - /bin/ramroot [WHERE [PATH...]]: the system into the RAM disk
 * and the root there, run by /etc/rc first.
 *
 * The boot diskette's volume is copied to /dev/rd0 block for block, the
 * system alone - /home and /tmp left empty (clone.c); mount(rd0, "/")
 * makes the copy the root.  Then the diskette is mounted at WHERE, and
 * each PATH of it at the same PATH of the new root: /etc/rc's "ramroot
 * /mnt/floppy1 /home" has the diskette at /mnt/floppy1 and its /home at
 * /home - the user's files kept on the diskette, the system in memory.
 * /etc/mtab is begun anew with what is mounted where (mtab.c) - with no
 * RAM disk, the diskette at /.  Only the copy is told, for the boot's
 * log; what is where /bin/mount tells.  A root that is not a diskette's
 * - ramroot run again - is left as it is. */
#include "unistd.h"
#include "clone.h"
#include "mtab.h"

#define RD "/dev/rd0"
#define RD_DEV 6

/* PATH of the diskette, at WHERE, mounted at PATH: 0, or -1 told. */
static int also(const char *disk, const char *where, const char *path)
{
	char from[PATHMAX], what[PATHMAX];
	int n = strlen(disk);

	if (join(from, where, path[0] == '/' ? path + 1 : path, PATHMAX) < 0 || mount(from, path) < 0) {
		complain("ramroot", path);
		return -1;
	}
	if (n + 1 + strlen(path) < PATHMAX) {	/* the table's DEVICE:PATH */
		memcpy(what, disk, n);
		what[n] = ':';
		memcpy(what + n + 1, path, strlen(path) + 1);
		mtab_add(what, path);
	}
	return 0;
}

int main(int argc, char **argv)
{
	struct stat st;
	const char *disk;
	int from, to, bad, i;

	if (stat("/", &st) < 0 || st.dev >= RD_DEV)
		return 0;			/* the root not a diskette's: done already */
	disk = device_path(st.dev);
	if ((to = open(RD, 2)) >= 0 && device_blocks(to) != 1024) {
		close(to);
		to = -1;
	}
	if (to < 0) {				/* no RAM disk: the diskette the root */
		mtab_new(disk);
		return 0;
	}
	if ((from = open(disk, 0)) < 0) {
		complain("ramroot", disk);
		close(to);
		return 1;
	}
	puts_fd(1, "ramroot: the system into " RD "\n");
	bad = clone_volume(from, to, 1024, "RAMDISK", 1);
	close(from);
	close(to);
	if (bad < 0 || mount(RD, "/") < 0) {
		complain("ramroot", RD);
		return 1;
	}
	puts_fd(1, "ramroot: ");
	putnum((unsigned)bad);
	puts_fd(1, " of 1024 blocks\n");
	bad = 0;
	mtab_new(RD);
	if (argc > 1) {
		if (mount(disk, argv[1]) < 0) {
			complain("ramroot", argv[1]);
			return 1;
		}
		mtab_add(disk, argv[1]);
		for (i = 2; i < argc; i++)
			if (also(disk, argv[1], argv[i]) < 0)
				bad = -1;
	}
	return bad < 0;
}
