/* fdformat.c - /bin/fdformat DEVICE: the diskette's tracks written anew,
 * as the machine's diskettes are laid out: 80 tracks a side, 10 sectors of
 * 512 bytes numbered 1 to 10, full of E5 - a side for fd0..fd3, both sides
 * for fdx0 and fdx1.  mkfs makes a volume of it after.
 *
 * The program builds a track's image as the controller's WRITE TRACK takes
 * it, and the kernel hands it to /boot/fd (kernel/ioctl.h); only the ID
 * fields' cylinder and side change from one track to the next.  The gap
 * after a data field is 30 bytes, not the 54 of the textbook: ten sectors
 * of 512 with 54 are longer than a revolution (our RT-11 FORMAT's FMTDZ
 * lays them out the same). */
#include "unistd.h"
#include "../kernel/ioctl.h"

#define SECTORS 10
#define SECSIZ 512
#define FILLER 0345			/* what a new sector is full of */
#define GAPBYT 0116			/* what a gap is full of */
#define GAP4A 80			/* before the index mark */
#define GAP1 50				/* after it */
#define GAP2 22				/* between an ID field and its data */
#define GAP3 30				/* after a data field */
#define SYNLEN 12			/* zeros before the sync bytes */
#define TRKLEN 6400			/* a revolution is 6250 bytes; the rest is
					 * gap for a slow drive */

static char track[TRKLEN];
static char *ids[SECTORS];		/* each ID field's cylinder byte */

/* n bytes of c at p; past them. */
static char *fill(char *p, int c, int n)
{
	memset(p, c, (unsigned)n);
	return p + n;
}

/* Twelve zeros and three of the sync byte s. */
static char *syncs(char *p, int s)
{
	p = fill(p, 0, SYNLEN);
	return fill(p, s, 3);
}

/* The track's image, its ID fields' cylinder and side left to stamp(). */
static void build(void)
{
	char *p;
	int s;

	fill(track, GAPBYT, TRKLEN);		/* gap everywhere, the fields over it */
	p = syncs(track + GAP4A, 0366);		/* the index mark */
	*p++ = (char)0374;
	p += GAP1;
	for (s = 0; s < SECTORS; s++) {
		p = syncs(p, 0365);			/* the ID field */
		*p++ = (char)0376;
		ids[s] = p;
		p += 2;
		*p++ = (char)(s + 1);
		*p++ = 2;				/* 512 bytes */
		*p++ = (char)0367;
		p = syncs(p + GAP2, 0365);		/* the data field */
		*p++ = (char)0373;
		p = fill(p, FILLER, SECSIZ);
		*p++ = (char)0367;
		p += GAP3;
	}
}

static void stamp(unsigned cylinder, unsigned side)
{
	int s;

	for (s = 0; s < SECTORS; s++) {
		ids[s][0] = (char)cylinder;
		ids[s][1] = (char)side;
	}
}

/* fdN's side by its name's last digit; fdx's both: 0 for the side asked
 * by the track. */
static int name_side(const char *device, int *both)
{
	const char *name = device;

	for (; *device; device++)
		if (*device == '/')
			name = device + 1;
	*both = name[2] == 'x';
	return *both ? 0 : (name[2] - '0') & 1;
}

int main(int argc, char **argv)
{
	struct fd_track t;
	unsigned tracks;
	int fd, both, side;

	if (argc != 2) {
		puts_fd(2, "usage: fdformat DEVICE\n");
		return 1;
	}
	side = name_side(argv[1], &both);
	tracks = both ? 160 : 80;
	if ((fd = open(argv[1], 1)) < 0) {
		complain("fdformat", argv[1]);
		return 1;
	}
	build();
	t.image = track;
	t.len = TRKLEN;
	for (t.track = 0; t.track < tracks; t.track++) {
		if (both)
			stamp(t.track >> 1, t.track & 1);
		else
			stamp(t.track, (unsigned)side);
		if (ioctl(fd, FD_WTRACK, &t) < 0) {
			complain("fdformat", argv[1]);
			close(fd);
			return 1;
		}
	}
	close(fd);
	puts_fd(1, "fdformat: ");
	puts_fd(1, argv[1]);
	puts_fd(1, ": ");
	putnum(tracks);
	puts_fd(1, " tracks\n");
	return 0;
}
