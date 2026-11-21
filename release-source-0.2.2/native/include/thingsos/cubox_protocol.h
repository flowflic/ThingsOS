// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define THINGSOS_CUBOX_SERVER_LEN 32
#define THINGSOS_CUBOX_TOKEN_LEN 256
#define THINGSOS_CUBOX_ID_LEN 48
#define THINGSOS_CUBOX_NAME_LEN 160
#define THINGSOS_CUBOX_TITLE_LEN 256
#define THINGSOS_CUBOX_DESC_LEN 512
#define THINGSOS_CUBOX_DOMAIN_LEN 96
#define THINGSOS_CUBOX_URL_LEN 512
#define THINGSOS_CUBOX_TIME_LEN 40
#define THINGSOS_CUBOX_AUTHOR_LEN 128
#define THINGSOS_CUBOX_SUMMARY_LEN 1024
#define THINGSOS_CUBOX_MESSAGE_LEN 192
#define THINGSOS_CUBOX_FOLDER_MAX 128
#define THINGSOS_CUBOX_LIST_PAGE_SIZE 20
#define THINGSOS_CUBOX_QA_MAX 16
#define THINGSOS_CUBOX_ANNOTATION_MAX 32

typedef enum {
    THINGSOS_CUBOX_FILTER_UNREAD = 0,
    THINGSOS_CUBOX_FILTER_ALL = 1,
    THINGSOS_CUBOX_FILTER_STARRED = 2
} thingsos_cubox_filter_t;

typedef struct {
    char id[THINGSOS_CUBOX_ID_LEN];
    char name[THINGSOS_CUBOX_NAME_LEN];
    char nested_name[THINGSOS_CUBOX_NAME_LEN];
    char parent_id[THINGSOS_CUBOX_ID_LEN];
    bool uncategorized;
} thingsos_cubox_folder_t;

typedef struct {
    char id[THINGSOS_CUBOX_ID_LEN];
    char title[THINGSOS_CUBOX_TITLE_LEN];
    char article_title[THINGSOS_CUBOX_TITLE_LEN];
    char description[THINGSOS_CUBOX_DESC_LEN];
    char domain[THINGSOS_CUBOX_DOMAIN_LEN];
    char url[THINGSOS_CUBOX_URL_LEN];
    char folder_id[THINGSOS_CUBOX_ID_LEN];
    char folder_name[THINGSOS_CUBOX_NAME_LEN];
    char create_time[THINGSOS_CUBOX_TIME_LEN];
    char update_time[THINGSOS_CUBOX_TIME_LEN];
    char tag_summary[THINGSOS_CUBOX_NAME_LEN];
    bool read;
    bool starred;
    bool archived;
    bool cached;
    uint32_t cached_bytes;
    uint16_t cached_images;
} thingsos_cubox_card_t;

typedef struct {
    char q[256];
    char a[512];
} thingsos_cubox_qa_t;

typedef struct {
    char text[512];
    char note[512];
    char color[32];
    char create_time[THINGSOS_CUBOX_TIME_LEN];
} thingsos_cubox_annotation_t;

typedef struct {
    thingsos_cubox_card_t card;
    char author[THINGSOS_CUBOX_AUTHOR_LEN];
    char summary[THINGSOS_CUBOX_SUMMARY_LEN];
    thingsos_cubox_qa_t qas[THINGSOS_CUBOX_QA_MAX];
    unsigned qa_count;
    thingsos_cubox_annotation_t annotations[THINGSOS_CUBOX_ANNOTATION_MAX];
    unsigned annotation_count;
    bool has_insight;
} thingsos_cubox_detail_t;

typedef struct {
    char url[THINGSOS_CUBOX_URL_LEN];
    char filename[32];
    bool downloaded;
} thingsos_cubox_image_ref_t;

bool thingsos_cubox_server_valid(const char* server);
bool thingsos_cubox_id_valid(const char* id);
bool thingsos_cubox_http_url_valid(const char* url);
bool thingsos_cubox_parse_credentials(
    const char* raw_input,
    const char* preferred_server,
    char* server_out,
    size_t server_cap,
    char* token_out,
    size_t token_cap);

const char* thingsos_cubox_image_ext(const void* data, size_t size);

bool thingsos_cubox_build_filter_request(
    thingsos_cubox_filter_t filter,
    const char* folder_id,
    const char* query,
    const char* last_card_id,
    unsigned page,
    unsigned limit,
    char* out,
    size_t cap);

bool thingsos_cubox_build_update_request(
    const char* card_id,
    int set_read,
    int set_starred,
    char* out,
    size_t cap);

bool thingsos_cubox_build_archive_request(const char* card_id, char* out, size_t cap);

bool thingsos_cubox_parse_envelope(
    const char* json,
    size_t length,
    int* code_out,
    char* message_out,
    size_t message_cap);

bool thingsos_cubox_parse_folders(
    const char* json,
    size_t length,
    thingsos_cubox_folder_t* out,
    unsigned capacity,
    unsigned* count_out,
    int* code_out,
    char* message_out,
    size_t message_cap);

bool thingsos_cubox_parse_cards(
    const char* json,
    size_t length,
    thingsos_cubox_card_t* out,
    unsigned capacity,
    unsigned* count_out,
    int* code_out,
    char* message_out,
    size_t message_cap);

bool thingsos_cubox_parse_detail(
    const char* json,
    size_t length,
    thingsos_cubox_detail_t* detail_out,
    char* content_out,
    size_t content_cap,
    size_t* content_len_out,
    int* code_out,
    char* message_out,
    size_t message_cap);

unsigned thingsos_cubox_extract_images(
    const char* markdown,
    thingsos_cubox_image_ref_t* refs,
    unsigned max_refs);

bool thingsos_cubox_build_article_markdown(
    const thingsos_cubox_detail_t* detail,
    const char* raw_markdown,
    const thingsos_cubox_image_ref_t* refs,
    unsigned ref_count,
    char* out,
    size_t cap,
    size_t* written_out);

uint32_t thingsos_cubox_crc32(const void* data, size_t size);
bool thingsos_cubox_save_folders(
    const char* path,
    const thingsos_cubox_folder_t* folders,
    unsigned count);
bool thingsos_cubox_load_folders(
    const char* path,
    thingsos_cubox_folder_t* folders,
    unsigned capacity,
    unsigned* count_out);
bool thingsos_cubox_save_cards(
    const char* path,
    const thingsos_cubox_card_t* cards,
    unsigned count);
bool thingsos_cubox_load_cards(
    const char* path,
    thingsos_cubox_card_t* cards,
    unsigned capacity,
    unsigned* count_out);
