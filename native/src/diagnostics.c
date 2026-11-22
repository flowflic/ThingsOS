// SPDX-License-Identifier: Apache-2.0
#include "thingsos/diagnostics.h"
#include <stdio.h>
#include <string.h>
/* 2: 512 events + separate failure ring (the size check also rejects v1). */
#define DIAG_MAGIC 0x44494132u
uint32_t thingsos_diag_crc(const void* p,size_t n) {
    const unsigned char* s=p;uint32_t crc=~0u;
    while(n--){crc^=*s++;for(unsigned i=0;i<8;++i)crc=(crc>>1)^(0xedb88320u&-(crc&1));}
    return ~crc;
}
void thingsos_diag_seal(thingsos_diag_journal_t* j) {
    j->magic=DIAG_MAGIC;j->checksum=thingsos_diag_crc(j,offsetof(thingsos_diag_journal_t,checksum));
}
bool thingsos_diag_valid(const thingsos_diag_journal_t* j) {
    if(j->magic!=DIAG_MAGIC || j->boot_count>DIAG_BOOTS || j->event_count>DIAG_EVENTS || j->failure_count>DIAG_FAILURES ||
       j->checksum!=thingsos_diag_crc(j,offsetof(thingsos_diag_journal_t,checksum)))return false;
    for(unsigned i=0;i<=j->boot_count;++i) {
        const thingsos_diag_boot_t* b=i==j->boot_count?&j->last_main:&j->boots[i];
        if(!memchr(b->version,0,sizeof(b->version)) || !memchr(b->elf,0,sizeof(b->elf)) ||
           !memchr(b->running,0,sizeof(b->running)) || !memchr(b->selected,0,sizeof(b->selected)))return false;
    }
    return true;
}
static void push_failure(thingsos_diag_journal_t* j,thingsos_diag_event_t e) {
    for(unsigned i=0;i<j->failure_count;++i)if(!memcmp(j->failures+i,&e,sizeof(e)))return;
    if(j->failure_count==DIAG_FAILURES){memmove(j->failures,j->failures+1,(DIAG_FAILURES-1)*sizeof(e));--j->failure_count;}
    j->failures[j->failure_count++]=e;
}
void thingsos_diag_push(thingsos_diag_journal_t* j,thingsos_diag_event_t e) {
    if(j->event_count==DIAG_EVENTS){memmove(j->events,j->events+1,(DIAG_EVENTS-1)*sizeof(e));--j->event_count;}
    j->events[j->event_count++]=e;++j->sequence;
    if(e.kind==DIAG_FAILURE)push_failure(j,e);
}
void thingsos_diag_keep_failure(thingsos_diag_journal_t* j,thingsos_diag_event_t e) {
    if(e.kind==DIAG_FAILURE)push_failure(j,e);
}
static bool quoted(thingsos_diag_sink sink,void* ctx,const char* s) {
    if(!sink(ctx,"\"",1))return false;
    char block[128];size_t used=0;
    for(;*s;++s){unsigned char c=(unsigned char)*s;char b[7];size_t n=1;b[0]=(char)c;
        if(c=='"'||c=='\\'){b[0]='\\';b[1]=(char)c;n=2;}
        else if(c<32){snprintf(b,sizeof(b),"\\u%04x",c);n=6;}
        if(used+n>sizeof(block)){if(!sink(ctx,block,used))return false;used=0;}
        memcpy(block+used,b,n);used+=n;
    }
    return (!used || sink(ctx,block,used)) && sink(ctx,"\"",1);
}
bool thingsos_diag_dump(const thingsos_diag_journal_t* j,thingsos_diag_sink sink,void* ctx) {
    if(!thingsos_diag_valid(j))return false;
    const char* head="{\"schema\":2,\"privacy\":\"numeric events only; no credentials, titles, paths or book text\",\"boots\":[";
    if(!sink(ctx,head,strlen(head)))return false;
    char line[320];
    for(unsigned i=0;i<=j->boot_count;++i) {
        const thingsos_diag_boot_t* b=i==j->boot_count?&j->last_main:j->boots+i;
        if(i==j->boot_count && !sink(ctx,"],\"last_main\":",14))return false;
        int n=snprintf(line,sizeof(line),"%s{\"id\":%u,\"reset\":%u,\"attempts\":%u,\"healthy\":%u,\"reason\":%u,\"version\":",i && i<j->boot_count?",":"",(unsigned)b->id,(unsigned)b->reset,(unsigned)b->attempts,(unsigned)b->healthy,(unsigned)b->reason);
        if(!sink(ctx,line,(size_t)n)||!quoted(sink,ctx,b->version)||!sink(ctx,",\"elf\":",7)||!quoted(sink,ctx,b->elf)||
           !sink(ctx,",\"running\":",11)||!quoted(sink,ctx,b->running)||!sink(ctx,",\"selected\":",12)||!quoted(sink,ctx,b->selected)||!sink(ctx,"}",1))return false;
    }
    /* "failures" keeps failure records in their own ring; recent ones also
       appear in "events". Consumers order by boot/ms. */
    for(unsigned part=0;part<2;++part) {
        const thingsos_diag_event_t* list=part?j->failures:j->events;
        unsigned count=part?j->failure_count:j->event_count;
        const char* open=part?"],\"failures\":[":",\"events\":[";
        if(!sink(ctx,open,strlen(open)))return false;
        for(unsigned i=0;i<count;++i) {
            const thingsos_diag_event_t* e=list+i;
            int n=snprintf(line,sizeof(line),"%s{\"boot\":%u,\"ms\":%u,\"utc\":%u,\"kind\":%u,\"stage\":%u,\"a\":%u,\"b\":%u,\"code\":%d,\"errno\":%d}",i?",":"",(unsigned)e->boot,(unsigned)e->ms,(unsigned)e->utc,(unsigned)e->kind,(unsigned)e->stage,(unsigned)e->a,(unsigned)e->b,(int)e->code,(int)e->error);
            if(!sink(ctx,line,(size_t)n))return false;
        }
    }
    return sink(ctx,"]}\n",3);
}
