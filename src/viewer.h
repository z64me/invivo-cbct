#ifndef VIEWER_H_INCLUDED
#define VIEWER_H_INCLUDED

#include <stdbool.h>

struct viewer;

void viewer_upload_pixels(struct viewer *v, const void *src, int srcW, int srcH, int idx);
struct viewer *viewer_create(int x, int y, int z);
int viewer_destroy(struct viewer *v);
int viewer_events(struct viewer *v);
int viewer_draw_quadrants(struct viewer *v);
void viewer_show(struct viewer *v);
void viewer_get_dim(struct viewer *v, int i, int *w, int *h);
void viewer_clear(struct viewer *v);
int viewer_label(struct viewer *v, const char *str, int x, int y);
int viewer_label_inverted(struct viewer *v, const char *str, int x, int y);
void viewer_get_quadrant(struct viewer *v, int x, int y, int *ul_x, int *ul_y);
int viewer_slider_int(struct viewer *v, int x, int y, int w, int *n, int lo, int hi);
bool viewer_button(struct viewer *v, const char *str, int x, int y);
void viewer_set_palette(struct viewer *v, int palette);
void viewer_set_inverted(struct viewer *v, bool is_inverted);
void viewer_set_axes(struct viewer *v, bool enabled, float x, float y, float z);
int viewer_get_mouse_wheel_in_quadrant(struct viewer *v, int quadrant);

#endif /* VIEWER_H_INCLUDED */
