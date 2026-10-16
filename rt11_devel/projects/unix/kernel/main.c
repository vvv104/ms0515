/* main.c - the kernel's start: the vectors, the clock, the console, and
 * the first process.
 *
 * Stage 2 has one process, init, the program the superblock names (words 7
 * and 8: its first block and its blocks), loaded into slot A at 040000 and
 * entered there with its stack at the slot's top. */
#include "kernel.h"
#include "rom.h"

#define SB_MAGIC 054125			/* 'U', 'X' */

static unsigned superblock[256];

static void panic(const char *why)
{
	tty_puts("\npanic: ");
	tty_puts(why);
	tty_puts("\n");
	for (;;)
		;
}

int main(void)
{
	unsigned i;

	*(volatile unsigned *)034 = (unsigned)trap_entry;	/* TRAP, priority 0 */
	*(volatile unsigned *)036 = 0;
	*(volatile unsigned *)0130 = (unsigned)tty_entry;	/* the keyboard */
	*(volatile unsigned *)0132 = 0340;
	clock_start();

	if (rom_bread(1, superblock) != 0 || superblock[0] != SB_MAGIC)
		panic("no superblock");
	if (superblock[8] == 0 || superblock[8] > (USER_TOP - USER_BASE) / 512)
		panic("no init");
	for (i = 0; i < superblock[8]; i++)
		if (rom_bread(superblock[7] + i, (void *)(USER_BASE + i * 512)) != 0)
			panic("init unread");
	enter(USER_BASE, USER_TOP);
	return 0;
}
