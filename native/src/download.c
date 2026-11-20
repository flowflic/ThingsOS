// SPDX-License-Identifier: Apache-2.0
#include "thingsos/download.h"
#include <string.h>
static bool number(const char** text,size_t* value) {
    const char* p=*text;size_t n=0;
    if(*p<'0'||*p>'9')return false;
    do {
        unsigned digit=(unsigned)(*p++-'0');
        if(n>(SIZE_MAX-digit)/10)return false;
        n=n*10+digit;
    }while(*p>='0'&&*p<='9');
    *text=p;*value=n;return true;
}
bool thingsos_download_range(const char* text,size_t offset,size_t total,int64_t length) {
    if(!text||!total||offset>=total||length<0||(uint64_t)length!=total-offset||strncmp(text,"bytes ",6))return false;
    text+=6;size_t start,end,bytes;
    if(!number(&text,&start)||*text++!='-'||!number(&text,&end)||*text++!='/'||!number(&text,&bytes)||*text)return false;
    return start==offset&&end==total-1&&bytes==total;
}
int thingsos_download_read(thingsos_download_t* d,uint8_t* data,size_t n) {
    if(!d||!data||!n||!d->read||!d->open||!d->close||d->received>d->bytes)return -1;
    size_t remaining=d->bytes-d->received;
    if(!remaining)return 0;
    if(n>remaining)n=remaining;
    for(;;) {
        int got=d->read(d->context,data,n);
        if(got>0) {
            if((size_t)got>n){d->close(d->context);return -1;}
            d->received+=(size_t)got;return got;
        }
        d->close(d->context);
        bool opened=false;
        while(d->retries<4&&!opened) {
            ++d->retries;
            opened=d->open(d->context,d->received);
        }
        if(!opened)return -1;
    }
}
