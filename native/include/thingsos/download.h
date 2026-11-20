// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct {
    void* context;
    int (*read)(void*,uint8_t*,size_t);
    bool (*open)(void*,size_t);
    void (*close)(void*);
    size_t bytes,received;
    unsigned retries;
} thingsos_download_t;
/* Four reconnect attempts for the entire transaction, never reset by progress.
   open must authenticate the server and validate its exact Content-Range. */
int thingsos_download_read(thingsos_download_t*,uint8_t*,size_t);
bool thingsos_download_range(const char*,size_t offset,size_t total,int64_t length);
