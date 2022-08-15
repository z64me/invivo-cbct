#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <SDL2/SDL.h>

#define WINDOW_NAME "Invivo CBCT Viewer"
#define IM_W 536 // TODO make dimensions not hard-coded
#define IM_H 536

struct viewer
{
	SDL_Window *window;
	SDL_Renderer *renderer;
	SDL_Texture *buf;
};

struct viewer *viewer_create(void)
{
	struct viewer *v;
	
	v = calloc(1, sizeof(*v));
	
	v->window = SDL_CreateWindow(
		WINDOW_NAME
		, SDL_WINDOWPOS_UNDEFINED
		, SDL_WINDOWPOS_UNDEFINED
		, IM_W
		, IM_H
		, SDL_WINDOW_SHOWN
	);
	
	if (!v->window)
		return 0;
	
	v->renderer = SDL_CreateRenderer(
		v->window,
		-1,
		SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
	);
	
	if (!v->renderer)
		return 0;
	
	v->buf = SDL_CreateTexture(
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

void viewer_upload_pixels(struct viewer *v, const void *src, int srcW, int srcH)
{
	const uint8_t *src8 = src;
	uint8_t *dst8;
	void *dst;
	int pitch;
	int i;
	
	SDL_Texture *tex = v->buf;
	
	SDL_LockTexture(tex, 0, &dst, &pitch);
	
	dst8 = dst;
	for (i = 0; i < srcW * srcH; ++i, src8 += 2, dst8 += 4)
	{
		uint16_t v = ((src8[1] << 8) | (src8[0]));
		float conv = v * (1.0f / 65535.0f) * 255;
		
		// TODO brightness and contrast
		
		dst8[0] = 0xff;
		dst8[1] = conv;
		dst8[2] = conv;
		dst8[3] = conv;
	}
	
	SDL_UnlockTexture(tex);
}

int viewer_draw(struct viewer *v)
{
	SDL_Renderer *ren = v->renderer;
	
	SDL_RenderClear(ren);
	SDL_RenderCopy(ren, v->buf, 0, 0);
	SDL_RenderPresent(ren);
	
	return 0;
}

int viewer_destroy(struct viewer *v)
{
	SDL_DestroyTexture(v->buf);
	SDL_DestroyRenderer(v->renderer);
	SDL_DestroyWindow(v->window);	
	free(v);
	
	return 0;
}
