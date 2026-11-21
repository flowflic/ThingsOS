// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdbool.h>
#include <stdint.h>
#define THINGSOS_USB_SECTOR 512u
typedef struct {
    uint64_t sectors;
    bool enabled, ejected;
    void* context;
    bool (*read)(void*,uint64_t,void*);
    bool (*write)(void*,uint64_t,const void*);
} thingsos_usb_disk_t;
/* Caller owns the storage lock. Partial transfers preserve adjacent bytes. */
bool thingsos_usb_disk_io(thingsos_usb_disk_t*,bool write,uint64_t lba,
                         uint32_t offset,void* buffer,uint32_t bytes);

typedef struct {
    bool active, connected, ejected;
    unsigned reads, writes;
    int error;
    uint64_t capacity_bytes;
} thingsos_usb_status_t;
