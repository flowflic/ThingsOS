// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define THINGSOS_RELEASE_ORIGIN "https://thingsos.scholarcn.com/"
typedef struct {
    char version[32], path[192], notes[1024];
    size_t bytes;
    uint8_t sha256[32];
} thingsos_release_t;
/* Requires a terminated JSON document and the current native partition layout. */
bool thingsos_release_parse(const char*,size_t,thingsos_release_t*);
bool thingsos_release_newer(const char* candidate,const char* installed);
typedef struct {
    unsigned serial;
    bool active, checked, available, failed;
    char message[192];
    thingsos_release_t release;
} thingsos_release_state_t;
