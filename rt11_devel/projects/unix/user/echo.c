/* echo.c - /bin/echo: its arguments, a space between, on a line. */
#include "unistd.h"

int main(int argc, char **argv)
{
	int i;

	for (i = 1; i < argc; i++) {
		if (i > 1)
			puts_fd(1, " ");
		puts_fd(1, argv[i]);
	}
	puts_fd(1, "\n");
	return 0;
}
