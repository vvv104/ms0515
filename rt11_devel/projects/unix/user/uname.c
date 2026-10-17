/* uname.c - /bin/uname: the system's name and version, as the kernel says
 * it at the boot (kernel/version.h). */
#include "unistd.h"
#include "../kernel/version.h"

int main(void)
{
	puts_fd(1, UBIX "\n");
	return 0;
}
