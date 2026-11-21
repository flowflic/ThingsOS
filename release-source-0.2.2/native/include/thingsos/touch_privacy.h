// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdbool.h>
typedef struct {bool withheld;} thingsos_touch_privacy_t;
/* A private contact remains private through its release, even if the page
   changes before the finger lifts. Caller serializes samples on touch_access. */
static inline bool thingsos_touch_log_allowed(thingsos_touch_privacy_t* state,bool private_input,bool down) {
    bool suppress=private_input||state->withheld;
    if(down && private_input)state->withheld=true;
    if(!down)state->withheld=false;
    return !suppress;
}
