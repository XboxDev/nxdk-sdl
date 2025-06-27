#include <SDL.h>
#include "../SDL_sysjoystick.h"
#include "SDL_xboxjoystick.h"

#include <xid_driver.h>

enum SBC_BUTTON {
	BUTTON_FIRE_MAIN,
	BUTTON_FIRE_SUB,
	BUTTON_LOCK_ON,
	BUTTON_EJECT,
	BUTTON_HATCH,
	BUTTON_IGNITION,
	BUTTON_START,
	BUTTON_MULTI_TOGGLE,
	BUTTON_MULTI_ZOOM,
	BUTTON_MULTI_MODE,
	BUTTON_SUB_MODE,
	BUTTON_ZOOM_IN,
	BUTTON_ZOOM_OUT,
	BUTTON_FSS,
	BUTTON_MANIPULATOR,
	BUTTON_LINE_COLOR_CHANGE,
	BUTTON_WASHING,
	BUTTON_EXTINGUISHER,
	BUTTON_CHAFF,
	BUTTON_TANK_DETACH,
	BUTTON_OVERRIDE,
	BUTTON_NIGHT_SCOPE,
	BUTTON_F1,
	BUTTON_F2,
	BUTTON_F3,
	BUTTON_CYCLE_MAIN,
	BUTTON_CYCLE_SUB,
	BUTTON_MAG_CHANGE,
	BUTTON_COMM_1,
	BUTTON_COMM_2,
	BUTTON_COMM_3,
	BUTTON_COMM_4,
	BUTTON_COMM_5,
	BUTTON_SIGHT_CHANGE,
	// Toggle Switches
	SWITCH_FILTER,
	SWITCH_OXYGEN,
	SWITCH_FUEL,
	SWITCH_BUFFER,
	SWITCH_LOCATION,
	
	SBC_BUTTON_COUNT
};

enum SBC_AXIS {
    AXIS_AIM_X,
    AXIS_AIM_Y,
    
    AXIS_TURN,
    
    AXIS_SIGHT_X,
    AXIS_SIGHT_Y,
    
    AXIS_SLIDE,
    AXIS_BRAKE,
    AXIS_ACCEL,
    
    AXIS_TUNER,
    AXIS_SHIFTER,
    
    SBC_AXIS_COUNT
};

// Steel Battalion Controller
typedef struct _XINPUT_SBC
{
    Uint16 buttons[3];
    Uint16 aimingLeverX; // 0 = Left, 0xFFFF = Right
    Uint16 aimingLeverY; // 0 = Top,  0xFFFF = Bottom
    Sint16 turningLever;
    Sint16 sightChangeX;
    Sint16 sightChangeY;
    Uint16 slidePedal;
    Uint16 brakePedal;
    Uint16 accelPedal;
    Uint8  tuner;   // 0-15 is from 9oclock, around clockwise
    Sint8  shifter; // -2 = R, -1 = N, 0 = Error, 1 = 1st, 2 = 2nd, 3 = 3rnd, 4 = 4th, 5 = 5th
} XINPUT_SBC, *PXINPUT_SBC;

static inline psbc_data get_sbc_data(SDL_Joystick * joystick) {
    return (joystick == NULL || joystick->hwdata == NULL) ? NULL : &joystick->hwdata->data.sbc;
}

void sbc_open(SDL_Joystick * joystick) {
    if (joystick == NULL) return;
    
    joystick->naxes = SBC_AXIS_COUNT;
    joystick->nballs = 0;
    joystick->nhats = 0;
    joystick->nbuttons = SBC_BUTTON_COUNT; //This includes the toggle switches
}

static SDL_bool get_sbc_button_pressed(PXINPUT_SBC xsbc, enum SBC_BUTTON btn) {
    unsigned int button_offset = btn / 16;
	Uint16 button_mask = 1 << (btn % 16);

    return (xsbc->buttons[button_offset] & button_mask) ? SDL_TRUE : SDL_FALSE;
}

void sbc_update(SDL_Joystick *joystick) {
    psbc_data sbc = get_sbc_data(joystick);
    if (sbc == NULL) return;
    
    XINPUT_SBC xsbc;
    SDL_memcpy(&xsbc, joystick->hwdata->raw_data + 2, sizeof(XINPUT_SBC));
    
    for (enum SBC_BUTTON btn = 0; btn < SBC_BUTTON_COUNT; btn++) {
        SDL_PrivateJoystickButton(joystick, btn, get_sbc_button_pressed(&xsbc, btn));
    }
    
    // Aiming Lever (convert from unsigned to signed)
    SDL_PrivateJoystickAxis(joystick, AXIS_AIM_X, xsbc.aimingLeverX >> 1);
    SDL_PrivateJoystickAxis(joystick, AXIS_AIM_Y, xsbc.aimingLeverY >> 1);
    
    // Turning Lever
    SDL_PrivateJoystickAxis(joystick, AXIS_TURN, xsbc.turningLever);
    
    // Sight Change
    SDL_PrivateJoystickAxis(joystick, AXIS_SIGHT_X, xsbc.sightChangeX);
    SDL_PrivateJoystickAxis(joystick, AXIS_SIGHT_Y, xsbc.sightChangeY);
    
    // Pedals (convert from unsigned to signed)
    SDL_PrivateJoystickAxis(joystick, AXIS_SLIDE, xsbc.slidePedal >> 1);
    SDL_PrivateJoystickAxis(joystick, AXIS_ACCEL, xsbc.accelPedal >> 1);
    SDL_PrivateJoystickAxis(joystick, AXIS_BRAKE, xsbc.brakePedal >> 1);
    
    // Tuner MIN=0, MAX=15
    SDL_PrivateJoystickAxis(joystick, AXIS_TUNER, xsbc.tuner);
    
    // Shifter MIN=-1, MAX=5
    SDL_PrivateJoystickAxis(joystick, AXIS_SHIFTER, xsbc.shifter);
}

void sbc_close(SDL_Joystick * joystick) {
    
}
