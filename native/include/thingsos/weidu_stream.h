/* SPDX-License-Identifier: Apache-2.0 */
#ifndef THINGSOS_WEIDU_STREAM_H
#define THINGSOS_WEIDU_STREAM_H
#include "thingsos/weidu.h"
#include "thingsos/content.h"
bool thingsos_weidu_stream_path(const char*,const char*,char*,size_t);
bool thingsos_weidu_stream_book(const char*,thingsos_weidu_book_t*);
bool thingsos_weidu_stream_identity(const char*,char*,size_t);
bool thingsos_weidu_stream_open(thingsos_epub_t*,const char*);
bool thingsos_weidu_stream_xml(const thingsos_epub_t*,unsigned,unsigned char**,size_t*);
bool thingsos_weidu_stream_ready(const char*,const char*,unsigned);
bool thingsos_weidu_stream_images_ready(const char*,const char*,unsigned,bool accept_partial);
bool thingsos_weidu_cover_chapter(const char* title);
bool thingsos_weidu_stream_needs_cover(const char* root,const char* id);
bool thingsos_weidu_stream_image_path(const thingsos_epub_t*,const char*,char*,size_t);
unsigned thingsos_weidu_stream_count(const char*,const char*);
thingsos_weidu_error_t thingsos_weidu_stream_fetch(thingsos_weidu_client_t*,const thingsos_weidu_book_t*,unsigned);
/* Fetch up to three consecutive chapters with one network lifetime. */
thingsos_weidu_error_t thingsos_weidu_stream_window(thingsos_weidu_client_t*,const thingsos_weidu_book_t*,unsigned,unsigned);
/* The worker normalizes to a private, atomically replaced chapter file. */
bool thingsos_weidu_normalize(const char*,const char*,const char*,bool,const char**);
bool thingsos_weidu_normalize_control(const char*,const char*,const char*,bool,const char**,const thingsos_weidu_control_t*);
typedef bool (*thingsos_weidu_image_cache_fn)(void*,char* url,char name[41]);
bool thingsos_weidu_image_cache_key(char* url,char* normalized,size_t normalized_cap,char name[41]);
bool thingsos_weidu_scan_images(const char*,bool,const thingsos_weidu_control_t*,
    thingsos_weidu_image_cache_fn,void*);
bool thingsos_weidu_normalize_images(const char*,const char*,const char*,bool,const char**,
    const thingsos_weidu_control_t*,thingsos_weidu_image_cache_fn,void*);
#endif
