// SPDX-License-Identifier: Apache-2.0
/* Actual 2026-10-28 portrait left / return / right / return capture. */
#include "thingsos/tilt.h"
#include "tilt_mount.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"mount:%d: %s\n",__LINE__,#x);exit(1);}}while(0)
int main(int argc,char** argv) {
    CHECK(argc==2);FILE* f=fopen(argv[1],"r");CHECK(f);
    thingsos_tilt_stream_t stream={0};char line[256];unsigned frames=0,turns=0;
    const int expected[]={-1,1};uint64_t first=0;
    while(fgets(line,sizeof(line),f)) {
        unsigned reset,landscape,level,blocked;unsigned long long ms;int x,y,z;
        CHECK(sscanf(line,"%u,%llu,%d,%d,%d,%u,%u,%u",&reset,&ms,&x,&y,&z,&landscape,&level,&blocked)==8);
        if(reset)stream=(thingsos_tilt_stream_t){0};
        if(!frames)first=ms;
        thingsos_metalio_tilt_mount(x,y,z,&x,&y,&z);
        thingsos_tilt_stream_sample(&stream,x,y,z,ms,landscape!=0,level,blocked!=0);
        uint64_t at=0;int event=thingsos_tilt_stream_take(&stream,ms,&at);
        if(event) {
            fprintf(stderr,"turn direction=%d after=%llu ms\n",event,(unsigned long long)(ms-first));
            CHECK(turns<2&&event==expected[turns]);
            CHECK(turns?ms-first>=10000&&ms-first<13000:ms-first>=3500&&ms-first<7000);
            ++turns;
        }
        ++frames;
    }
    fclose(f);CHECK(frames>1000&&turns==2);
    CHECK(stream.state.calibrated&&stream.state.armed);
    puts("PASS: actual Metalio raw capture produces one left and one right turn, none on return or idle");
    return 0;
}
