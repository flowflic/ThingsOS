// SPDX-License-Identifier: Apache-2.0
#include "thingsos/tilt.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"tilt:%d: %s\n",__LINE__,#x);exit(1);}} while(0)
static uint64_t ms;
static int chip_feed(thingsos_tilt_t* s,int rx,int ry,int rz,unsigned n) {
    int result=0;
    for(unsigned i=0;i<n;++i) {
        int x,y,z;thingsos_tilt_screen_axes(rx,ry,rz,false,&x,&y,&z);
        ms+=60;int v=thingsos_tilt_sample(s,x,y,z,ms);if(v){CHECK(!result);result=v;}
    }
    return result;
}
static int feed(thingsos_tilt_t* s,int x,int y,int z,unsigned count) {
    int result=0;
    for(unsigned i=0;i<count;++i) {ms+=40;int v=thingsos_tilt_sample(s,x,y,z,ms);if(v){CHECK(!result);result=v;}}
    return result;
}
/* Calibrate and turn relative to a stable tilted grip, through the same raw
   chip stream used by the input task. Exercise both signs and reader axes. */
static void tilted_grip(unsigned level,bool landscape,int rest_x,int rest_z) {
    thingsos_tilt_stream_t stream={0};
    uint64_t clock=1000,at=0;
    double radius=sqrt((double)rest_x*rest_x+(double)rest_z*rest_z);
    double base=atan2((double)rest_x,(double)rest_z);
    for(unsigned i=0;i<30;++i) {
        clock+=40;
        thingsos_tilt_stream_sample(&stream,landscape?0:-rest_x,landscape?rest_x:0,-rest_z,
            clock,landscape,level,false);
        CHECK(!thingsos_tilt_stream_take(&stream,clock,&at));
        if(i<10)CHECK(!stream.state.calibrated); /* Still require 400ms stable. */
    }
    CHECK(stream.state.calibrated && stream.state.armed);
    int origin=stream.state.origin_angle;
    for(int direction=-1;direction<=1;direction+=2) {
        double angle=base+direction*15*3.141592653589793/180;
        int x=(int)lround(radius*sin(angle)),z=(int)lround(radius*cos(angle));
        unsigned turns=0;
        for(unsigned i=0;i<20;++i) {
            clock+=40;
            thingsos_tilt_stream_sample(&stream,landscape?0:-x,landscape?x:0,-z,
                clock,landscape,level,false);
            int event=thingsos_tilt_stream_take(&stream,clock,&at);
            if(event){CHECK(event==direction);++turns;}
        }
        CHECK(turns==1); /* Holding the turn cannot repeat it. */
        for(unsigned i=0;i<30;++i) {
            clock+=40;
            thingsos_tilt_stream_sample(&stream,landscape?0:-rest_x,landscape?rest_x:0,-rest_z,
                clock,landscape,level,false);
            CHECK(!thingsos_tilt_stream_take(&stream,clock,&at));
        }
        CHECK(stream.state.armed && stream.state.origin_angle==origin);
    }
}
int main(void) {
    thingsos_tilt_t s={0};
    CHECK(!feed(&s,0,0,1000,20));CHECK(s.calibrated);
    CHECK(!feed(&s,30,25,995,30)); /* Hand tremor. */
    CHECK(feed(&s,450,0,890,20)==1); /* Right side down, no diagonal component. */
    CHECK(!feed(&s,0,0,1000,20));
    CHECK(feed(&s,400,260,880,20)==1);
    CHECK(!feed(&s,400,260,880,100)); /* Held angle never repeats. */
    ms+=2000;CHECK(!feed(&s,400,260,880,30)); /* Panel scan gap. */
    CHECK(!feed(&s,0,0,1000,30));CHECK(s.armed);
    CHECK(feed(&s,-400,260,880,20)==-1);
    CHECK(!feed(&s,-400,260,880,30));
    CHECK(!feed(&s,0,0,2500,3)); /* Bump cannot establish a new zero. */
    CHECK(!feed(&s,-400,260,880,30));
    CHECK(!feed(&s,0,0,1000,30));CHECK(s.armed);
    CHECK(feed(&s,400,-260,880,20)==1); /* Pitch is independent of the wrist roll. */
    thingsos_tilt_reset(&s);CHECK(!s.calibrated);
    CHECK(!feed(&s,0,450,890,20)); /* Non-flat reading posture. */
    CHECK(feed(&s,400,620,670,20)==1);
    thingsos_tilt_reset(&s);
    CHECK(!feed(&s,400,260,880,4));ms+=1000;
    CHECK(!feed(&s,0,0,1000,20));CHECK(s.calibrated);
    /* Realistic upright grip: a roll changes Y by only 60mg, not 120mg. */
    thingsos_tilt_reset(&s);CHECK(!feed(&s,0,-980,180,20));CHECK(s.calibrated);
    CHECK(feed(&s,340,-920,180,14)==1);
    CHECK(!feed(&s,340,-920,180,30));
    CHECK(!feed(&s,0,-980,180,30));CHECK(s.armed);
    CHECK(feed(&s,-340,-920,180,14)==-1);
    thingsos_tilt_reset(&s);CHECK(!feed(&s,0,980,180,20));
    CHECK(feed(&s,340,920,180,14)==1);
    /* Actual device capture: neutral, right roll, return, left roll. */
    thingsos_tilt_reset(&s);
    CHECK(!chip_feed(&s,52,-201,-956,20));CHECK(s.calibrated);
    CHECK(chip_feed(&s,-710,-90,-730,30)==1);
    CHECK(!chip_feed(&s,-710,-90,-730,30));
    CHECK(!chip_feed(&s,40,-35,-980,30));CHECK(s.armed);
    CHECK(chip_feed(&s,750,-140,-640,30)==-1);
    int x,y,z;thingsos_tilt_screen_axes(-700,-100,-700,false,&x,&y,&z);
    CHECK(x==700 && y==-100 && z==700);
    thingsos_tilt_screen_axes(50,500,-860,true,&x,&y,&z);
    CHECK(x==500 && y==50 && z==860);
    /* A natural roll keeps moving; it must not require a motionless plateau. */
    thingsos_tilt_reset(&s);CHECK(!feed(&s,0,0,1000,15));CHECK(s.calibrated);
    int moving_turn=0;
    for(int i=0;i<10;++i){ms+=40;int v=thingsos_tilt_sample(&s,80+i*50,0,1000-i*20,ms);if(v){CHECK(!moving_turn);moving_turn=v;}}
    CHECK(moving_turn==1);CHECK(!feed(&s,530,0,820,30));
    CHECK(!feed(&s,0,0,1000,12));CHECK(s.armed);
    CHECK(feed(&s,-230,0,970,8)==-1); /* Small comfortable wrist turn. */
    /* A single acceleration impulse cannot turn a page. */
    thingsos_tilt_reset(&s);CHECK(!feed(&s,0,0,1000,15));
    CHECK(!feed(&s,750,0,1000,1));CHECK(!feed(&s,0,0,1000,15));
    /* Low-power sensor noise must not restart the initial zero forever. */
    thingsos_tilt_reset(&s);
    for(unsigned i=0;i<25;++i){static const int jitter[]={0,60,-40,45,-55,20};ms+=40;CHECK(!thingsos_tilt_sample(&s,jitter[i%6],-30,1000+jitter[(i+2)%6],ms));}
    CHECK(s.calibrated);
    /* Display pauses drop the candidate, without disabling a ready detector. */
    ms+=700;CHECK(!thingsos_tilt_sample(&s,0,-30,1000,ms));CHECK(s.armed);
    CHECK(feed(&s,260,-30,960,8)==1);
    /* Lifting from a desk into an upright grip must establish a new neutral. */
    thingsos_tilt_reset(&s);CHECK(!feed(&s,0,0,1000,15));
    CHECK(!feed(&s,250,-850,450,25));CHECK(s.calibrated && s.armed);
    CHECK(feed(&s,480,-750,440,10)==1);
    /* An ordinary slow tilt still works without an acceleration impulse. */
    thingsos_tilt_reset(&s);CHECK(!feed(&s,0,0,1000,15));
    int slow_turn=0;
    for(int i=1;i<=22;++i){ms+=80;int v=thingsos_tilt_sample(&s,i*12,0,1000,ms);if(v){CHECK(!slow_turn);slow_turn=v;}}
    CHECK(slow_turn==1);
    /* Angle plus acceleration assist: a quick 9-degree turn is enough. */
    thingsos_tilt_reset(&s);CHECK(!feed(&s,0,0,1000,15));
    CHECK(feed(&s,160,0,990,8)==1);CHECK(!feed(&s,160,0,990,30));
    /* The same sub-threshold angle approached slowly is not hand tremor input. */
    thingsos_tilt_reset(&s);CHECK(!feed(&s,0,0,1000,15));
    for(int i=1;i<=13;++i){ms+=100;CHECK(!thingsos_tilt_sample(&s,i*10,0,1000,ms));}
    CHECK(!feed(&s,130,0,1000,20));
    /* A brief 9-degree wrist turn returns before the old filtered hold could
       confirm. Two samples are enough; one isolated impulse is still ignored. */
    thingsos_tilt_reset(&s);CHECK(!feed(&s,0,0,1000,15));
    CHECK(feed(&s,160,0,990,2)==1);
    CHECK(!feed(&s,0,0,1000,12));CHECK(s.armed);
    CHECK(feed(&s,-160,0,990,2)==-1);
    /* A genuine return settles at a changed +6..7-degree grip. Establish that
       grip once, without repeatedly drifting or firing while simply held. */
    thingsos_tilt_reset(&s);CHECK(!feed(&s,0,0,1000,15));
    CHECK(feed(&s,350,0,940,10)==1);
    CHECK(!feed(&s,115,0,993,30));CHECK(s.armed && s.base_angle>=6000 && s.base_angle<=7500);
    int shifted_zero=s.base_angle;
    CHECK(!feed(&s,115,0,993,30));CHECK(s.base_angle==shifted_zero);
    CHECK(feed(&s,300,0,950,5)==1);
    CHECK(!feed(&s,0,0,1000,20)); /* Returning to the original zero is not a left turn. */
    /* Low / medium / high: slow angle threshold ordering and medium default. */
    int first[3]={0};
    for(unsigned level=0;level<3;++level) {
        thingsos_tilt_reset(&s);
        for(unsigned i=0;i<15;++i){ms+=40;CHECK(!thingsos_tilt_sample_level(&s,0,0,1000,ms,level));}
        for(int i=1;i<=20;++i){ms+=100;int v=thingsos_tilt_sample_level(&s,i*10,0,1000,ms,level);if(v){CHECK(v==1 && !first[level]);first[level]=i;}}
    }
    CHECK(!first[0] && first[1]>first[2] && first[2]>0);
    thingsos_tilt_t medium={0},standard={0},invalid={0};
    for(unsigned i=0;i<150;++i) {
        int x=i<20?0:i<50?350:i<80?0:i<110?-350:0;
        ms+=40;
        int expected=thingsos_tilt_sample(&standard,x,0,940,ms);
        CHECK(thingsos_tilt_sample_level(&medium,x,0,940,ms,1)==expected);
        CHECK(thingsos_tilt_sample_level(&invalid,x,0,940,ms,99)==expected);
        CHECK(standard.angle==medium.angle && standard.armed==medium.armed && standard.base_angle==medium.base_angle);
    }
    thingsos_tilt_queue_t queue={0};thingsos_tilt_input_t input;
    for(unsigned i=0;i<80;++i)thingsos_tilt_queue_push(&queue,(thingsos_tilt_input_t){(int)i,0,0,i*40});
    CHECK(queue.count==64);
    for(unsigned i=16;i<80;++i){CHECK(thingsos_tilt_queue_pop(&queue,&input));CHECK(input.x==(int)i && input.ms==i*40);}
    CHECK(!thingsos_tilt_queue_pop(&queue,&input));
    /* Detection keeps running in the input task while UI does no polling. */
    thingsos_tilt_stream_t stream={0};uint64_t clock=1000,event_at=0;
    for(unsigned i=0;i<80;++i){clock+=40;thingsos_tilt_stream_sample(&stream,0,0,-1000,clock,false,1,false);}
    for(unsigned i=0;i<10;++i){clock+=40;thingsos_tilt_stream_sample(&stream,-450,0,-890,clock,false,1,false);}
    for(unsigned i=0;i<10;++i){clock+=40;thingsos_tilt_stream_sample(&stream,0,0,-1000,clock,false,1,false);}
    CHECK(thingsos_tilt_stream_take(&stream,clock,&event_at)==1 && clock-event_at<=1000);
    CHECK(!thingsos_tilt_stream_take(&stream,clock,&event_at));
    stream.pending=1;stream.pending_at=clock;stream.state.last=clock;
    CHECK(!thingsos_tilt_stream_take(&stream,clock+240,&event_at) && stream.pending==1);
    stream.state.last=clock+280;
    CHECK(thingsos_tilt_stream_take(&stream,clock+280,&event_at)==1 && !stream.pending);
    clock+=280;
    for(unsigned i=0;i<20;++i){clock+=40;thingsos_tilt_stream_sample(&stream,0,0,-1000,clock,false,1,false);}
    for(unsigned i=0;i<10;++i){clock+=40;thingsos_tilt_stream_sample(&stream,450,0,-890,clock,false,1,false);}
    for(unsigned i=0;i<40;++i){clock+=40;thingsos_tilt_stream_sample(&stream,0,0,-1000,clock,false,1,false);}
    CHECK(!thingsos_tilt_stream_take(&stream,clock,&event_at)); /* Expired, never replay after a long block. */
    for(unsigned i=0;i<10;++i){clock+=40;thingsos_tilt_stream_sample(&stream,-450,0,-890,clock,false,1,false);}
    thingsos_tilt_stream_sample(&stream,0,0,-1000,clock+40,false,1,true);
    CHECK(!thingsos_tilt_stream_take(&stream,clock+40,&event_at) && stream.state.calibrated && !stream.state.armed);
    for(unsigned i=0;i<20;++i){clock+=40;thingsos_tilt_stream_sample(&stream,0,0,-1000,clock,false,1,false);}
    for(unsigned i=0;i<80;++i){clock+=40;thingsos_tilt_stream_sample(&stream,-450,0,-890,clock,false,1,false);}
    CHECK(thingsos_tilt_stream_take(&stream,clock,&event_at)==1); /* Still held: preserve one intent across a long frame. */
    CHECK(!thingsos_tilt_stream_take(&stream,clock,&event_at));
    memset(&stream,0,sizeof(stream));clock=1000;
    for(unsigned i=0;i<20;++i){clock+=40;thingsos_tilt_stream_sample(&stream,0,0,-1000,clock,false,1,false);}
    CHECK(stream.state.calibrated);int neutral=stream.state.base_angle;
    for(unsigned i=0;i<3;++i){clock+=40;thingsos_tilt_stream_sample(&stream,0,0,-1000,clock,false,1,true);}
    CHECK(stream.state.calibrated && stream.state.base_angle==neutral && !stream.pending);
    for(unsigned i=0;i<4;++i){clock+=40;thingsos_tilt_stream_sample(&stream,0,0,-1000,clock,false,1,false);}
    CHECK(stream.state.armed); /* Resume after neutral dwell, without another 400 ms calibration. */
    for(unsigned i=0;i<8;++i){clock+=40;thingsos_tilt_stream_sample(&stream,-450,0,-890,clock,false,1,false);}
    CHECK(thingsos_tilt_stream_take(&stream,clock,&event_at)==1);
    for(unsigned i=0;i<20;++i){clock+=40;thingsos_tilt_stream_sample(&stream,-450,0,-890,clock,false,1,false);}
    CHECK(!thingsos_tilt_stream_take(&stream,clock,&event_at));
    /* Stable 20/24-degree grips are neutral postures, not perpetual gestures.
       Sensitivity must not prevent startup; returns use the original grip. */
    for(unsigned level=0;level<3;++level)
        for(unsigned landscape=0;landscape<2;++landscape)
            for(int sign=-1;sign<=1;sign+=2) {
                tilted_grip(level,landscape!=0,sign*350,950);
                tilted_grip(level,landscape!=0,sign*407,926);
            }

    /* High sensitivity + a flat Metalio mounting rest near 7.6 degrees:
       the rest must become the neutral, otherwise the feature never arms. */
    thingsos_tilt_reset(&s);
    ms = 0;
    for (int i = 0; i < 30; ++i) { ms += 40; CHECK(!thingsos_tilt_sample_level(&s, 132, 0, 1000, ms, 2)); }
    CHECK(s.calibrated);
    int high_flat_turn = 0;
    for (int i = 0; i < 20; ++i) {
        ms += 40;
        int v = thingsos_tilt_sample_level(&s, 450, 0, 890, ms, 2);
        if (v) { CHECK(!high_flat_turn); high_flat_turn = v; }
    }
    CHECK(high_flat_turn == 1);

    /* A deliberate left/right wrist turn must not turn the opposite page on
       the quick return to neutral. The Metalio reported both tilts advancing. */
    for (int dir = -1; dir <= 1; dir += 2) {
        thingsos_tilt_t quick = {0};
        ms = 0;
        for (int i = 0; i < 20; ++i) { ms += 40; CHECK(!thingsos_tilt_sample(&quick, 0, 0, 1000, ms)); }
        int turned = 0;
        for (int i = 0; i < 20; ++i) {
            ms += 40;
            int v = thingsos_tilt_sample(&quick, dir * 400, dir * 260, 880, ms);
            if (v) { CHECK(!turned); turned = v; }
        }
        CHECK(turned == dir);
        int returned = 0;
        for (int i = 1; i <= 6; ++i) {
            int f = 6 - i;
            ms += 40;
            int v = thingsos_tilt_sample(&quick, dir * 400 * f / 6, dir * 260 * f / 6, 1000 - 120 * f / 6, ms);
            if (v) { returned = v; break; }
        }
        CHECK(!returned);
        for (int i = 0; i < 30 && !returned; ++i) {
            ms += 40;
            returned = thingsos_tilt_sample(&quick, 0, 0, 1000, ms);
        }
        CHECK(!returned);
    }
    /* Replaying the Metalio left-turn / slow-return shape: the return must
       not become a forward gesture after the detector re-arms. */
    {
        thingsos_tilt_t slow = {0};
        ms = 0;
        for (int i = 0; i < 20; ++i) { ms += 40; CHECK(!thingsos_tilt_sample(&slow, 0, 0, 1000, ms)); }
        int slow_turn = 0;
        for (int i = 0; i < 20; ++i) {
            ms += 40;
            int v = thingsos_tilt_sample(&slow, -350, 0, 950, ms);
            if (v) { CHECK(!slow_turn); slow_turn = v; }
        }
        CHECK(slow_turn == -1);
        int slow_return = 0;
        for (int i = 1; i <= 30; ++i) {
            int f = 30 - i;
            ms += 40;
            int v = thingsos_tilt_sample(&slow, -350 * f / 30, 20 * f / 30, 950 + 60 * f / 30, ms);
            if (v) { slow_return = v; break; }
        }
        CHECK(!slow_return);
        for (int i = 0; i < 30 && !slow_return; ++i) {
            ms += 40;
            slow_return = thingsos_tilt_sample(&slow, 0, 20, 1010, ms);
        }
        CHECK(!slow_return);
    }
    puts("PASS: directions, jitter, return, hold, stale samples, posture, reset, mailbox, touch baseline preservation");
    return 0;
}
