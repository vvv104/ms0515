/* FILES - RT-11's files through rt11.h, put to the test: FILES.DAT
 * beside the program (the harness writes it: block n's word i is
 * n*256+i) opened and measured, a block read and summed, another read
 * without waiting and summed after the wait, the end of the file met;
 * OUT.DAT made of two blocks the harness knows; a file that is not there
 * looked up.  A line a step, for ../tests. */

#include <rt11.h>
#include <stdio.h>

#define IN 1
#define OUT 2

static unsigned buffer[256];

static unsigned sum(void)
{
	unsigned s = 0;
	int i;

	for (i = 0; i < 256; i++)
		s += buffer[i];
	return s;
}

int main(void)
{
	unsigned spec[4];
	int n, i, spins = 0;

	rt11_filespec(spec, "FILES.DAT");
	n = rt11_lookup(IN, spec);
	printf("FILES: %d blocks\n", n);
	if (n < 0)
		return 1;

	n = rt11_readw(IN, 1, buffer, 256);
	printf("READW %d %u\n", n, sum());

	n = rt11_read(IN, 2, buffer, 256);
	while (rt11_wait(IN) != 0 && spins < 30000)	/* never: .WAIT waits */
		spins++;
	printf("READ %d %u\n", n, sum());

	n = rt11_readw(IN, 3, buffer, 256);
	printf("EOF %d %d\n", n, rt11_error());
	rt11_close(IN);

	rt11_filespec(spec, "DK:OUT.DAT");
	n = rt11_enter(OUT, spec, 2);
	for (i = 0; i < 256; i++)
		buffer[i] = 0xA500 + (unsigned)i;
	rt11_writew(OUT, 0, buffer, 256);
	for (i = 0; i < 256; i++)
		buffer[i] = 0x5A00 + (unsigned)i;
	rt11_writew(OUT, 1, buffer, 256);
	printf("ENTER %d %d\n", n, rt11_close(OUT));

	rt11_filespec(spec, "NOFILE.DAT");
	n = rt11_lookup(IN, spec);
	printf("NOFILE %d %d\n", n, rt11_error());
	puts("FILES DONE");
	return 0;
}
