/* ed.c - /bin/ed [FILE]: the line editor, after Unix's own.
 *
 * The file is held in memory, its lines one after another, each with its
 * newline - TEXT bytes and LINES lines at most.  A command is a line: an
 * address or two before a letter, its argument after it.  An address is a
 * number, . the current line, $ the last, /pattern/ the next line that
 * holds the pattern, ?pattern? the one before, each with +N or -N after;
 * a , alone is all the lines.  The patterns are re.c's, an empty one the
 * last one used.
 *
 *	(.)a  (.)i  (.,.)c	lines typed after, before, in place of; a . alone ends them
 *	(.,.)d			deleted
 *	(.,.)p  (.,.)n		printed, n with each one's number
 *	(.,.)s/re/to/[g]	the first match, or all with g, replaced; & is the match
 *	(.,.+1)j		joined into one
 *	(.,.)m(.)  (.,.)t(.)	moved, copied after the line addressed, 0 the start
 *	($)=			the line's number
 *	(1,$)w [FILE]  ($)r [FILE]  e [FILE]  f [FILE]	written, read in, edited, named
 *	q  Q			the end; q not over changes not written but the second time
 *	(.+1)			a line alone: printed and made the current one
 *
 * What cannot be done is a ? and nothing more, as in the old one. */
#include "unistd.h"
#include "re.h"

#define TEXT 16384
#define LINES 1024
#define LINE 256

static char text[TEXT];
static unsigned size;
static unsigned ends[LINES + 1];	/* ends[n]: where line n ends - line n+1 begins */
static int lines, dot, dirty, warned;
static char name[PATHMAX], pattern[LINE];

static struct {
	char buf[256];
	int have, at;
} in;

static int fail(void)
{
	puts_fd(1, "?\n");
	return 0;
}

static void tell(long n)
{
	putnum((unsigned)n);
	puts_fd(1, "\n");
}

/* A line of the input into line, its newline gone: its length, -1 at the
 * end. */
static int input(char *line, int room)
{
	int n = 0;
	char c;

	for (;;) {
		if (in.at == in.have) {
			in.at = 0;
			if ((in.have = read(0, in.buf, sizeof in.buf)) <= 0) {
				in.have = 0;
				if (!n)
					return -1;
				break;
			}
		}
		if ((c = in.buf[in.at++]) == '\n')
			break;
		if (n < room - 1)
			line[n++] = c;
	}
	line[n] = 0;
	return n;
}

/* The lines found again in the text: 0, or -1 when there are too many. */
static int reindex(void)
{
	unsigned i;

	lines = 0;
	for (i = 0; i < size; i++)
		if (text[i] == '\n') {
			if (lines == LINES)
				return -1;
			ends[++lines] = i + 1;
		}
	return 0;
}

static void reverse(unsigned from, unsigned to)
{
	char c;

	while (from + 1 < to) {
		c = text[from];
		text[from++] = text[--to];
		text[to] = c;
	}
}

/* [mid, end) put before [from, mid). */
static void rotate(unsigned from, unsigned mid, unsigned end)
{
	reverse(from, mid);
	reverse(mid, end);
	reverse(from, end);
}

/* n bytes at the text's end - put there by the caller - moved in after
 * line `after`: 0, or -1 with them dropped when the lines are too many. */
static int place(int after, unsigned n)
{
	unsigned at = ends[after];

	rotate(at, size - n, size);
	if (reindex() == 0) {
		dirty = 1;
		return 0;
	}
	rotate(at, at + n, size);
	size -= n;
	reindex();
	return -1;
}

/* The line s, len bytes, put in after line `after`: 0, or -1. */
static int put_line(int after, const char *s, unsigned len)
{
	if (size + len + 1 > TEXT)
		return -1;
	memcpy(text + size, s, len);
	text[size + len] = '\n';
	size += len + 1;
	return place(after, len + 1);
}

static void delete(int a, int b)
{
	unsigned from = ends[a - 1], n = ends[b] - from, i;

	for (i = from; i + n < size; i++)
		text[i] = text[i + n];
	size -= n;
	reindex();
	dirty = 1;
}

/* Line n into line, its newline gone, cut at room: its length. */
static int get_line(int n, char *line, int room)
{
	int len = (int)(ends[n] - ends[n - 1]) - 1;

	if (len > room - 1)
		len = room - 1;
	memcpy(line, text + ends[n - 1], (unsigned)len);
	line[len] = 0;
	return len;
}

static void show(int n, int numbered)
{
	if (numbered) {
		putnum((unsigned)n);
		puts_fd(1, "\t");
	}
	write(1, text + ends[n - 1], (int)(ends[n] - ends[n - 1]));
}

/* FILE read in after line `after`: its bytes, -1 when it cannot be. */
static long load(const char *file, int after)
{
	unsigned start = size;
	int fd = open(file, 0), got;
	char more;

	if (fd < 0)
		return -1;
	while (size < TEXT && (got = read(fd, text + size, TEXT - size)) > 0)
		size += (unsigned)got;
	got = size == TEXT ? read(fd, &more, 1) : 0;
	close(fd);
	if (got > 0 || (size > start && text[size - 1] != '\n' && size == TEXT)) {
		size = start;
		return -1;
	}
	if (size > start && text[size - 1] != '\n')
		text[size++] = '\n';
	if (size > start && place(after, size - start) < 0)
		return -1;
	return (long)(size - start);
}

/* Lines a..b written to FILE: their bytes, -1 when they cannot be. */
static long save(const char *file, int a, int b)
{
	unsigned n = ends[b] - ends[a - 1];
	int fd = creat(file), bad;

	if (fd < 0)
		return -1;
	bad = n && write(fd, text + ends[a - 1], (int)n) != (int)n;
	close(fd);
	return bad ? -1 : (long)n;
}

/* The text after a delimiter, to the next one or the end, into out - \x
 * kept as it is - *p past it. */
static void take(const char **p, int d, char *out)
{
	const char *s = *p;
	int n = 0;

	while (*s && *s != d) {
		if (*s == '\\' && s[1] && n < LINE - 2)
			out[n++] = *s++;
		if (n < LINE - 1)
			out[n++] = *s;
		s++;
	}
	if (*s == d)
		s++;
	out[n] = 0;
	*p = s;
}

/* The pattern's line from the current one on - forward or back, round
 * the ends: its number, -1 for none. */
static int find(int forward)
{
	char line[LINE];
	const char *start;
	int n = dot, i;

	for (i = 0; i < lines; i++) {
		n = forward ? n % lines + 1 : n > 1 ? n - 1 : lines;
		get_line(n, line, sizeof line);
		if (re_match(pattern, line, &start))
			return n;
	}
	return -1;
}

static int number(const char **p)
{
	int n = 0;

	while (**p >= '0' && **p <= '9')
		n = n * 10 + *(*p)++ - '0';
	return n;
}

/* An address at *p into *n: 1 there was one, 0 none, -1 a bad one. */
static int address(const char **p, int *n)
{
	const char *s = *p;
	char re[LINE];
	int got = 1, sign, d;

	if (*s == '.' || *s == '$') {
		*n = *s++ == '.' ? dot : lines;
	} else if (*s >= '0' && *s <= '9') {
		*n = number(&s);
	} else if (*s == '/' || *s == '?') {
		d = *s++;
		take(&s, d, re);
		if (re[0])
			memcpy(pattern, re, LINE);
		if (!pattern[0] || (*n = find(d == '/')) < 0)
			return -1;
	} else {
		got = 0;
	}
	while (*s == '+' || *s == '-') {
		sign = *s++ == '+' ? 1 : -1;
		if (!got)
			*n = dot;
		got = 1;
		*n += sign * (*s >= '0' && *s <= '9' ? number(&s) : 1);
	}
	*p = s;
	return got && (*n < 0 || *n > lines) ? -1 : got;
}

/* The addresses before the command: how many - 0, 1 or 2 - in *a and *b,
 * the current line for none; -1 for bad ones. */
static int range(const char **p, int *a, int *b)
{
	int got;

	if (**p == ',') {
		(*p)++;
		*a = lines ? 1 : 0;
		*b = lines;
		return 2;
	}
	if ((got = address(p, a)) < 0)
		return -1;
	if (!got)
		*a = dot;
	*b = *a;
	if (got && **p == ',') {
		(*p)++;
		if (address(p, b) <= 0 || *b < *a)
			return -1;
		return 2;
	}
	return got;
}

static int lined(int a, int b)
{
	return a >= 1 && a <= b && b <= lines;
}

/* What follows m or t, or w r e f: the rest of the line, a space before
 * a name skipped. */
static const char *argument(const char *p)
{
	while (*p == ' ')
		p++;
	return p;
}

/* Lines typed put in after line `after`, a . alone the end: 0, or -1 when
 * one did not fit. */
static int typed(int after)
{
	char line[LINE];
	int len;

	while ((len = input(line, sizeof line)) >= 0 && !(len == 1 && line[0] == '.')) {
		if (put_line(after, line, (unsigned)len) < 0)
			return -1;
		dot = ++after;
	}
	return 0;
}

/* c to out at *o, room kept for the NUL: 0, or -1 when it is full. */
static int out_c(char *out, int *o, int c)
{
	if (*o >= LINE - 1)
		return -1;
	out[(*o)++] = (char)c;
	return 0;
}

/* The replacement `to` for the match [start, end) to out: 0, or -1. */
static int replace(const char *to, const char *start, const char *end, char *out, int *o)
{
	const char *t;
	int bad = 0;

	for (; *to; to++)
		if (*to == '&')
			for (t = start; t < end; t++)
				bad |= out_c(out, o, *t);
		else
			bad |= out_c(out, o, *to == '\\' && to[1] ? *++to : *to);
	return bad;
}

/* Line n's matches replaced into out: how many, -1 when it grew too long. */
static int substitute(int n, const char *to, int all, char *out)
{
	char line[LINE];
	const char *s = line, *start, *end;
	int o = 0, done = 0, bad = 0;

	get_line(n, line, sizeof line);
	while ((end = re_match(pattern, s, &start)) != 0) {
		while (s < start)
			bad |= out_c(out, &o, *s++);
		bad |= replace(to, start, end, out, &o);
		done++;
		s = end;
		if (end == start) {			/* an empty match: a character on */
			if (!*s)
				break;
			bad |= out_c(out, &o, *s++);
		}
		if (!all || pattern[0] == '^' || !*s)
			break;
	}
	while (*s)
		bad |= out_c(out, &o, *s++);
	out[o] = 0;
	return bad ? -1 : done;
}

/* s/re/to/[g] over lines a..b: 0, or -1 when no line changed. */
static int subst(int a, int b, const char *p)
{
	char re[LINE], to[LINE], out[LINE];
	int d = *p++, n, got, done = 0;

	if (!d || d == ' ' || !lined(a, b))
		return -1;
	take(&p, d, re);
	take(&p, d, to);
	if (re[0])
		memcpy(pattern, re, LINE);
	if (!pattern[0])
		return -1;
	for (n = a; n <= b; n++) {
		if ((got = substitute(n, to, *p == 'g', out)) < 0)
			return -1;
		if (!got)
			continue;
		delete(n, n);
		if (put_line(n - 1, out, (unsigned)strlen(out)) < 0)
			return -1;
		dot = n;
		done = 1;
	}
	return done ? 0 : -1;
}

/* Lines a..b joined into one: the newlines between them taken out. */
static int join_lines(int a, int b)
{
	unsigned i, from;
	int n;

	if (!lined(a, b))
		return -1;
	for (n = b - 1; n >= a; n--) {
		from = ends[n] - 1;
		for (i = from; i + 1 < size; i++)
			text[i] = text[i + 1];
		size--;
	}
	reindex();
	dirty = 1;
	dot = a;
	return 0;
}

/* Lines a..b copied after line `after` (t), or moved there (m). */
static int transfer(int a, int b, const char *p, int move)
{
	unsigned from, n;
	int after, count = b - a + 1;

	p = argument(p);
	if (address(&p, &after) <= 0 || !lined(a, b) || *p)
		return -1;
	if (move && after >= a - 1 && after <= b)
		return after == a - 1 || after == b ? 0 : -1;
	from = ends[a - 1];
	n = ends[b] - from;
	if (size + n > TEXT || lines + count > LINES)
		return -1;
	memcpy(text + size, text + from, n);
	size += n;
	if (place(after, n) < 0)
		return -1;
	dot = after + count;
	if (move) {
		if (after < a)
			a += count, b += count;
		delete(a, b);
		if (after > b)
			dot -= count;
	}
	return 0;
}

/* w, r, e, f with their file - the one remembered without one. */
static int file_command(int c, int given, int a, int b, const char *p, int again)
{
	const char *f = argument(p);
	long n;
	int before = lines;

	if (c == 'e' && dirty && !again) {	/* the changes not written: once more */
		warned = 1;
		return -1;
	}
	if (*f && (!name[0] || c == 'e' || c == 'f') && strlen(f) < PATHMAX)
		memcpy(name, f, (unsigned)strlen(f) + 1);
	if (!*f)
		f = name;
	if (c == 'f') {
		puts_fd(1, name);
		puts_fd(1, "\n");
		return 0;
	}
	if (!*f)
		return -1;
	if (c == 'w') {
		if (!given)
			a = lines ? 1 : 0, b = lines;
		if ((n = save(f, a ? a : 1, b)) < 0)
			return -1;
		if (a <= 1 && b == lines)
			dirty = 0;
	} else if (c == 'r') {
		if ((n = load(f, given ? b : lines)) < 0)
			return -1;
		dot = (given ? b : before) + (lines - before);
	} else {
		size = 0;
		reindex();
		n = load(f, 0);
		dirty = 0;
		dot = lines;
		if (n < 0)
			return -1;
	}
	tell(n);
	return 0;
}

/* One command line: 1 when ed is to end. */
static int command(const char *p)
{
	int a, b, given = range(&p, &a, &b), again = warned, c;

	warned = 0;
	if (given < 0)
		return fail();
	c = *p++;
	switch (c) {
	case 0:
		if (!given)
			a = b = dot + 1;
		if (!lined(b, b))
			return fail();
		dot = b;
		show(b, 0);
		return 0;
	case 'p':
	case 'n':
		if (*p || !lined(a, b))
			return fail();
		for (dot = a; dot <= b; dot++)
			show(dot, c == 'n');
		dot = b;
		return 0;
	case 'd':
		if (*p || !lined(a, b))
			return fail();
		delete(a, b);
		dot = a > lines ? lines : a;
		return 0;
	case 'a':
	case 'i':
	case 'c':
		if (*p || (c == 'c' && !lined(a, b)) || (c == 'i' && given && b < 1))
			return fail();
		if (c == 'c')
			delete(a, b);
		return typed(c == 'a' ? b : c == 'i' ? (b > 0 ? b - 1 : 0) : a - 1) < 0 ? fail() : 0;
	case '=':
		tell(given ? b : lines);
		return 0;
	case 's':
		return subst(a, b, p) < 0 ? fail() : 0;
	case 'j':
		return join_lines(a, given < 2 ? a + 1 : b) < 0 ? fail() : 0;
	case 'm':
	case 't':
		return transfer(a, b, p, c == 'm') < 0 ? fail() : 0;
	case 'w':
	case 'r':
	case 'e':
	case 'f':
		return file_command(c, given, a, b, p, again) < 0 ? fail() : 0;
	case 'q':
	case 'Q':
		if (*p)
			return fail();
		if (c == 'q' && dirty && !again) {
			warned = 1;
			return fail();
		}
		return 1;
	}
	return fail();
}

int main(int argc, char **argv)
{
	char line[LINE];
	long n;

	if (argc > 1) {
		if (strlen(argv[1]) < PATHMAX)
			memcpy(name, argv[1], (unsigned)strlen(argv[1]) + 1);
		if ((n = load(name, 0)) < 0)
			fail();
		else
			tell(n);
		dirty = 0;
		dot = lines;
	}
	while (input(line, sizeof line) >= 0)
		if (command(line))
			break;
	return 0;
}
