#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "common.h"

/* read little-endian encoded u32 */
uint32_t LEu32(const void *ptr)
{
	const uint8_t *b = ptr;
	
	assert(b);
	
	return (b[3] << 24) | (b[2] << 16) | (b[1] << 8) | (b[0]);
}

/* read big-endian encoded u32 */
uint32_t BEu32(const void *ptr)
{
	const uint8_t *b = ptr;
	
	assert(b);
	
	return (b[0] << 24) | (b[1] << 16) | (b[2] << 8) | (b[3]);
}

/* write little-endian encoded u32 to file */
void fputLEu32(const uint32_t v, FILE *fp)
{
	fputc(v, fp);
	fputc(v >> 8, fp);
	fputc(v >> 16, fp);
	fputc(v >> 24, fp);
}

/* memchr copy-pasted from Android Bionic
 * https://android.googlesource.com/platform/bionic/+/ics-mr0/libc/string/memchr.c
 */
void *memchr(const void *mem, int c, size_t memSz)
{
	const unsigned char *p = mem;
	const unsigned char *end = p + memSz;
	for (;;) {
		if (p >= end || p[0] == c) break;
		p++;
		if (p >= end || p[0] == c) break;
		p++;
		if (p >= end || p[0] == c) break;
		p++;
		if (p >= end || p[0] == c) break;
		p++;
	}
	if (p >= end)
		return 0;
	else
		return (void*) p;
}

/* memmem copy-pasted from Android Bionic
 * https://android.googlesource.com/platform/bionic/+/ics-mr0/libc/string/memmem.c
 */
void *memmem(const void *hay, size_t haySz, const void *needle, size_t needleSz)
{
	size_t n = haySz;
	size_t m = needleSz;
	if (m > n || !m || !n)
		return 0;
	if (m > 1) {
		const unsigned char *y = (const unsigned char*) hay;
		const unsigned char *x = (const unsigned char*) needle;
		size_t j = 0;
		size_t k = 1, l = 2;
		if (x[0] == x[1]) {
			k = 2;
			l = 1;
		}
		while (j <= n-m) {
			if (x[1] != y[j+1]) {
				j += k;
			} else {
				if (!memcmp(x+2, y+j+2, m-2) && x[0] == y[j])
					return (void*) &y[j];
				j += l;
			}
		}
	} else {
		/* degenerate case */
		return memchr(hay, ((unsigned char*)needle)[0], n);
	}
	return 0;
}

/* find a string within a memory block */
void *memstr(const void *hay, size_t haySz, const char *needle)
{
	if (!needle)
		return 0;
	
	return memmem(hay, haySz, needle, strlen(needle));
}

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

/* duplicate a memory block with padding appended */
void *memduppad(const void *mem, size_t sz, size_t padbytes)
{
	void *result;
	
	if (!mem || !sz)
		return 0;
	
	if (!(result = malloc(sz + padbytes)))
		return 0;
	
	if (padbytes)
		memset(((unsigned char *)result) + sz, 0, padbytes);
	
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

/* minimal file writer
 * returns 0 on failure
 * returns non-zero on success
 */
int savefile(const char *fn, const void *dat, const size_t sz)
{
	FILE *fp;
	
	/* rudimentary error checking returns 0 on any error */
	if (
		!fn
		|| !sz
		|| !dat
		|| !(fp = fopen(fn, "wb"))
		|| fwrite(dat, 1, sz, fp) != sz
		|| fclose(fp)
	)
		return 0;
	
	return 1;
}
