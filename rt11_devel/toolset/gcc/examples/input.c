/* INPUT - the console's input through getchar: a line typed (the
 * harness types it) read up to its '\n', its length and its characters
 * backwards printed, then a character by rt11_ttyin itself. */

#include <rt11.h>
#include <stdio.h>

int main(void)
{
	char line[40];
	int n = 0, c;

	puts("TYPE A LINE");
	while ((c = getchar()) != '\n' && n < 39)
		line[n++] = (char)c;
	line[n] = 0;
	printf("INPUT %d:", n);
	while (n > 0)
		putchar(line[--n]);
	putchar('\n');
	c = rt11_ttyin();
	printf("TTYIN %d\n", c);
	puts("INPUT DONE");
	return 0;
}
