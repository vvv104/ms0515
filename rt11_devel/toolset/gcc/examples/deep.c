/* DEEP - the stack's room asked for with STACK (../CMakeLists.txt:
 * 4096 bytes): a recursion 120 deep with a frame of some 26 bytes, 3 KB
 * of stack, which the default kilobyte would not hold.  The sum of the
 * depths comes back: "DEEP 7260". */

#include <stdio.h>

static int deep(int n)
{
	volatile int pad[10];

	pad[0] = n;
	if (n == 0)
		return 0;
	return deep(n - 1) + pad[0];
}

int main(void)
{
	printf("DEEP %d\n", deep(120));
	return 0;
}
