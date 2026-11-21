// SPDX-License-Identifier: Apache-2.0
#ifndef THINGSOS_KEYS_H
#define THINGSOS_KEYS_H
#include <stdbool.h>
#include <stdint.h>
typedef enum { THINGSOS_KEY_NONE, THINGSOS_KEY_PREVIOUS, THINGSOS_KEY_MENU,
               THINGSOS_KEY_NEXT, THINGSOS_KEY_HOME } thingsos_key_action_t;
typedef struct { unsigned key; uint64_t started; bool cancelled, long_sent; } thingsos_keys_t;
/* Official hardware guide: key band y >= 1300, pitch 160, centers 80/240/400.
   The UI owner maps the left key to Previous in bare reading and Back in menus.
   Holding the middle key for one second goes home while still held: the action
   fires as soon as the threshold passes and the release adds nothing. */
#define THINGSOS_KEY_HOLD_HOME_MS 1000u
static inline thingsos_key_action_t thingsos_keys_step(
    thingsos_keys_t* s, bool contact, unsigned x, unsigned y, uint64_t time) {
    unsigned hit = contact && x < 480 && y >= 1300 && y <= 4095 ? x / 160 + 1 : 0;
    if (!contact) {
        thingsos_key_action_t action = THINGSOS_KEY_NONE;
        if (s->key && !s->cancelled && !s->long_sent && time >= s->started + 20)
            action = s->key == 2 && time >= s->started + THINGSOS_KEY_HOLD_HOME_MS
                         ? THINGSOS_KEY_HOME
                         : (thingsos_key_action_t)s->key;
        *s = (thingsos_keys_t){0};
        return action;
    }
    if (s->key && hit != s->key) s->cancelled = true;
    if (!hit || s->cancelled) return THINGSOS_KEY_NONE;
    if (!s->key) { s->key = hit; s->started = time; }
    if (hit == 2 && !s->long_sent && time >= s->started + THINGSOS_KEY_HOLD_HOME_MS) {
        s->long_sent = true;
        return THINGSOS_KEY_HOME;
    }
    return THINGSOS_KEY_NONE;
}
#endif
