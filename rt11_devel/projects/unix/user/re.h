/* re.h - the patterns of grep and ed (re.c). */
#ifndef UNIX_RE_H
#define UNIX_RE_H

/* The first match of the pattern in text: its end, its start at *start;
 * 0 for none.  ^ is the text's start, $ its end, . any character, x* any
 * number of x - the most it can - \x the x itself; the rest stands for
 * itself. */
const char *re_match(const char *re, const char *text, const char **start);

#endif
