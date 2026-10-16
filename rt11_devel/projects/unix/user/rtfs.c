/* rtfs.c - /bin/rtfs: the files of an RT-11 volume on a diskette's side,
 * read from the system - a side's device, /dev/fd0..fd3, is the blocks of
 * an RT-11 DZ volume one for one.
 *
 *	rtfs ls DEVICE			the directory: the names, their blocks
 *	rtfs get DEVICE NAME [TO]	a file copied off, whole blocks - RT-11
 *					keeps no length in bytes - to TO, or to
 *					its name in lower case
 *	rtfs put DEVICE FILE [NAME]	a file of the system written on, as NAME
 *					or under its own name in upper case; a
 *					file of the name there given up first,
 *					as PIP does
 *
 * The directory is RT-11's: from block 6 a chain of segments of two
 * blocks, each a header of five words - the segments, the next one, the
 * highest in use, the extra bytes of an entry, the first block of its
 * files - and the entries after it, seven words and the extra bytes each:
 * the status, the name and the type in RAD50, the length, the channel and
 * the job, the date.  A file's first block is its segment's first one and
 * the lengths of the entries before it.  put takes the first empty entry
 * long enough, the file in its first blocks and an empty entry after it
 * for the rest - the entries behind moved up, when the segment has room
 * for one more. */
#include "unistd.h"

#define DIRSTART 6			/* the first segment's block */
#define MAXSEG 31
#define E_MPTY 01000			/* empty room */
#define E_PERM 02000			/* a file */
#define E_EOS 04000			/* the segment's end */

struct found {
	unsigned start, length;
};

static int dev;
static unsigned seg[512];		/* a directory segment, two blocks */
static char buf[512];

static int block(unsigned b, void *to)
{
	return lseek(dev, (long)b * 512, 0) >= 0 && read(dev, to, 512) == 512 ? 0 : -1;
}

static int write_block(unsigned b, const void *from)
{
	return lseek(dev, (long)b * 512, 0) >= 0 && write(dev, from, 512) == 512 ? 0 : -1;
}

/* Segment s into seg, or out of it. */
static int segment(unsigned s, int out)
{
	unsigned b = DIRSTART + (s - 1) * 2;

	if (out)
		return write_block(b, seg) || write_block(b + 1, seg + 256) ? -1 : 0;
	return block(b, seg) || block(b + 1, seg + 256) ? -1 : 0;
}

/* A RAD50 word as three characters at s. */
static void rad50(unsigned w, char *s)
{
	static const char set[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ$.%0123456789";

	s[2] = set[w % 40];
	w /= 40;
	s[1] = set[w % 40];
	s[0] = set[w / 40 % 40];
}

/* An entry's NAME.TYP, its spaces left out. */
static void name_of(const unsigned *e, char *name)
{
	char raw[9];
	int i, n = 0;

	rad50(e[1], raw);
	rad50(e[2], raw + 3);
	rad50(e[3], raw + 6);
	for (i = 0; i < 6; i++)
		if (raw[i] != ' ')
			name[n++] = raw[i];
	name[n++] = '.';
	for (i = 6; i < 9; i++)
		if (raw[i] != ' ')
			name[n++] = raw[i];
	name[n] = 0;
}

static void put_padded(const char *s, int width)
{
	puts_fd(1, s);
	while (width-- > strlen(s))
		puts_fd(1, " ");
}

/* Every file of the directory: listed, or the one named found; -1 when
 * the directory cannot be read. */
static int walk(const char *want, struct found *f)
{
	char name[12];
	unsigned s = 1, start, *e, files = 0, used = 0, n;

	for (n = 0; s && n < MAXSEG; n++, s = seg[1]) {
		if (segment(s, 0))
			return -1;
		start = seg[4];
		for (e = seg + 5; e + 7 <= seg + 512 && !(e[0] & E_EOS); start += e[4], e += 7 + seg[3] / 2) {
			if (!(e[0] & E_PERM))
				continue;
			name_of(e, name);
			if (want && !strcmp(want, name)) {
				f->start = start;
				f->length = e[4];
				return 1;
			}
			if (!want) {
				put_padded(name, 12);
				putnum(e[4]);
				puts_fd(1, "\n");
				files++;
				used += e[4];
			}
		}
	}
	if (!want) {
		putnum(files);
		puts_fd(1, " files, ");
		putnum(used);
		puts_fd(1, " blocks\n");
	}
	return 0;
}

/* The file off the volume into to. */
static int get(const char *want, const char *to)
{
	struct found f;
	unsigned b;
	int out, r = walk(want, &f);

	if (r <= 0) {
		puts_fd(2, "rtfs: ");
		puts_fd(2, want);
		puts_fd(2, r < 0 ? ": the directory unread\n" : ": not found\n");
		return 1;
	}
	if ((out = creat(to, 0644)) < 0) {
		complain("rtfs", to);
		return 1;
	}
	for (b = 0; b < f.length; b++)
		if (block(f.start + b, buf) || write(out, buf, 512) != 512) {
			complain("rtfs", to);
			close(out);
			return 1;
		}
	close(out);
	return 0;
}

/* NAME.TYP, upper case, in three RAD50 words; -1 when it is no RT-11 name. */
static int rad50_name(const char *name, unsigned *w)
{
	static const char set[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ$.%0123456789";
	char c[9];
	int i, k, n = 0;

	for (i = 0; i < 9; i++)
		c[i] = ' ';
	for (i = 0; name[i] && name[i] != '.'; i++) {
		if (n == 6)
			return -1;
		c[n++] = name[i];
	}
	if (name[i] == '.') {
		for (i++, n = 6; name[i]; i++) {
			if (n == 9)
				return -1;
			c[n++] = name[i];
		}
	}
	for (k = 0; k < 3; k++) {
		w[k] = 0;
		for (i = 0; i < 3; i++) {
			n = 0;
			while (set[n] && set[n] != c[k * 3 + i])
				n++;
			if (!set[n] || (set[n] == '.' && c[k * 3 + i] == '.'))
				return -1;
			w[k] = w[k] * 40 + (unsigned)n;
		}
	}
	return 0;
}

/* Every file of the name given up: its entry empty room. */
static int give_up(const unsigned *w)
{
	unsigned s = 1, *e, n, changed;

	for (n = 0; s && n < MAXSEG; n++, s = seg[1]) {
		if (segment(s, 0))
			return -1;
		for (changed = 0, e = seg + 5; e + 7 <= seg + 512 && !(e[0] & E_EOS); e += 7 + seg[3] / 2)
			if ((e[0] & E_PERM) && e[1] == w[0] && e[2] == w[1] && e[3] == w[2]) {
				e[0] = E_MPTY;
				changed = 1;
			}
		if (changed && segment(s, 1))
			return -1;
	}
	return 0;
}

/* The n blocks of in onto the volume at start; short reads padded with zeros. */
static int write_out(int in, unsigned start, unsigned n)
{
	unsigned b;
	int got, i;

	for (b = 0; b < n; b++) {
		got = read(in, buf, 512);
		for (i = got < 0 ? 0 : got; i < 512; i++)
			buf[i] = 0;
		if (got < 0 || write_block(start + b, buf))
			return -1;
	}
	return 0;
}

/* The file at from onto the volume as w; 0, or 1 told. */
static int put(const char *from, const unsigned *w, const char *name)
{
	struct stat st;
	unsigned s = 1, start, *e, *eos, *p, size, n, k, blocks;
	int in = open(from, 0);

	if (in < 0 || fstat(in, &st) < 0) {
		complain("rtfs", from);
		return 1;
	}
	blocks = (unsigned)((st.size + 511) / 512);
	if (give_up(w) < 0)
		goto unread;
	for (n = 0; s && n < MAXSEG; n++, s = seg[1]) {
		if (segment(s, 0))
			goto unread;
		size = 7 + seg[3] / 2;
		for (eos = seg + 5; eos + size <= seg + 512 && !(*eos & E_EOS); eos += size)
			;
		start = seg[4];
		for (e = seg + 5; e < eos; start += e[4], e += size) {
			if (!(e[0] & E_MPTY) || e[4] < blocks)
				continue;
			if (e[4] > blocks && eos + size + 1 > seg + 512)
				break;			/* no room for the rest's entry here */
			if (write_out(in, start, blocks))
				goto unread;
			if (e[4] > blocks) {
				for (p = eos; p >= e + size; p--)	/* the entries behind one up */
					p[size] = p[0];
				e[size] = E_MPTY;
				for (k = 1; k < size; k++)
					e[size + k] = 0;
				e[size + 4] = e[4] - blocks;
			}
			e[0] = E_PERM;
			e[1] = w[0];
			e[2] = w[1];
			e[3] = w[2];
			e[4] = blocks;
			for (k = 5; k < size; k++)
				e[k] = 0;
			close(in);
			return segment(s, 1) ? 1 : 0;
		}
	}
	close(in);
	puts_fd(2, "rtfs: ");
	puts_fd(2, name);
	puts_fd(2, ": no room on the volume\n");
	return 1;
unread:
	close(in);
	puts_fd(2, "rtfs: the directory unread\n");
	return 1;
}

int main(int argc, char **argv)
{
	char want[16], to[16];
	int i;

	unsigned w[3];
	const char *last;

	if (argc < 3 || (strcmp(argv[1], "ls") && strcmp(argv[1], "get") && strcmp(argv[1], "put")) ||
	    (!strcmp(argv[1], "ls") && argc != 3) || (strcmp(argv[1], "ls") && (argc < 4 || argc > 5))) {
		puts_fd(2, "usage: rtfs ls DEVICE, rtfs get DEVICE NAME [TO], rtfs put DEVICE FILE [NAME]\n");
		return 1;
	}
	if ((dev = open(argv[2], strcmp(argv[1], "put") ? 0 : 2)) < 0) {
		complain("rtfs", argv[2]);
		return 1;
	}
	if (!strcmp(argv[1], "put")) {
		for (last = argv[3]; *last; last++)
			;
		while (last > argv[3] && last[-1] != '/')
			last--;
		if (argc == 5)
			last = argv[4];
		for (i = 0; last[i] && i < 15; i++)
			want[i] = last[i] >= 'a' && last[i] <= 'z' ? (char)(last[i] - 040) : last[i];
		want[i] = 0;
		if (rad50_name(want, w) < 0) {
			puts_fd(2, "rtfs: ");
			puts_fd(2, want);
			puts_fd(2, ": no RT-11 name\n");
			return 1;
		}
		return put(argv[3], w, want);
	}
	if (!strcmp(argv[1], "ls")) {
		if (walk(0, 0) < 0) {
			puts_fd(2, "rtfs: the directory unread\n");
			return 1;
		}
		return 0;
	}
	for (i = 0; argv[3][i] && i < 15; i++) {	/* RT-11's names are upper case */
		want[i] = argv[3][i] >= 'a' && argv[3][i] <= 'z' ? (char)(argv[3][i] - 040) : argv[3][i];
		to[i] = argv[3][i] >= 'A' && argv[3][i] <= 'Z' ? (char)(argv[3][i] + 040) : argv[3][i];
	}
	want[i] = to[i] = 0;
	return get(want, argc == 5 ? argv[4] : to);
}
