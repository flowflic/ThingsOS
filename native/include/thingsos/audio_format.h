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
