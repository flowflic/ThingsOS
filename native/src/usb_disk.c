// SPDX-License-Identifier: Apache-2.0
#include "thingsos/usb_disk.h"
#include <string.h>
#include <limits.h>
bool thingsos_usb_disk_io(thingsos_usb_disk_t* d,bool write,uint64_t lba,
                         uint32_t offset,void* buffer,uint32_t bytes) {
    if(!d || !d->enabled || d->ejected || !d->read || (write && !d->write) ||
       !buffer || offset>=THINGSOS_USB_SECTOR || lba>=d->sectors ||
       d->sectors>UINT64_MAX/THINGSOS_USB_SECTOR ||
       bytes>(d->sectors-lba)*THINGSOS_USB_SECTOR-offset)return false;
    unsigned char sector[THINGSOS_USB_SECTOR];
    unsigned char* p=buffer;
    while(bytes) {
        uint32_t n=THINGSOS_USB_SECTOR-offset;
        if(n>bytes)n=bytes;
        if(!write || offset || n!=THINGSOS_USB_SECTOR)
            if(!d->read(d->context,lba,sector))return false;
        if(write) {
            memcpy(sector+offset,p,n);
            if(!d->write(d->context,lba,sector))return false;
        } else memcpy(p,sector+offset,n);
        p+=n;bytes-=n;++lba;offset=0;
    }
    return true;
}
