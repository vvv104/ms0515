/* sh.c - the shell, stage 4a: a command a line, its words cut at the
 * spaces, run as a child - fork, exec, wait - and the prompt again.
 *
 * A command without a '/' is looked for in /bin.  Its own commands:
 *
 *	t		the time since the boot
 *	status		the exit status of the last command
 *	exit		the shell's end - and the session's
 *
 * Pipes and commands left running in the background come with swap: the
 * machine keeps two processes at once, the shell and its command. */
#include "unistd.h"

#define MAXARGS 16
#define PATHMAX 64

static int last_status;

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

static void say(const char *s)
{
	puts_fd(1, s);
}

/* The command's path: as given with a '/', else in /bin. */
static void pathof(const char *name, char *path)
{
	const char *s;
	int i = 0;

	for (s = name; *s && *s != '/'; s++)
		;
	if (!*s) {
		for (s = "/bin/"; *s; s++)
			path[i++] = *s;
	}
	for (s = name; *s && i < PATHMAX - 1; s++)
		path[i++] = *s;
	path[i] = 0;
}

static void run(char **word)
{
	char path[PATHMAX];
	int pid, status;

	pathof(word[0], path);
	pid = fork();
	if (pid < 0) {
		complain("sh", word[0]);
		return;
	}
	if (pid == 0) {
		execv(path, word);
		puts_fd(2, word[0]);
		puts_fd(2, errno == ENOENT ? ": not found\n" : ": cannot run\n");
		exit(127);
	}
	while (wait(&status) != pid)
		;
	last_status = status;
}

int main(void)
{
	char line[128], *word[MAXARGS + 1];
	int n;

	for (;;) {
		say("$ ");
		n = read(0, line, sizeof line - 1);
		if (n <= 0)
			exit(0);
		if (line[n - 1] == '\n')
			n--;
		line[n] = 0;
		n = words(line, word, MAXARGS);
		word[n] = 0;
		if (n == 0)
			continue;
		if (!strcmp(word[0], "exit"))
			exit(0);
		if (!strcmp(word[0], "t")) {
			say("time ");
			putnum((unsigned)time());
			say("\n");
		} else if (!strcmp(word[0], "status")) {
			say("status ");
			putnum((unsigned)last_status);
			say("\n");
		} else {
			run(word);
		}
	}
}
