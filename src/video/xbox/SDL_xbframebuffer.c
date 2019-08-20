/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2016 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/
#include "../../SDL_internal.h"

#if SDL_VIDEO_DRIVER_XBOX

#include "../SDL_sysvideo.h"
#include "SDL_xbframebuffer_c.h"


#define XBOX_SURFACE   "_SDL_XboxSurface"


#include <pbkit/pbkit.h>
#include <hal/xbox.h>
#include <hal/video.h>
#include <debug.h>


//Screen dimension constants
const extern int SCREEN_WIDTH;
const extern int SCREEN_HEIGHT;
const extern int SCREEN_BPP;

int SDL_XBOX_CreateWindowFramebuffer(_THIS, SDL_Window * window, Uint32 * format, void ** pixels, int *pitch)
{
    SDL_Surface *surface;
    const Uint32 surface_format = SDL_PIXELFORMAT_RGB888;
    int w, h;
    int bpp;
    Uint32 Rmask, Gmask, Bmask, Amask;

    /* Free the old framebuffer surface */
    surface = (SDL_Surface *) SDL_GetWindowData(window, XBOX_SURFACE);
    SDL_FreeSurface(surface);

    /* Create a new one */
    SDL_PixelFormatEnumToMasks(surface_format, &bpp, &Rmask, &Gmask, &Bmask, &Amask);
    SDL_GetWindowSize(window, &w, &h);
    surface = SDL_CreateRGBSurface(0, w, h, bpp, Rmask, Gmask, Bmask, Amask);
    if (!surface) {
        return -1;
    }

    /* Save the info and return! */
    SDL_SetWindowData(window, XBOX_SURFACE, surface);
    *format = surface_format;
    *pixels = surface->pixels;
    *pitch = surface->pitch;
    return 0;
}

int SDL_XBOX_UpdateWindowFramebuffer(_THIS, SDL_Window * window, const SDL_Rect * rects, int numrects)
{
    SDL_Surface *surface;

    surface = (SDL_Surface *) SDL_GetWindowData(window, XBOX_SURFACE);
    if (!surface) {
        return SDL_SetError("Couldn't find Xbox surface for window");
    }

#if 0
    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        for (int x = 0; x < SCREEN_WIDTH; x++) {
            unsigned char *pixel = (uint8_t*)surface->pixels;
            pixel += y*surface->pitch;
            pixel += x*surface->format->BytesPerPixel;

            uint32_t pixel32 = *(uint32_t *)pixel;

            unsigned char *videoBuffer = XVideoGetFB();
            videoBuffer += (y * SCREEN_WIDTH + x) * (SCREEN_BPP >> 3);
            videoBuffer[0] = (pixel32 & surface->format->Bmask) >> surface->format->Bshift;
            videoBuffer[1] = (pixel32 & surface->format->Gmask) >> surface->format->Gshift;
            videoBuffer[2] = (pixel32 & surface->format->Rmask) >> surface->format->Rshift;
            videoBuffer[3] = 0;
        }
    }
#endif

    // Happens to be in a compatible format
    memcpy(XVideoGetFB(), surface->pixels, SCREEN_WIDTH*SCREEN_HEIGHT*4);

    return 0;
}

void SDL_XBOX_DestroyWindowFramebuffer(_THIS, SDL_Window * window)
{
    SDL_Surface *surface;

    surface = (SDL_Surface *) SDL_SetWindowData(window, XBOX_SURFACE, NULL);
    SDL_FreeSurface(surface);
}

#endif /* SDL_VIDEO_DRIVER_XBOX */

/* vi: set ts=4 sw=4 expandtab: */
