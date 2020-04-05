/*
  Original Xbox Audio Driver for Simple DirectMedia Layer
  (based on SDL_naclaudio.c)
  Copyright (C) 1997-2019 Sam Lantinga <slouken@libsdl.org>
  Copyright (C) 2020 Jannik Vogel

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

#if SDL_AUDIO_DRIVER_XBOX

#include "SDL_xboxaudio.h"

#include "SDL_audio.h"
#include "SDL_mutex.h"
#include "../SDL_audio_c.h"
#include "../SDL_audiodev_c.h"

#include <xboxkrnl/xboxkrnl.h>
#include <hal/audio.h>

/* The tag name used by Original Xbox audio */
#define XBOXAUDIO_DRIVER_NAME         "xbox"

#define SAMPLE_FRAME_COUNT 1024

static void
xbox_audio_callback(void *pac97device, void *data)
{
    SDL_AudioDevice* _this = (SDL_AudioDevice*) data;
    SDL_AudioCallback callback = _this->callbackspec.callback;
    void* buffer = _this->hidden->buffers[_this->hidden->next_buffer];
    const int len = (int) _this->spec.size;

    /* This is run from a DPC, so store the FPU state */
    KFLOATING_SAVE float_save;
    NTSTATUS status = KeSaveFloatingPointState(&float_save);
    SDL_assert(status == STATUS_SUCCESS);

    /* Only do something if audio is enabled */
    if (!SDL_AtomicGet(&_this->enabled) || SDL_AtomicGet(&_this->paused)) {
        if (_this->stream) {
            SDL_AudioStreamClear(_this->stream);
        }
        SDL_memset(buffer, _this->spec.silence, len);
    } else {
        if (_this->stream == NULL) {  /* no conversion necessary. */
            SDL_LockMutex(_this->mixer_lock);
            callback(_this->callbackspec.userdata, buffer, len);
            SDL_UnlockMutex(_this->mixer_lock);
        } else {  /* streaming/converting */
            const int stream_len = _this->callbackspec.size;
            while (SDL_AudioStreamAvailable(_this->stream) < len) {
                callback(_this->callbackspec.userdata, _this->work_buffer, stream_len);
                if (SDL_AudioStreamPut(_this->stream, _this->work_buffer, stream_len) == -1) {
                    SDL_AudioStreamClear(_this->stream);
                    SDL_AtomicSet(&_this->enabled, 0);
                    break;
                }
            }

            const int got = SDL_AudioStreamGet(_this->stream, buffer, len);
            SDL_assert((got < 0) || (got == len));
            if (got != len) {
                SDL_memset(buffer, _this->spec.silence, len);
            }
        }
    }

    /* Send samples to XAudio */
    XAudioProvideSamples(_this->hidden->buffers[_this->hidden->next_buffer], len, FALSE);

    /* Advance to next buffer */
    _this->hidden->next_buffer = (_this->hidden->next_buffer + 1) % BUFFER_COUNT;

    /* This is run from a DPC, so restore the FPU state */
    status = KeRestoreFloatingPointState(&float_save);
    SDL_assert(status == STATUS_SUCCESS);
}

static void
XBOXAUDIO_CloseDevice(SDL_AudioDevice *device)
{
    SDL_PrivateAudioData *hidden = (SDL_PrivateAudioData *) device->hidden;

    /* Reset hardware and disable callback */
    XAudioInit(16, 2, NULL, NULL);

    /* Free buffers */
    for (int i = 0; i < BUFFER_COUNT; ++i) {
        MmFreeContiguousMemory(hidden->buffers[i]);
    }
}

static int
XBOXAUDIO_OpenDevice(_THIS, void *handle, const char *devname, int iscapture)
{
    _this->hidden = (SDL_PrivateAudioData *) SDL_calloc(1, (sizeof *_this->hidden));
    if (_this->hidden == NULL) {
        return SDL_OutOfMemory();
    }

    _this->spec.freq = 48000;
    _this->spec.format = AUDIO_S16LSB;
    _this->spec.channels = 2;
    _this->spec.samples = SAMPLE_FRAME_COUNT;

    /* Calculate the final parameters for this audio specification */
    SDL_CalculateAudioSpec(&_this->spec);

    XAudioInit(16, 2, xbox_audio_callback, (void *)_this);

    /* Allocate buffers */
    for (int i = 0; i < BUFFER_COUNT; ++i) {
        _this->hidden->buffers[i] = MmAllocateContiguousMemoryEx(_this->spec.size, 0, 0xFFFFFFFF, 0, PAGE_READWRITE | PAGE_WRITECOMBINE);
        if (_this->hidden->buffers[i] == NULL) {
            return SDL_OutOfMemory();
        }

        /* Fill buffer with silence */
        memset(_this->hidden->buffers[i], _this->spec.silence, _this->spec.size);

        /* Send samples to XAudio */
        XAudioProvideSamples(_this->hidden->buffers[i], _this->spec.size, FALSE);
    }

    _this->hidden->next_buffer = 0;

    /* Start audio playback */
    XAudioPlay();

    return 0;
}

static int
XBOXAUDIO_Init(SDL_AudioDriverImpl * impl)
{
    /* Set the function pointers */
    impl->OpenDevice = XBOXAUDIO_OpenDevice;
    impl->CloseDevice = XBOXAUDIO_CloseDevice;
    impl->OnlyHasDefaultOutputDevice = 1;
    impl->ProvidesOwnCallbackThread = 1;
    /*
     *    impl->WaitDevice = XBOXAUDIO_WaitDevice;
     *    impl->GetDeviceBuf = XBOXAUDIO_GetDeviceBuf;
     *    impl->PlayDevice = XBOXAUDIO_PlayDevice;
     *    impl->Deinitialize = XBOXAUDIO_Deinitialize;
     */

    return 1;
}

AudioBootStrap XBOXAUDIO_bootstrap = {
    XBOXAUDIO_DRIVER_NAME, "Original Xbox audio driver", XBOXAUDIO_Init, 0
};

#endif /* SDL_AUDIO_DRIVER_XBOX */

/* vi: set ts=4 sw=4 expandtab: */
