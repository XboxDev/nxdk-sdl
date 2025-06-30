#pragma once

#include <xid_driver.h>

typedef struct gamepad_data {
    Uint16 low_frequency_rumble;
    Uint16 high_frequency_rumble;
    Uint32 rumble_expiry;
} gamepad_data;

typedef struct sbc_data {
    Uint8 lights[STEELBATTALION_LIGHT_BYTES];
} sbc_data;

//Struct linked to SDL_Joystick
typedef struct joystick_hwdata
{
    xid_dev_t *xid_dev;
    union {
        xid_gamepad_in gamepad;
        xid_steelbattalion_in sbc;
    } in;
    union {
        gamepad_data gamepad;
        sbc_data sbc;
    } data;
} joystick_hwdata;

xid_dev_t * xid_from_joystick(SDL_Joystick * joystick);

void gamepad_open(SDL_Joystick * joystick);
void gamepad_update(SDL_Joystick *joystick);
Sint32 gamepad_rumble(SDL_Joystick *joystick, Uint16 lf_rumble, Uint16 hf_rumble, Uint32 duration_ms);
void gamepad_close(SDL_Joystick * joystick);

void sbc_open(SDL_Joystick * joystick);
void sbc_update(SDL_Joystick *joystick);
void sbc_close(SDL_Joystick * joystick);
