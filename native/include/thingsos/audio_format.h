// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
typedef struct {uint32_t rate,offset,bytes;unsigned channels;} thingsos_wav_t;
/* Bounded RIFF PCM16 parser: optional chunks/padding, overflow and truncation. */
bool thingsos_wav_read(FILE*,thingsos_wav_t*);
bool thingsos_wav_header(FILE*,uint32_t rate,unsigned channels,uint32_t bytes);
typedef struct {int32_t previous;uint32_t until,rate;bool ready;} thingsos_audio_resampler_t;
/* Mono PCM to fixed 16 kHz; caller provides ceil(frames*16000/rate)+2 slots. */
size_t thingsos_audio_resample(thingsos_audio_resampler_t*,const int16_t*,size_t,unsigned,int16_t*,size_t);
/* Resume point of a parked (paused, file closed) playback: offset from the
   start of the audio data of the next byte not yet decoded, given the data
   size, the bytes not yet read from the file and the bytes still buffered.
   MP3 resumes there (the decoder re-syncs on the frame header) instead of
   decoding every earlier frame again. Clamped for inconsistent input. */
uint64_t thingsos_audio_resume_byte(uint64_t source_size,uint64_t unread,uint64_t buffered);
