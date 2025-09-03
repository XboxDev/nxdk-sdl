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

static inline sbc_data * get_sbc_data(SDL_Joystick * joystick) {
    return (joystick == NULL || joystick->hwdata == NULL) ? NULL : &joystick->hwdata->data.sbc;
}

void sbc_open(SDL_Joystick * joystick) {
    if (joystick == NULL) return;
    
    joystick->naxes = SBC_AXIS_COUNT;
    joystick->nballs = 0;
    joystick->nhats = 0;
    joystick->nbuttons = SBC_BUTTON_COUNT; //This includes the toggle switches
}

static Uint8 get_sbc_button_pressed(xid_steelbattalion_in * insbc, enum SBC_BUTTON btn) {
    unsigned int button_offset = btn / 16;
	Uint16 button_mask = 1 << (btn % 16);

    return (insbc->buttons[button_offset] & button_mask) ? SDL_PRESSED : SDL_RELEASED;
}

void sbc_update(SDL_Joystick *joystick) {
    sbc_data * sbc = get_sbc_data(joystick);
    if (sbc == NULL) return;
    
    xid_steelbattalion_in * insbc = &joystick->hwdata->in.sbc;
    
    for (enum SBC_BUTTON btn = 0; btn < SBC_BUTTON_COUNT; btn++) {
        SDL_PrivateJoystickButton(joystick, btn, get_sbc_button_pressed(insbc, btn));
    }
    
    // Aiming Lever (convert from unsigned to signed)
    SDL_PrivateJoystickAxis(joystick, AXIS_AIM_X, insbc->aimingLeverX);
    SDL_PrivateJoystickAxis(joystick, AXIS_AIM_Y, insbc->aimingLeverY);
    
    // Turning Lever
    SDL_PrivateJoystickAxis(joystick, AXIS_TURN, insbc->turningLever);
    
    // Sight Change
    SDL_PrivateJoystickAxis(joystick, AXIS_SIGHT_X, insbc->sightChangeX);
    SDL_PrivateJoystickAxis(joystick, AXIS_SIGHT_Y, insbc->sightChangeY);
    
    // Pedals (convert from unsigned to signed)
    SDL_PrivateJoystickAxis(joystick, AXIS_SLIDE, insbc->slidePedal);
    SDL_PrivateJoystickAxis(joystick, AXIS_ACCEL, insbc->accelPedal);
    SDL_PrivateJoystickAxis(joystick, AXIS_BRAKE, insbc->brakePedal);
    
    // Tuner MIN=0, MAX=15
    SDL_PrivateJoystickAxis(joystick, AXIS_TUNER, insbc->tuner);
    
    // Shifter MIN=-2, MAX=5
    SDL_PrivateJoystickAxis(joystick, AXIS_SHIFTER, insbc->shifter);
}

void sbc_close(SDL_Joystick * joystick) {
    
}
