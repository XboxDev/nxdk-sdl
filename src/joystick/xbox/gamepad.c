#include <SDL.h>
#include "../SDL_sysjoystick.h"
#include "SDL_xboxjoystick.h"

#include <xid_driver.h>

#define BUTTON_DEADZONE 0x20

static inline gamepad_data * get_gamepad_data(SDL_Joystick * joystick) {
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
    gamepad_data * gamepad = get_gamepad_data(joystick);
    if (gamepad == NULL) return;

    // Check if the rumble timer has expired.
    if (gamepad->rumble_expiry && SDL_GetTicks() > gamepad->rumble_expiry) {
        usbh_xid_rumble(joystick->hwdata->xid_dev, 0, 0);
        gamepad->rumble_expiry = 0;
        gamepad->low_frequency_rumble = 0;
        gamepad->high_frequency_rumble = 0;
    }
    
    xid_gamepad_in * inpad = &joystick->hwdata->in.gamepad;
    
    // Fill out the rest of the buttons from the analog inputs
    if (inpad->a > BUTTON_DEADZONE) inpad->dButtons |= XINPUT_GAMEPAD_A;
    if (inpad->b > BUTTON_DEADZONE) inpad->dButtons |= XINPUT_GAMEPAD_B;
    if (inpad->x > BUTTON_DEADZONE) inpad->dButtons |= XINPUT_GAMEPAD_X;
    if (inpad->y > BUTTON_DEADZONE) inpad->dButtons |= XINPUT_GAMEPAD_Y;
    if (inpad->black > BUTTON_DEADZONE) inpad->dButtons |= XINPUT_GAMEPAD_RIGHT_SHOULDER;
    if (inpad->white > BUTTON_DEADZONE) inpad->dButtons |= XINPUT_GAMEPAD_LEFT_SHOULDER;
    
    //HAT
    Sint32 hat = SDL_HAT_CENTERED;
    if (inpad->dButtons & XINPUT_GAMEPAD_DPAD_UP)    hat |= SDL_HAT_UP;
    if (inpad->dButtons & XINPUT_GAMEPAD_DPAD_DOWN)  hat |= SDL_HAT_DOWN;
    if (inpad->dButtons & XINPUT_GAMEPAD_DPAD_LEFT)  hat |= SDL_HAT_LEFT;
    if (inpad->dButtons & XINPUT_GAMEPAD_DPAD_RIGHT) hat |= SDL_HAT_RIGHT;
    SDL_PrivateJoystickHat(joystick, 0, hat);

    //DIGITAL BUTTONS
    static const uint16_t btn_map[10] =
    {
      XINPUT_GAMEPAD_A,
      XINPUT_GAMEPAD_B,
      XINPUT_GAMEPAD_X,
      XINPUT_GAMEPAD_Y,
      XINPUT_GAMEPAD_LEFT_SHOULDER,
      XINPUT_GAMEPAD_RIGHT_SHOULDER,
      XINPUT_GAMEPAD_BACK,
      XINPUT_GAMEPAD_START,
      XINPUT_GAMEPAD_LEFT_THUMB,
      XINPUT_GAMEPAD_RIGHT_THUMB
    };
    
    for (int i = 0; i < (sizeof(btn_map) / sizeof(btn_map[0])); i++) {
      SDL_PrivateJoystickButton(joystick, i, (inpad->dButtons & btn_map[i]) ? SDL_PRESSED : SDL_RELEASED);    
    }

    //TRIGGERS
    //LEFT TRIGGER (0-255 must be converted to signed short)
    SDL_PrivateJoystickAxis(joystick, 2, ((inpad->leftTrigger << 8) | inpad->leftTrigger) - 0x8000);
    //RIGHT TRIGGER (0-255 must be converted to signed short)
    SDL_PrivateJoystickAxis(joystick, 5, ((inpad->rightTrigger << 8) | inpad->rightTrigger) - 0x8000);

    //ANALOG STICKS
    //LEFT X-AXIS
    SDL_PrivateJoystickAxis(joystick, 0, inpad->leftStickX);
    //LEFT Y-AXIS
    SDL_PrivateJoystickAxis(joystick, 1, inpad->leftStickY);
    //RIGHT X-AXIS
    SDL_PrivateJoystickAxis(joystick, 3, inpad->rightStickX);
    //RIGHT Y-AXIS
    SDL_PrivateJoystickAxis(joystick, 4, inpad->rightStickY);
}

Sint32 gamepad_rumble(SDL_Joystick * joystick, Uint16 lf_rumble, Uint16 hf_rumble, Uint32 duration_ms) {
    gamepad_data * gamepad = get_gamepad_data(joystick);
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
