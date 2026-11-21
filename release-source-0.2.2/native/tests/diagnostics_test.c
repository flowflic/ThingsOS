// SPDX-License-Identifier: Apache-2.0
#include "thingsos/diagnostics.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static char output[160000];static size_t used;
static bool sink(void* context,const char* text,size_t n) {
    (void)context;if(used+n>=sizeof(output))return false;memcpy(output+used,text,n);used+=n;output[used]=0;return true;
}
static bool broken(void* context,const char* text,size_t n){(void)context;(void)text;(void)n;return false;}
int main(int argc,char** argv) {
    thingsos_diag_journal_t j={0};assert(!thingsos_diag_valid(&j));
    j.boot_count=1;j.boots[0].id=7;strcpy(j.boots[0].version,"test\"\\\n");
    strcpy(j.boots[0].elf,"abc");strcpy(j.boots[0].running,"ota_0");strcpy(j.boots[0].selected,"rescue");
    /* Two early failures (one duplicated), then a flood of network events. */
    thingsos_diag_event_t f1={.boot=7,.ms=1,.kind=DIAG_FAILURE,.stage=1,.code=0x400,.error=20};
    thingsos_diag_event_t f2={.boot=7,.ms=2,.kind=DIAG_FAILURE,.stage=5,.a=1,.b=272};
    thingsos_diag_push(&j,f1);thingsos_diag_push(&j,f2);thingsos_diag_push(&j,f1);thingsos_diag_keep_failure(&j,f2);
    assert(j.failure_count==2 && j.event_count==3);
    for(unsigned i=0;i<DIAG_EVENTS+88;++i)thingsos_diag_push(&j,(thingsos_diag_event_t){.boot=7,.ms=100+i,.kind=DIAG_PACK,.code=-1,.error=28});
    assert(j.event_count==DIAG_EVENTS && j.events[0].ms==100+88 && j.events[DIAG_EVENTS-1].ms==100+DIAG_EVENTS+87);
    /* The flood evicted them from "events" but not from the failure ring. */
    assert(j.failure_count==2 && !memcmp(&j.failures[0],&f1,sizeof(f1)) && !memcmp(&j.failures[1],&f2,sizeof(f2)));
    for(unsigned i=0;i<DIAG_FAILURES+5;++i)thingsos_diag_push(&j,(thingsos_diag_event_t){.boot=8,.ms=i,.kind=DIAG_FAILURE});
    assert(j.failure_count==DIAG_FAILURES && j.failures[0].boot==8 && j.failures[0].ms==5);
    thingsos_diag_keep_failure(&j,(thingsos_diag_event_t){.boot=9,.kind=DIAG_PACK});assert(j.failures[DIAG_FAILURES-1].ms==DIAG_FAILURES+4);
    thingsos_diag_seal(&j);assert(thingsos_diag_valid(&j));
    assert(thingsos_diag_dump(&j,sink,NULL));assert(strstr(output,"test\\\"\\\\\\u000a"));
    assert(strstr(output,"\"errno\":28") && strstr(output,"\"selected\":\"rescue\""));
    assert(strstr(output,"\"schema\":2,") && strstr(output,"],\"failures\":[{\"boot\":8,"));
    assert(!thingsos_diag_dump(&j,broken,NULL));
    if(argc>1){FILE* f=fopen(argv[1],"wb");assert(f);assert(fwrite(output,1,used,f)==used);assert(!fclose(f));}
    j.events[0].code=0;assert(!thingsos_diag_valid(&j));
    thingsos_diag_seal(&j);j.event_count=DIAG_EVENTS+1;thingsos_diag_seal(&j);assert(!thingsos_diag_valid(&j));
    j.event_count=0;memset(j.boots[0].version,'x',sizeof(j.boots[0].version));thingsos_diag_seal(&j);assert(!thingsos_diag_valid(&j));
    puts("PASS: 512-event rotation, failure ring survives floods and deduplicates, CRC corruption, JSON escaping, sink failure, malformed record rejection");return 0;
}
