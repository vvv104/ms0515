/* sync.c - /bin/sync: the cache written out (the kernel writes through, so
 * it has nothing left to write; the call is there for what is to come). */
#include "unistd.h"

int main(void)
{
	sync();
	return 0;
}
