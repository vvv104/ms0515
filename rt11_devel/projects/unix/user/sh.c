/* sh.c - the shell: a command line at a time, its words cut at the spaces,
 * run as children - fork, exec, wait - and the prompt again.
 *
 *	a | b | c	a pipeline: each command's output the next one's input,
 *			one command after another through a file in /tmp
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
 * end - init runs /etc/rc so; typed at the shell, the shell runs the file
 * itself, not a shell of its own: a command of the file needs the other
 * slot, so a cd there stays.  The session's shell - pid 1, which /etc/rc
 * gives its place to and the kernel runs anew when a session ends - starts
 * in /home; one started by hand stays where it was started.
 *
 * The machine keeps two processes at once, the shell and a command: a
 * pipeline's commands run one after another, the output of each a file
 * the next one reads, so a pipeline is not put in the background; while a
 * command runs in the background the next finds no room. */
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
static void child(char **word, int in, int out)
{
	char path[PATHMAX];

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

/* The command run as a child on in and out, which are closed here; waited
 * for unless `background`, its status then the last.  0, or -1 when there
 * was no room for it. */
static int start(char **word, int in, int out, int background)
{
	int pid = fork(), got, status = 0;

	if (pid == 0)
		child(word, in, out);
	if (in != 0)
		close(in);
	if (out != 1)
		close(out);
	if (pid < 0) {
		complain("sh", word[0]);
		return -1;
	}
	if (background) {
		say("[");
		putnum((unsigned)pid);
		say("]\n");
		return 0;
	}
	while ((got = wait(&status)) >= 0 && got != pid)
		;
	last_status = status;
	return 0;
}

/* The commands cmd[0..n-1] as a pipeline: one after another, each one's
 * output a file of /tmp the next one reads; the files gone after. */
static void run(char **cmd[], int n, int background)
{
	static const char *const pipes[] = {"/tmp/.p0", "/tmp/.p1"};
	int i, in = 0, out = 1;

	if (n > 1 && background) {
		puts_fd(2, "sh: a pipeline in the background: no room\n");
		return;
	}
	for (i = 0; i < n; i++) {
		if (i > 0 && (in = open(pipes[(i - 1) & 1], 0)) < 0) {
			complain("sh", pipes[(i - 1) & 1]);
			break;
		}
		if (i < n - 1 && (out = creat(pipes[i & 1])) < 0) {
			complain("sh", pipes[i & 1]);
			if (in != 0)
				close(in);
			break;
		}
		if (start(cmd[i], in, i < n - 1 ? out : 1, background) < 0)
			break;
	}
	if (n > 1) {
		unlink(pipes[0]);
		unlink(pipes[1]);
	}
}

/* What lines are read from: a descriptor and a buffer of its own, a
 * script's not to spoil its caller's. */
struct input {
	int fd, have, at;
	char buf[128];
};

/* The next line of the input into line, its '\n' taken off, '#' and on cut
 * away; -1 at the end.  A terminal's read gives a line at a time; a file's
 * is read a buffer ahead. */
static int next_line(struct input *in, char *line, int size)
{
	int n = 0;
	char c;

	for (;;) {
		if (in->at == in->have) {
			in->at = 0;
			in->have = read(in->fd, in->buf, sizeof in->buf);
			if (in->have <= 0) {
				in->have = 0;
				if (n == 0)
					return -1;
				break;
			}
		}
		c = in->buf[in->at++];
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

static int interpret(char *line);

/* The commands of the file, one line after another, in this process: a
 * shell of its own would take the slot its commands need.  1 when one of
 * them was exit. */
static int source(const char *path)
{
	struct input in;
	char line[128];
	int done = 0;

	if ((in.fd = open(path, 0)) < 0) {
		complain("sh", path);
		return 0;
	}
	in.have = in.at = 0;
	while (!done && next_line(&in, line, sizeof line) >= 0)
		done = interpret(line);
	close(in.fd);
	return done;
}

/* The shell's own command among the words, done: 1, or 0 when it is not
 * one.  exit leaves *done set. */
static int own(char **word, int n, int *done)
{
	if (!strcmp(word[0], "exit")) {
		*done = 1;
		return 1;
	}
	if (!strcmp(word[0], "exec") && n > 1) {
		char path[PATHMAX];

		pathof(word[1], path);
		execv(path, word + 1);
		complain("sh", word[1]);
		return 1;
	}
	if (!strcmp(word[0], "cd")) {		/* the shell's own directory, and its children's */
		if (chdir(n > 1 ? word[1] : HOME) < 0)
			complain("sh", n > 1 ? word[1] : HOME);
		return 1;
	}
	if (!strcmp(word[0], "sh") && n == 2) {	/* a script: here, not in a child */
		*done = source(word[1]);
		return 1;
	}
	if (!strcmp(word[0], "t")) {
		say("time ");
		putnum((unsigned)time());
		say("\n");
		return 1;
	}
	if (!strcmp(word[0], "status")) {
		say("status ");
		putnum((unsigned)last_status);
		say("\n");
		return 1;
	}
	return 0;
}

/* A line's commands done: 1 when the shell is to end. */
static int interpret(char *line)
{
	char *word[MAXARGS + MAXCMDS + 1], **cmd[MAXCMDS];
	int n, i, ncmd, background = 0, done = 0;

	n = words(line, word, MAXARGS + MAXCMDS);
	word[n] = 0;
	if (n == 0 || own(word, n, &done))
		return done;
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
			return 0;
		}
	run(cmd, ncmd, background);
	return 0;
}

int main(int argc, char **argv)
{
	struct input in;
	char line[128];

	if (argc > 1) {
		source(argv[1]);
		return 0;
	}
	if (getpid() == 1)
		chdir(HOME);			/* none: the root it is */
	in.fd = 0;
	in.have = in.at = 0;
	for (;;) {
		say("# ");			/* not $: the ROM's font has the currency sign there */
		if (next_line(&in, line, sizeof line) < 0 || interpret(line))
			return 0;
	}
}
