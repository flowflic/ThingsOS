// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "thingsos/store.h"
#define THINGSOS_DIRECTORY_LIMIT 512
#define THINGSOS_FILE_ROWS 8
typedef struct {
    char name[192];
    bool directory;
    uint64_t size;
} thingsos_file_entry_t;
typedef struct {
    char path[THINGSOS_RECORD_PATH_BYTES];
    thingsos_file_entry_t entries[THINGSOS_DIRECTORY_LIMIT];
    size_t count, page;
    bool truncated;
} thingsos_browser_t;
bool thingsos_browser_scan(thingsos_browser_t*, const char* root, const char* logical_path);
bool thingsos_browser_enter(thingsos_browser_t*, const char* root, size_t);
bool thingsos_browser_parent(thingsos_browser_t*, const char* root);
bool thingsos_browser_path(const thingsos_browser_t*, size_t, char[THINGSOS_RECORD_PATH_BYTES]);
size_t thingsos_browser_matches(const thingsos_browser_t*,const char*);
size_t thingsos_browser_match(const thingsos_browser_t*,const char*,size_t);
bool thingsos_book_path(const char*);
/* Browser-only filtering; never deletes legacy or host-owned data. */
bool thingsos_directory_visible(const char* name);

bool thingsos_collision_name(const char* name,unsigned copy,char out[192]);
