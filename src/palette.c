#include "palette.h"

#include <assert.h>

struct palette
{
	const char *name;
	const uint8_t data[256 * 3 + 1]; // 3 bytes per color + zero terminator
};

#define COUNT (sizeof(palette_arr) / sizeof(palette_arr[0]))

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
	assert(palette >= (int)0);
	assert(palette < (int)COUNT);
	
	return palette_arr[palette].name;
}
