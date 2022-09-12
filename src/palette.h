#ifndef PALETTE_H_INCLUDED
#define PALETTE_H_INCLUDED

#include <stdint.h>

int palette_count(void);
void palette_color(uint8_t dst[3], int palette, int index);
const char *palette_name(int palette);

#endif /* PALETTE_H_INCLUDED */
