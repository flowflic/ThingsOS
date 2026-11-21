/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "thingsos/weidu.h"
#include "thingsos/content.h"
#define THINGSOS_MP_ARTICLE_LIMIT 10
/* Protocol reference only: finlater/weread.koplugin@2bee0cc6, independently
   authored streaming C implementation. Article.author holds its originalId. */
bool thingsos_mp_account_id(const char*);
thingsos_weidu_error_t thingsos_mp_refresh(thingsos_weidu_client_t*,const thingsos_weidu_book_t* account);
bool thingsos_mp_page(const char*,const thingsos_weidu_session_t*,const char* account,
                     size_t,thingsos_weidu_book_t*,size_t,size_t*,size_t*);
thingsos_weidu_error_t thingsos_mp_download(thingsos_weidu_client_t*,const thingsos_weidu_book_t*,const thingsos_weidu_book_t*,const char* output);
bool thingsos_mp_cached(const char* output,const thingsos_weidu_session_t*,const thingsos_weidu_book_t*,const thingsos_weidu_book_t*,char*,size_t);
/* Streaming HTML to minimal Markdown; scripts, page chrome and images are omitted. */
bool thingsos_mp_text(const char*,const char*,const char* title,bool (*cancel)(void*),void*);
