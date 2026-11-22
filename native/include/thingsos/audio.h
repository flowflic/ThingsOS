// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef enum {THINGSOS_AUDIO_IDLE,THINGSOS_AUDIO_STARTING,THINGSOS_AUDIO_PLAYING,THINGSOS_AUDIO_PLAY_PAUSED,
 THINGSOS_AUDIO_RECORDING,THINGSOS_AUDIO_RECORD_PAUSED,THINGSOS_AUDIO_STOPPING,THINGSOS_AUDIO_ERROR} thingsos_audio_state_t;
typedef struct {thingsos_audio_state_t state;unsigned serial,volume,peak;uint32_t position_ms,duration_ms;int error;bool ended;
 char path[512],message[128];} thingsos_audio_status_t;
#ifndef THINGSOS_HOST_UI
bool thingsos_audio_play(const char* path,uint32_t offset_ms);
bool thingsos_audio_record(void);
bool thingsos_audio_pause(bool paused);
bool thingsos_audio_stop(void);
bool thingsos_audio_seek(uint32_t offset_ms);
void thingsos_audio_volume(unsigned percent);
/* Reserve the persistent MP3 working set in PSRAM. Call once early at boot,
   before network jobs fragment PSRAM; playback reserves lazily otherwise. */
void thingsos_audio_reserve(void);
/* Boot only: create the permanent interrupt descriptors the audio I2S/GDMA
   interrupts will use while main SRAM is still compact. */
void thingsos_audio_prepare_interrupts(void);
void thingsos_audio_status(thingsos_audio_status_t*);
bool thingsos_audio_active(void);
/* True while an open audio session holds the exclusive network claim
   (playing or recording; a parked playback pause releases it). */
bool thingsos_audio_claimed(void);
/* Diagnostic build: one-shot heap report after the first audio session. */
void thingsos_audio_heap_diag_poll(void);
/* Called by the UI with thingsos_io_lock held. Temporarily releases that lock
   while the worker finalizes/syncs its file, then reacquires it before return. */
bool thingsos_audio_quiesce_locked(void);
/* Pause playback and release I2S for a Bluetooth mode change, retaining the
   open file and position. Caller holds the shared UI I/O lock. */
bool thingsos_audio_suspend_locked(void);
bool thingsos_audio_route_idle(void);
#endif
