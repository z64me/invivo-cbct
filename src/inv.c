#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"

struct inv
{
	void *data;
	size_t dataSz;
};

void inv_free(struct inv *inv)
{
	if (!inv)
		return;
	
	if (inv->data)
		free(inv->data);
	
	free(inv);
}

struct inv *inv_parse(const void *src, size_t srcSz)
{
	struct inv *inv;
	
	if (!(inv = calloc(1, sizeof(*inv))))
		return 0;
	
	/* make a copy of the provided data */
	if (!(inv->data = memdup(src, srcSz)))
		goto L_fail;
	inv->dataSz = srcSz;
	
	return inv;
	
L_fail:
	inv_free(inv);
	return 0;
}

struct inv *inv_load(const char *fn)
{
	struct inv *inv;
	void *data;
	size_t dataSz;
	
	/* load inv file */
	if (!(data = loadfile(fn, &dataSz)))
	{
		fprintf(stderr, "failed to load invivo file '%s'\n", fn);
		return 0;
	}
	
	/* parse inv file */
	if (!(inv = inv_parse(data, dataSz)))
	{
		fprintf(stderr, "failed to parse invivo file '%s'\n", fn);
		return 0;
	}
	
	/* cleanup */
	free(data);
	
	return inv;
}
