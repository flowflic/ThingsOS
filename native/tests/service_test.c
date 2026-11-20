// SPDX-License-Identifier: Apache-2.0
#include "thingsos/service.h"
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
typedef struct {unsigned starts,stops;int error;} fake_t;
static int start(void* p){fake_t* f=p;++f->starts;return f->error;}
static int stop(void* p){fake_t* f=p;++f->stops;return f->error;}
int main(void) {
    fake_t f={0};thingsos_service_t s={0};thingsos_service_ops_t ops={&f,start,stop};
    assert(!thingsos_service_poll(&s,false,false,false,&ops)&&!f.starts&&!s.running);
    assert(!thingsos_service_poll(&s,true,false,true,&ops)&&!f.starts);
    assert(!thingsos_service_poll(&s,true,false,false,&ops)&&s.running&&f.starts==1);
    assert(!thingsos_service_poll(&s,true,false,false,&ops)&&f.starts==1);
    assert(!thingsos_service_poll(&s,false,false,true,&ops)&&s.running&&!f.stops);
    f.error=7;assert(thingsos_service_poll(&s,false,false,false,&ops)==7&&s.running);
    f.error=0;assert(!thingsos_service_poll(&s,false,false,false,&ops)&&!s.running&&f.stops==2);
    assert(!thingsos_service_poll(&s,false,false,false,&ops)&&f.stops==2);
    f.error=9;assert(thingsos_service_poll(&s,true,false,false,&ops)==9&&!s.running);
    f.error=0;assert(!thingsos_service_poll(&s,false,true,false,&ops)&&s.running);
    assert(!thingsos_service_poll(&s,false,true,false,&ops)&&s.running&&f.stops==2);
    puts("HTTP lifecycle: lazy start, idempotence, busy deferral, failure recovery and always-on rescue passed");
    return 0;
}
