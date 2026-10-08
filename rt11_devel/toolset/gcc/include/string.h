/* string.h - bytes and C strings. */

#ifndef STRING_H
#define STRING_H

#include <stddef.h>

void *memcpy(void *to, const void *from, size_t n);
void *memmove(void *to, const void *from, size_t n);
void *memset(void *block, int c, size_t n);
int memcmp(const void *a, const void *b, size_t n);
void *memchr(const void *block, int c, size_t n);

size_t strlen(const char *s);
char *strcpy(char *to, const char *from);
char *strncpy(char *to, const char *from, size_t n);
char *strcat(char *to, const char *from);
char *strncat(char *to, const char *from, size_t n);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
char *strstr(const char *s, const char *needle);

#endif
