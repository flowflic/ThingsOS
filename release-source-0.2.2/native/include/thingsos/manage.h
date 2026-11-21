// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "thingsos/files.h"
typedef enum {THINGSOS_MANAGE_OK,THINGSOS_MANAGE_INVALID,THINGSOS_MANAGE_EXISTS,THINGSOS_MANAGE_IO,THINGSOS_MANAGE_STORE,THINGSOS_MANAGE_RESTORE_FAILED} thingsos_manage_result_t;
/* NULL new_name deletes. Records and filesystem move together; the caller owns IO. */
thingsos_manage_result_t thingsos_book_manage(thingsos_records_t*,const char* root,const char* path,const char* new_name);
bool thingsos_manage_recover(thingsos_records_t*,const char* root);
bool thingsos_book_name(const char*);
/* Remote SD browsing excludes hidden/system directories. Only books and
   raster images and MP3/WAV audio can be changed; directories and other file
   types are read-only. Callers must release matching audio handles first. */
bool thingsos_remote_directory(const char*);
bool thingsos_managed_path(const char*);
bool thingsos_managed_name(const char*);
uint64_t thingsos_path_id(const char*);
typedef bool (*thingsos_path_mapper_t)(const char*,char*,size_t);
void thingsos_path_mapper(thingsos_path_mapper_t);
