/* sh.c - the shell: a command line at a time, its words cut at the spaces,
 * run as children - fork, exec, wait - and the prompt again.
 *
 *	a | b | c	a pipeline: each command's output the next one's input
 *	cmd &		not waited for: its pid is told, the prompt comes at once
 *	cmd > file	the output into the file, made or emptied
 *	cmd < file	the input from the file
 *
 * A command without a '/' is looked for in /bin; '#' to the line's end is a
 * comment.  Its own commands:
 *
 *	cd [DIR]	the shell's own directory, and its children's; /home alone
 *	t		the time since the boot
 *	status		the exit status of the last command waited for
 *	exec CMD ARGS	the command in the shell's place
 *	exit		the shell's end - and the session's
 *
 * sh FILE runs the commands of the file, without a prompt, and ends at its
 * end - init runs /etc/rc so.  The session's shell - pid 1, which /etc/rc
 * gives its place to and the kernel runs anew when a session ends - starts
 * in /home; one started by hand stays where it was started.
 *
 * The machine keeps two processes at once; more, a pipeline's or one in
 * the background, need the kernel's swap, and without it the fork that
 * finds no room is reported and the rest of the line given up. */
#include "unistd.h"

#define MAXARGS 16
#define MAXCMDS 6
#define PATHMAX 64
#define HOME "/home"

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

/* The command's words cleared of "> file" and "< file", the descriptor
 * opened for each put at *in or *out; 0, or -1 with the error told. */
static int redirect(char **word, int *in, int *out)
{
	char **w, **to;
	int fd;

	for (w = to = word; *w; w++) {
		if (!strcmp(*w, ">>") && w[1]) {	/* to the end, the file made if need be */
			if ((fd = open(w[1], 1)) < 0)
				fd = creat(w[1]);
			if (fd < 0 || lseek(fd, 0L, 2) < 0) {
				complain("sh", w[1]);
				return -1;
			}
			*out = fd;
			w++;
			continue;
		}
		if ((!strcmp(*w, ">") || !strcmp(*w, "<")) && w[1]) {
			fd = **w == '>' ? creat(w[1]) : open(w[1], 0);
			if (fd < 0) {
				complain("sh", w[1]);
				return -1;
			}
			if (**w == '>')
				*out = fd;
			else
				*in = fd;
			w++;
			continue;
		}
		*to++ = *w;
	}
	*to = 0;
	return 0;
}

/* The child's part: its input and output put in place, the program run. */
static void child(char **word, int in, int out, int other)
{
	char path[PATHMAX];

	if (other >= 0)
		close(other);
	if (redirect(word, &in, &out) < 0 || !word[0])
		exit(1);

	if (in != 0) {
		close(0);
		dup(in);
		close(in);
	}
	if (out != 1) {
		close(1);
		dup(out);
		close(out);
	}
	pathof(word[0], path);
	execv(path, word);
	puts_fd(2, word[0]);
	puts_fd(2, errno == ENOENT ? ": not found\n" : errno == EACCES ? ": not allowed\n" : ": cannot run\n");
	exit(127);
}

/* The commands cmd[0..n-1] as a pipeline; waited for unless `background`. */
static void run(char **cmd[], int n, int background)
{
	int pid[MAXCMDS], started = 0, in = 0, fd[2], i, status, left, got;

	for (i = 0; i < n; i++) {
		fd[0] = -1;
		fd[1] = 1;
		if (i < n - 1 && pipe(fd) < 0) {
			complain("sh", "pipe");
			break;
		}
		pid[i] = fork();
		if (pid[i] < 0) {
			complain("sh", cmd[i][0]);
			if (fd[0] >= 0) {
				close(fd[0]);
				close(fd[1]);
			}
			break;
		}
		if (pid[i] == 0)
			child(cmd[i], in, fd[1], fd[0]);
		started++;
		if (in != 0)
			close(in);
		if (fd[1] != 1)
			close(fd[1]);
		in = fd[0];
	}
	if (in > 0)
		close(in);
	if (background) {
		for (i = 0; i < started; i++) {
			say("[");
			putnum((unsigned)pid[i]);
			say("]\n");
		}
		return;
	}
	for (left = started; left > 0;) {
		got = wait(&status);
		if (got < 0)
			break;
		for (i = 0; i < started; i++)
			if (pid[i] == got) {
				left--;
				if (i == started - 1)
					last_status = status;
			}
	}
}

/* The next line of fd into line, its '\n' taken off, '#' and on cut away;
 * -1 at the end.  A terminal's read gives a line at a time; a file's is
 * read a buffer ahead. */
static int next_line(int fd, char *line, int size)
{
	static char buf[128];
	static int have, at;
	int n = 0;
	char c;

	for (;;) {
		if (at == have) {
			at = 0;
			have = read(fd, buf, sizeof buf);
			if (have <= 0) {
				have = 0;
				if (n == 0)
					return -1;
				break;
			}
		}
		c = buf[at++];
		if (c == '\n')
			break;
		if (n < size - 1)
			line[n++] = c;
	}
	line[n] = 0;
	for (n = 0; line[n] && line[n] != '#'; n++)
		;
	line[n] = 0;
	return n;
}

int main(int argc, char **argv)
{
	char line[128], *word[MAXARGS + MAXCMDS + 1], **cmd[MAXCMDS];
	int n, i, ncmd, background, script = 0, from = 0;

	if (argc > 1) {
		if ((from = open(argv[1], 0)) < 0) {
			complain("sh", argv[1]);
			exit(1);
		}
		script = 1;
	} else if (getpid() == 1) {
		chdir(HOME);			/* none: the root it is */
	}
	for (;;) {
		if (!script)
			say("# ");			/* not $: the ROM's font has the currency sign there */
		if (next_line(from, line, sizeof line) < 0)
			exit(0);
		n = words(line, word, MAXARGS + MAXCMDS);
		word[n] = 0;
		if (n == 0)
			continue;
		if (!strcmp(word[0], "exit"))
			exit(0);
		if (!strcmp(word[0], "exec") && n > 1) {
			char path[PATHMAX];

			pathof(word[1], path);
			execv(path, word + 1);
			complain("sh", word[1]);
			continue;
		}
		if (!strcmp(word[0], "cd")) {		/* the shell's own directory, and its children's */
			if (chdir(n > 1 ? word[1] : HOME) < 0)
				complain("sh", n > 1 ? word[1] : HOME);
			continue;
		}
		if (!strcmp(word[0], "t")) {
			say("time ");
			putnum((unsigned)time());
			say("\n");
			continue;
		}
		if (!strcmp(word[0], "status")) {
			say("status ");
			putnum((unsigned)last_status);
			say("\n");
			continue;
		}
		background = 0;
		if (!strcmp(word[n - 1], "&")) {
			background = 1;
			word[--n] = 0;
		}
		/* The pipeline's commands: the words cut at each "|". */
		ncmd = 0;
		cmd[ncmd++] = word;
		for (i = 0; i < n; i++)
			if (!strcmp(word[i], "|") && ncmd < MAXCMDS) {
				word[i] = 0;
				cmd[ncmd++] = &word[i + 1];
			}
		for (i = 0; i < ncmd; i++)
			if (!cmd[i][0]) {
				puts_fd(2, "sh: an empty command in the pipe\n");
				ncmd = 0;
			}
		if (ncmd)
			run(cmd, ncmd, background);
	}
}
