#include <SDL.h>
#include "../SDL_sysjoystick.h"
#include "SDL_xboxjoystick.h"

#include <xid_driver.h>

#define BUTTON_DEADZONE 0x20

//XINPUT defines and struct format from
//https://docs.microsoft.com/en-us/windows/win32/api/xinput/ns-xinput-xinput_gamepad
#define XINPUT_GAMEPAD_DPAD_UP 0x0001
#define XINPUT_GAMEPAD_DPAD_DOWN 0x0002
#define XINPUT_GAMEPAD_DPAD_LEFT 0x0004
#define XINPUT_GAMEPAD_DPAD_RIGHT 0x0008
#define XINPUT_GAMEPAD_START 0x0010
#define XINPUT_GAMEPAD_BACK 0x0020
#define XINPUT_GAMEPAD_LEFT_THUMB 0x0040
#define XINPUT_GAMEPAD_RIGHT_THUMB 0x0080
#define XINPUT_GAMEPAD_LEFT_SHOULDER 0x0100
#define XINPUT_GAMEPAD_RIGHT_SHOULDER 0x0200
#define XINPUT_GAMEPAD_A 0x1000
#define XINPUT_GAMEPAD_B 0x2000
#define XINPUT_GAMEPAD_X 0x4000
#define XINPUT_GAMEPAD_Y 0x8000
#define MAX_PACKET_SIZE 32

typedef struct _XINPUT_GAMEPAD
{
    Uint16 wButtons;
    Uint8 bLeftTrigger;
    Uint8 bRightTrigger;
    Sint16 sThumbLX;
    Sint16 sThumbLY;
    Sint16 sThumbRX;
    Sint16 sThumbRY;
} XINPUT_GAMEPAD, *PXINPUT_GAMEPAD;

static inline pgamepad_data get_gamepad_data(SDL_Joystick * joystick) {
    return (joystick == NULL || joystick->hwdata == NULL) ? NULL : &joystick->hwdata->data.gamepad;
}

void gamepad_open(SDL_Joystick * joystick) {
    if (joystick == NULL) return;
    
    joystick->naxes = 6;     /* LStickY, LStickX, LTrigg, RStickY, RStickX, RTrigg */
    joystick->nballs = 0;    /* No balls here */
    joystick->nhats = 1;     /* D-pad */
    joystick->nbuttons = 10; /* A, B, X, Y, RB, LB, Back, Start, LThumb, RThumb */
}

void gamepad_update(SDL_Joystick *joystick) {
    pgamepad_data gamepad = get_gamepad_data(joystick);
    if (gamepad == NULL) return;

    // Check if the rumble timer has expired.
    if (gamepad->rumble_expiry && SDL_GetTicks() > gamepad->rumble_expiry) {
        usbh_xid_rumble(joystick->hwdata->xid_dev, 0, 0);
        gamepad->rumble_expiry = 0;
        gamepad->low_frequency_rumble = 0;
        gamepad->high_frequency_rumble = 0;
    }
    
    XINPUT_GAMEPAD xpad;
    {
        Uint8 * rdata = joystick->hwdata->raw_data;
        Uint16 wButtons = *((Uint16*)&rdata[2]);
        xpad.wButtons = 0;

        // Map digital buttons
        if (wButtons & (1 << 0)) xpad.wButtons |= XINPUT_GAMEPAD_DPAD_UP;
        if (wButtons & (1 << 1)) xpad.wButtons |= XINPUT_GAMEPAD_DPAD_DOWN;
        if (wButtons & (1 << 2)) xpad.wButtons |= XINPUT_GAMEPAD_DPAD_LEFT;
        if (wButtons & (1 << 3)) xpad.wButtons |= XINPUT_GAMEPAD_DPAD_RIGHT;
        if (wButtons & (1 << 4)) xpad.wButtons |= XINPUT_GAMEPAD_START;
        if (wButtons & (1 << 5)) xpad.wButtons |= XINPUT_GAMEPAD_BACK;
        if (wButtons & (1 << 6)) xpad.wButtons |= XINPUT_GAMEPAD_LEFT_THUMB;
        if (wButtons & (1 << 7)) xpad.wButtons |= XINPUT_GAMEPAD_RIGHT_THUMB;

        //Analog buttons are converted to digital
        if (rdata[4] > BUTTON_DEADZONE) xpad.wButtons |= XINPUT_GAMEPAD_A;
        if (rdata[5] > BUTTON_DEADZONE) xpad.wButtons |= XINPUT_GAMEPAD_B;
        if (rdata[6] > BUTTON_DEADZONE) xpad.wButtons |= XINPUT_GAMEPAD_X;
        if (rdata[7] > BUTTON_DEADZONE) xpad.wButtons |= XINPUT_GAMEPAD_Y;
        if (rdata[8] > BUTTON_DEADZONE) xpad.wButtons |= XINPUT_GAMEPAD_RIGHT_SHOULDER; //BLACK
        if (rdata[9] > BUTTON_DEADZONE) xpad.wButtons |= XINPUT_GAMEPAD_LEFT_SHOULDER; //WHITE

        //Map the left and right triggers
        xpad.bLeftTrigger = rdata[10];
        xpad.bRightTrigger = rdata[11];

        //Map analog sticks
        xpad.sThumbLX = *((Sint16 *)&rdata[12]);
        xpad.sThumbLY = *((Sint16 *)&rdata[14]);
        xpad.sThumbRX = *((Sint16 *)&rdata[16]);
        xpad.sThumbRY = *((Sint16 *)&rdata[18]);
    }
    
    //HAT
    {
        Sint32 hat = SDL_HAT_CENTERED;
        if (xpad.wButtons & XINPUT_GAMEPAD_DPAD_UP)    hat |= SDL_HAT_UP;
        if (xpad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN)  hat |= SDL_HAT_DOWN;
        if (xpad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT)  hat |= SDL_HAT_LEFT;
        if (xpad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) hat |= SDL_HAT_RIGHT;
        if (hat != joystick->hats[0]) {
            SDL_PrivateJoystickHat(joystick, 0, hat);
        }
    }

    //DIGITAL BUTTONS
    static const Sint32 btn_map[10][2] =
    {
      {0, XINPUT_GAMEPAD_A},
      {1, XINPUT_GAMEPAD_B},
      {2, XINPUT_GAMEPAD_X},
      {3, XINPUT_GAMEPAD_Y},
      {4, XINPUT_GAMEPAD_LEFT_SHOULDER},
      {5, XINPUT_GAMEPAD_RIGHT_SHOULDER},
      {6, XINPUT_GAMEPAD_BACK},
      {7, XINPUT_GAMEPAD_START},
      {8, XINPUT_GAMEPAD_LEFT_THUMB},
      {9, XINPUT_GAMEPAD_RIGHT_THUMB}
    };
    
    for (Sint32 i = 0; i < (sizeof(btn_map) / sizeof(btn_map[0])); i++) {
      if (joystick->buttons[btn_map[i][0]] != ((xpad.wButtons & btn_map[i][1]) > 0))
          SDL_PrivateJoystickButton(joystick, btn_map[i][0], (xpad.wButtons & btn_map[i][1]) ? SDL_PRESSED : SDL_RELEASED);
    }

    //TRIGGERS
    //LEFT TRIGGER (0-255 must be converted to signed short)
    if (xpad.bLeftTrigger != joystick->axes[2].value)
        SDL_PrivateJoystickAxis(joystick, 2, ((xpad.bLeftTrigger << 8) | xpad.bLeftTrigger) - (1 << 15));
    //RIGHT TRIGGER (0-255 must be converted to signed short)
    if (xpad.bRightTrigger != joystick->axes[5].value)
        SDL_PrivateJoystickAxis(joystick, 5, ((xpad.bRightTrigger << 8) | xpad.bRightTrigger) - (1 << 15));

    //ANALOG STICKS
    {
        Sint16 axis;
        //LEFT X-AXIS
        axis = xpad.sThumbLX;
        if (axis != joystick->axes[0].value)
            SDL_PrivateJoystickAxis(joystick, 0, axis);
        //LEFT Y-AXIS
        axis = xpad.sThumbLY;
        if (axis != joystick->axes[1].value)
            SDL_PrivateJoystickAxis(joystick, 1, ~axis);
        //RIGHT X-AXIS
        axis = xpad.sThumbRX;
        if (axis != joystick->axes[3].value)
            SDL_PrivateJoystickAxis(joystick, 3, axis);
        //RIGHT Y-AXIS
        axis = xpad.sThumbRY;
        if (axis != joystick->axes[4].value)
            SDL_PrivateJoystickAxis(joystick, 4, ~axis);
    }
}

Sint32 gamepad_rumble(SDL_Joystick * joystick, Uint16 lf_rumble, Uint16 hf_rumble, Uint32 duration_ms) {
    pgamepad_data gamepad = get_gamepad_data(joystick);
    if (gamepad == NULL) return -1;
    
    //Check if rumble values are new values.
    if (gamepad->low_frequency_rumble  == lf_rumble
    &&  gamepad->high_frequency_rumble == hf_rumble)
    {
        //Rumble values not changed, reset the expiry timer and leave.
        gamepad->rumble_expiry = SDL_GetTicks() + duration_ms;
        return 0;
    }

    xid_dev_t * xid_dev = xid_from_joystick(joystick);
    if (usbh_xid_rumble(xid_dev, lf_rumble, hf_rumble) != USBH_OK)
    {
        return -1;
    }
    
    gamepad->low_frequency_rumble = lf_rumble;
    gamepad->high_frequency_rumble = hf_rumble;
    gamepad->rumble_expiry = SDL_GetTicks() + duration_ms;
    
    return 0;
}

void gamepad_close(SDL_Joystick * joystick) {
    usbh_xid_rumble(joystick->hwdata->xid_dev, 0, 0);
}
