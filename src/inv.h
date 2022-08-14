#ifndef INV_H_INCLUDED
#define INV_H_INCLUDED

struct inv;

struct inv *inv_parse(const void *src, size_t srcSz);
struct inv *inv_load(const char *fn);
void inv_free(struct inv *inv);

#endif /* INV_H_INCLUDED */
