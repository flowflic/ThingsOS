// SPDX-License-Identifier: Apache-2.0
#include "thingsos/tilt.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
void thingsos_tilt_screen_axes(int raw_x,int raw_y,int raw_z,bool landscape,int* x,int* y,int* z) {
    /* Measured on RDP-G01-W: portrait right edge down makes chip X negative.
       Native panel device axes are not the portrait reading axes. */
    int portrait_x=-raw_x,portrait_y=raw_y;
    *x=landscape?portrait_y:portrait_x;
    *y=landscape?-portrait_x:portrait_y;
    *z=-raw_z;
}
void thingsos_tilt_reset(thingsos_tilt_t* s) { memset(s,0,sizeof(*s)); }
static int roll_angle(int x,int y,int z) {
    return (int)(atan2((double)x,sqrt((double)y*y+(double)z*z))*57295.779513);
}
typedef struct {int angle,assisted,neutral,held;unsigned confirm,cooldown;} tilt_profile_t;
static const tilt_profile_t* tilt_profile(unsigned level) {
    static const tilt_profile_t profiles[3]={
        {14000,10000,6000,8000,100,600},
        {10000,7000,5000,6000,60,450},
        {7000,5000,3000,4000,40,350}
    };
    return &profiles[level<3?level:1];
}
/* A rolling reference cannot establish stillness: slow motion could stay
   close to the preceding sample indefinitely. Keep one fixed dwell anchor. */
static bool tilt_settled(thingsos_tilt_t* s,uint64_t ms,unsigned dwell) {
    if(!s->rest_active || abs(s->angle-s->rest_angle)>2500 ||
       abs(s->x-s->rest_x)>100 || abs(s->y-s->rest_y)>100 || abs(s->z-s->rest_z)>100) {
        s->rest_active=true;s->rest_since=ms;s->rest_angle=s->angle;
        s->rest_x=s->x;s->rest_y=s->y;s->rest_z=s->z;
    }
    return ms-s->rest_since>=dwell;
}
static void tilt_neutral(thingsos_tilt_t* s,uint64_t ms,bool rebase) {
    if(rebase) {
        s->bx=s->x;s->by=s->y;s->bz=s->z;
        s->origin_angle=s->angle;
    }
    s->base_angle=s->origin_angle;
    s->calibrated=true;s->armed=true;s->candidate=s->motion_candidate=0;
    s->fired_direction=0;s->peak_angle=s->angle;s->since=ms;s->motion_until=0;
    s->rest_active=s->posture_pending=s->touch_recenter=false;
}
static int tilt_fire(thingsos_tilt_t* s,int direction,uint64_t ms,const tilt_profile_t* profile) {
    s->armed=false;s->candidate=s->motion_candidate=0;s->since=ms;
    s->cooldown=ms+profile->cooldown;s->fired_direction=direction;s->peak_angle=s->angle;
    s->rest_active=s->posture_pending=s->touch_recenter=false;
    return direction;
}
bool thingsos_tilt_holds_direction(const thingsos_tilt_t* s,int direction,unsigned level) {
    return (direction==1 || direction==-1) && direction*(s->angle-s->base_angle)>=tilt_profile(level)->held;
}
int thingsos_tilt_sample(thingsos_tilt_t* s,int x,int y,int z,uint64_t ms) {
    return thingsos_tilt_sample_level(s,x,y,z,ms,1);
}
int thingsos_tilt_sample_level(thingsos_tilt_t* s,int x,int y,int z,uint64_t ms,unsigned level) {
    const tilt_profile_t* profile=tilt_profile(level);
    int64_t norm=(int64_t)x*x+(int64_t)y*y+(int64_t)z*z;
    if(norm<600LL*600 || norm>1800LL*1800) {
        /* Reject the sample without losing the last valid turn's return
           direction/peak. Clearing that recovery evidence here leaves a
           changed grip permanently unarmed even after a real reversal. A
           held edge still cannot rearm, and invalid input never sets zero. */
        if(s->calibrated){s->candidate=s->motion_candidate=0;s->since=ms;
            /* An invalid sample is not a page turn. A ready detector stays
               ready; only an already fired/touch-blocked cycle needs its
               existing return gate and impact cooldown. */
            if(!s->armed)s->cooldown=ms+profile->cooldown;
            s->motion_until=0;s->rest_active=s->posture_pending=false;}
        else thingsos_tilt_reset(s);
        return 0;
    }
    if(s->initialized && (ms<s->last || ms-s->last>300)) {
        if(!s->calibrated)thingsos_tilt_reset(s);
        else {
            /* Fresh pose confirmation may restart after a scan; do not lock
               a ready detector merely because the display paused it. */
            s->candidate=s->motion_candidate=0;s->since=ms;s->motion_until=0;
            s->rest_active=s->posture_pending=false;
            s->last=ms;s->x=x;s->y=y;s->z=z;s->angle=roll_angle(x,y,z);return 0;
        }
    }
    if(!s->initialized) {
        s->initialized=true;s->x=x;s->y=y;s->z=z;s->angle=roll_angle(x,y,z);
        s->since=s->last=ms;return 0;
    }
    uint64_t dt=ms-s->last;
    if(dt<30)return 0;
    s->last=ms;
    int residual_x=abs(x-s->x);
    bool steady=residual_x<130 && abs(y-s->y)<130 && abs(z-s->z)<130;
    s->x=(s->x+x)/2;s->y=(s->y+y)/2;s->z=(s->z+z)/2;
    int angle=roll_angle(s->x,s->y,s->z);
    /* Gravity angle is primary. Same-axis dynamic acceleration plus angular
       change gives a brief assist for a natural wrist turn; slow turns work too. */
    int rate=(int)((int64_t)abs(angle-s->angle)*1000/(int64_t)dt);
    if(residual_x>=60 && rate>=18000)s->motion_until=ms+350;
    s->angle=angle;
    if(!s->calibrated) {
        /* Match Pico: a stable reading grip is the relative zero, even when
           tilted. Absolute roll must not prevent the 400ms calibration. */
        if(!steady)s->since=ms;
        if(ms-s->since>=400) {
            tilt_neutral(s,ms,true);
        }
        return 0;
    }
    if(s->armed && abs(s->y-s->by)>550 && abs(s->z-s->bz)>250) {
        /* A transient cross-axis excursion is not a new grip. Retain the old
           calibration until the changed posture has actually settled. Never
           capture the held end of an already fired wrist turn as neutral. */
        if(!s->posture_pending){s->posture_pending=true;s->rest_active=false;}
        s->candidate=s->motion_candidate=0;s->motion_until=0;
        if(tilt_settled(s,ms,400))tilt_neutral(s,ms,true);
        return 0;
    }
    if(s->posture_pending) {
        s->posture_pending=false;s->rest_active=false;
        s->candidate=s->motion_candidate=0;s->motion_until=0;s->since=ms;
    }
    int delta=s->angle-s->base_angle;
    if(!s->armed) {
        s->motion_candidate=0;
        int returned=s->angle-s->origin_angle;
        /* The ready range must stay inside the trigger range. In particular,
           an 8-degree return gate overlaps high sensitivity's 5/7 degrees
           and would repeatedly fire after rearming at a held small tilt. */
        bool near=abs(returned)<=profile->neutral;
        if(s->fired_direction*(s->angle-s->peak_angle)>0)s->peak_angle=s->angle;
        int retreat=profile->neutral>5000?profile->neutral:5000;
        bool reversed=s->fired_direction && s->fired_direction*(s->peak_angle-s->angle)>=retreat;
        if(!near&&!reversed&&!s->touch_recenter){s->rest_active=false;return 0;}
        unsigned dwell=near?140:s->touch_recenter?400:240;
        if(tilt_settled(s,ms,dwell)&&ms>=s->cooldown)tilt_neutral(s,ms,!near);
        return 0;
    }
    int threshold=ms<=s->motion_until?profile->assisted:profile->angle;
    /* Short wrist turns can finish before the low-pass angle settles. Require
       two fresh gravity-consistent samples in the same direction plus the
       existing acceleration assist; a single impact still cannot trigger. */
    int raw_delta=roll_angle(x,y,z)-s->base_angle;
    int motion=raw_delta>=profile->assisted?1:raw_delta<=-profile->assisted?-1:0;
    if(motion && ms<=s->motion_until && norm>=850LL*850 && norm<=1150LL*1150) {
        if(motion!=s->motion_candidate){s->motion_candidate=motion;s->motion_since=ms;}
        else if(ms-s->motion_since>=(profile->confirm>40?profile->confirm-20:40) &&
                motion*delta>=profile->assisted-2000) {
            return tilt_fire(s,motion,ms,profile);
        }
    } else s->motion_candidate=0;
    int direction=delta>=threshold?1:delta<=-threshold?-1:0;
    if(!direction){s->candidate=0;s->since=ms;return 0;}
    if(direction!=s->candidate){s->candidate=direction;s->since=ms;return 0;}
    if(ms-s->since<profile->confirm)return 0;
    return tilt_fire(s,direction,ms,profile);
}

void thingsos_tilt_queue_push(thingsos_tilt_queue_t* q,thingsos_tilt_input_t sample) {
    if(q->count==THINGSOS_TILT_QUEUE_SIZE){q->head=(q->head+1)%THINGSOS_TILT_QUEUE_SIZE;--q->count;}
    q->samples[(q->head+q->count)%THINGSOS_TILT_QUEUE_SIZE]=sample;++q->count;
}
void thingsos_tilt_stream_sample(thingsos_tilt_stream_t* s,int x,int y,int z,uint64_t ms,
    bool landscape,unsigned level,bool blocked) {
    if(!s->configured || s->landscape!=landscape || s->level!=level) {
        memset(s,0,sizeof(*s));s->configured=true;s->landscape=landscape;s->level=level;
    }
    thingsos_tilt_screen_axes(x,y,z,landscape,&x,&y,&z);
    int direction=thingsos_tilt_sample_level(&s->state,x,y,z,ms,level);
    if(blocked) {
        /* A finger suppresses intents, not a valid neutral posture. Track
           fresh samples while held; release still needs a neutral return. */
        s->pending=0;s->state.candidate=s->state.motion_candidate=0;s->state.motion_until=0;
        if(s->state.calibrated) {
            s->state.armed=false;s->state.since=ms;
            s->state.fired_direction=0;s->state.touch_recenter=true;
            s->state.rest_active=true;s->state.rest_since=ms;
            s->state.rest_angle=s->state.angle;
            s->state.rest_x=s->state.x;s->state.rest_y=s->state.y;s->state.rest_z=s->state.z;
            if(s->state.cooldown<ms+140)s->state.cooldown=ms+140;
        }
        return;
    }
    if(direction){s->pending=direction;s->pending_at=ms;}
}
int thingsos_tilt_stream_take(thingsos_tilt_stream_t* s,uint64_t now,uint64_t* at) {
    int direction=s->pending;
    if(!direction)return 0;
    if(now<s->pending_at || now<s->state.last){s->pending=0;return 0;}
    if(now-s->state.last>200) {
        if(now-s->pending_at>1000)s->pending=0;
        return 0; /* Wait for one fresh sample instead of erasing a valid turn. */
    }
    s->pending=0;
    if(now-s->pending_at>1000 && !thingsos_tilt_holds_direction(&s->state,direction,s->level))return 0;
    *at=s->pending_at;return direction;
}
bool thingsos_tilt_queue_pop(thingsos_tilt_queue_t* q,thingsos_tilt_input_t* sample) {
    if(!q->count)return false;
    *sample=q->samples[q->head];q->head=(q->head+1)%THINGSOS_TILT_QUEUE_SIZE;--q->count;return true;
}
