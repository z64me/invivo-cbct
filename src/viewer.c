#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <SDL2/SDL.h>

#define WINDOW_NAME "Invivo CBCT Viewer"
#define IM_W 536 // TODO make dimensions not hard-coded
#define IM_H 536
#define VP_W 256 // a single viewport
#define VP_H 256
#define BUF_NUM  3

struct viewer
{
	SDL_Window *window;
	SDL_Renderer *renderer;
	SDL_Texture *buf[BUF_NUM];
};

struct viewer *viewer_create(void)
{
	struct viewer *v;
	int i;
	
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
	
	for (i = 0; i < BUF_NUM; ++i)
		v->buf[i] = SDL_CreateTexture(
			v->renderer
			, SDL_PIXELFORMAT_RGBA8888
			, SDL_TEXTUREACCESS_STREAMING
			, IM_W
			, IM_H
		);
	
	return v;
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
				}
				break;
		}
	}
	
	return rval;
	(void)v;
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
	}
	
	SDL_UnlockTexture(tex);
}

int viewer_draw(struct viewer *v)
{
	SDL_Renderer *ren = v->renderer;
	
	SDL_RenderClear(ren);
	SDL_RenderCopy(ren, v->buf[0], 0, &(SDL_Rect){0, 0, VP_W, VP_H});
	SDL_RenderCopy(ren, v->buf[1], 0, &(SDL_Rect){VP_W, 0, VP_W, VP_H});
	SDL_RenderCopy(ren, v->buf[2], 0, &(SDL_Rect){0, VP_H, VP_W, VP_H});
	SDL_RenderPresent(ren);
	
	return 0;
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
