/* hello.c - /bin/hello: the first program loaded by exec. */
#include "unistd.h"

int main(void)
{
	puts_fd(1, "hello, world\n");
	return 0;
}
