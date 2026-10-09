/* HELLO - the least a C program on the MS 0515 is: a line on the console
 * by the monitor's .PRINT, and back to the monitor. */

#include <rt11.h>

int main(void)
{
	rt11_print("HELLO FROM GCC");
	return 0;
}
