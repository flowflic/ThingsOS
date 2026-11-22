// SPDX-License-Identifier: Apache-2.0
#pragma once

/* flomo remote MCP (https://flomoapp.com/mcp, streamable HTTP, Bearer token).
   Read-only subset: memo_search, memo_batch_get, memo_recommended, tag_tree,
   get_daily_review. Pure functions here are host-tested. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define THINGSOS_FLOMO_HOST "flomoapp.com"
#define THINGSOS_FLOMO_PATH "/mcp"
#define THINGSOS_FLOMO_TOKEN_LEN 256
#define THINGSOS_FLOMO_ID_LEN 32
#define THINGSOS_FLOMO_TIME_LEN 32
#define THINGSOS_FLOMO_TAGS_LEN 192
#define THINGSOS_FLOMO_PREVIEW_LEN 1600
#define THINGSOS_FLOMO_TAG_LEN 96
#define THINGSOS_FLOMO_MESSAGE_LEN 192
#define THINGSOS_FLOMO_PAGE_SIZE 20
#define THINGSOS_FLOMO_SEARCH_MAX 50
#define THINGSOS_FLOMO_RELATED_MAX 8

typedef struct {
    char id[THINGSOS_FLOMO_ID_LEN];
    char created_at[THINGSOS_FLOMO_TIME_LEN];
    char updated_at[THINGSOS_FLOMO_TIME_LEN];
    char tags[THINGSOS_FLOMO_TAGS_LEN];      /* "#a #b/c", display only */
    char preview[THINGSOS_FLOMO_PREVIEW_LEN]; /* plain text, may be server-truncated */
    uint32_t word_count;
    bool truncated;
    bool has_image;
    bool has_voice;
    bool has_link;
} thingsos_flomo_memo_t;

typedef struct {
    char name[THINGSOS_FLOMO_TAG_LEN];
} thingsos_flomo_tag_t;

/* Accepts a bare token, "Bearer <token>", or a pasted config line that contains
   "Bearer <token>". Rejects spaces/control characters inside the token. */
bool thingsos_flomo_parse_token(const char* raw, char* out, size_t cap);
bool thingsos_flomo_id_valid(const char* id);

/* JSON-RPC tools/call bodies. Empty strings omit optional arguments. */
bool thingsos_flomo_build_search(
    unsigned rpc_id, const char* keywords, const char* tag, const char* end_date,
    unsigned limit, char* out, size_t cap);
bool thingsos_flomo_build_batch_get(unsigned rpc_id, const char* memo_id, char* out, size_t cap);
bool thingsos_flomo_build_recommended(
    unsigned rpc_id, const char* memo_id, unsigned limit, char* out, size_t cap);
bool thingsos_flomo_build_tag_tree(unsigned rpc_id, unsigned limit, char* out, size_t cap);
bool thingsos_flomo_build_daily_review(unsigned rpc_id, char* out, size_t cap);

/* Locates the JSON-RPC message in an SSE ("data:" lines) or plain JSON body,
   copies it NUL-terminated into out (out may alias body). */
bool thingsos_flomo_unwrap_sse(const char* body, size_t length, char* out, size_t cap);

/* Parses a tools/call reply. On success *result_out is a cJSON object that the
   caller frees with cJSON_Delete (structuredContent, or content[0].text JSON).
   On failure message_out explains (JSON-RPC error, isError text, bad JSON). */
bool thingsos_flomo_parse_result(
    const char* json, size_t length, void** result_out, char* message_out, size_t message_cap);

/* Reads result.memos[] (search, review, batch_get, recommended). */
unsigned thingsos_flomo_read_memos(
    const void* result, thingsos_flomo_memo_t* out, unsigned capacity);
/* Full content of the first memo in a batch_get result, as plain text. */
bool thingsos_flomo_read_full(
    const void* result, const char* memo_id, thingsos_flomo_memo_t* memo_out,
    char* content, size_t cap, size_t* len_out);
unsigned thingsos_flomo_read_tags(const void* result, thingsos_flomo_tag_t* out, unsigned capacity);

/* flomo Markdown subset to readable text: drops <mark>/<u>/<b> tags and **,
   decodes basic entities, keeps list markers and line breaks. In-place safe. */
size_t thingsos_flomo_plain_text(const char* in, size_t length, char* out, size_t cap);

/* "2026-11-17T18:45:47+08:00" -> "2026-11-16" (one calendar day earlier). */
bool thingsos_flomo_day_before(const char* iso, char* out, size_t cap);

uint32_t thingsos_flomo_crc32(const void* data, size_t size);
bool thingsos_flomo_save_memos(const char* path, const thingsos_flomo_memo_t* memos, unsigned count);
bool thingsos_flomo_load_memos(
    const char* path, thingsos_flomo_memo_t* memos, unsigned capacity, unsigned* count_out);
