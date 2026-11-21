// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "thingsos/runtime.h"
#define THINGSOS_RECORD_BOOKS 128
#define THINGSOS_RECORD_DAYS 64
#define THINGSOS_BOOK_DAYS 2048
#define THINGSOS_RECORD_PATH_BYTES 512
#define THINGSOS_STORE_BYTES 100000
typedef struct {
    uint64_t id, milliseconds, order;
    uint32_t chapter, offset;
    uint32_t page, pages, percent;
    char path[THINGSOS_RECORD_PATH_BYTES], title[192];
} thingsos_book_record_t;
typedef struct {
    uint32_t date;
    uint64_t milliseconds;
} thingsos_day_record_t;
typedef struct { uint64_t id, milliseconds; uint32_t date; } thingsos_book_day_t;
typedef struct {
    thingsos_book_day_t book_days[THINGSOS_BOOK_DAYS];
    size_t book_day_count;
    uint64_t generation, card_id, total_ms, undated_ms, order;
    char library_path[THINGSOS_RECORD_PATH_BYTES];
    thingsos_book_record_t books[THINGSOS_RECORD_BOOKS];
    size_t book_count;
    thingsos_location_t marks[THINGSOS_BOOKMARK_LIMIT];
    size_t mark_count;
    thingsos_day_record_t days[THINGSOS_RECORD_DAYS];
    size_t day_count;
    uint32_t hour_date;
    uint64_t hour_ms[24];
} thingsos_records_t;
typedef enum {
    THINGSOS_STORE_OK,
    THINGSOS_STORE_MISSING,
    THINGSOS_STORE_RECOVERED,
    THINGSOS_STORE_CORRUPT,
    THINGSOS_STORE_IO_ERROR
} thingsos_store_result_t;
void thingsos_records_init(thingsos_records_t*);
thingsos_book_record_t* thingsos_records_book(
    thingsos_records_t*, uint64_t, const char*, const char*);
bool thingsos_records_time(thingsos_records_t*, uint64_t, uint32_t, uint64_t);
bool thingsos_records_hour(thingsos_records_t*, uint32_t date, unsigned hour, uint64_t delta);
bool thingsos_records_encode(const thingsos_records_t*, unsigned char*, size_t, size_t*);
bool thingsos_records_decode(thingsos_records_t*, const unsigned char*, size_t);
thingsos_store_result_t thingsos_store_load(thingsos_records_t*, const char* root);
bool thingsos_store_save(thingsos_records_t*, const char* root);
/* Device writes retain the old wire layout, so rollback can still read positions. */
bool thingsos_store_save_compatible(thingsos_records_t*, const char* root);
bool thingsos_date_valid(uint32_t date);
