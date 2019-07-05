/*
  MIT License

  Copyright (c) 2019 Lucas Eriksson

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.
*/

#include "../../SDL_internal.h"

#if SDL_JOYSTICK_XBOX

#include "SDL_joystick.h"
#include "SDL_events.h"
#include "../SDL_joystick_c.h"
#include "../SDL_sysjoystick.h"
#include <hal/input.h>

void USBGetEvents(void);

#include <assert.h>
#include <stdbool.h>

#define NUM_AXES 6  /* LStickY, LStickX, LTrigg, RStickY, RStickX, RTrigg */
#define NUM_BUTTONS 10 /* A, B, X, Y, black, white, back, start, LThumb, RThumb */
#define NUM_HATS 1  /* D-pad */
#define NUM_BALLS 0  /* No balls here */

#define BUTTON_DEADZONE 0x20

typedef struct joystick_hwdata
{
    XPAD_INPUT *padData;
} joystick_hwdata, *pjoystick_hwdata;

static int SDL_XBOX_JoystickInit(void) {
    XInput_Init();
    return 0;
}

static bool joystickConnected(SDL_Joystick *joystick) {
    return g_Pads[joystick->player_index].hPresent != 0;
}

static void SDL_XBOX_JoystickQuit(void) {
    XInput_Quit();
}

static int SDL_XBOX_JoystickGetCount() {
    return XInputGetPadCount();
}

static void SDL_XBOX_JoystickDetect() {
    USBGetEvents();
    return;
}

static const char* SDL_XBOX_JoystickNameForDeviceIndex(int index) {
    switch (index) {
    case 0:
        return "Original Xbox Controller #1";
    case 1:
        return "Original Xbox Controller #2";
    case 2:
        return "Original Xbox Controller #3";
    case 3:
        return "Original Xbox Controller #4";
    default:
        /* FIXME: NXDK does not yet support more than 4 gamepads */
        assert(false);
        return "Invalid device index";
    }
}

static int SDL_XBOX_JoystickGetDevicePlayerIndex(int index) {
    return index;
}

static SDL_JoystickGUID SDL_XBOX_JoystickGetDeviceGUID(int index) {
    /* FIXME: This should be implemented properly */
    SDL_JoystickGUID ret;
    SDL_memset(&ret, 0, sizeof(SDL_JoystickGUID));
    return ret;
}

static SDL_JoystickID SDL_XBOX_JoystickGetDeviceInstanceID(int index) {
    return index;
}

static int SDL_XBOX_JoystickOpen(SDL_Joystick *joystick, int index) {
    assert(index >= 0);
    assert(index <= 3);
    if (g_Pads[index].hPresent == 0) {
        return -1;
    }

    joystick->player_index = index;

    joystick->hwdata = (pjoystick_hwdata)malloc(sizeof(joystick_hwdata));
    joystick->hwdata->padData = &g_Pads[index];
    joystick->guid = SDL_XBOX_JoystickGetDeviceGUID(index);

    joystick->naxes    = NUM_AXES;
    joystick->nballs   = NUM_BALLS;
    joystick->nhats    = NUM_HATS;
    joystick->nbuttons = NUM_BUTTONS;

    return 0;
}

static bool analogButtonPressed(SDL_Joystick *joystick, int button_index) {
    assert(button_index >= 0);
    assert(button_index <= 7);
    return joystick->hwdata->padData->CurrentButtons.ucAnalogButtons[button_index] > BUTTON_DEADZONE;
}

static bool digitalButtonPressed(SDL_Joystick *joystick, int button_bit) {
    assert(button_bit > 0);
    assert((button_bit & (button_bit - 1)) == 0);
    assert(button_bit <= 128);
    return (joystick->hwdata->padData->CurrentButtons.usDigitalButtons & button_bit) != 0;
}

static void analogButtonUpdate(SDL_Joystick *joystick, int button, int index) {
    assert(index >= 0);
    assert(index <= NUM_BUTTONS);
    if (analogButtonPressed(joystick, button) != joystick->buttons[index]) {
        SDL_PrivateJoystickButton(joystick, (Uint8)index,
                                  joystick->buttons[index] ? SDL_RELEASED : SDL_PRESSED);
    }
}

static void digitalButtonUpdate(SDL_Joystick *joystick, int button, int index) {
    assert(index >= 0);
    assert(index <= NUM_BUTTONS);
    if (digitalButtonPressed(joystick, button) != joystick->buttons[index]) {
        SDL_PrivateJoystickButton(joystick, (Uint8)index,
                                  joystick->buttons[index] ? SDL_RELEASED : SDL_PRESSED);
    }
}

static void axisUpdate(SDL_Joystick *joystick, Sint16 value, int axis_index) {
    assert(axis_index >= 0);
    assert(axis_index < NUM_AXES);
    if (value != joystick->axes[axis_index].value) {
        SDL_PrivateJoystickAxis(joystick, (Uint8)axis_index, value);
    }
}

static int SDL_XBOX_JoystickRumble(SDL_Joystick *joystick,
                                   Uint16 low_frequency_rumble,
                                   Uint16 high_frequency_rumble,
                                   Uint32 duration_ms) {
    /* FIXME: This should be implemented some day. */
    return SDL_Unsupported();
}

static void SDL_XBOX_JoystickUpdate(SDL_Joystick *joystick) {
    if (!joystickConnected(joystick)) {
        return;
    }

    XInput_GetEvents();

    int hat = SDL_HAT_CENTERED;
    Sint16 lsX, lsY;
    Sint16 rsX, rsY;
    Sint16 ltrigg, rtrigg;

    digitalButtonUpdate(joystick, XPAD_BACK, 6);
    digitalButtonUpdate(joystick, XPAD_START, 7);
    digitalButtonUpdate(joystick, XPAD_LEFT_THUMB, 8);
    digitalButtonUpdate(joystick, XPAD_RIGHT_THUMB, 9);

    analogButtonUpdate(joystick, XPAD_A, 0);
    analogButtonUpdate(joystick, XPAD_B, 1);
    analogButtonUpdate(joystick, XPAD_X, 2);
    analogButtonUpdate(joystick, XPAD_Y, 3);
    analogButtonUpdate(joystick, XPAD_WHITE, 4);
    analogButtonUpdate(joystick, XPAD_BLACK, 5);

    if (digitalButtonPressed(joystick, XPAD_DPAD_UP)) { hat |= SDL_HAT_UP; }
    if (digitalButtonPressed(joystick, XPAD_DPAD_LEFT)) { hat |= SDL_HAT_LEFT; }
    if (digitalButtonPressed(joystick, XPAD_DPAD_RIGHT)) { hat |= SDL_HAT_RIGHT; }
    if (digitalButtonPressed(joystick, XPAD_DPAD_DOWN)) { hat |= SDL_HAT_DOWN; }

    if (hat != joystick->hats[0]) {
        SDL_PrivateJoystickHat(joystick, 0, hat);
    }

    XPAD_INPUT *xpi = joystick->hwdata->padData;

    axisUpdate(joystick,  xpi->sLThumbX, 0);
    axisUpdate(joystick,  xpi->sLThumbY, 1);
    ltrigg = xpi->CurrentButtons.ucAnalogButtons[XPAD_LEFT_TRIGGER];
    ltrigg = ((ltrigg << 8) | ltrigg) - (1 << 15);
    axisUpdate(joystick, ltrigg, 2);

    axisUpdate(joystick,  xpi->sRThumbX, 3);
    axisUpdate(joystick,  xpi->sRThumbY, 4);
    rtrigg = xpi->CurrentButtons.ucAnalogButtons[XPAD_RIGHT_TRIGGER];
    rtrigg = ((rtrigg << 8) | rtrigg) - (1 << 15);
    axisUpdate(joystick, rtrigg, 5);

    return;
}

static void SDL_XBOX_JoystickClose(SDL_Joystick *joystick) {
    if (joystick->hwdata != NULL) {
        free(joystick->hwdata);
    }
    return;
}

SDL_JoystickDriver SDL_XBOX_JoystickDriver = {
    SDL_XBOX_JoystickInit,
    SDL_XBOX_JoystickGetCount,
    SDL_XBOX_JoystickDetect,
    SDL_XBOX_JoystickNameForDeviceIndex,
    SDL_XBOX_JoystickGetDevicePlayerIndex,
    SDL_XBOX_JoystickGetDeviceGUID,
    SDL_XBOX_JoystickGetDeviceInstanceID,
    SDL_XBOX_JoystickOpen,
    SDL_XBOX_JoystickRumble,
    SDL_XBOX_JoystickUpdate,
    SDL_XBOX_JoystickClose,
    SDL_XBOX_JoystickQuit
};

#endif /* SDL_JOYSTICK_XBOX */
