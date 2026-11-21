// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdbool.h>
typedef struct {bool running;} thingsos_service_t;
typedef struct {
    void* context;
    int (*start)(void*);
    int (*stop)(void*);
} thingsos_service_ops_t;
/* Called by the UI owner without holding the shared I/O mutex. Busy work
   defers lifecycle changes; rescue always retains its recovery server. */
int thingsos_service_poll(thingsos_service_t*,bool requested,bool rescue,bool busy,const thingsos_service_ops_t*);
