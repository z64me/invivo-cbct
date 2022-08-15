#ifndef INV_H_INCLUDED
#define INV_H_INCLUDED

struct inv;

const void *inv_get_gray(struct inv *inv, int *w, int *h, int *num);
int inv_dump(struct inv *inv, const char *fn);
struct inv *inv_parse(const void *src, size_t srcSz);
struct inv *inv_load(const char *fn);
struct inv *inv_load_binary(const char *fn, int w, int h);
void inv_free(struct inv *inv);

#endif /* INV_H_INCLUDED */
