/* main.c - the kernel, stage 1: it is loaded and says so. */
#include "rom.h"

static void puts_rom(const char *s)
{
	while (*s)
		rom_putc(*s++);
}

int main(void)
{
	puts_rom("\r\n\r\nlogin: ");
	for (;;)
		;
}
