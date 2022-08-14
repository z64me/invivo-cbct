#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"

/* duplicate a memory block */
void *memdup(const void *mem, size_t sz)
{
	void *result;
	
	if (!mem || !sz)
		return 0;
	
	if (!(result = malloc(sz)))
		return 0;
	
	return memcpy(result, mem, sz);
}

/* minimal file loader
 * returns 0 on failure
 * returns pointer to loaded file on success
 */
void *loadfile(const char *fn, size_t *sz)
{
	FILE *fp;
	void *dat;
	
	/* rudimentary error checking returns 0 on any error */
	if (
		!fn
		|| !sz
		|| !(fp = fopen(fn, "rb"))
		|| fseek(fp, 0, SEEK_END)
		|| !(*sz = ftell(fp))
		|| fseek(fp, 0, SEEK_SET)
		|| !(dat = malloc(*sz))
		|| fread(dat, 1, *sz, fp) != *sz
		|| fclose(fp)
	)
		return 0;
	
	return dat;
}
