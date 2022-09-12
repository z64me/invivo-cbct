#ifndef VIEWER_H_INCLUDED
#define VIEWER_H_INCLUDED

struct viewer;

void viewer_upload_pixels(struct viewer *v, const void *src, int srcW, int srcH, int idx);
struct viewer *viewer_create(int x, int y, int z);
int viewer_destroy(struct viewer *v);
int viewer_events(struct viewer *v);
int viewer_draw(struct viewer *v);
void viewer_get_dim(struct viewer *v, int i, int *w, int *h);
void viewer_clear(struct viewer *v);
void viewer_label(struct viewer *v, const char *str, int x, int y);
void viewer_get_quadrant(struct viewer *v, int x, int y, int *ul_x, int *ul_y);

#endif /* VIEWER_H_INCLUDED */
