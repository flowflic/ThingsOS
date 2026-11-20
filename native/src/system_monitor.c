/* SPDX-License-Identifier: Apache-2.0 */
#include "thingsos/system_monitor.h"
#include <string.h>
bool thingsos_system_cpu_step(thingsos_system_cpu_window_t* w,const uint64_t stamp[2],
                              const uint64_t idle[2],uint16_t* used) {
    if(!w||!stamp||!idle||!used)return false;
    bool reset=!w->initialized;uint64_t elapsed[2]={0},delta[2]={0};
    for(unsigned i=0;i<2&&!reset;++i) {
        if(stamp[i]<=w->stamp[i]||idle[i]<w->idle[i]){reset=true;break;}
        elapsed[i]=stamp[i]-w->stamp[i];delta[i]=idle[i]-w->idle[i];
        if(elapsed[i]>10000000 || delta[i]>elapsed[i]+1000)reset=true;
    }
    if(reset) {
        memcpy(w->stamp,stamp,sizeof(w->stamp));memcpy(w->idle,idle,sizeof(w->idle));
        w->initialized=true;w->valid=false;return false;
    }
    /* Do not replace a good two-second baseline on rapid manual refreshes. */
    if(elapsed[0]<500000||elapsed[1]<500000){*used=w->used_bp;return w->valid;}
    uint64_t sum=0;
    for(unsigned i=0;i<2;++i) {
        uint64_t busy=elapsed[i]>delta[i]?elapsed[i]-delta[i]:0;
        sum+=(busy*10000+elapsed[i]/2)/elapsed[i];
    }
    w->used_bp=(uint16_t)((sum+1)/2);w->valid=true;
    memcpy(w->stamp,stamp,sizeof(w->stamp));memcpy(w->idle,idle,sizeof(w->idle));
    *used=w->used_bp;return true;
}
bool thingsos_system_flash_layout(uint64_t chip,uint64_t slot,uint64_t image,
                                   thingsos_system_memory_t* out) {
    if(!out)return false;
    memset(out,0,sizeof(*out));
    if(!chip||!slot||!image||slot>chip||image>slot)return false;
    *out=(thingsos_system_memory_t){.total=chip,.free=slot-image,.valid=true};return true;
}
unsigned thingsos_system_ratio(uint64_t used,uint64_t total,unsigned width) {
    if(!total)return 0;
    if(used>=total)return width;
    /* Inputs are device memory sizes; avoid a multiply overflow for callers. */
    if(used>UINT64_MAX/(width?width:1)) {
        uint64_t q=total/width,r=total%width;unsigned low=0,high=width;
        while(low<high){unsigned mid=low+(high-low)/2+1;
            uint64_t threshold=q*mid+(r*mid+width-1)/width;
            if(used>=threshold)low=mid;else high=mid-1;}
        return low;
    }
    return (unsigned)((used*width)/total);
}
