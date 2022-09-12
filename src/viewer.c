#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include <SDL2/SDL.h>
#include "palette.h"

#define WINDOW_NAME "Invivo CBCT Viewer"
#define VP_W 256 // a single viewport
#define VP_H 256
#define BUF_NUM  3
#define TEXT_PAD 4

#define SDL_EZTEXT_IMPLEMENTATION
#include "SDL_EzText/SDL_EzText.h"

struct viewer
{
	SDL_Window *window;
	SDL_Renderer *renderer;
	SDL_Texture *buf[BUF_NUM];
	int im_x;
	int im_y;
	int im_z;
	int palette;
};

/* get the best contrasting font color against a background color */
static uint32_t best_contrast(uint32_t background_rgb)
{
	uint8_t red = background_rgb >> 16;
	uint8_t green = background_rgb >> 8;
	uint8_t blue = background_rgb;
	double yiq = (299 * red + 587 * green + 114 * blue) / 1000;
	
	return yiq >= 128 ? 0 : -1;
	//return background_rgb >= 0xffffff / 2 ? 0 : -1;
}

void viewer_get_quadrant(struct viewer *v, int x, int y, int *ul_x, int *ul_y)
{
	assert(v);
	
	if (ul_x)
		*ul_x = x * VP_W + TEXT_PAD;
	
	if (ul_y)
		*ul_y = y * VP_H + TEXT_PAD;
}

struct viewer *viewer_create(int x, int y, int z)
{
	struct viewer *v;
	int i;
	int dim[3][2] = { // XXX same order as enum inv_plane
		{x, y} // axial
		, {y, z} // sagittal
		, {x, z} // coronal
	};
	
	v = calloc(1, sizeof(*v));
	
	// linear filtering = smooth scaling
	SDL_SetHint("SDL_HINT_RENDER_SCALE_QUALITY", "1");
	
	v->window = SDL_CreateWindow(
		WINDOW_NAME
		, SDL_WINDOWPOS_UNDEFINED
		, SDL_WINDOWPOS_UNDEFINED
		, VP_W * 2 // window houses 2x2 viewports
		, VP_H * 2
		, SDL_WINDOW_SHOWN
	);
	
	if (!v->window)
		return 0;
	
	v->renderer = SDL_CreateRenderer(
		v->window
		, -1
		, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
	);
	
	if (!v->renderer)
		return 0;
	
	v->im_x = x;
	v->im_y = y;
	v->im_z = z;
	
	// palette display disabled by default
	v->palette = -1;
	
	for (i = 0; i < BUF_NUM; ++i)
		v->buf[i] = SDL_CreateTexture(
			v->renderer
			, SDL_PIXELFORMAT_RGBA8888
			, SDL_TEXTUREACCESS_STREAMING
			, dim[i][0]
			, dim[i][1]
		);
	
	return v;
}

void viewer_get_dim(struct viewer *v, int i, int *w, int *h)
{
	assert(v);
	assert(i < BUF_NUM);
	assert(i >= 0);
	
	SDL_QueryTexture(v->buf[i], 0, 0, w, h);
}

int viewer_events(struct viewer *v)
{
	SDL_Event event;
	int rval = 0;
	
	while (SDL_PollEvent(&event))
	{
		switch (event.type)
		{
			case SDL_QUIT:
				rval = 1;
				break;
			
			case SDL_KEYDOWN:
				switch (event.key.keysym.sym)
				{
					case SDLK_ESCAPE:
						rval = 1;
						break;
					
					case SDLK_p:
						v->palette += 1;
						v->palette %= palette_count();
						fprintf(stderr, "palette = '%s'\n", palette_name(v->palette));
						break;
				}
				break;
		}
	}
	
	return rval;
	(void)v;
}

void viewer_clear(struct viewer *v)
{
	SDL_Renderer *ren = v->renderer;
	int red = 0;
	int green = 0;
	int blue = 0;
	uint32_t rgb;
	
	if (v->palette >= 0)
	{
		uint8_t tmp[3];
		
		palette_color(tmp, v->palette, 0);
		
		red   = tmp[0];
		green = tmp[1];
		blue  = tmp[2];
	}
	
	SDL_SetRenderDrawColor(ren, red, green, blue, -1);
	SDL_RenderClear(ren);
	
	/* get best font color to contrast against background */
	rgb = (red << 16) | (green << 8) | blue;
	rgb = best_contrast(rgb);
	SDL_EzText_SetColor((rgb << 8) | 0xff);
}

void viewer_upload_pixels(struct viewer *v, const void *src, int srcW, int srcH, int idx)
{
	const uint8_t *src8 = src;
	uint8_t *dst8;
	void *dst;
	int pitch;
	int i;
	
	SDL_Texture *tex = v->buf[idx];
	
	SDL_LockTexture(tex, 0, &dst, &pitch);
	
	dst8 = dst;
	for (i = 0; i < srcW * srcH; ++i, src8 += 1, dst8 += 4)
	{
		dst8[0] = 0xff; // opacity
		dst8[1] = *src8;
		dst8[2] = *src8;
		dst8[3] = *src8;
		
		// override gray shade with palette color
		if (v->palette >= 0)
		{
			uint8_t tmp[3];
			
			palette_color(tmp, v->palette, *src8);
			
			dst8[1] = tmp[2];
			dst8[2] = tmp[1];
			dst8[3] = tmp[0];
		}
	}
	
	SDL_UnlockTexture(tex);
}

static float get_aspect(float w, float h)
{
	return w / h;
}

/* draws a texture while preserving its aspect ratio */
static void draw_aspect(SDL_Renderer *ren, SDL_Texture *tex, SDL_Rect dst)
{
	int w;
	int h;
	float aspect;
	float dst_aspect = get_aspect(dst.w, dst.h);
	
	SDL_QueryTexture(tex, 0, 0, &w, &h);
	
	aspect = get_aspect(w, h);
	
	/* maintain aspect ratio using letterboxing/pillarboxing */
	if (aspect != dst_aspect)
	{
		dst.x += dst.w / 2;
		dst.y += dst.h / 2;
		
		if (aspect < dst_aspect)
		{
			h = dst.h;
			w = dst.h * aspect;
		}
		else
		{
			w = dst.w;
			h = dst.w / aspect;
		}
		
		dst.w = w;
		dst.h = h;
		
		dst.x -= dst.w / 2;
		dst.y -= dst.h / 2;
	}
	
	SDL_RenderCopy(ren, tex, 0, &dst);
}

int viewer_draw_quadrants(struct viewer *v)
{
	SDL_Renderer *ren = v->renderer;
	
	draw_aspect(ren, v->buf[0], (SDL_Rect){0, 0, VP_W, VP_H});
	draw_aspect(ren, v->buf[1], (SDL_Rect){VP_W, 0, VP_W, VP_H});
	draw_aspect(ren, v->buf[2], (SDL_Rect){0, VP_H, VP_W, VP_H});
	
	return 0;
}

void viewer_show(struct viewer *v)
{
	assert(v);
	
	SDL_RenderPresent(v->renderer);
}

int viewer_destroy(struct viewer *v)
{
	int i;
	
	for (i = 0; i < BUF_NUM; ++i)
		SDL_DestroyTexture(v->buf[i]);
	SDL_DestroyRenderer(v->renderer);
	SDL_DestroyWindow(v->window);	
	free(v);
	
	return 0;
}

int viewer_label(struct viewer *v, const char *str, int x, int y)
{
	SDL_EzText(v->renderer, x, y, str);
	
	return SDL_EzTextStringHeight(str);
}
