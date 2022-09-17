#ifndef INV_H_INCLUDED
#define INV_H_INCLUDED

#include <stdbool.h>

struct inv;

enum inv_plane
{ // directions are from the skull's point of view
	INV_PLANE_AXIAL = 0   // bottom to top
	, INV_PLANE_SAGITTAL  // right to left
	, INV_PLANE_CORONAL   // front to back
	, INV_PLANE_NUM       // num planes in this enum
};

void *inv_make_8bit(void *pixels16bit, int w, int h);
int inv_get_width(struct inv *inv);
int inv_get_height(struct inv *inv);
int inv_get_num_images(struct inv *inv);
const void *inv_get_plane(struct inv *inv, void *dst, int image, enum inv_plane plane);
const void *inv_get_gray(struct inv *inv, int *w, int *h, int *num);
int inv_dump(struct inv *inv, const char *fn);
int inv_dump_pointcloud(struct inv *inv, const char *fn, int minv, int maxv, float density);
struct inv *inv_parse(const void *src, size_t srcSz, bool isThreaded);
struct inv *inv_load(const char *fn, bool isThreaded);
struct inv *inv_load_binary(const char *fn, int w, int h);
struct inv *inv_load_series(const char *pattern, int start, int end);
void inv_free(struct inv *inv);
int inv_write(struct inv *inv, const char *outfn, const char *firstname, const char *lastname, const char *dob);
const char *inv_get_patient_name(struct inv *inv);
const char *inv_get_patient_birthday(struct inv *inv);
const char *inv_get_watermark(struct inv *inv);
const char *inv_get_imagedate(struct inv *inv);

#endif /* INV_H_INCLUDED */
