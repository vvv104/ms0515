/* more.c - /bin/more [FILE...]: the files, or the input, a screen at a time:
 * PAGE lines, a line longer than the screen counted for the rows it takes,
 * then --More-- and a key from the console (2, which the shell never
 * redirects), read in the console's raw mode: space the next page, Enter
 * the next line, q the end.  Its output not the console, all of it goes
 * through as cat would. */
#include "unistd.h"
#include "../kernel/ioctl.h"

#define PAGE 23				/* the screen's 25 rows, --More-- and the cursor's */
#define COLS 80

static int paging, left = PAGE, column;

/* A key from the console: what to do next - lines to give, 0 the end. */
static int ask(void)
{
	char c = 0;

	puts_fd(1, "--More--");
	ioctl(2, TTY_RAW, (void *)1);
	while (read(2, &c, 1) == 1 && c != ' ' && c != '\r' && c != '\n' && c != 'q' && c != 'Q')
		;
	ioctl(2, TTY_RAW, (void *)0);
	puts_fd(1, "\r        \r");
	return c == 'q' || c == 'Q' ? 0 : c == ' ' ? PAGE : 1;
}

/* The bytes of fd out, the screen counted: 0, or 1 when q was pressed. */
static int show(int fd)
{
	char buf[256];
	int n, i, from;

	while ((n = read(fd, buf, sizeof buf)) > 0) {
		if (!paging) {
			write(1, buf, n);
			continue;
		}
		for (i = from = 0; i < n; i++) {
			if (buf[i] != '\n' && ++column < COLS)
				continue;
			column = 0;
			if (--left > 0)
				continue;
			write(1, buf + from, i + 1 - from);
			from = i + 1;
			if ((left = ask()) == 0)
				return 1;
		}
		write(1, buf + from, n - from);
	}
	return 0;
}

int main(int argc, char **argv)
{
	struct stat st;
	int i, fd;

	paging = fstat(1, &st) < 0;		/* the console: no file behind it */
	if (argc < 2)
		return show(0) * 0;
	for (i = 1; i < argc; i++) {
		if ((fd = open(argv[i], 0)) < 0) {
			complain("more", argv[i]);
			continue;
		}
		i = show(fd) ? argc : i;
		close(fd);
	}
	return 0;
}
