#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include <stdbool.h>
#include <SDL2/SDL.h>
#include "palette.h"

#define WINDOW_NAME "Invivo CBCT Viewer"
#define VP_W 300 // a single quadrant
#define VP_H 300
#define BUF_NUM  3
#define TEXT_PAD 4

#define COMMON_CONTROL_H 15

#define SDL_EZTEXT_IMPLEMENTATION
#include "SDL_EzText/SDL_EzText.h"

struct viewer_mouse
{
	int x;
	int y;
	bool is_held;
	bool was_pressed;
};

struct viewer
{
	SDL_Window *window;
	SDL_Renderer *renderer;
	SDL_Texture *buf[BUF_NUM];
	int im_x;
	int im_y;
	int im_z;
	int palette;
	struct viewer_mouse mouse;
	struct color
	{
		uint8_t r;
		uint8_t g;
		uint8_t b;
	} contrast;
	bool is_inverted;
};

static bool is_mouse_in_rect(struct viewer *v, int x, int y, int w, int h)
{
	struct viewer_mouse m;
	
	assert(v);
	
	m = v->mouse;
	
	return m.x >= x
		&& m.y >= y
		&& m.x <= x + w
		&& m.y <= y + h
	;
}

static bool is_mouse_held(struct viewer *v)
{
	assert(v);
	
	return v->mouse.is_held;
}

static bool was_mouse_pressed(struct viewer *v)
{
	assert(v);
	
	return v->mouse.was_pressed;
}

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
	
	v->mouse.was_pressed = false;
	
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
					
					/*
					case SDLK_p:
						v->palette += 1;
						v->palette %= palette_count();
						fprintf(stderr, "palette = '%s'\n", palette_name(v->palette));
						break;
					*/
				}
				break;
			
			case SDL_MOUSEMOTION:
				v->mouse.x = event.motion.x;
				v->mouse.y = event.motion.y;
				break;
			
			case SDL_MOUSEBUTTONDOWN:
				v->mouse.is_held = true;
				break;
			
			case SDL_MOUSEBUTTONUP:
				if (v->mouse.is_held)
					v->mouse.was_pressed = true;
				v->mouse.is_held = false;
				break;
		}
	}
	
	return rval;
	(void)v;
}

void viewer_clear(struct viewer *v)
{
	SDL_Renderer *ren = v->renderer;
	uint8_t red = 0;
	uint8_t green = 0;
	uint8_t blue = 0;
	uint32_t rgb;
	
	if (v->is_inverted)
		red = green = blue = 255;
	
	if (v->palette >= 0)
	{
		uint8_t tmp[3];
		
		palette_color(tmp, v->palette, red);
		
		red   = tmp[0];
		green = tmp[1];
		blue  = tmp[2];
	}
	
	SDL_SetRenderDrawColor(ren, red, green, blue, -1);
	SDL_RenderClear(ren);
	
	/* get best font color to contrast against background */
	rgb = (red << 16) | (green << 8) | blue;
	rgb = best_contrast(rgb);
	v->contrast.r = rgb >> 16;
	v->contrast.g = rgb >> 8;
	v->contrast.b = rgb;
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
		int s = *src8;
		
		if (v->is_inverted)
			s = 255 - s;
		
		dst8[0] = 0xff; // opacity
		dst8[1] = s;
		dst8[2] = s;
		dst8[3] = s;
		
		// override gray shade with palette color
		if (v->palette >= 0)
		{
			uint8_t tmp[3];
			
			palette_color(tmp, v->palette, s);
			
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

int viewer_slider_int(struct viewer *v, int x, int y, int w, int *n, int lo, int hi)
{
	int h = COMMON_CONTROL_H;
	int a;
	
	assert(v);
	assert(n);
	assert(lo < hi);
	
	a = v->contrast.r < 127 ? 0x80 : 0xc0;
	
	if (is_mouse_in_rect(v, x, y, w, h) && is_mouse_held(v))
		*n = lo + (((float)(v->mouse.x - x)) / w) * (hi - lo);
	
	SDL_SetRenderDrawBlendMode(v->renderer, SDL_BLENDMODE_BLEND);
	SDL_SetRenderDrawColor(v->renderer, v->contrast.r, v->contrast.g, v->contrast.b, a);
	SDL_RenderFillRect(v->renderer, &(SDL_Rect){x, y, w, h});
	SDL_SetRenderDrawColor(v->renderer, v->contrast.r, v->contrast.g, v->contrast.b, 0xff);
	SDL_RenderFillRect(v->renderer, &(SDL_Rect){x, y, ((float)*n) / (hi - lo) * w, h});
	
	return SDL_EzTextStringHeight("A");
}

int viewer_label_inverted(struct viewer *v, const char *str, int x, int y)
{
	int z;
	int retval;
	
	assert(v);
	
	/* inverted color state */
	z = 255 - v->contrast.r;
	SDL_EzText_SetColor((z << 24) | (z << 16) | (z << 8) | 0xff);
	
	/* display */
	retval = viewer_label(v, str, x, y);
	
	/* restore state */
	z = v->contrast.r;
	SDL_EzText_SetColor((z << 24) | (z << 16) | (z << 8) | 0xff);
	
	return retval;
}

bool viewer_button(struct viewer *v, const char *str, int x, int y)
{
	int h = COMMON_CONTROL_H;
	bool is_hovered = false;
	bool is_inverted = false;
	int w = SDL_EzTextStringWidth(str) + 4 * 2;
	
	assert(v);
	
	if (is_mouse_in_rect(v, x, y, w, h))
	{
		is_hovered = true;
		
		if (is_mouse_held(v))
			is_inverted = true;
	}
	
	SDL_SetRenderDrawBlendMode(v->renderer, SDL_BLENDMODE_BLEND);
	if (is_hovered)
	{
		int a = 0x60;
		
		if (is_inverted)
			a = 0xff;
		else if (v->contrast.r == 0) // weaker contrast on lighter backgrounds
			a = 0x30;
		
		SDL_SetRenderDrawColor(v->renderer, v->contrast.r, v->contrast.g, v->contrast.b, a);
		SDL_RenderFillRect(v->renderer, &(SDL_Rect){x, y, w, h});
	}
	SDL_SetRenderDrawColor(v->renderer, v->contrast.r, v->contrast.g, v->contrast.b, 0xff);
	SDL_RenderDrawRect(v->renderer, &(SDL_Rect){x, y, w, h});
	
	if (is_inverted)
	{
		int z = 255 - v->contrast.r;
		SDL_EzText_SetColor((z << 24) | (z << 16) | (z << 8) | 0xff);
	}
	
	viewer_label(v, str, x + 4, y);
	
	if (is_inverted)
	{
		int z = v->contrast.r;
		SDL_EzText_SetColor((z << 24) | (z << 16) | (z << 8) | 0xff);
	}
	
	return is_hovered && was_mouse_pressed(v);
}

void viewer_set_palette(struct viewer *v, int palette)
{
	assert(v);
	
	v->palette = palette;
}

void viewer_set_inverted(struct viewer *v, bool is_inverted)
{
	assert(v);
	
	v->is_inverted = is_inverted;
}
