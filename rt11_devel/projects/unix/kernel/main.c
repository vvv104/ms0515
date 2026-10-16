/* main.c - the kernel's start: the vectors, the clock, the console, the
 * root file system, and the first process, /etc/init, run by exec. */
#include "kernel.h"
#include "rom.h"

void panic(const char *why)
{
	tty_puts("\npanic: ");
	tty_puts(why);
	tty_puts("\n");
	for (;;)
		;
}

int main(void)
{
	static const char *const argv[] = {"init", 0};

	VECTOR(034, trap_entry, 0);		/* TRAP, priority 0 */
	VECTOR(0130, tty_entry, 0340);		/* the keyboard */
	clock_start();
	if (fs_mount() != 0)
		panic("no file system");
	exec("/etc/init", argv);
	panic("no /etc/init");
	return 0;
}
