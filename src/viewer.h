#ifndef VIEWER_H_INCLUDED
#define VIEWER_H_INCLUDED

struct viewer;

void viewer_upload_pixels(struct viewer *v, const void *src, int srcW, int srcH, int idx);
struct viewer *viewer_create(void);
int viewer_destroy(struct viewer *v);
int viewer_events(struct viewer *v);
int viewer_draw(struct viewer *v);

#endif /* VIEWER_H_INCLUDED */
