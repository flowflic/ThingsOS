// SPDX-License-Identifier: Apache-2.0
#include "thingsos/refresh_mask.h"
#include <string.h>
bool thingsos_refresh_include_rect(bool* rows,unsigned height,uint8_t* columns,unsigned width,
                                  unsigned x,unsigned y,unsigned w,unsigned h) {
    if(!rows || !columns || !w || !h || x>=width || y>=height || w>width-x || h>height-y)return false;
    for(unsigned row=y;row<y+h;++row)rows[row]=true;
    for(unsigned col=x;col<x+w;++col)columns[col/2]|=(uint8_t)(15u<<(4*(col%2)));
    return true;
}
bool thingsos_refresh_mask_equal(const uint8_t* source,unsigned phases,uint8_t* target,size_t bytes) {
    if (!source || !target || source==target || !phases || phases>SIZE_MAX/64 || bytes<(size_t)phases*64)
        return false;
    memcpy(target,source,(size_t)phases*64);
    for (unsigned phase=0;phase<phases;++phase) for (unsigned gray=0;gray<16;++gray)
        target[(size_t)phase*64+gray*4+gray/4]&=(uint8_t)~(3u<<(6-2*(gray%4)));
    return true;
}
bool thingsos_refresh_text_wave(const uint8_t* source,unsigned phases,uint8_t* target,size_t bytes) {
    if(!thingsos_refresh_mask_equal(source,phases,target,bytes))return false;
    for(unsigned p=phases;p>0;--p) {
        size_t at=(size_t)(p-1)*64+63;
        if((source[at]&3)==2) {target[at]=(target[at]&0xfc)|2;return true;}
    }
    return false;
}
