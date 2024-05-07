#ifdef WANT_THREADS
#define JAS_FOR_JASPER_APP_USE_ONLY /* XXX expose libjasper's threading */
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <limits.h>
#include <math.h>
#include <jasper/jasper.h>
#include <base64.h>
#include <stb_image.h>

#include "inv.h"
#include "common.h"
#include "palette.h"

/* fallback if no threading is available */
#if defined(WANT_THREADS) && !defined(JAS_THREADS)
#	warning JasPer was compiled without threads. Threading is not available.
#	undef WANT_THREADS
#endif

struct inv
{
	void *data;
	size_t dataSz;
	
	/* xml vars */
	void *AppendedData;
	size_t AppendedDataSz;
	char PatientName[512];
	char PatientBirthday[512];
	char Watermark[512];
	char ImageDate[512];
	
	/* AppendedData contents */
	void *gray; // 16-bit grayscale image strip
	void *grayEnd; // end of gray, for bounds checking
	uint32_t *grayJPCsz; // size of each JPC container
	size_t graySz; // size of gray memory block
	unsigned grayNum; // number of images in strip
	unsigned grayJPC; // number of JPC containers
	int grayWidth; // dimensions of grayscale images
	int grayHeight;
	bool isThreaded; // is threading enabled
	int cmpno;
};

/* loads all raw pixel data from a JPC into a buffer */
static void jpcLoadPixelsInto(void **dst, void *src, uint32_t sz, void *dstEnd)
{
	jas_image_t *image;
	jas_stream_t *stream;
	unsigned cmp;
	int fmt;
	uint8_t *gray = *dst;
	
	/* open stream */
	stream = jas_stream_memopen(src, sz);
	if (!stream)
	{
		fprintf(stderr, "jas_stream_memopen error\n");
		abort();
	}
	
	/* get image format */
	if ((fmt = jas_image_getfmt(stream)) < 0)
	{
		fprintf(stderr, "jas_image_getfmt error\n");
		abort();
	}
	
	/* decode stream to image */
	if (!(image = jas_image_decode(stream, fmt, 0)))
	{
		fprintf(stderr, "jas_image_decode error\n");
		abort();
	}
	
	/* get 16-bit grayscale pixel data for each component */
	for (cmp = 0; cmp < jas_image_numcmpts(image); ++cmp)
	{
		int width = jas_image_width(image);
		int height = jas_image_height(image);
		int x;
		int y;
		
		/* exhausted the allocated pixel buffer */
		if (gray >= (uint8_t*)dstEnd)
		{
			fprintf(stderr, "error: more images than expected\n");
			abort();
		}
		
		for (y = 0; y < height; ++y)
		{
			for (x = 0; x < width; ++x)
			{
				uint16_t v = jas_image_readcmptsample(image, cmp, x, y);
				
				v -= 0x8000;
				
				*gray = v; gray++;
				*gray = v >> 8; gray++;
			}
		}
	}
	
	/* cleanup */
	jas_stream_close(stream);
	jas_image_destroy(image);
	
	*dst = gray;
}

#ifdef WANT_THREADS
struct jpcJob
{
	jas_thread_t thread;
	void *outbuf;
	void *outbufEnd;
	void *data;
	uint32_t sz;
};

static int jpcJob(void *handle)
{
	struct jpcJob *job = handle;
	int result = 0;
	
	/* init thread */
	if (jas_init_thread())
	{
		fprintf(stderr, "jas_init_thread error inside job\n");
		return -1;
	}
	
	/* process image */
	jpcLoadPixelsInto(&job->outbuf, job->data, job->sz, job->outbufEnd);
	
	/* this reports progress */
	fprintf(stdout, "%p\n", job->data);
	
	/* cleanup thread */
	jas_cleanup_thread();
	
	return result;
}
#endif /* WANT_THREADS */

/* gets the inner value of an XML element
 * returns dst on success
 * returns 0 on failure
 */
static char *xml_get_inner(char *dst, int dstSz, const char *src)
{
	const char *begin;
	const char *end;
	int len;
	
	assert(dst);
	assert(src);
	
	/* clear destination buffer in case nothing is found */
	memset(dst, 0, dstSz);
	
	/* beginning and end of the relevant block */
	if (!(begin = strchr(src, '"')))
		return 0;
	if (!(end = strchr(begin + 1, '"')))
		return 0;
	
	/* copy inner contents */
	len = end - begin;
	if (len > dstSz)
		len = dstSz;
	len -= 1;
	memcpy(dst, begin + 1, len);
	
	return dst;
}

/* hacky XML value finder so I don't have to include a whole XML library */
static void xml_get_inv_value(char *dst, int dstSz, const char *tag, const char *src)
{
	const char *begin;
	const char *end;
	const char *mat;
	
	assert(dst);
	assert(dstSz > 0);
	assert(tag);
	assert(src);
	
	/* clear destination buffer in case nothing is found */
	memset(dst, 0, dstSz);
	
	/* beginning and end of the relevant block */
	if (!(begin = strstr(src, tag)))
		return;
	if (!(end = strstr(begin + 1, tag)))
		return;
	
	/* prioritize 'BinaryValue' field */
	if (((mat = strstr(begin, "BinaryValue")) && mat < end))
	{
		char *endstr;
		int n;
		
		/* get base64, decode to string, append terminator */
		xml_get_inner(dst, dstSz, mat);
		endstr = b64decode(dst);
		*endstr = '\0';
		
		/* 'BinaryValue' string begins with a 4-byte length */
		n = LEu32(dst);
		if (n)
			memmove(dst, dst + 4, n + 1);
		
		return;
	}
	
	/* fall back to 'Value' field */
	if (((mat = strstr(begin, "Value")) && mat < end))
	{
		/* get string */
		xml_get_inner(dst, dstSz, mat);
		return;
	}
}

static void jasper_cleanup(void)
{
	jas_cleanup_thread();
	jas_cleanup_library();
}

static int jasper_begin(bool isThreaded)
{
	static jas_std_allocator_t allocator;
	
	jas_conf_clear();
	jas_std_allocator_init(&allocator);
	jas_conf_set_allocator(&allocator.base);
	jas_conf_set_max_mem_usage(SIZE_MAX); // XXX threaded operations easily consume > 1 GiB memory
	jas_conf_set_multithread(isThreaded);
	jas_conf_set_vlogmsgf(jas_vlogmsgf_discard); // XXX silence warnings
	jas_conf_set_debug_level(0);
	
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
	uint8_t *data;
	uint8_t *dataStart;
	uint8_t *dataJPCblock;
	uint32_t *grayJPCsz;
	size_t dataSz = inv->AppendedDataSz;
	unsigned int i;
	int cmpnoLast;
	
	assert(inv);
	assert(inv->AppendedData);
	assert(inv->AppendedDataSz);
	
	dataStart = inv->AppendedData;
	
	/* number of JPC containers */
	inv->grayJPC = LEu32(dataStart + 4);
	inv->cmpno = LEu32(dataStart + 8);
	cmpnoLast = LEu32(dataStart + 12);
	assert(inv->grayJPC);
	
	/* and their sizes */
	inv->grayJPCsz = grayJPCsz = malloc(inv->grayJPC * sizeof(*grayJPCsz));
	assert(grayJPCsz);
	for (i = 0, data = dataStart + 4 * sizeof(uint32_t); i < inv->grayJPC; ++i, data += sizeof(uint32_t))
		grayJPCsz[i] = LEu32(data);
	//for (i = 0; i < inv->grayJPC; ++i)
	//	fprintf(stdout, "%08x\n", grayJPCsz[i]);
	
	/* the JPC block is located immediately after the header, which has a length
	 * of four 32-bit words + an array of 32-bit words describing each JPC's size
	 */
	dataJPCblock = dataStart + (4 + inv->grayJPC) * sizeof(uint32_t);
	
	/* what I have deduced about the file structure so far:
	 * 
	 * struct inv_header
	 * {
	 *    uint32_t magic;     // 0x2020205F aka string "   _" (magic value?)
	 *    uint32_t num;       // (LE) number of JPC containers
	 *    uint32_t cmpnum;    // (LE) number of components per container
	 *    uint32_t lastcnum;  // (LE) number of components in last container
	 *    uint32_t size[num]; // (LE) filesize of each JPC file
	 * };
	 * 
	 * immediately following the header is a series of JPEG 2000
	 * codestreams (JPC specifically) sandwiched together; the
	 * size table defined in the header is used to determine how
	 * many bytes to advance to find the next JPC in the series
	 * 
	 * the file appears to end with a three-byte footer 0x0A2020 aka string "\n  "
	 */
	
	/* first pass: assert that all dimensions match
	 * TODO: consider getting dimensions from the XML instead + sanity check while parsing
	 */
	for (i = 0, data = dataJPCblock; i < inv->grayJPC; data += grayJPCsz[i++])
	{
		int width;
		int height;
		
		/* extract width */
		width = BEu32(data + 8);
		height = BEu32(data + 12);
		
		/* first JPC sets width */
		if (!inv->grayWidth)
		{
			inv->grayWidth = width;
			inv->grayHeight = height;
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
	
	/* allocate memory for 16-bit image data for each image */
	inv->grayNum = (inv->grayJPC - (cmpnoLast != 0)) * inv->cmpno + cmpnoLast;
	inv->graySz = 2 * inv->grayWidth * inv->grayHeight * inv->grayNum;
	inv->gray = calloc(1, inv->graySz);
	if (!inv->gray)
	{
		fprintf(stderr, "memory error\n");
		return 1;
	}
	inv->grayEnd = ((uint8_t*)inv->gray) + inv->graySz;
	
	/* initialize libjasper */
	if (jasper_begin(inv->isThreaded))
	{
		fprintf(stderr, "libjasper error\n");
		return 1;
	}
	
	/* second pass: parse all the JPC containers using libjasper */
	if (inv->isThreaded == false)
	{
		void *dst;
		
	#ifndef WANT_THREADS
	L_nothreading:
	#endif
		/* parse each image */
		for (i = 0, dst = inv->gray, data = dataJPCblock; i < inv->grayJPC; data += grayJPCsz[i++])
		{
			jpcLoadPixelsInto(&dst, data, grayJPCsz[i], inv->grayEnd);
			
			/* it can be slow, so I tossed this here to report progress */
			fprintf(stdout, "%p\n", data);
		}
	}
	else
	{
	#ifdef WANT_THREADS
		struct jpcJob *job;
		int jobNum = inv->grayJPC;
		uint8_t *gray;
		
		/* init */
		job = calloc(jobNum, sizeof(*job));
		if (!job)
		{
			fprintf(stderr, "memory error\n");
			return 1;
		}
		
		/* spin up a thread for each image */
		for (i = 0, gray = inv->gray, data = dataJPCblock; i < inv->grayJPC; data += grayJPCsz[i++])
		{
			struct jpcJob *thisjob = &job[i];
			
			thisjob->outbuf = gray + inv->grayWidth * inv->grayHeight * 2 * inv->cmpno * i;
			thisjob->outbufEnd = inv->grayEnd;
			thisjob->data = data;
			thisjob->sz = grayJPCsz[i];
			
			if (jas_thread_create(&thisjob->thread, jpcJob, thisjob))
			{
				fprintf(stderr, "jas_thread_create error\n");
				return 1;
			}
		}
		
		/* wait on threads to finish */
		for (i = 0; i < inv->grayJPC; ++i)
		{
			int result;
			
			if (jas_thread_join(&job[i].thread, &result) || result)
			{
				fprintf(stderr, "jas_thread_join error on job %d\n", i);
				return 1;
			}
		}
		
		/* cleanup jobs */
		free(job);
	#else
		fprintf(stderr, "Warning: Threading is not available. Falling back to slow mode.\n");
		goto L_nothreading;
	#endif
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

/* allocates and returns a new inv populated with some default values */
static struct inv *inv_new(void)
{
	struct inv *inv = calloc(1, sizeof(*inv));
	
	if (!inv)
	{
		fprintf(stderr, "memory error\n");
		return 0;
	}
	
	/* patient info set to 'unset' by default */
	strcpy(inv->PatientName, "unset");
	strcpy(inv->PatientBirthday, "unset");
	strcpy(inv->Watermark, "unset");
	strcpy(inv->ImageDate, "unset");
	
	/* components */
	inv->cmpno = 7;
	
	return inv;
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

static uint16_t GetSample16(struct inv *inv, int x, int y, int z)
{
	assert(inv);
	assert(x < (int)inv->grayWidth);
	assert(y < (int)inv->grayHeight);
	assert(z < (int)inv->grayNum);
	
	return GetFrame16(inv, z)[y * inv->grayWidth + x];
}

const void *inv_get_plane(struct inv *inv, void *dst, int image, enum inv_plane plane)
{
	const uint16_t *gray = inv->gray;
	uint16_t *dstv = dst;
	int w = inv->grayWidth;
	int h = inv->grayHeight;
	int d = inv->grayNum;
	int x;
	int y;
	int z;
	
	assert(image >= 0);
	
	memset(dst, 0, w * h * sizeof(*gray));
	
	switch (plane)
	{
		case INV_PLANE_AXIAL:
			if (image >= d)
				break;
			//image %= d;
			for (y = 0; y < h; ++y)
				for (x = 0; x < w; ++x, ++dstv)
					*dstv = GetSample16(inv, x, y, image);
			//if (image < d)
			//	memcpy(dst, inv_get_frame(inv, image), w * h * sizeof(*gray));
			break;
		
		case INV_PLANE_SAGITTAL:
			if (image >= w)
				break;
			//image %= w;
			dstv += d * h - 1;
			for (z = 0; z < d; ++z)
				for (y = 0; y < h; ++y, --dstv)
					*dstv = GetSample16(inv, image, y, z);
			break;
		
		case INV_PLANE_CORONAL:
			if (image >= h)
				break;
			//image %= h;
			for (z = 0; z < d; ++z)
				for (x = 0; x < w; ++x, ++dstv)
					*dstv = GetSample16(inv, x, image, d - z - 1);
			break;
		
		default:
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
	
	if (inv->grayJPCsz)
		free(inv->grayJPCsz);
	
	if (inv->gray)
		free(inv->gray);
	
	free(inv);
}

struct inv *inv_parse(const void *src, size_t srcSz, bool isThreaded)
{
	struct inv *inv;
	void *data;
	size_t dataSz;
	
	if (!(inv = inv_new()))
		return 0;
	
	/* make a copy of the provided data */
	if (!(inv->data = data = memduppad(src, srcSz, 1)))
		goto L_fail;
	inv->dataSz = dataSz = srcSz;
	inv->isThreaded = isThreaded;
	
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
	
	/* parse remaining XML values */
	{
		char *mat;
		
		/* retrieve */
		xml_get_inv_value(inv->PatientName, sizeof(inv->PatientName), "PatientName", inv->data);
		xml_get_inv_value(inv->PatientBirthday, sizeof(inv->PatientBirthday), "PatientBirthDay", inv->data);
		xml_get_inv_value(inv->Watermark, sizeof(inv->Watermark), "PatientSex", inv->data);
		xml_get_inv_value(inv->ImageDate, sizeof(inv->ImageDate), "ImageDate", inv->data);
		
		/* convert 'Last^First' -> 'Last,First' format */
		if ((mat = strchr(inv->PatientName, '^')))
			*mat = ',';
		
		/* debug output */
		fprintf(stdout, "PatientName = '%s'\n", inv->PatientName);
		fprintf(stdout, "PatientBirthday = '%s'\n", inv->PatientBirthday);
		fprintf(stdout, "Watermark = '%s'\n", inv->Watermark);
		fprintf(stdout, "ImageDate = '%s'\n", inv->ImageDate);
	}
	
	return inv;
	
L_fail:
	inv_free(inv);
	return 0;
}

struct inv *inv_load(const char *fn, bool isThreaded)
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
	if (!(inv = inv_parse(data, dataSz, isThreaded)))
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
		//fwrite(((uint8_t*)inv->gray) + 10 * inv->cmpno * 536 * 536 * 2, 1, 536 * 536 * 2, test);
		fclose(test);
	}*/
	
	/* success */
	return 0;
}

struct inv *inv_load_binary(const char *fn, int w, int h)
{
	struct inv *inv;
	
	if (!(inv = inv_new()))
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

struct inv *inv_load_series(const char *pattern, int start, int end)
{
	struct inv *inv = 0;
	uint8_t *gray = 0;
	int low = start < end ? start : end;
	int high = end > start ? end : start;
	int num = (high - low) + 1;
	int direction = end > start ? 1 : -1;
	int i;
	
	if (!(inv = inv_new()))
		goto L_fail;
	
	/* load every image */
	for (i = start; i != end + direction; i += direction)
	{
		uint8_t *img;
		uint8_t *pix;
		char path[2048];
		int w;
		int h;
		int n;
		int k;
		
		/* format %04d.png -> 0001.png */
		snprintf(path, sizeof(path), pattern, i);
		
		//fprintf(stderr, "load image %s\n", path);
		
		/* load image */
		if (!(img = stbi_load(path, &w, &h, &n, STBI_rgb_alpha)))
		{
			fprintf(stderr, "failed to open image '%s'\n", path);
			goto L_fail;
		}
		
		/* first image dictates dimensions */
		if (i == start)
		{
			inv->graySz = num * w * h * 2;
			inv->grayNum = num;
			inv->grayWidth = w;
			inv->grayHeight = h;
			if (!(inv->gray = calloc(1, inv->graySz)))
			{
				fprintf(stderr, "memory error\n");
				goto L_fail;
			}
			gray = inv->gray;
		}
		
		/* sanity check dimensions */
		if (w != inv->grayWidth || h != inv->grayHeight)
		{
			fprintf(stderr, "error: images in series are not all the same dimensions\n");
			goto L_fail;
		}
		
		/* convert 4-channel 8-bit grayscale to 1-channel 16-bit grayscale */
		for (pix = img, k = 0; k < w * h; ++k, pix += 4)
		{
			// TODO these are the same magic values from inv_make_8bit
			float brightness = -0.23;
			float contrast = 17.500031;
			float conv = *pix;
			uint16_t v;
			
			/* this is the inverse operation from inv_make_8bit */
			conv /= 255;
			conv -= 0.5f;
			conv -= brightness;
			conv /= contrast;
			conv += 0.5f;
			conv *= 65535.0f;
			
			/* LE byte order */
			v = round(conv);
			*gray = v; ++gray;
			*gray = v >> 8; ++gray;
		}
		
		/* cleanup */
		stbi_image_free(img);
	}
	
	return inv;
	
L_fail:
	inv_free(inv);
	return 0;
}

/* convert 16-bit pixel data to color-corrected 8-bit pixel data */
void *inv_make_8bit(void *pixels16bit, int w, int h, int threshold_min, int threshold_max)
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
		
		/* clamp to thresholds */
		if (*dst < threshold_min)
			*dst = 0;
		else if (*dst > threshold_max)
			*dst = 0;
	}
	
	return pixels16bit;
}

/* dump inv to point cloud (Stanford .ply) */
int inv_dump_pointcloud(struct inv *inv, const char *fn, int minv, int maxv, int palette, float density)
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
			pix8 = inv_make_8bit(pix16, w, h, minv, maxv);
			
			/* for every pixel in image */
			for (y = 0; y < h; y += density)
			{
				for (x = 0; x < w; x += density)
				{
					int v = pix8[(int)floor(y) * w + (int)floor(x)];
					uint8_t rgb[] = {v, v, v};
					int a = v;
					
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
					
					/* palette */
					if (palette >= 0)
						palette_color(rgb, palette, v);
					
					/* x y z r g b a */
					fprintf(fp, "%f %f %f %d %d %d %d\n", x, y, z, rgb[0], rgb[1], rgb[2], a);
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

/* prepares a base64 string with a length prefix prior to encoding */
static void PrefixedBase64(char *dst, const char *src)
{
	char buf[1024];
	int srclen;
	
	assert(dst);
	assert(src);
	
	srclen = strlen(src);
	
	/* xx 00 00 00 */
	buf[0] = srclen;
	buf[1] = buf[2] = buf[3] = 0;
	
	/* xx 00 00 00 H e l l o W o r l d */
	strcpy(buf + 4, src);
	
	/* encode */
	bintob64(dst, buf, srclen + 4);
}

/* writes an Invivo .inv file to the specified destination */
int inv_write(struct inv *inv, const char *outfn, const char *firstname, const char *lastname, const char *dob)
{
	FILE *fp;
	const uint8_t *gray;
	int containerNum; // number of JPC containers
	int i;
	char PatientName[512]; // Last^First format
	char PatientBirthday[32]; // YYYYMMDD format
	const char *Watermark = "www.holland.vg";
	char currentDate[32];
	char PatientNameBin[1024];
	char PatientBirthdayBin[1024];
	char WatermarkBin[1024];
	size_t grayJPCszOff;
	uint32_t *grayJPCsz = 0;
	int cmpnoLast;
	int imgrem; // remaining images to write
	
	assert(inv);
	assert(outfn);
	assert(firstname);
	assert(lastname);
	assert(dob);
	
	/* prepare current date in YYYYMMDD format */
	{
		time_t t = time(0);
		struct tm *tm = localtime(&t);
		
		strftime(currentDate, sizeof(currentDate), "%Y%m%d", tm);
	}
	
	/* format strings */
	sprintf(PatientName, "%s^%s", lastname, firstname);
	strcpy(PatientBirthday, dob);
	
	/* prepare base64 strings */
	PrefixedBase64(PatientNameBin, PatientName);
	PrefixedBase64(PatientBirthdayBin, PatientBirthday);
	PrefixedBase64(WatermarkBin, Watermark);
	
	/* initialize libjasper */
	if (jasper_begin(false))
	{
		fprintf(stderr, "libjasper error\n");
		return 1;
	}
	
	/* open destination file */
	if (!(fp = fopen(outfn, "wb+")))
	{
		fprintf(stderr, "failed to open '%s' for writing\n", outfn);
		return 1;
	}
	
	/* prepare these for later */
	gray = inv->gray;
	for (containerNum = 0; containerNum * inv->cmpno < (int)inv->grayNum; )
		++containerNum;
	cmpnoLast = inv->grayNum % inv->cmpno;
	imgrem = inv->grayNum;
	
	/* allocate space for size of each JPC container */
	grayJPCsz = calloc(containerNum, sizeof(*grayJPCsz));
	assert(grayJPCsz);
	
	/* write XML part */
	fprintf(fp, "<INVFile version='2.0' byteOrder='LittleEndian'>\n");
		fprintf(fp, "<CaseInfo SampleFileName=\"www.holland.vg\">\n");
			fprintf(fp, "<IdentifyInfo GroupID=\"8\">\n");
				fprintf(fp, "<ImageType ElementID=\"8\" Value=\"ORIGINAL\\PRIMARY\\AXIAL\"></ImageType>\n");
				fprintf(fp, "<ImageDate ElementID=\"35\" Value=\"%s\"></ImageDate>\n", currentDate);
				fprintf(fp, "<Modality ElementID=\"96\" Value=\"CT\"></Modality>\n");
				fprintf(fp, "<Manufacture ElementID=\"112\" Value=\"Imaging Sciences International\"></Manufacture>\n");
			fprintf(fp, "</IdentifyInfo>\n");
			fprintf(fp, "<Patient GroupID=\"16\">\n");
				fprintf(fp, "<PatientName ElementID=\"16\" Format=\"binary\" BinaryValue=\"%s\" Value=\"%s\"></PatientName>\n", PatientNameBin, PatientName);
				fprintf(fp, "<PatientBirthDay ElementID=\"48\" Format=\"binary\" BinaryValue=\"%s\" Value=\"%s\"></PatientBirthDay>\n", PatientBirthdayBin, PatientBirthday);
				fprintf(fp, "<PatientSex ElementID=\"64\" Format=\"binary\" BinaryValue=\"%s\" Value=\"%s\"></PatientSex>\n", WatermarkBin, Watermark);
			fprintf(fp, "</Patient>\n");
		fprintf(fp, "</CaseInfo>\n");
		fprintf(fp, "<Volume  Source='Appended' Offset='0' ScalarType='Int16' Dimensions='%d %d %d' NumComp='1' Name='' Spacing='0.3 0.3 0.3' Origin='0 0 0' CoordinateSystem='0 0 0 1 0 0 0' WindowLevel='1 1' />\n", inv->grayWidth, inv->grayHeight, inv->grayNum);
		fprintf(fp, "<AppendedData encoding='raw'>   _"); // NOTE trailing magic bytes "   _" may be necessary
	
	/* write AppendedData header */
	fputLEu32(containerNum, fp);
	fputLEu32(inv->cmpno, fp);
	fputLEu32(cmpnoLast, fp);
	grayJPCszOff = ftell(fp); // takes note of where the JPC size table lives
	for (i = 0; i < containerNum; ++i)
		fputLEu32(0, fp);
	
	/* close file for now */
	fclose(fp);
	
	/* for each collection of images in series */
	for (i = 0; i < containerNum; ++i)
	{
		jas_image_t *image = jas_image_create(0, 0, JAS_CLRSPC_SRGB);
		jas_image_cmptparm_t tmp = {
			.tlx = 0
			, .tly = 0
			, .hstep = 1
			, .vstep = 1
			, .width = inv->grayWidth
			, .height = inv->grayHeight
			, .prec = 16
			, .sgnd = 1
		};
		jas_stream_t *out = jas_stream_fopen(outfn, "ab+");
		int cmp;
		int cmpno = imgrem >= inv->cmpno ? inv->cmpno : imgrem;
		
		assert(imgrem > 0);
		
		/* report progress */
		fprintf(stderr, "%d / %d\n", i + 1, containerNum);
		
		/* add empty components to image */
		for (cmp = 0; cmp < cmpno; ++cmp)
			jas_image_addcmpt(image, cmp, &tmp);
		
		/* populate pixels across each component */
		for (cmp = 0; cmp < cmpno; ++cmp, --imgrem)
		{
			int width = jas_image_width(image);
			int height = jas_image_height(image);
			int x;
			int y;
			
			for (y = 0; y < height; ++y)
			{
				for (x = 0; x < width; ++x)
				{
					uint16_t v;
					
					v = *gray; gray++;
					v |= *gray << 8; gray++;
					
					v += 0x8000;
					
					jas_image_writecmptsample(image, cmp, x, y, v);
				}
			}
		}
		
		/* write JPC and note size */
		jas_image_encode(image, out, jas_image_strtofmt("jpc"), 0);
		jas_stream_flush(out);
		grayJPCsz[i] = jas_stream_getrwcount(out);
		
		/* cleanup */
		jas_stream_close(out);
		jas_image_destroy(image);
	}
	
	assert(imgrem == 0);
	
	/* write the XML footer */
	fp = fopen(outfn, "rb+");
	fseek(fp, 0, SEEK_END);
	fprintf(fp, "\x0a  </AppendedData>\n"); // NOTE magic byte prefix "\n  " may be necessary
	fprintf(fp, "</INVFile>\n");
	
	/* update header with size of each JPC container */
	fseek(fp, grayJPCszOff, SEEK_SET);
	for (i = 0; i < containerNum; ++i)
		fputLEu32(grayJPCsz[i], fp);
	
	/* cleanup libjasper */
	jasper_cleanup();
	
	/* cleanup */
	free(grayJPCsz);
	fclose(fp);
	
	/* success */
	return 0;
}

const char *inv_get_patient_name(struct inv *inv)
{
	assert(inv);
	
	return inv->PatientName;
}

const char *inv_get_patient_birthday(struct inv *inv)
{
	assert(inv);
	
	return inv->PatientBirthday;
}

const char *inv_get_watermark(struct inv *inv)
{
	assert(inv);
	
	return inv->Watermark;
}

const char *inv_get_imagedate(struct inv *inv)
{
	assert(inv);
	
	return inv->ImageDate;
}
