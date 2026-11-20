/* SPDX-License-Identifier: Apache-2.0 */
#ifndef THINGSOS_TOUCH_QUEUE_H
#define THINGSOS_TOUCH_QUEUE_H
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#define THINGSOS_TOUCH_QUEUE_SIZE 16
typedef struct {
    uint64_t at_ms;
    uint16_t x, y;
    uint8_t count;
    bool down;
} thingsos_touch_sample_t;
typedef struct {
    thingsos_touch_sample_t origin, latest, samples[THINGSOS_TOUCH_QUEUE_SIZE];
    bool motion[THINGSOS_TOUCH_QUEUE_SIZE], valid, blocked;
    unsigned head, count, overflows;
} thingsos_touch_queue_t;
/* Caller serializes producer/consumer. Keep edges and coalesce only motion:
   a DOWN's original time and position must survive a complete display scan. */
static inline void thingsos_touch_queue_push(thingsos_touch_queue_t* q, thingsos_touch_sample_t s) {
    thingsos_touch_sample_t old=q->latest;
    bool changed=!q->valid || old.down!=s.down || old.count!=s.count ||
        (s.down && (old.x!=s.x || old.y!=s.y));
    bool move=q->valid && old.down && s.down && old.count==s.count;
    if(!q->valid || !old.down)q->origin=s;
    q->latest=s;q->valid=true;
    if(q->blocked && s.down)return;
    if(q->blocked)q->blocked=false;
    else if(!changed)return;
    unsigned tail=(q->head+q->count)%THINGSOS_TOUCH_QUEUE_SIZE;
    if(move && q->count) {
        unsigned last=(tail+THINGSOS_TOUCH_QUEUE_SIZE-1)%THINGSOS_TOUCH_QUEUE_SIZE;
        /* Retain a turn back toward the origin: otherwise a swipe that
           returns to its starting row could be replayed as a tap. */
        int ox=(int)old.x-q->origin.x,oy=(int)old.y-q->origin.y;
        int nx=(int)s.x-q->origin.x,ny=(int)s.y-q->origin.y;
        bool outward=(ox>=0?nx>=ox:nx<=ox) && (oy>=0?ny>=oy:ny<=oy);
        if(q->motion[last] && outward){q->samples[last]=s;return;}
    }
    if(q->count==THINGSOS_TOUCH_QUEUE_SIZE) {
        /* Cancel the gesture explicitly rather than dropping an UP and
           synthesizing a tap/long press from an incomplete sequence. */
        q->head=q->count=0;++q->overflows;q->blocked=s.down;
        q->samples[0]=(thingsos_touch_sample_t){s.at_ms,0,0,2,true};
        q->motion[0]=false;q->count=1;
        if(s.down)return;
        tail=1;
    }
    q->samples[tail]=s;q->motion[tail]=move;++q->count;
}
static inline bool thingsos_touch_queue_pop(thingsos_touch_queue_t* q, thingsos_touch_sample_t* s) {
    if(q->count) {
        *s=q->samples[q->head];q->head=(q->head+1)%THINGSOS_TOUCH_QUEUE_SIZE;--q->count;
        return true;
    }
    if(!q->valid)return false;
    *s=q->latest;return true;
}
#endif
