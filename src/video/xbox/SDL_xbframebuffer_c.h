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
#include "SDL_pixels.h"
#include <assert.h>

extern int SDL_XBOX_CreateWindowFramebuffer(_THIS, SDL_Window * window, Uint32 * format, void ** pixels, int *pitch);
extern int SDL_XBOX_UpdateWindowFramebuffer(_THIS, SDL_Window * window, const SDL_Rect * rects, int numrects);
extern void SDL_XBOX_DestroyWindowFramebuffer(_THIS, SDL_Window * window);

static inline
Uint32 pixelFormatSelector(int bpp) {
    Uint32 ret_val = 0;
    switch(bpp) {
    case 15:
        ret_val = SDL_PIXELFORMAT_RGB555;
        break;
    case 16:
        ret_val = SDL_PIXELFORMAT_RGB565;
        break;
    case 32:
        ret_val = SDL_PIXELFORMAT_ARGB8888;
        break;
    default:
        assert(0);
        break;
    }
    return ret_val;
}

/* vi: set ts=4 sw=4 expandtab: */
