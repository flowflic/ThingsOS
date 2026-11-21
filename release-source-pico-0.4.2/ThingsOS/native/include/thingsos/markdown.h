// SPDX-License-Identifier: Apache-2.0
#ifndef THINGSOS_MARKDOWN_H
#define THINGSOS_MARKDOWN_H
#include "thingsos/content.h"
enum { MD_HIDE=1, MD_BOLD=2, MD_ITALIC=4, MD_CODE=8, MD_QUOTE=16,
       MD_LIST=32, MD_LINK=64, MD_H1=128, MD_H2=256, MD_H3=512 };
typedef struct { uint32_t begin,end,flags; } thingsos_md_run_t;
typedef struct {
    FILE* file;
    uint32_t count,bytes,next,used,at;
    thingsos_md_run_t runs[128];
} thingsos_md_cursor_t;
bool thingsos_markdown_path(const char* path);
/* Source byte offsets stay unchanged: bookmarks never refer to rendered text.
   Builds a disposable SD index; parser scratch uses PSRAM exclusively. */
bool thingsos_markdown_prepare(const char* path,thingsos_chapter_t* chapter);
bool thingsos_markdown_image_path(const char* source,const char* reference,char out[256]);
thingsos_md_cursor_t* thingsos_markdown_cursor(const thingsos_chapter_t*,size_t start);
bool thingsos_markdown_style(thingsos_md_cursor_t*,size_t offset,thingsos_md_run_t*);
void thingsos_markdown_cursor_close(thingsos_md_cursor_t*);
#endif
