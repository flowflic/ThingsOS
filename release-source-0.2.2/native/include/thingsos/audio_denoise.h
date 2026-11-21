// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* NSNet3: 512-sample causal STFT window, 256-sample hop and startup delay.
 * This framer owns timing only; inference remains in the firmware adapter. */
#define THINGSOS_DENOISE_HOP 256u
typedef bool (*thingsos_denoise_process_t)(void*, int16_t*, int16_t*);
typedef struct {
    thingsos_denoise_process_t process;
    void* context;
    int16_t block[THINGSOS_DENOISE_HOP];
    int16_t filtered[THINGSOS_DENOISE_HOP];
    size_t used, skip;
    uint64_t accepted, emitted;
    bool failed, finished;
} thingsos_audio_denoise_t;

void thingsos_audio_denoise_init(thingsos_audio_denoise_t*, thingsos_denoise_process_t, void*);
/* Output never exceeds floor((used + count) / HOP) * HOP. No allocation. */
bool thingsos_audio_denoise_feed(thingsos_audio_denoise_t*, const int16_t*, size_t,
                                int16_t*, size_t, size_t*);
/* Zero-pad inference and retain exactly the remaining real samples (<=511).
 * Call on pause/stop, then destroy the backend. Start a fresh segment on resume. */
bool thingsos_audio_denoise_finish(thingsos_audio_denoise_t*, int16_t*, size_t, size_t*);
