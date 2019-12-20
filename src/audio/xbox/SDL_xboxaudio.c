// Copy of dummy driver, but always allowed by default (demand_only = 0)

#include "../dummy/SDL_dummyaudio.c"

AudioBootStrap XBOXAUDIO_bootstrap = {
    "xbox", "Original Xbox audio driver", DUMMYAUDIO_Init, 0
};
