/* SPDX-License-Identifier: Apache-2.0 */
#ifndef THINGSOS_WEIDU_H
#define THINGSOS_WEIDU_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
typedef struct {
    char vid[128], skey[384], rt[512];
} thingsos_weidu_session_t;
typedef struct {
    /* 公众号 article ids are ~88 characters ("MP_WXS_…_WR…"), so the id field has
       to hold more than a numeric book id. */
    char id[128], title[192], author[128];
    uint32_t updated;
} thingsos_weidu_book_t;
typedef struct {
    char uid[64], title[192];
    uint32_t index;
    bool paid, plain;
    uint8_t level, level_tag; /* Uses old zeroed padding; toc.dat remains 264 B/row. */
} thingsos_weidu_chapter_t;
typedef enum {
    WD_IDLE,
    WD_LOGIN,
    WD_QR,
    WD_SHELF,
    WD_DOWNLOAD,
    WD_READY,
    WD_ERROR,
    WD_CANCELLED
} thingsos_weidu_phase_t;
typedef struct {
    unsigned serial, count, completed, total;
    thingsos_weidu_phase_t phase;
    bool busy, authenticated, background;
    char qr[240], message[160], path[512], title[192];
} thingsos_weidu_status_t;
typedef enum {
    WD_OK,
    WD_NETWORK,
    WD_AUTH,
    WD_PROTOCOL,
    WD_STORAGE,
    WD_MEMORY,
    WD_LIMIT,
    WD_CANCEL,
    WD_CLOCK,
    WD_ACCESS,
    WD_TLS,
    WD_DNS,
    WD_VERIFY, /* Official interactive human verification is required. */
    WD_UNCONFIRMED /* Valid response, but no business acknowledgment. */
} thingsos_weidu_error_t;
typedef struct {
    void* context;
    thingsos_weidu_error_t (*http)(
        void*, const char* method, const char* path, const char* body, const char* referer,
        const char* destination, size_t limit, thingsos_weidu_session_t*);
    bool (*session)(void*, thingsos_weidu_session_t*, bool save);
    bool (*cancelled)(void*);
    uint64_t (*now_ms)(void*);
    uint32_t (*unix_time)(void*);
    uint32_t (*random)(void*);
    void (*wait_ms)(void*, unsigned);
    void (*publish)(void*, const thingsos_weidu_status_t*);
    /* Authentication responses must remain in RAM, never an SD temporary file. */
    thingsos_weidu_error_t (*auth)(
        void*, const char*, const char*, const char*, char*, size_t, size_t*,
        thingsos_weidu_session_t*);
    thingsos_weidu_error_t (*reader)(void*, const char*, char*, size_t, thingsos_weidu_session_t*);
    /* Public cover fetches must never carry the account cookies. */
    thingsos_weidu_error_t (*image)(void*, const char*, const char*, size_t);
    void (*finish_network)(void*);
    /* RAM-only account exchanges with an explicit chapter Referer. */
    thingsos_weidu_error_t (*exchange)(void*,const char*,const char*,const char*,const char*,char*,size_t,size_t*,thingsos_weidu_session_t*);
    thingsos_weidu_error_t (*reader_info)(void*,const char*,char ps[128],char pc[128],char token[128],thingsos_weidu_session_t*);
    /* Zero-based retry attempt of the request currently using http(). */
    unsigned attempt;
    /* Two reader workers may prefetch chapter tokens while the coordinator
       writes the previous chapter. Indices are pending chapter rows. */
    bool (*prefetch_start)(void*, const char* toc, const char* book_id,
                           const unsigned* indices, unsigned count);
    thingsos_weidu_error_t (*prefetch_get)(void*, unsigned sequence, char* ps,
                                           size_t capacity, unsigned timeout_ms);
    void (*prefetch_stop)(void*);
} thingsos_weidu_io_t;
typedef struct {
    thingsos_weidu_io_t io;
    thingsos_weidu_session_t session;
    thingsos_weidu_status_t status;
    char root[512], output[512];
} thingsos_weidu_client_t;
typedef struct {
    unsigned state, match, bytes, offset;
    char value[128];
} thingsos_weidu_ps_t;
bool thingsos_weidu_field_feed(thingsos_weidu_ps_t*,const char*,const unsigned char*,size_t);
bool thingsos_weidu_ps_feed(thingsos_weidu_ps_t*, const unsigned char*, size_t);
bool thingsos_weidu_token(const char*, size_t);
bool thingsos_weidu_session_valid(const thingsos_weidu_session_t*);
void thingsos_weidu_md5(const void*, size_t, char out[33]);
bool thingsos_weidu_encode_id(const char*, char out[160]);
void thingsos_weidu_sign(const char*, char out[17]);
bool thingsos_weidu_body(const char*, const char*, const char*, uint32_t, uint32_t, char*, size_t);
thingsos_weidu_error_t thingsos_weidu_decode_shards(const char* const*, size_t, const char*);
typedef struct {bool (*cancelled)(void*);void* context;} thingsos_weidu_control_t;
static inline bool thingsos_weidu_stopped(const thingsos_weidu_control_t* c) {
    return c && c->cancelled && c->cancelled(c->context);
}
thingsos_weidu_error_t thingsos_weidu_decode_control(const char* const*,size_t,const char*,const thingsos_weidu_control_t*);
bool thingsos_weidu_pack_control(const char*,const thingsos_weidu_book_t*,const char*,const char*,unsigned,char*,size_t,const thingsos_weidu_control_t*);
/* Optional bounded failure detail; no network/session data. */
bool thingsos_weidu_pack_detail(
    const char*, const thingsos_weidu_book_t*, const char*, const char*, unsigned, char*, size_t);
bool thingsos_weidu_pack(
    const char*, const thingsos_weidu_book_t*, const char*, const char*, unsigned);
bool thingsos_weidu_cached(const char* output, const char* id, char* path, size_t capacity);
bool thingsos_weidu_cache_id(const char* path, char* id, size_t capacity);
bool thingsos_weidu_cache_register(const char* path,const char* id);
bool thingsos_weidu_cache_identity(const char*,char*,size_t);
bool thingsos_weidu_cache_named(const char* output,const char* id,const char* title,char* path,size_t capacity);
unsigned thingsos_weidu_cache_names_migrate(const char* output);
bool thingsos_weidu_shelf_page(
    const char* root, const thingsos_weidu_session_t*, size_t start, thingsos_weidu_book_t*,
    size_t capacity, size_t* count, size_t* total);
thingsos_weidu_error_t thingsos_weidu_login(thingsos_weidu_client_t*);
thingsos_weidu_error_t thingsos_weidu_renew(thingsos_weidu_client_t*);
thingsos_weidu_error_t thingsos_weidu_sync(thingsos_weidu_client_t*);
bool thingsos_weidu_shelf_has(const char*,const thingsos_weidu_session_t*,const char*);
bool thingsos_weidu_shelf_membership(const char*,const thingsos_weidu_session_t*,const thingsos_weidu_book_t*,size_t,bool*);
thingsos_weidu_error_t thingsos_weidu_shelf_add(thingsos_weidu_client_t*,const thingsos_weidu_book_t*);
thingsos_weidu_error_t thingsos_weidu_download(
    thingsos_weidu_client_t*, const thingsos_weidu_book_t*);
thingsos_weidu_error_t thingsos_weidu_covers(thingsos_weidu_client_t*, const thingsos_weidu_book_t*, size_t);
thingsos_weidu_error_t thingsos_weidu_catalog_results(const char* response,size_t bytes,
    thingsos_weidu_book_t* books,unsigned capacity,unsigned* count);
bool thingsos_weidu_catalog_cover(const char* response,size_t bytes,const char* id,char* url,size_t capacity);
bool thingsos_weidu_cache_cover(thingsos_weidu_client_t*,const char* id,const char* url);
bool thingsos_weidu_partial(const char*, const char*, unsigned*);
thingsos_weidu_error_t thingsos_weidu_restore(thingsos_weidu_client_t*, const thingsos_weidu_book_t*);
const char* thingsos_weidu_error_text(thingsos_weidu_error_t);
bool thingsos_weidu_flush(FILE*);
bool thingsos_weidu_cover_url(const char*, char*, size_t);
/* Resolve a Location against a previously validated public HTTPS image URL. */
bool thingsos_weidu_image_redirect_url(const char* current,const char* location,char* out,size_t capacity);
#endif

/* Match a UTF-8 title substring across the full shelf before recent-order paging; empty query returns all books. total is the filtered count. */
bool thingsos_weidu_shelf_search(const char*, const thingsos_weidu_session_t*, size_t, thingsos_weidu_book_t*, size_t, size_t*, size_t*, const char*);
