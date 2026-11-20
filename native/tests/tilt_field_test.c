// SPDX-License-Identifier: Apache-2.0
/* Log-constrained regressions, not a claimed 25 Hz replay: V1 logs sampled raw
   vectors sparsely. Reproduce the observed state failures with explicit input. */
#include "thingsos/tilt.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"tilt-field:%d: %s\n",__LINE__,#x);exit(1);}}while(0)
typedef struct {thingsos_tilt_stream_t stream;uint64_t clock;unsigned level;bool landscape;} test_t;
static int point(test_t* t,int x,int y,int z,unsigned frames) {
    int result=0;
    for(unsigned i=0;i<frames;++i) {
        t->clock+=40;
        thingsos_tilt_stream_sample(&t->stream,t->landscape?y:-x,t->landscape?x:y,-z,
            t->clock,t->landscape,t->level,false);
        uint64_t at=0;int event=thingsos_tilt_stream_take(&t->stream,t->clock,&at);
        if(event){CHECK(!result);result=event;CHECK(t->clock-at<=40);}
    }
    return result;
}
static void start(test_t* t,unsigned level,bool landscape) {
    *t=(test_t){.clock=1000,.level=level,.landscape=landscape};
    CHECK(!point(t,0,0,1000,30));CHECK(t->stream.state.calibrated&&t->stream.state.armed);
}
static void shifted_return(unsigned level,bool landscape,int sign) {
    test_t t;start(&t,level,landscape);
    CHECK(point(&t,sign*574,0,819,8)==sign); /* 35-degree excursion. */
    CHECK(!point(&t,sign*574,0,819,80));CHECK(!t.stream.state.armed); /* Holding cannot rearm. */
    /* V1 logged a peak near 25 degrees and a settled grip at 17..19 degrees,
       outside its fixed return gate for over 133 seconds. A real reverse
       movement followed by a settled new grip must complete the cycle. */
    CHECK(!point(&t,sign*309,0,951,18));
    CHECK(t.stream.state.calibrated&&t.stream.state.armed);
    CHECK(abs(t.stream.state.origin_angle-sign*18000)<2000);
    CHECK(!point(&t,sign*309,0,951,80));
    CHECK(point(&t,sign*574,0,819,8)==sign);
    CHECK(!point(&t,sign*309,0,951,20));CHECK(t.stream.state.armed);
    CHECK(point(&t,-sign*35,0,999,8)==-sign);
    CHECK(!point(&t,sign*309,0,951,20));CHECK(t.stream.state.armed);
}
static void transient_posture(unsigned level,bool landscape) {
    test_t t;start(&t,level,landscape);int origin=t.stream.state.origin_angle;
    for(unsigned i=0;i<6;++i) {
        CHECK(!point(&t,0,800,600,1));
        CHECK(t.stream.state.calibrated); /* The old code cleared this mid-motion. */
    }
    CHECK(!point(&t,0,0,1000,20));
    CHECK(t.stream.state.calibrated&&t.stream.state.armed&&t.stream.state.origin_angle==origin);
    CHECK(point(&t,450,0,890,10)==1);
    CHECK(!point(&t,0,0,1000,20));CHECK(t.stream.state.armed);
}
static void slow_return(unsigned level,bool landscape,int sign) {
    test_t t;start(&t,level,landscape);
    CHECK(point(&t,sign*574,0,819,15)==sign);
    for(int degree=34;degree>=0;--degree) {
        double angle=degree*3.141592653589793/180;
        CHECK(!point(&t,sign*(int)lround(1000*sin(angle)),0,(int)lround(1000*cos(angle)),2));
    }
    CHECK(!point(&t,0,0,1000,20));CHECK(t.stream.state.armed);
    CHECK(abs(t.stream.state.origin_angle)<1000);
    CHECK(point(&t,-sign*574,0,819,10)==-sign);
}
static void held_posture(unsigned level,bool landscape,int sign) {
    test_t t;start(&t,level,landscape);
    CHECK(point(&t,sign*574,0,819,15)==sign);
    CHECK(!point(&t,sign*574,500,650,100));
    CHECK(t.stream.state.calibrated&&!t.stream.state.armed);
    CHECK(abs(t.stream.state.origin_angle)<1000);
    CHECK(!point(&t,0,0,1000,20));CHECK(t.stream.state.armed);
}
static void touch_grip(unsigned level,bool landscape,int sign) {
    test_t t;start(&t,level,landscape);
    for(unsigned i=0;i<15;++i) {
        t.clock+=40;
        thingsos_tilt_stream_sample(&t.stream,landscape?0:-sign*309,landscape?sign*309:0,-951,
            t.clock,landscape,level,true);
        uint64_t at=0;CHECK(!thingsos_tilt_stream_take(&t.stream,t.clock,&at));
    }
    CHECK(!point(&t,sign*309,0,951,20));CHECK(t.stream.state.armed);
    CHECK(abs(t.stream.state.origin_angle-sign*18000)<2000);
    CHECK(point(&t,sign*574,0,819,10)==sign);
    CHECK(!point(&t,sign*309,0,951,20));CHECK(t.stream.state.armed);
}
static void release_boundary(unsigned level,bool landscape,int sign) {
    test_t t;start(&t,level,landscape);
    CHECK(point(&t,sign*574,0,819,15)==sign);
    /* The former wide return gate overlaps high sensitivity's trigger range.
       Resting at 8 degrees after a reversal must not repeatedly fire. */
    CHECK(!point(&t,sign*139,0,990,80));
    CHECK(t.stream.state.armed);
    CHECK(!point(&t,sign*139,0,990,80));
}
static void impact_during_return(unsigned level,bool landscape,int sign) {
    test_t t;start(&t,level,landscape);
    CHECK(point(&t,sign*574,0,819,15)==sign);
    /* The second device capture retained peak=12.7 degrees but fired=0 after
       an invalid acceleration sample. A settled changed grip then stayed
       unarmed despite a real reversal. The invalid sample must not erase
       the valid turn's recovery direction or establish a new neutral. */
    CHECK(!point(&t,sign*2300,0,0,1));
    CHECK(t.stream.state.fired_direction==sign&&!t.stream.state.armed);
    CHECK(!point(&t,sign*574,0,819,50));CHECK(!t.stream.state.armed);
    CHECK(!point(&t,sign*309,0,951,20));CHECK(t.stream.state.armed);
    CHECK(point(&t,-sign*35,0,999,10)==-sign);
}
static void invalid_sample_while_ready(unsigned level,bool landscape,int sign) {
    test_t t;start(&t,level,landscape);int origin=t.stream.state.origin_angle;
    /* Actual second capture: a 331,-391,-140 mg vector (about 531 mg)
       arrived while ready. Rejecting input must not invent a fired turn or
       force the ready detector into a return cycle that has no direction. */
    CHECK(!point(&t,sign*331,-391,140,1));
    CHECK(t.stream.state.armed&&t.stream.state.origin_angle==origin);
    CHECK(!point(&t,0,0,1000,20));
    CHECK(point(&t,sign*574,0,819,10)==sign);
    CHECK(!point(&t,0,0,1000,20));CHECK(t.stream.state.armed);
    CHECK(!point(&t,sign*2300,0,0,3));
    CHECK(t.stream.state.armed&&t.stream.state.origin_angle==origin);
    CHECK(!point(&t,0,0,1000,20));
    CHECK(point(&t,-sign*574,0,819,10)==-sign);
}
int main(void) {
    unsigned combinations=0;
    for(unsigned level=0;level<3;++level)for(unsigned wide=0;wide<2;++wide) {
        transient_posture(level,wide!=0);
        for(int sign=-1;sign<=1;sign+=2) {
            shifted_return(level,wide!=0,sign);
            slow_return(level,wide!=0,sign);
            held_posture(level,wide!=0,sign);++combinations;
            touch_grip(level,wide!=0,sign);
            release_boundary(level,wide!=0,sign);
            impact_during_return(level,wide!=0,sign);
            invalid_sample_while_ready(level,wide!=0,sign);
        }
    }
    printf("PASS: %u log-constrained direction/profile/orientation combinations; transient posture, shifted return, hold and slow return\n",combinations);
    return 0;
}
