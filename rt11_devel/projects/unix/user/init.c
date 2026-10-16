/* init.c - the first process: it asks for a name, greets it, and takes
 * commands a line at a time until the shell comes:
 *
 *	t		the time since the boot
 *	ls [DIR]	the names in a directory, the root's by default
 *	cat FILE	the file on the console
 *	run PROG [ARG...]	the program in init's place; when it exits, the
 *			kernel starts init again
 *
 * anything else is answered with a question mark. */
#include "unistd.h"

#define MAXARGS 8

static void say(const char *s)
{
	puts_fd(1, s);
}

/* A line read, its '\n' taken off. */
static void readline(char *buf, int size)
{
	int n = read(0, buf, size - 1);

	if (n < 0)
		n = 0;
	if (n > 0 && buf[n - 1] == '\n')
		n--;
	buf[n] = 0;
}

/* The words of line, cut in place at the spaces: how many. */
static int words(char *line, char **word, int max)
{
	int n = 0;

	while (*line && n < max) {
		while (*line == ' ')
			*line++ = 0;
		if (!*line)
			break;
		word[n++] = line;
		while (*line && *line != ' ')
			line++;
	}
	return n;
}

static void complain(const char *command, const char *name)
{
	say(command);
	say(": ");
	say(name);
	say(errno == ENOENT ? ": not found\n" : errno == ENOTDIR ? ": not a directory\n" : ": cannot\n");
}

static void ls(const char *dir)
{
	struct {
		unsigned ino;
		char name[14];
	} d;
	char name[15];
	int fd = open(dir, 0), first = 1;

	if (fd < 0) {
		complain("ls", dir);
		return;
	}
	while (read(fd, &d, sizeof d) == sizeof d) {
		if (!d.ino || d.name[0] == '.')
			continue;
		memcpy(name, d.name, 14);
		name[14] = 0;
		if (!first)
			say("  ");
		say(name);
		first = 0;
	}
	say("\n");
	close(fd);
}

static void cat(const char *path)
{
	char buf[64];
	int fd = open(path, 0), n;

	if (fd < 0) {
		complain("cat", path);
		return;
	}
	while ((n = read(fd, buf, sizeof buf)) > 0)
		write(1, buf, n);
	close(fd);
}

int main(void)
{
	char line[64], *word[MAXARGS + 1];
	int n;

	say("\n\nlogin: ");
	readline(line, sizeof line);
	say("Hello, ");
	say(line);
	say("\n");
	for (;;) {
		say("$ ");
		readline(line, sizeof line);
		n = words(line, word, MAXARGS);
		word[n] = 0;
		if (n == 0)
			continue;
		if (!strcmp(word[0], "t") && n == 1) {
			say("time ");
			putnum((unsigned)time());
			say("\n");
		} else if (!strcmp(word[0], "ls")) {
			ls(n > 1 ? word[1] : "/");
		} else if (!strcmp(word[0], "cat") && n == 2) {
			cat(word[1]);
		} else if (!strcmp(word[0], "run") && n >= 2) {
			execv(word[1], word + 1);
			complain("run", word[1]);
		} else {
			say(word[0]);
			say("?\n");
		}
	}
}
