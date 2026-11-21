// SPDX-License-Identifier: Apache-2.0
#include "thingsos/service.h"
int thingsos_service_poll(thingsos_service_t* s,bool requested,bool rescue,bool busy,const thingsos_service_ops_t* ops) {
    if(!s||!ops||!ops->start||!ops->stop)return -1;
    bool wanted=requested||rescue;
    if(busy||wanted==s->running)return 0;
    int result=wanted?ops->start(ops->context):ops->stop(ops->context);
    if(!result)s->running=wanted;
    return result;
}
