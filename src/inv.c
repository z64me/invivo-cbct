#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <jasper/jasper.h>

#include "inv.h"
#include "common.h"

struct inv
{
	void *data;
	size_t dataSz;
	
	/* xml vars */
	void *AppendedData;
	size_t AppendedDataSz;
	
	/* AppendedData contents */
	void *gray; // 16-bit grayscale image strip
	size_t graySz; // size of gray memory block
	unsigned grayNum; // number of images in strip
	unsigned grayJPC; // number of JPC containers
	int grayWidth; // dimensions of grayscale images
	int grayHeight;
};

static void jasper_cleanup(void)
{
	jas_cleanup_thread();
	jas_cleanup_library();
}

static int jasper_begin(void)
{
	static jas_std_allocator_t allocator;
	
	jas_conf_clear();
	jas_std_allocator_init(&allocator);
	jas_conf_set_allocator(&allocator.base);
	jas_conf_set_max_mem_usage(1024 * 1024 * 1024 * 1); // 1 GiB
	
	if (jas_init_library())
	{
		fprintf(stderr, "jas_init_library error\n");
		return 1;
	}
	if (jas_init_thread())
	{
		fprintf(stderr, "jas_init_thread error\n");
		return 1;
	}
	
	/* success */
	return 0;
}

static inline int AppendedData_parse(struct inv *inv)
{
	const uint8_t magic[] = { 0xff, 0x4f, 0xff, 0x51 }; // JPC start
	uint8_t *data;
	uint8_t *dataStart;
	uint8_t *gray;
	size_t dataSz = inv->AppendedDataSz;
	unsigned int i;
	
	assert(inv);
	assert(inv->AppendedData);
	assert(inv->AppendedDataSz);
	
	dataStart = inv->AppendedData;
	
	/* number of JPC containers */
	inv->grayJPC = LEu32(dataStart + 4);
	
	/* what I have deduced about the header so far:
	 * struct inv_header
	 * {
	 *    uint32_t magic;     // 0x2020205F aka string "   _" (magic value?)
	 *    uint32_t num;       // (LE) number of JPC containers
	 *    uint32_t cmpnum;    // (LE) number of components per container?
	 *    uint32_t unk0;      // unknown
	 *    uint32_t unk1[num]; // array of 32-bit values, one for each container
	 * };
	 * the remainder of the file is a series of JPEG 2000
	 * codestreams (JPC specifically) sandwiched together
	 */
	
	/* first pass: assert that all dimensions match */
	for (i = 0, data = dataStart;;)
	{
		int width;
		int height;
		
		/* get start of next JPC */
		++data;
		data = memmem(data, dataSz - (data - dataStart), magic, sizeof(magic));
		if (!data)
			break;
		
		/* number of JPC containers encountered */
		++i;
		
		/* extract width */
		width = BEu32(data + 8);
		height = BEu32(data + 12);
		
		/* first JPC sets width */
		if (!inv->grayWidth)
		{
			inv->grayWidth = width;
			inv->grayHeight = height;
			continue;
		}
		
		/* images within all JPC containers expected to be the same dimensions */
		if (inv->grayWidth != width || inv->grayHeight != height)
		{
			fprintf(stderr, "JPC image dimensions mismatch: "
				"expected %dx%d, got %dx%d\n"
				, inv->grayWidth, inv->grayHeight, width, height
			);
			return 1;
		}
	}
	
	/* detected a different number of containers than the header suggests */
	if (i != inv->grayJPC)
	{
		fprintf(stderr, "expected %d JPC containers, found %d\n", inv->grayJPC, i);
		return 1;
	}
	
	/* allocate memory for 16-bit image data for each image (7 components per JPC) */
	inv->graySz = 2 * inv->grayWidth * inv->grayHeight * inv->grayJPC * 7;
	inv->gray = calloc(1, inv->graySz);
	if (!inv->gray)
	{
		fprintf(stderr, "memory error\n");
		return 1;
	}
	
	/* initialize libjasper */
	if (jasper_begin())
	{
		fprintf(stderr, "libjasper error\n");
		return 1;
	}
	
	/* second pass: parse all the JPC containers using libjasper */
	for (inv->grayNum = 0, gray = inv->gray, data = dataStart;;)
	{
		jas_image_t *image;
		jas_stream_t *stream;
		int fmt;
		
		/* get start of next JPC */
		++data;
		data = memmem(data, dataSz - (data - dataStart), magic, sizeof(magic));
		if (!data)
			break;
		
		/* open stream */
		stream = jas_stream_memopen((void*)data, dataSz - (data - dataStart));
		if (!stream)
		{
			fprintf(stderr, "jas_stream_memopen error\n");
			return 1;
		}
		
		/* get image format */
		if ((fmt = jas_image_getfmt(stream)) < 0)
		{
			fprintf(stderr, "jas_image_getfmt error\n");
			return -1;
		}
		
		/* decode stream to image */
		if (!(image = jas_image_decode(stream, fmt, 0)))
		{
			fprintf(stderr, "jas_image_decode error\n");
			return -1;
		}
		
		/* get 16-bit grayscale pixel data for each component */
		for (i = 0; i < jas_image_numcmpts(image); ++i, inv->grayNum++)
		{
			int width = jas_image_width(image);
			int height = jas_image_height(image);
			int x;
			int y;
			
			for (y = 0; y < height; ++y)
			{
				for (x = 0; x < width; ++x)
				{
					uint16_t v = jas_image_readcmptsample(image, i, x, y);
					
					v -= 0x8000;
					
					*gray = v; gray++;
					*gray = v >> 8; gray++;
				}
			}
			
			/* exhausted the allocated pixel buffer */
			if (gray > ((uint8_t*)inv->gray) + inv->graySz)
			{
				fprintf(stderr, "error: more images than expected\n");
				return 1;
			}
		}
		
		/* cleanup */
		jas_stream_close(stream);
		jas_image_destroy(image);
		
		fprintf(stdout, "%p\n", data);
	}
	
	/* cleanup libjasper */
	jasper_cleanup();
	
	/* success */
	return 0;
}

int inv_get_num_images(struct inv *inv)
{
	return inv->grayNum;
}

int inv_get_width(struct inv *inv)
{
	return inv->grayWidth;
}

int inv_get_height(struct inv *inv)
{
	return inv->grayHeight;
}

const void *inv_get_frame(struct inv *inv, unsigned image)
{
	assert(inv);
	assert(image < inv->grayNum);
	
	return ((uint16_t*)inv->gray) + inv->grayWidth * inv->grayHeight * image;
}

static const uint16_t *GetFrame16(struct inv *inv, unsigned image)
{
	return inv_get_frame(inv, image);
}

const void *inv_get_plane(struct inv *inv, void *dst, int image, enum inv_plane plane)
{
	const uint16_t *gray = inv->gray;
	uint16_t *dstv = dst;
	int w = inv->grayWidth;
	int h = inv->grayHeight;
	int z = inv->grayNum;
	int x;
	int y;
	
	assert(image >= 0);
	
	memset(dst, 0, w * h * sizeof(*gray));
	
	switch (plane)
	{
		case INV_PLANE_AXIAL:
			if (image < z)
				memcpy(dst, inv_get_frame(inv, image), w * h * sizeof(*gray));
			break;
		
		case INV_PLANE_SAGITTAL:
			dstv += w * h - 1;
			for (y = 0; y < z; ++y)
			{
				for (x = 0; x < w; ++x, --dstv)
				{
					*dstv = GetFrame16(inv, y)[x * w + image];
				}
			}
			break;
		
		case INV_PLANE_CORONAL:
			for (y = 0; y < z; ++y)
			{
				for (x = 0; x < w; ++x, ++dstv)
				{
					*dstv = GetFrame16(inv, z - y - 1)[image * w + x];
				}
			}
			break;
		
		case INV_PLANE_NUM:
			break;
	}
	
	return dst;
}

void inv_free(struct inv *inv)
{
	if (!inv)
		return;
	
	if (inv->data)
		free(inv->data);
	
	if (inv->AppendedData)
		free(inv->AppendedData);
	
	if (inv->gray)
		free(inv->gray);
	
	free(inv);
}

struct inv *inv_parse(const void *src, size_t srcSz)
{
	struct inv *inv;
	void *data;
	size_t dataSz;
	
	if (!(inv = calloc(1, sizeof(*inv))))
		return 0;
	
	/* make a copy of the provided data */
	if (!(inv->data = data = memduppad(src, srcSz, 1)))
		goto L_fail;
	inv->dataSz = dataSz = srcSz;
	
	/* find, copy, and strip AppendedData
	 * XXX this should take place BEFORE parsing as XML because
	 *     INV files contain non-XML-compliant data initially
	 */
	{
		const char *tag = "AppendedData";
		const char *tagEnd = "</AppendedData>";
		char *data8 = data;
		char *start;
		char *end;
		size_t sz;
		
		/* start = first byte after pattern '<AppendedData...>' */
		start = memstr(data, dataSz, tag);
		if (!start)
		{
			fprintf(stderr, "failed to locate '%s' tag\n", tag);
			goto L_fail;
		}
		if (!(start = strchr(start, '>')))
		{
			fprintf(stderr, "'%s' tag: no '>' found\n", tag);
			goto L_fail;
		}
		start += 1;
		
		/* end = first byte of pattern '</AppendedData>' */
		if (!(end = memstr(start, dataSz - (start - data8), tagEnd)))
		{
			fprintf(stderr, "failed to locate '%s' tag\n", tagEnd);
			goto L_fail;
		}
		
		/* sz = difference between start and end */
		sz = end - start;
		
		/* copy */
		inv->AppendedData = memdup(start, sz);
		inv->AppendedDataSz = sz;
		
		/* parse */
		AppendedData_parse(inv);
		
		/* strip */
		memmove(start, end, strlen(end) + 1);
	}
	
	return inv;
	
L_fail:
	inv_free(inv);
	return 0;
}

struct inv *inv_load(const char *fn)
{
	struct inv *inv;
	void *data;
	size_t dataSz;
	
	/* load inv file */
	if (!(data = loadfile(fn, &dataSz)))
	{
		fprintf(stderr, "failed to load invivo file '%s'\n", fn);
		return 0;
	}
	
	/* parse inv file */
	if (!(inv = inv_parse(data, dataSz)))
	{
		fprintf(stderr, "failed to parse invivo file '%s'\n", fn);
		return 0;
	}
	
	/* cleanup */
	free(data);
	
	return inv;
}

/* write raw image sequence that can be loaded in ImageJ
 * ImageJ available here: https://imagej.nih.gov/ij/index.html
 * File -> Import -> Raw
 * Then select the following options:
 *  - Image type: 16-bit Unsigned
 *  - Width: 536 pixels
 *  - Height: 536 pixels
 *  - Offset to first image: 0 bytes
 *  - Number of images: 440
 *  - Gap between images: 0 bytes
 *  - Little-endian byte-order
 * (Leave all other settings off.)
 * (The dimensions assume you're using my dental CBCT.)
 */
int inv_dump(struct inv *inv, const char *fn)
{
	assert(inv);
	assert(fn);
	
	if (!savefile(fn, inv->gray, inv->graySz))
	{
		fprintf(stderr, "error writing file '%s'\n", fn);
		return 1;
	}
	
	fprintf(stdout, "wrote %d images\n", inv->grayNum);
	
	/*{
		FILE *test = fopen(fn, "wb");
		// write entire image sequence:
		fwrite(inv->gray, 1, inv->graySz, test);
		// write only first frame of 10th JPC in file (the dimensions assume my dental CBCT):
		//fwrite(((uint8_t*)inv->gray) + 10 * 7 * 536 * 536 * 2, 1, 536 * 536 * 2, test);
		fclose(test);
	}*/
	
	/* success */
	return 0;
}

struct inv *inv_load_binary(const char *fn, int w, int h)
{
	struct inv *inv;
	
	if (!(inv = calloc(1, sizeof(*inv))))
		goto L_fail;
	
	/* load binary file */
	if (!(inv->gray = loadfile(fn, &inv->graySz)))
	{
		fprintf(stderr, "failed to load binary file '%s'\n", fn);
		goto L_fail;
	}
	
	/* dimensions and more */
	inv->grayNum = inv->graySz / (w * h * 2);
	inv->grayWidth = w;
	inv->grayHeight = h;
	
	/* sanity check */
	if (w * h * inv->grayNum * 2 != inv->graySz)
	{
		fprintf(stderr, "binary file '%s' sanity check\n", fn);
		goto L_fail;
	}
	
	return inv;
	
L_fail:
	inv_free(inv);
	return 0;
}

/* convert 16-bit pixel data to color-corrected 8-bit pixel data */
void *inv_make_8bit(void *pixels16bit, int w, int h)
{
	uint8_t *src = pixels16bit;
	uint8_t *dst = pixels16bit;
	// TODO why are these specific values required to match ImageJ's output?
	float brightness = -0.23;
	float contrast = 17.500031;
	int i;
	
	for (i = 0; i < w * h; ++i, src += 2, dst += 1)
	{
		uint16_t v = ((src[1] << 8) | (src[0]));
		float conv = v * (1.0f / 65535.0f);
		
		/* simple brightness and contrast
		 * https://www.gegl.org/brightness-contrast.c.html
		 */
		conv -= 0.5f;
		conv *= contrast;
		conv += brightness;
		conv += 0.5f;
		if (conv < 0)
			conv = 0;
		if (conv > 1)
			conv = 1;
		
		/* final 8-bit grayscale shade */
		conv *= 255;
		*dst = conv;
	}
	
	return pixels16bit;
}

/* dump inv to point cloud (Stanford .ply) */
int inv_dump_pointcloud(struct inv *inv, const char *fn, int minv, int maxv, float density)
{
	FILE *fp;
	uint16_t *pix16;
	uint8_t *pix8;
	int digits = 32;
	int numv = 0;
	int loops;
	float x;
	float y;
	float z;
	float ox[256];
	float oy[256];
	float oz[256];
	int w;
	int h;
	int d;
	int i;
	bool is_adaptive = false;
	
	assert(inv);
	assert(fn);
	assert(density <= 1);
	assert(minv >= 0 && minv <= 255);
	assert(maxv >= 0 && maxv <= 255);
	assert(inv->gray);
	assert(inv->grayWidth);
	assert(inv->grayHeight);
	assert(inv->grayNum);
	
	/* clear */
	for (i = 0; i < 256; ++i)
		ox[i] = oy[i] = oz[i] = -1;
	
	/* adaptive density */
	if (density <= 0)
	{
		fprintf(stderr, "warning: adaptive density is experimental\n");
		is_adaptive = true;
		density = 1;
	}
	
	/* 3D dimensions of full image stack (width, height, depth) */
	w = inv->grayWidth;
	h = inv->grayHeight;
	d = inv->grayNum;
	
	/* when density == 1.00, advance 1 pixel at a time;
	 * when density == 0.25, advance 4 pixels at a time; etc
	 * etc
	 */
	density = 1.0 / density;
	
	/* temporary 16-bit pixel buffer */
	pix16 = malloc(w * h * sizeof(*pix16));
	if (!pix16)
	{
		fprintf(stderr, "memory error\n");
		return -1;
	}
	
	/* output file */
	fp = fopen(fn, "wb+");
	if (!fp)
	{
		fprintf(stderr, "failed to open '%s' for writing\n", fn);
		return -1;
	}
	
	/* this loop works like so:
	 *  - write header
	 *  - write vertex data
	 *  - overwrite header with information gathered while writing the
	 *    vertex data (specifically, the number of vertices within)
	 *  - exit loop
	 */
	for (loops = 0; ; ++loops)
	{
		/* write header
		 * (overwrites header on second pass)
		 */
		fprintf(fp,
			"ply\n"
			"format ascii 1.0\n"
			"element vertex %*d\n"
			"property float x\n"
			"property float y\n"
			"property float z\n"
			"property uchar red\n"
			"property uchar green\n"
			"property uchar blue\n"
			"property uchar alpha\n"
			"element face 0\n"
			"property list uchar uint vertex_indices\n"
			"end_header\n", digits, numv
		);
		
		/* if we've looped once already, exit the loop */
		if (loops)
			break;
		
		/* write vertex data, one image at a time */
		for (z = 0; z < d; z += density)
		{
			/* get pixels and convert to 8-bit (value range [0,255]) */
			inv_get_plane(inv, pix16, (int)floor(z), INV_PLANE_AXIAL);
			pix8 = inv_make_8bit(pix16, w, h);
			
			/* for every pixel in image */
			for (y = 0; y < h; y += density)
			{
				for (x = 0; x < w; x += density)
				{
					int v = pix8[(int)floor(y) * w + (int)floor(x)];
					
					/* skip pixels with values out of desired range */
					if (v < minv || v > maxv)
						continue;
					
					/* experimenting with adaptive density */
					if (is_adaptive)
					{
						float ad;
						
						v >>= 4;
						
						ad = v / 1024.0;
						
						if (!ad)
							continue;
						
						ad = 1.0 / ad;
						
						if (fabs(x - ox[v]) >= ad
							|| fabs(y - oy[v]) >= ad
							|| fabs(z - oz[v]) >= ad
						)
						{
							ox[v] = x - fmod(x, ad);
							oy[v] = y - fmod(y, ad);
							oz[v] = z - fmod(z, ad);
						}
						else
							continue;
						
						v <<= 4;
					}
					
					/* x y z r g b a */
					fprintf(fp, "%f %f %f %d %d %d %d\n", x, y, z, v, v, v, v);
					numv += 1;
				}
			}
		}
		
		/* rewind to header */
		fseek(fp, 0, SEEK_SET);
	}
	
	/* debug output */
	fprintf(stderr, "wrote %d vertices\n", numv);
	
	/* cleanup */
	free(pix16);
	fclose(fp);
	
	/* success */
	return 0;
}

const void *inv_get_gray(struct inv *inv, int *w, int *h, int *num)
{
	assert(inv);
	assert(w);
	assert(h);
	assert(num);
	
	*w = inv->grayWidth;
	*h = inv->grayHeight;
	*num = inv->grayNum;
	
	return inv->gray;
}
