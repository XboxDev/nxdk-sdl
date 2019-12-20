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


#include <hal/video.h>
#include <assert.h>


int SDL_XBOX_CreateWindowFramebuffer(_THIS, SDL_Window * window, Uint32 * format, void ** pixels, int *pitch)
{
    SDL_Surface *surface;
    VIDEO_MODE vm = XVideoGetMode();
    const Uint32 surface_format = pixelFormatSelector(vm.bpp);
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

    VIDEO_MODE vm = XVideoGetMode();

    // Get information about SDL window surface
    const void *src = surface->pixels;
    Uint32 src_format = surface->format->format;
    int src_pitch = surface->pitch;

    // Get information about GPU framebuffer
    void *dst = XVideoGetFB();
    Uint32 dst_format = pixelFormatSelector(vm.bpp);
    int dst_bytes_per_pixel = SDL_BYTESPERPIXEL(dst_format);
    int dst_pitch = vm.width * dst_bytes_per_pixel;

    // Check if the SDL window fits into GPU framebuffer
    int width = surface->w;
    int height = surface->h;
    assert(width <= vm.width);
    assert(height <= vm.height);

    // Copy SDL window surface to GPU framebuffer
    SDL_ConvertPixels(width, height, src_format, src, src_pitch, dst_format, dst, dst_pitch);

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
