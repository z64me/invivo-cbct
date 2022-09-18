#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>
#include "inv.h"
#include "viewer.h"
#include "palette.h"

/* XXX this was added only for creating animated GIFs */
int global_image_index = 0;

static int max2(int a, int b)
{
	return a > b ? a : b;
}

static int max3(int a, int b, int c)
{
	return max2(max2(a, b), c);
}

/* convert yyyymmdd -> yyyy/mm/dd format */
static const char *formatdate(const char *yyyymmdd)
{
	static char output[1024];
	const char *y = yyyymmdd + 0;
	const char *m = yyyymmdd + 4;
	const char *d = yyyymmdd + 6;
	
	/* string validation */
	if (!yyyymmdd || strlen(yyyymmdd) != 8)
		return "";
	
	snprintf(output, sizeof(output), "%.4s/%.2s/%.2s", y, m, d);
	
	return output;
}

int main(int argc, char *argv[])
{
	struct viewer *viewer = 0;
	const char *progname = argv[0];
	const char *fn = argv[argc - 1];
	const char *dump = 0;
	const char *points = 0;
	const char *invivo = 0;
	char invivo_first[256] = {0};
	char invivo_last[256] = {0};
	char invivo_dob[256] = {0};
	struct inv *inv;
	bool isBinary = false;
	bool isSeries = false;
	bool isThreaded = false;
	bool showViewer = false;
	int series_low;
	int series_high;
	int width;
	int height;
	int points_minv = 0;
	int points_maxv = 0;
	float points_density = 0;
	int points_palette = -1;
	int i;
	
	/* show arguments */
	if (argc < 2)
	{
		fprintf(stderr, "args: %s --options invivo.inv\n", progname);
		fprintf(stderr, "  the input file is always the last argument;\n");
		fprintf(stderr, "  optional arguments (these are the --options):\n");
		fprintf(stderr, "    --threads\n");
		fprintf(stderr, "        * enables multithreading (if available)\n");
		fprintf(stderr, "    --viewer\n");
		fprintf(stderr, "        * opens viewer window after loading data\n");
		fprintf(stderr, "    --binary  W,H\n");
		fprintf(stderr, "        * indicates input file is binary data\n");
		fprintf(stderr, "          previously exported using the --dump option\n");
		fprintf(stderr, "        * e.g. --binary 536,536\n");
		fprintf(stderr, "    --series  start,end\n");
		fprintf(stderr, "        * indicates input path is an image series,\n");
		fprintf(stderr, "          expecting C style formatting e.g. tmp/%%04d.png\n");
		fprintf(stderr, "        * loads images numbered [start,end], inclusive\n");
		fprintf(stderr, "        * reverse image order by reversing start/end e.g. 256,1\n");
		fprintf(stderr, "        * e.g. --series 1,256\n");
		fprintf(stderr, "    --invivo  out.inv Last,First,DOB\n");
		fprintf(stderr, "        * writes Invivo .inv file\n");
		fprintf(stderr, "          (supports converting binary data back to inv)\n");
		fprintf(stderr, "        * DOB (DateOfBirth) is expected to be in YYYYMMDD format\n");
		fprintf(stderr, "    --dump    out.bin\n");
		fprintf(stderr, "        * specifies output binary file to create;\n");
		fprintf(stderr, "        * the file will contain a series of raw images\n");
		fprintf(stderr, "          stored in 16-bit unsigned little-endian format\n");
		fprintf(stderr, "    --points  out.ply min,max,palette,density\n");
		fprintf(stderr, "        * specifies output point cloud file to create;\n");
		fprintf(stderr, "        * the file will be a Stanford .ply containing a series\n");
		fprintf(stderr, "          of vertices in xyzrgba format\n");
		fprintf(stderr, "        * writes only points w/ shades in value range [min,max];\n");
		fprintf(stderr, "          min/max are expected to be in the range [0,255]\n");
		fprintf(stderr, "        * palette is the name of a palette from the viewer options,\n");
		fprintf(stderr, "          but without spaces (e.g. Jet or GreenFireBlue);\n");
		fprintf(stderr, "          ('None' indicates no palette (aka gray));\n");
		fprintf(stderr, "        * density is expected to be in range 0.0 < n <= 1.0,\n");
		fprintf(stderr, "          but if no value is specified, density is adaptive,\n");
		fprintf(stderr, "          where brighter pixel clusters are assumed to be denser\n");
		fprintf(stderr, "          (XXX adaptive density is experimental; don't use it)\n");
		fprintf(stderr, "        * e.g. --points out.ply 20,255,None,0.25\n");
		return -1;
	}
	
	/* parse arguments */
	for (i = 1; i < argc - 1; ++i)
	{
		const char *this = argv[i];
		const char *next = argv[i + 1];
		
		/* arguments start with -- */
		if (strlen(this) >= 2 && !memcmp(this, "--", 2))
			this += 2;
		
		if (!strcmp(this, "binary"))
		{
			if (sscanf(next, "%d,%d", &width, &height) != 2)
			{
				fprintf(stderr, "argument '%s %s' malformatted\n", this, next);
				return -1;
			}
			
			isBinary = true;
			
			i += 1;
		}
		else if (!strcmp(this, "threads"))
		{
			isThreaded = true;
		}
		else if (!strcmp(this, "viewer"))
		{
			showViewer = true;
		}
		else if (!strcmp(this, "series"))
		{
			if (sscanf(next, "%d,%d", &series_low, &series_high) != 2)
			{
				fprintf(stderr, "argument '%s %s' malformatted\n", this, next);
				return -1;
			}
			
			isSeries = true;
			
			i += 1;
		}
		else if (!strcmp(this, "dump"))
		{
			dump = next;
			
			i += 1;
		}
		else if (!strcmp(this, "invivo"))
		{
			const char *extra = argv[i + 2];
			int got;
			
			invivo = next;
			
			got = sscanf(extra, "%[^,],%[^,],%s", invivo_last, invivo_first, invivo_dob);
			
			if (got != 3)
			{
			L_invivo_fail:
				fprintf(stderr, "argument '%s %s %s' malformatted\n", this, next, extra);
				return -1;
			}
			
			/* not YYYYMMDD format */
			if (strlen(invivo_dob) != 8 || strspn(invivo_dob, "0123456789") != 8)
				goto L_invivo_fail;
			
			i += 2;
		}
		else if (!strcmp(this, "points"))
		{
			char palname[1024];
			const char *extra = argv[i + 2];
			int got;
			
			points = next;
			
			got = sscanf(extra, "%d,%d,%[^,],%f", &points_minv, &points_maxv, palname, &points_density);
			
			/* density is optional; if not specified, use adaptive density */
			if (got == 3)
				points_density = 0;
			
			/* not enough parameters */
			if (got < 3)
			{
			L_points_fail:
				fprintf(stderr, "argument '%s %s %s' malformatted\n", this, next, extra);
				return -1;
			}
			
			/* sanity checks */
			if (points_minv < 0 || points_minv > 255)
			{
				fprintf(stderr, "minv bad value, expected value 0 <= n <= 255\n");
				goto L_points_fail;
			}
			if (points_maxv < 0 || points_maxv > 255)
			{
				fprintf(stderr, "maxv bad value, expected value 0 <= n <= 255\n");
				goto L_points_fail;
			}
			if (got > 2 && (points_density <= 0 || points_density > 1))
			{
				fprintf(stderr, "density bad value, expected value 0.0 < n <= 1.0\n");
				goto L_points_fail;
			}
			if ((points_palette = palette_find(palname)) < -1)
			{
				fprintf(stderr, "unknown palette '%s'\n", palname);
				goto L_points_fail;
			}
			
			i += 2;
		}
		else
		{
			fprintf(stderr, "unknown argument '%s'\n", this);
			return -1;
		}
	}
	
	/* sanity check */
	if (isBinary && isSeries)
	{
		fprintf(stderr, "error: both --binary and --series used\n");
		return -1;
	}
	
	/* load inv file */
	if (isBinary)
	{
		if (!(inv = inv_load_binary(fn, width, height)))
			return -1;
	}
	else if (isSeries)
	{
		if (!(inv = inv_load_series(fn, series_low, series_high)))
			return -1;
	}
	else
	{
		if (!(inv = inv_load(fn, isThreaded)))
			return -1;
	}
	
	/* dump inv file to raw 16-bit image strip */
	if (dump && inv_dump(inv, dump))
		return -1;
	
	/* dump inv file to point cloud */
	if (points && inv_dump_pointcloud(inv, points, points_minv, points_maxv, points_palette, points_density))
		return -1;
	
	/* write Invivo .inv file */
	if (invivo && inv_write(inv, invivo, invivo_first, invivo_last, invivo_dob))
		return -1;
	
	/* viewer */
	if (showViewer)
	{
		int num = inv_get_num_images(inv);
		int w = inv_get_width(inv);
		int h = inv_get_height(inv);
		int big = max3(w, h, num);
		int arr[] = { num, w, h }; // num images per plane: axial, sagittal, coronal order
		int where[] = { arr[0] / 2, arr[1] / 2, arr[2] / 2 };
		int where_last[] = { -1, -1, -1 };
		float where_precise[] = { where[0], where[1], where[2] };
		float where_percent[] = { 0, 0, 0 };
		int is_animated[] = { 0, 0, 0 };
		int palette = -1;
		uint16_t *pix = calloc(big * big, sizeof(*pix));
		bool show_axis_guides = false;
		
		if (!(viewer = viewer_create(w, h, num)))
			return -1;
		for (;;)
		{
			int i;
			const char *plane_name[] = { "Axial", "Sagittal", "Coronal", "Patient Info" };
			
			if (viewer_events(viewer))
				break;
			
			viewer_clear(viewer);
			
			for (i = 0; i < INV_PLANE_NUM; ++i)
			{
				int w;
				int h;
				
				if (is_animated[i])
				{
					where_precise[i] = fmod(where_precise[i] + 1 * is_animated[i], arr[i]);
					where[i] = where_precise[i];
				}
				else
				{
					/* handle mousewheel */
					where[i] += viewer_get_mouse_wheel_in_quadrant(viewer, i);
					
					/* if not animated, where takes priority over where_precise */
					where_precise[i] = where[i];
				}
				
				/* bounds checking */
				if (where[i] < 0)
					where_precise[i] = where[i] = arr[i] - 1;
				else if (where[i] >= arr[i])
					where_precise[i] = where[i] = 0;
				
				/* percent of each axis, used for axis guide */
				where_percent[i] = where_precise[i] / arr[i];
				
				/* optimization: only reprocess on change */
				if (where[i] == where_last[i])
					continue;
				where_last[i] = where[i];
				
				/* write every nth frame */
				global_image_index = !(where[i] % 7) ? where[i] : -1;
				
				viewer_get_dim(viewer, i, &w, &h);
				inv_get_plane(inv, pix, where[i], i);
				inv_make_8bit(pix, w, h);
				viewer_upload_pixels(viewer, pix, w, h, i);
			}
			
			/* update axis guides */
			viewer_set_axes(viewer, show_axis_guides, where_percent);
			
			/* draw quadrants and process axis guide changes */
			{
				float where_percent_old[INV_PLANE_NUM];
				
				for (i = 0; i < INV_PLANE_NUM; ++i)
					where_percent_old[i] = where_percent[i];
				
				/* this function can change the axis guide values */
				viewer_draw_quadrants(viewer);
				
				for (i = 0; i < INV_PLANE_NUM; ++i)
				{
					if (where_percent[i] != where_percent_old[i])
					{
						where_precise[i] = where_percent[i] * arr[i];
						where[i] = where_precise[i];
						where_last[i] = -1;
					}
				}
			}
			
			/* draw name above each quadrant */
			for (i = 0; i <= INV_PLANE_NUM; ++i)
			{
				int x;
				int y;
				int h;
				int pad = 2;
				
				viewer_get_quadrant(viewer, i % 2, i / 2, &x, &y);
				y += (h = viewer_label(viewer, plane_name[i], x, y));
				
				/* controls and more will live here */
				if (i == INV_PLANE_NUM)
				{
					int p;
					int indent = 10;
					
					/* patient info */
					x += indent;
					{
						viewer_label(viewer, formatdate(inv_get_imagedate(inv)), x + 180, y - h);
						y += viewer_label(viewer, inv_get_patient_name(inv), x, y);
						y += viewer_label(viewer, formatdate(inv_get_patient_birthday(inv)), x, y);
						y += viewer_label(viewer, inv_get_watermark(inv), x, y);
					}
					x -= indent;
					
					/* anatomical planes */
					y += h;
					if (viewer_button(viewer, "Reset", x + 200, y))
						for (p = 0; p < INV_PLANE_NUM; ++p)
							where_precise[p] = (where[p] = arr[p] / 2);
					y += viewer_label(viewer, "Anatomical Planes", x, y);
					x += indent;
					for (p = 0; p < INV_PLANE_NUM; ++p)
					{
						y += viewer_label(viewer, plane_name[p], x, y);
						x += indent;
						{
							int w = 100;
							int h;
							int old = where[p];
							
							h = viewer_slider_int(viewer, x, y, w, &where[p], 0, arr[p] - 1);
							if (where[p] != old)
								where_precise[p] = where[p];
							
							x += w + indent;
							{
								char buf[16];
								
								snprintf(buf, sizeof(buf), "%d", where[p]);
								
								if (viewer_button(viewer, "<", x, y) && !is_animated[p])
									where[p] -= 1;
								viewer_label_inverted(viewer, buf, x - w, y);
								if (viewer_button(viewer, ">", x + 24, y) && !is_animated[p])
									where[p] += 1;
								if (viewer_button(viewer, "<<", x + 48, y))
									is_animated[p] = -1;
								if (viewer_button(viewer, ">>", x + 48 + 32, y))
									is_animated[p] = 1;
								if (is_animated[p] && viewer_button(viewer, "@", x + 48 + 32 + 32, y))
									is_animated[p] = 0;
							}
							x -= w + indent;
							
							y += h;
						}
						x -= indent;
					}
					x -= indent;
					
					/* misc */
					y += h;
					y += viewer_label(viewer, "Settings", x, y);
					x += indent;
					{
						char buf[1024];
						int w = 200;
						
						/* palette */
						snprintf(buf, sizeof(buf), "Palette: %s", palette_name(palette));
						x += w;
						{
							int palette_old = palette;
							
							if (viewer_button(viewer, "<", x, y))
								palette -= 1;
							if (viewer_button(viewer, ">", x + 24, y))
								palette += 1;
							
							/* on change, queue refresh */
							if (palette != palette_old)
								for (p = 0; p < INV_PLANE_NUM; ++p)
									where_last[p] = -1;
							
							/* wrapping */
							if (palette >= palette_count())
								palette = -1;
							else if (palette < -1)
								palette = palette_count() - 1;
							
							viewer_set_palette(viewer, palette);
						}
						x -= w;
						y += viewer_label(viewer, buf, x, y) + pad;
						
						/* invert intensity */
						{
							static bool inverted = false;
							const char *result = inverted ? "*" : " ";
							
							if (viewer_button(viewer, result, x, y))
							{
								inverted = !inverted;
								viewer_set_inverted(viewer, inverted);
								
								/* on change, queue refresh */
								for (p = 0; p < INV_PLANE_NUM; ++p)
									where_last[p] = -1;
							}
						}
						y += viewer_label(viewer, "Invert Intensity", x + 24, y) + pad;
						
						/* show axis guides */
						{
							const char *result = show_axis_guides ? "*" : " ";
							
							if (viewer_button(viewer, result, x, y))
								show_axis_guides = !show_axis_guides;
						}
						y += viewer_label(viewer, "Show Axis Guides", x + 24, y) + pad;
					}
					x -= indent;
				}
			}
			
			viewer_show(viewer);
		}
		viewer_destroy(viewer);
		free(pix);
	}
	
	/* valgrind test */
	#if 1
	{
		int num = inv_get_num_images(inv);
		int w = inv_get_width(inv);
		int h = inv_get_height(inv);
		int big = max3(w, h, num);
		uint16_t *pix = calloc(big * big, sizeof(*pix));
		
		for (int frame = 0; frame < big; ++frame)
			inv_get_plane(inv, pix, frame, INV_PLANE_AXIAL);
		for (int frame = 0; frame < big; ++frame)
			inv_get_plane(inv, pix, frame, INV_PLANE_SAGITTAL);
		for (int frame = 0; frame < big; ++frame)
			inv_get_plane(inv, pix, frame, INV_PLANE_CORONAL);
		
		inv_make_8bit(pix, big, big);
		
		free(pix);
	}
	#endif
	
	/* cleanup */
	inv_free(inv);
	return 0;
}
