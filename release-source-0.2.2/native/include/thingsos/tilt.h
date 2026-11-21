// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdbool.h>
#include <stdint.h>
/* Screen coordinates in mg: positive X means right side lowered; Y is not a trigger condition. */
typedef struct {
    int x,y,z,bx,by,bz,candidate,angle,base_angle,motion_candidate,origin_angle;
    unsigned stable;
    bool initialized, calibrated, armed;
    uint64_t last, since, cooldown,motion_until,motion_since;
    /* Complete a wrist cycle from its peak and a settled return, rather than
       requiring the exact first grip forever. No new event while held. */
    int fired_direction,peak_angle,rest_x,rest_y,rest_z,rest_angle;
    uint64_t rest_since;
    bool rest_active,posture_pending,touch_recenter;
} thingsos_tilt_t;
/* Chip mg -> portrait / fixed landscape reader axes; shared by device and tests. */
void thingsos_tilt_screen_axes(int raw_x,int raw_y,int raw_z,bool landscape,int* x,int* y,int* z);
void thingsos_tilt_reset(thingsos_tilt_t* state);
/* Returns +1 next, -1 previous, 0 no gesture. Feed only fresh samples. */
int thingsos_tilt_sample(thingsos_tilt_t*, int x,int y,int z,uint64_t ms);
/* 0 low, 1 medium (accepted default), 2 high; invalid values use medium. */
int thingsos_tilt_sample_level(thingsos_tilt_t*,int x,int y,int z,uint64_t ms,unsigned level);
bool thingsos_tilt_holds_direction(const thingsos_tilt_t*,int direction,unsigned level);

/* Input-task detector with one bounded intent mailbox. UI latency must not
   erase a completed wrist turn; old intents never accumulate or replay. */
typedef struct {
    thingsos_tilt_t state;
    int pending;
    uint64_t pending_at;
    bool configured,landscape;
    unsigned level;
} thingsos_tilt_stream_t;
void thingsos_tilt_stream_sample(thingsos_tilt_stream_t*,int,int,int,uint64_t,bool,unsigned,bool);
int thingsos_tilt_stream_take(thingsos_tilt_stream_t*,uint64_t,uint64_t*);

/* Bounded sampler history, protected by the board input mutex. */
#define THINGSOS_TILT_QUEUE_SIZE 64
typedef struct { int x,y,z; uint64_t ms; } thingsos_tilt_input_t;
typedef struct { thingsos_tilt_input_t samples[THINGSOS_TILT_QUEUE_SIZE]; unsigned head,count; } thingsos_tilt_queue_t;
void thingsos_tilt_queue_push(thingsos_tilt_queue_t*,thingsos_tilt_input_t);
bool thingsos_tilt_queue_pop(thingsos_tilt_queue_t*,thingsos_tilt_input_t*);
