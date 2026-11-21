// SPDX-License-Identifier: Apache-2.0
#include "thingsos/usb_disk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"USB disk line %d\n",__LINE__);exit(1);}}while(0)
static unsigned char media[4*512];
static bool failed;
static unsigned writes;
static bool read_sector(void* p,uint64_t lba,void* out) {
    (void)p;if(failed || lba>=4)return false;memcpy(out,media+lba*512,512);return true;
}
static bool write_sector(void* p,uint64_t lba,const void* in) {
    (void)p;if(failed || lba>=4)return false;memcpy(media+lba*512,in,512);++writes;return true;
}
int main(void) {
    thingsos_usb_disk_t d={4,true,false,NULL,read_sector,write_sector};
    unsigned char data[600],before[sizeof(media)];
    for(unsigned i=0;i<sizeof(media);++i)media[i]=(unsigned char)(i*37);
    memcpy(before,media,sizeof(media));
    CHECK(thingsos_usb_disk_io(&d,false,0,500,data,600));
    CHECK(!memcmp(data,media+500,600));
    memset(data,0x5a,sizeof(data));
    CHECK(thingsos_usb_disk_io(&d,true,0,500,data,600));
    CHECK(writes==3 && !memcmp(media,before,500) && !memcmp(media+500,data,600));
    CHECK(!memcmp(media+1100,before+1100,sizeof(media)-1100));
    unsigned saved=writes;
    CHECK(!thingsos_usb_disk_io(&d,true,4,0,data,1));
    CHECK(!thingsos_usb_disk_io(&d,true,3,500,data,13));
    CHECK(!thingsos_usb_disk_io(&d,true,0,512,data,1));
    CHECK(!thingsos_usb_disk_io(&d,true,UINT64_MAX,0,data,1));
    CHECK(writes==saved);
    d.ejected=true;CHECK(!thingsos_usb_disk_io(&d,true,0,0,data,512));
    d.ejected=false;d.enabled=false;CHECK(!thingsos_usb_disk_io(&d,false,0,0,data,512));
    d.enabled=true;failed=true;CHECK(!thingsos_usb_disk_io(&d,true,0,1,data,10));
    CHECK(writes==saved);puts("USB storage bounds, partial I/O, eject and failure paths passed");
}
