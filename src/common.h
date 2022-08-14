#ifndef COMMON_H_INCLUDED
#define COMMON_H_INCLUDED

#include <stddef.h>

void *loadfile(const char *fn, size_t *sz);
void *memdup(const void *mem, size_t sz);

#endif /* COMMON_H_INCLUDED */
