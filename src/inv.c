#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"

struct inv
{
	void *data;
	size_t dataSz;
	
	/* xml vars */
	void *AppendedData;
	size_t AppendedDataSz;
};

void inv_free(struct inv *inv)
{
	if (!inv)
		return;
	
	if (inv->data)
		free(inv->data);
	
	if (inv->AppendedData)
		free(inv->AppendedData);
	
	free(inv);
}

struct inv *inv_parse(const void *src, size_t srcSz)
{
	struct inv *inv;
	void *data;
	size_t dataSz;
	
	if (!(inv = calloc(1, sizeof(*inv))))
		return 0;
	
	/* make a copy of the provided data */
	if (!(inv->data = data = memduppad(src, srcSz, 1)))
		goto L_fail;
	inv->dataSz = dataSz = srcSz;
	
	/* find, copy, and strip AppendedData
	 * XXX this should take place BEFORE parsing as XML because
	 *     INV files contain non-XML-compliant data initially
	 */
	{
		const char *tag = "AppendedData";
		const char *tagEnd = "</AppendedData>";
		char *data8 = data;
		char *start;
		char *end;
		size_t sz;
		
		/* start = first byte after pattern '<AppendedData...>' */
		start = memstr(data, dataSz, tag);
		if (!start)
		{
			fprintf(stderr, "failed to locate '%s' tag\n", tag);
			goto L_fail;
		}
		if (!(start = strchr(start, '>')))
		{
			fprintf(stderr, "'%s' tag: no '>' found\n", tag);
			goto L_fail;
		}
		start += 1;
		
		/* end = first byte of pattern '</AppendedData>' */
		if (!(end = memstr(start, dataSz - (start - data8), tagEnd)))
		{
			fprintf(stderr, "failed to locate '%s' tag\n", tagEnd);
			goto L_fail;
		}
		
		/* sz = difference between start and end */
		sz = end - start;
		
		/* copy */
		inv->AppendedData = memdup(start, sz);
		inv->AppendedDataSz = sz;
		
		/* strip */
		memmove(start, end, strlen(end) + 1);
	}
	
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
