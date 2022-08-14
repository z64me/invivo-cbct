#ifndef COMMON_H_INCLUDED
#define COMMON_H_INCLUDED

#include <stddef.h>

void *loadfile(const char *fn, size_t *sz);
void *memdup(const void *mem, size_t sz);
void *memduppad(const void *mem, size_t sz, size_t padbytes);
void *memchr(const void *mem, int c, size_t memSz);
void *memmem(const void *hay, size_t haySz, const void *needle, size_t needleSz);
void *memstr(const void *hay, size_t haySz, const char *needle);

#endif /* COMMON_H_INCLUDED */
