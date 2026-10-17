/* clear.c - /bin/clear: the screen wiped, the cursor at its top - VT52's
 * ESC H and ESC J, which the ROM's console takes. */
#include "unistd.h"

int main(void)
{
	puts_fd(1, "\033H\033J");
	return 0;
}
