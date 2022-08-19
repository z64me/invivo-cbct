#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include "inv.h"
#include "viewer.h"

int main(int argc, char *argv[])
{
	struct viewer *viewer = 0;
	const char *progname = argv[0];
	const char *fn = argv[argc - 1];
	const char *dump = 0;
	const char *points = 0;
	struct inv *inv;
	bool isBinary = false;
	int width;
	int height;
	int points_minv = 0;
	int points_maxv = 0;
	float points_density = 1;
	int i;
	
	/* show arguments */
	if (argc < 2)
	{
		fprintf(stderr, "args: %s invivo.inv\n", progname);
		fprintf(stderr, "  optional args:\n");
		fprintf(stderr, "    --binary  W,H\n");
		fprintf(stderr, "        indicates input file is binary\n");
		fprintf(stderr, "        data previously exported using\n");
		fprintf(stderr, "        the --dump option\n");
		fprintf(stderr, "        e.g. --binary 536,536\n");
		fprintf(stderr, "    --dump    out.bin\n");
		fprintf(stderr, "        specifies output binary file\n");
		fprintf(stderr, "    --points  out.ply min,max,density\n");
		fprintf(stderr, "        specifies output point cloud file (Stanford .ply);\n");
		fprintf(stderr, "        writes only points in value range [min,max];\n");
		fprintf(stderr, "        min/max expected to be in range [0,255]\n");
		fprintf(stderr, "        density expected to be in range 0.0 < n <= 1.0\n");
		fprintf(stderr, "        e.g. --points out.ply 20,255,0.25\n");
		return -1;
	}
	
	/* parse arguments */
	for (i = 1; i < argc - 1; ++i)
	{
		const char *this = argv[i];
		const char *next = argv[i + 1];
		
		/* arguments start with -- */
		if (strlen(this) < 2)
			goto L_unknown;
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
		else if (!strcmp(this, "dump"))
		{
			dump = next;
			
			i += 1;
		}
		else if (!strcmp(this, "points"))
		{
			const char *extra = argv[i + 2];
			
			points = next;
			
			if (sscanf(extra, "%d,%d,%f", &points_minv, &points_maxv, &points_density) != 3)
			{
			L_points_fail:
				fprintf(stderr, "argument '%s %s %s' malformatted\n", this, next, extra);
				return -1;
			}
			
			// Sanity check values
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
			if (points_density <= 0 || points_density > 1)
			{
				fprintf(stderr, "density bad value, expected value 0.0 < n <= 1.0\n");
				goto L_points_fail;
			}
			
			i += 2;
		}
		else
		{
		L_unknown:
			fprintf(stderr, "unknown argument '%s'\n", this);
			return -1;
		}
	}
	
	/* load inv file */
	if (isBinary)
	{
		if (!(inv = inv_load_binary(fn, width, height)))
			return -1;
	}
	else
	{
		if (!(inv = inv_load(fn)))
			return -1;
	}
	
	/* dump inv file to raw 16-bit image strip */
	if (dump && inv_dump(inv, dump))
		return -1;
	
	/* dump inv file to point cloud */
	if (points && inv_dump_pointcloud(inv, points, points_minv, points_maxv, points_density))
		return -1;
	
	/* viewer */
	{
		int num = inv_get_num_images(inv);
		int w = inv_get_width(inv);
		int h = inv_get_height(inv);
		uint16_t *pix = malloc(w * h * sizeof(*pix));
		
	#if 1 /* valgrind exclude */
		if (!(viewer = viewer_create()))
			return -1;
		for (;;)
		{
			static float frame = 0;
			int i;
			//frame = num / 2 - 1;
			
			if (viewer_events(viewer))
				break;
			
			for (i = 0; i < INV_PLANE_NUM; ++i)
			{
				inv_get_plane(inv, pix, frame, i);
				inv_make_8bit(pix, w, h);
				viewer_upload_pixels(viewer, pix, w, h, i);
			}
			
			viewer_draw(viewer);
			
			frame += 1;
			if (frame >= w)
				frame = 0;
		}
		viewer_destroy(viewer);
	#endif
	#if 0 /* valgrind test */
		for (int frame = 0; frame < w; ++frame)
			inv_get_plane(inv, pix, frame, INV_PLANE_AXIAL);
		for (int frame = 0; frame < w; ++frame)
			inv_get_plane(inv, pix, frame, INV_PLANE_SAGITTAL);
		for (int frame = 0; frame < w; ++frame)
			inv_get_plane(inv, pix, frame, INV_PLANE_CORONAL);
		inv_make_8bit(pix, w, h);
	#endif
		free(pix);
	}
	
	/* cleanup */
	inv_free(inv);
	return 0;
}
