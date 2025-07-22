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
#include "../../events/SDL_keyboard_c.h"

#include "SDL_xbkeyboard.h"

/* USB libraries. */
#include <usbh_lib.h>
#include <usbh_hid.h>

#define USB_SUBCLASS_BOOT 1
#define USB_PROTOCOL_KEYBOARD 1

static void
XBOX_KeyToUNICODE(SDL_Scancode sc)
{
	/* this is hardcoded for a US QWERTY keyboard */
	SDL_Keymod mod;
	char text[2] = {0};

	mod = SDL_GetModState();

	/* this is concurring with existing SDL practice. */
	if (mod & KMOD_CTRL)
		return;

	if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z) {
		/* good day! these are easy */
		text[0] = (sc - SDL_SCANCODE_A) + 65;

		if (!((mod & KMOD_CAPS) || (mod & KMOD_SHIFT)))
			text[0] |= 0x20; /* inline tolower() */
	} else if (sc >= SDL_SCANCODE_KP_1 && sc <= SDL_SCANCODE_KP_0 && (mod & KMOD_NUM)) {
		text[0] = (sc == SDL_SCANCODE_KP_0)
			? '0'
			: ((sc - SDL_SCANCODE_KP_1) + '1');
	} else {
		/* okay, now things get a little complicated */
		switch (sc) {
#define KT(SCANCODE, NORMAL, SHIFT) \
	case SDL_SCANCODE_##SCANCODE: text[0] = (mod & KMOD_SHIFT) ? (SHIFT) : (NORMAL); break

		KT(MINUS, '-', '_');
		KT(EQUALS, '=', '+');
		KT(LEFTBRACKET, '[', '{');
		KT(RIGHTBRACKET, ']', '}');
		KT(BACKSLASH, '\\', '|');
		KT(1, '1', '!');
		KT(2, '2', '@');
		KT(3, '3', '#');
		KT(4, '4', '$');
		KT(5, '5', '%');
		KT(6, '6', '^');
		KT(7, '7', '&');
		KT(8, '8', '*');
		KT(9, '9', '(');
		KT(0, '0', ')');
		KT(GRAVE, '`', '~');
		KT(COMMA, ',', '<');
		KT(PERIOD, '.', '>');
		KT(SLASH, '/', '?');

#undef KT

		/* keys without SHIFT variants. */
#define KT(SCANCODE, ASCII) \
	case SDL_SCANCODE_##SCANCODE: text[0] = (ASCII); break
		KT(KP_DIVIDE, '/');
		KT(KP_MULTIPLY, '*');
		KT(KP_MINUS, '-');
		KT(KP_PLUS, '+');
		KT(KP_PERIOD, '.');

#undef KT

		default: break;
		}
	}

	if (*text)
		SDL_SendKeyboardText(text);
}

static void
XBOX_KeyboardCallback(struct usbhid_dev *dev, uint16_t ep_addr, int status, uint8_t *rdata, uint32_t data_len)
{
	struct XBOX_keyboard *kbd;
	size_t i;

	/* Layout of data:
	 * [0]   = modifiers
	 * [1]   = reserved
	 * [2-7] = keycodes */

	kbd = dev->user_data;

	if (data_len != 8) {
		/* something's definitely wrong, or we got an invalid device.
		 * reset it back to NULL, and hope we get a good one next poll */
		kbd->fd = NULL;
		return;
	}

	{
		/* Modifiers have changed! */
		static const struct {
			uint8_t hidmask;
			SDL_Scancode scancode;
		} translation[] = {
			{0x01, SDL_SCANCODE_LCTRL},
			{0x02, SDL_SCANCODE_LSHIFT},
			{0x04, SDL_SCANCODE_LALT},
			{0x08, SDL_SCANCODE_LGUI},
			{0x10, SDL_SCANCODE_RCTRL},
			{0x20, SDL_SCANCODE_RSHIFT},
			{0x40, SDL_SCANCODE_RALT},
			{0x80, SDL_SCANCODE_RGUI},
		};

		for (i = 0; i < SDL_arraysize(translation); i++) {
			if (!(rdata[0] & translation[i].hidmask)
				&& (kbd->ordata[0] & translation[i].hidmask))
				SDL_SendKeyboardKey(SDL_RELEASED, translation[i].scancode);

			if ((rdata[0] & translation[i].hidmask)
				&& !(kbd->ordata[0] & translation[i].hidmask))
				SDL_SendKeyboardKey(SDL_PRESSED, translation[i].scancode);
		}
	}

	/* rdata[1] is "reserved" */

	for (i = 2; i < 8; i++) {
		if (kbd->ordata[i] > 3) {
			if (!memchr(rdata + 2, kbd->ordata[i], 6))
				SDL_SendKeyboardKey(SDL_RELEASED, kbd->ordata[i]);
		}

		if (rdata[i] > 3) {
			if (!memchr(kbd->ordata + 2, rdata[i], 6)) {
				SDL_SendKeyboardKey(SDL_PRESSED, rdata[i]);
				XBOX_KeyToUNICODE(rdata[i]);
			}
		}
	}

	memcpy(kbd->ordata, rdata, 8);
}

int
XBOX_KeyboardInit(struct XBOX_keyboard *kbd)
{
	usbh_core_init();
	usbh_hid_init();

	memset(kbd, 0, sizeof(*kbd));

	return 0;

	(void)kbd;
}

void
XBOX_KeyboardQuit(struct XBOX_keyboard *kbd)
{
	usbh_hid_stop_int_read(kbd->fd, 0);

	/* usbh_core_deinit(); -- see comment in SDL_xboxjoystick.c */

	memset(kbd, 0, sizeof(*kbd));
}

void
XBOX_KeyboardPoll(struct XBOX_keyboard *kbd)
{
	struct usbhid_dev *device;

	usbh_pooling_hubs();

	if (kbd->fd) {
		if (kbd->fd->user_data) {
			/* TODO: we need to manage the state of the LEDs here. */
			return;
		}

		/* keyboard was disconnected. */
		SDL_ResetKeyboard();
		kbd->fd = NULL;
	}

	/* if we don't have a keyboard yet (or it was destroyed), scan for one. */
	for (device = usbh_hid_get_device_list(); device; device = device->next) {
		uint8_t protocol;

		if (device->bProtocolCode != USB_PROTOCOL_KEYBOARD || device->bSubClassCode != USB_SUBCLASS_BOOT)
			continue;

		if (usbh_hid_get_protocol(device, &protocol) != 0)
			continue;

		if (protocol != 0) {
			if (usbh_hid_set_protocol(device, 0) != 0)
				continue;

			if (usbh_hid_get_protocol(device, &protocol) != 0)
				continue;

			/* sigh */
			if (protocol != 0)
				continue;
		}

		/* hm. this is probably ok. */
		kbd->fd = device;
	}

	if (!kbd->fd)
		return;

	/* let the callback know about us */
	kbd->fd->user_data = kbd;

	/* now, start the callback. */
	usbh_hid_start_int_read(kbd->fd, 0, XBOX_KeyboardCallback);
}

/* vi: set ts=4 sw=4 expandtab: */
