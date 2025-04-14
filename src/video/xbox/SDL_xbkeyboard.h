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

#ifndef SDL_xbkeyboard_h_
#define SDL_xbkeyboard_h_

#include "../../SDL_internal.h"

/* forward declare */
struct usbhid_dev;

struct XBOX_keyboard {
	/* this is the USB HID structure */
	struct usbhid_dev *fd;

	/* the last data that we processed */
	uint8_t ordata[8];
};

extern int XBOX_KeyboardInit(struct XBOX_keyboard *kbd);
extern void XBOX_KeyboardQuit(struct XBOX_keyboard *kbd);
extern void XBOX_KeyboardPoll(struct XBOX_keyboard *kbd);

#endif /* SDL_xbkeyboard_h_ */

/* vi: set ts=4 sw=4 expandtab: */
