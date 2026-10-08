/* stdlib.h - numbers from text, the heap, a random sequence, the end.
 *
 * The heap is the memory above the program's stack, taken from the
 * monitor by .SETTOP as it is needed; malloc and free keep a list of
 * what was given back. */

#ifndef STDLIB_H
#define STDLIB_H

#include <stddef.h>

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#define RAND_MAX 32767

int abs(int n);
long labs(long n);
int atoi(const char *s);
long atol(const char *s);
/* A sign, then the digits of `base` (2..36; 0 means 10, or 8 after a 0,
 * or 16 after 0x); *end receives where they stopped. */
long strtol(const char *s, char **end, int base);
unsigned long strtoul(const char *s, char **end, int base);

void *malloc(size_t size);
void *calloc(size_t count, size_t size);
void *realloc(void *block, size_t size);
void free(void *block);

int rand(void);
void srand(unsigned seed);

void exit(int status) __attribute__((noreturn));
void abort(void) __attribute__((noreturn));

#endif
