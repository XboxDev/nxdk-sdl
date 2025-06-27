#pragma once

#include <xid_driver.h>

#define MAX_PACKET_SIZE 32
#define SBC_LIGHT_COUNT 48

typedef struct gamepad_data {
    Uint16 low_frequency_rumble;
    Uint16 high_frequency_rumble;
    Uint32 rumble_expiry;
} gamepad_data, *pgamepad_data;

typedef struct sbc_data {
    Uint8 lights[SBC_LIGHT_COUNT / 2];
} sbc_data, *psbc_data;

//Struct linked to SDL_Joystick
typedef struct joystick_hwdata
{
    xid_dev_t *xid_dev;
    Uint8 raw_data[MAX_PACKET_SIZE];
    union {
        gamepad_data gamepad;
        sbc_data sbc;
    } data;
} joystick_hwdata, *pjoystick_hwdata;

xid_dev_t * xid_from_joystick(SDL_Joystick * joystick);

void gamepad_open(SDL_Joystick * joystick);
void gamepad_update(SDL_Joystick *joystick);
Sint32 gamepad_rumble(SDL_Joystick *joystick, Uint16 lf_rumble, Uint16 hf_rumble, Uint32 duration_ms);
void gamepad_close(SDL_Joystick * joystick);

void sbc_open(SDL_Joystick * joystick);
void sbc_update(SDL_Joystick *joystick);
void sbc_close(SDL_Joystick * joystick);
