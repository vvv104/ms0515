/* pid.c - /bin/pid: "pid N parent P", its own process id and its parent's. */
#include "unistd.h"

int main(void)
{
	puts_fd(1, "pid ");
	putnum((unsigned)getpid());
	puts_fd(1, " parent ");
	putnum((unsigned)getppid());
	puts_fd(1, "\n");
	return 0;
}
