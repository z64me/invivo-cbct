#include "palette.h"

#include <assert.h>
#include <string.h>
#include <ctype.h>

/* make copy of string without spaces */
static const char *lowernospaces(char *buf, const char *src)
{
	char *dst = buf;
	
	assert(src);
	
	for (dst = buf; *src; ++src)
	{
		if (isspace(*src))
			continue;
		
		*dst = tolower(*src);
		++dst;
	}
	*dst = '\0';
	
	return buf;
}

struct palette
{
	const char *name;
	const uint8_t data[256 * 3 + 1]; // 3 bytes per color + zero terminator
};

#define COUNT (int)(sizeof(palette_arr) / sizeof(palette_arr[0]))

static struct palette palette_arr[] = {
	#include "palette_data.h"
};

int palette_count(void)
{
	return COUNT;
}

void palette_color(uint8_t dst[3], int palette, int index)
{
	const uint8_t *src;
	
	assert(palette >= (int)0);
	assert(palette < (int)COUNT);
	assert(index >= (int)0);
	assert(index < (int)256);
	
	src = palette_arr[palette].data + index * 3;
	
	dst[0] = src[0];
	dst[1] = src[1];
	dst[2] = src[2];
}

const char *palette_name(int palette)
{
	if (palette < 0)
		return "None";
	
	assert(palette >= (int)0);
	assert(palette < (int)COUNT);
	
	return palette_arr[palette].name;
}

/* find a palette with a given name
 * returns palette index (value >= 0) on successful match
 * returns -1 if name == 'none'
 * returns value < -1 if no match found
 */
int palette_find(const char *name)
{
	char work[512];
	char name_edit[512];
	int i;
	
	assert(name);
	
	/* make name lowercase without spaces */
	lowernospaces(name_edit, name);
	
	/* no palette */
	if (!strcmp(name_edit, "none"))
		return -1;
	
	for (i = 0; i < COUNT; ++i)
	{
		/* compare lowercase versions of each string without spaces */
		if (!strcmp(lowernospaces(work, palette_arr[i].name), name_edit))
			return i;
	}
	
	/* error */
	return -2;
}
