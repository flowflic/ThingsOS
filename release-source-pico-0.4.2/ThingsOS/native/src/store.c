// SPDX-License-Identifier: Apache-2.0
#include "thingsos/store.h"
#include "thingsos/mem.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#include <io.h>
#define sync_file(f) _commit(_fileno(f))
#define make_dir(p) _mkdir(p)
#else
#include <unistd.h>
#define sync_file(f) fsync(fileno(f))
#define make_dir(p) mkdir(p, 0775)
#endif
bool thingsos_date_valid(uint32_t date) {
    unsigned y = date / 10000, m = date / 100 % 100, d = date % 100;
    static const unsigned days[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (y < 2020 || y > 2099 || m < 1 || m > 12 || !d) return false;
    unsigned max = days[m - 1] + (m == 2 && y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
    return d <= max;
}
void thingsos_records_init(thingsos_records_t* r) {
    memset(r, 0, sizeof(*r));
    strcpy(r->library_path, "/sdcard/books");
}
thingsos_book_record_t* thingsos_records_book(
    thingsos_records_t* r, uint64_t id, const char* path, const char* title) {
    if (!r || !id || r->book_count > THINGSOS_RECORD_BOOKS) return NULL;
    for (size_t i = 0; i < r->book_count; i++)
        if (r->books[i].id == id) return r->books + i;
    if (!path || !title || r->book_count == THINGSOS_RECORD_BOOKS ||
        strlen(path) >= THINGSOS_RECORD_PATH_BYTES || strlen(title) >= 192 ||
        (!thingsos_user_file_allowed(path) && !(id == 1 && !strcmp(path, "builtin:example"))))
        return NULL;
    thingsos_book_record_t* book = r->books + r->book_count++;
    memset(book, 0, sizeof(*book));
    book->id = id;
    strcpy(book->path, path);
    strcpy(book->title, title);
    return book;
}
bool thingsos_records_time(thingsos_records_t* r, uint64_t id, uint32_t date, uint64_t delta) {
    if (!r || (date && !thingsos_date_valid(date))) return false;
    thingsos_book_record_t* book = thingsos_records_book(r, id, NULL, NULL);
    if (!book || UINT64_MAX - r->total_ms < delta || UINT64_MAX - book->milliseconds < delta)
        return false;
    if (!date) {
        if (UINT64_MAX - r->undated_ms < delta) return false;
        r->undated_ms += delta;
    } else {
        size_t i = 0;
        while (i < r->day_count && r->days[i].date != date) i++;
        if (i == r->day_count) {
            if (r->day_count == THINGSOS_RECORD_DAYS) {
                size_t oldest = 0;
                for (size_t k = 1; k < r->day_count; k++)
                    if (r->days[k].date < r->days[oldest].date) oldest = k;
                if (date < r->days[oldest].date) goto totals;
                i = oldest;
            } else
                i = r->day_count++;
            r->days[i] = (thingsos_day_record_t){ date, 0 };
        }
        if (UINT64_MAX - r->days[i].milliseconds < delta) return false;
        r->days[i].milliseconds += delta;
    }
totals:
    if (date && delta) {
        size_t i = 0;
        while (i < r->book_day_count && (r->book_days[i].id != id || r->book_days[i].date != date))
            ++i;
        if (i == r->book_day_count) {
            if (i == THINGSOS_BOOK_DAYS) {
                size_t oldest = 0;
                for (size_t k = 1; k < i; ++k)
                    if (r->book_days[k].date < r->book_days[oldest].date) oldest = k;
                if (date < r->book_days[oldest].date) goto lifetime;
                i = oldest;
            } else
                ++r->book_day_count;
            r->book_days[i] = (thingsos_book_day_t){ id, 0, date };
        }
        r->book_days[i].milliseconds += delta;
    }
lifetime:
    r->total_ms += delta;
    book->milliseconds += delta;
    return true;
}
/* Only today's hourly detail is retained; daily history remains authoritative. */
bool thingsos_records_hour(thingsos_records_t* r,uint32_t date,unsigned hour,uint64_t delta) {
    if(!r || !thingsos_date_valid(date) || hour>=24)return false;
    if(r->hour_date!=date) {memset(r->hour_ms,0,sizeof(r->hour_ms));r->hour_date=date;}
    if(UINT64_MAX-r->hour_ms[hour]<delta)return false;
    r->hour_ms[hour]+=delta;return true;
}
typedef struct {
    unsigned char* data;
    size_t at, cap;
    bool ok;
} wire_t;
static void put(wire_t* w, uint64_t n, unsigned bytes) {
    if (!w->ok || bytes > w->cap - w->at) {
        w->ok = false;
        return;
    }
    for (unsigned i = 0; i < bytes; i++) {
        w->data[w->at++] = (unsigned char)n;
        n >>= 8;
    }
}
static uint64_t get(wire_t* w, unsigned bytes) {
    if (!w->ok || bytes > w->cap - w->at) {
        w->ok = false;
        return 0;
    }
    uint64_t n = 0;
    for (unsigned i = 0; i < bytes; i++) n |= (uint64_t)w->data[w->at++] << (i * 8);
    return n;
}
static void text_put(wire_t* w, const char* s, size_t n) {
    if (!w->ok || n > w->cap - w->at) {
        w->ok = false;
        return;
    }
    memcpy(w->data + w->at, s, n);
    w->at += n;
}
static void text_get(wire_t* w, char* s, size_t n) {
    if (!w->ok || n > w->cap - w->at) {
        w->ok = false;
        return;
    }
    memcpy(s, w->data + w->at, n);
    w->at += n;
    if (!memchr(s, 0, n)) w->ok = false;
}
static uint32_t crc(const unsigned char* data, size_t n) {
    uint32_t v = UINT32_MAX;
    for (size_t i = 0; i < n; i++) {
        unsigned char b = i >= 20 && i < 24 ? 0 : data[i];
        v ^= b;
        for (unsigned j = 0; j < 8; j++) v = (v >> 1) ^ (0xedb88320u & (0u - (v & 1)));
    }
    return ~v;
}
static bool encode(
    const thingsos_records_t* r, unsigned char* data, size_t cap, size_t* bytes, bool compatible) {
    if (!r || !data || !bytes || cap < 24 || r->book_count > THINGSOS_RECORD_BOOKS ||
        r->mark_count > THINGSOS_BOOKMARK_LIMIT || r->day_count > THINGSOS_RECORD_DAYS)
        return false;
    wire_t w = { data, 0, cap, true };
    text_put(&w, compatible ? "THSTATE1" : "THSTATE2", 8);
    put(&w, r->generation, 8);
    put(&w, 0, 4);
    put(&w, 0, 4);
    put(&w, r->card_id, 8);
    put(&w, r->total_ms, 8);
    put(&w, r->undated_ms, 8);
    put(&w, r->order, 8);
    text_put(&w, r->library_path, THINGSOS_RECORD_PATH_BYTES);
    put(&w, r->book_count, 4);
    put(&w, r->mark_count, 4);
    put(&w, r->day_count, 4);
    for (size_t i = 0; i < r->book_count; i++) {
        const thingsos_book_record_t* b = r->books + i;
        put(&w, b->id, 8);
        put(&w, b->milliseconds, 8);
        put(&w, b->order, 8);
        put(&w, b->chapter, 4);
        put(&w, b->offset, 4);
        if (!compatible) {
            put(&w, b->page, 4);
            put(&w, b->pages, 4);
            put(&w, b->percent, 4);
        }
        text_put(&w, b->path, THINGSOS_RECORD_PATH_BYTES);
        text_put(&w, b->title, 192);
    }
    for (size_t i = 0; i < r->mark_count; i++) {
        put(&w, r->marks[i].book_id, 8);
        put(&w, r->marks[i].chapter, 4);
        put(&w, r->marks[i].byte_offset, 4);
    }
    for (size_t i = 0; i < r->day_count; i++) {
        put(&w, r->days[i].date, 4);
        put(&w, r->days[i].milliseconds, 8);
    }
    if (!w.ok) return false;
    *bytes = w.at;
    w.at = 16;
    put(&w, *bytes, 4);
    put(&w, crc(data, *bytes), 4);
    return true;
}
bool thingsos_records_encode(
    const thingsos_records_t* r, unsigned char* data, size_t cap, size_t* bytes) {
    return encode(r, data, cap, bytes, false);
}
bool thingsos_records_decode(thingsos_records_t* r, const unsigned char* data, size_t n) {
    if (!r || !data || n < 24 || n > THINGSOS_STORE_BYTES ||
        (memcmp(data, "THSTATE1", 8) && memcmp(data, "THSTATE2", 8)))
        return false;
    bool v2 = !memcmp(data, "THSTATE2", 8);
    wire_t w = { (unsigned char*)data, 8, n, true };
    uint64_t generation = get(&w, 8);
    uint32_t bytes = (uint32_t)get(&w, 4), expected = (uint32_t)get(&w, 4);
    if (bytes != n || crc(data, n) != expected) return false;
    thingsos_records_t* next = thingsos_psram_calloc(1, sizeof(*next));
    if (!next) return false;
    next->generation = generation;
    next->card_id = get(&w, 8);
    next->total_ms = get(&w, 8);
    next->undated_ms = get(&w, 8);
    next->order = get(&w, 8);
    text_get(&w, next->library_path, THINGSOS_RECORD_PATH_BYTES);
    next->book_count = (size_t)get(&w, 4);
    next->mark_count = (size_t)get(&w, 4);
    next->day_count = (size_t)get(&w, 4);
    if (!w.ok || next->book_count > THINGSOS_RECORD_BOOKS ||
        next->mark_count > THINGSOS_BOOKMARK_LIMIT || next->day_count > THINGSOS_RECORD_DAYS ||
        (!thingsos_user_file_allowed(next->library_path) && strcmp(next->library_path, "/sdcard")))
        goto fail;
    uint64_t sum = 0;
    for (size_t i = 0; i < next->book_count; i++) {
        thingsos_book_record_t* b = next->books + i;
        b->id = get(&w, 8);
        b->milliseconds = get(&w, 8);
        b->order = get(&w, 8);
        b->chapter = (uint32_t)get(&w, 4);
        b->offset = (uint32_t)get(&w, 4);
        if (v2) {
            b->page = (uint32_t)get(&w, 4);
            b->pages = (uint32_t)get(&w, 4);
            b->percent = (uint32_t)get(&w, 4);
            if (b->percent > 100 || b->page > b->pages) goto fail;
        }
        text_get(&w, b->path, THINGSOS_RECORD_PATH_BYTES);
        text_get(&w, b->title, 192);
        if (!w.ok || !b->id || b->order > next->order ||
            (!thingsos_user_file_allowed(b->path) &&
             !(b->id == 1 && !strcmp(b->path, "builtin:example"))) ||
            UINT64_MAX - sum < b->milliseconds)
            goto fail;
        for (size_t j = 0; j < i; j++)
            if (next->books[j].id == b->id) goto fail;
        sum += b->milliseconds;
    }
    if (sum != next->total_ms || next->undated_ms > sum) goto fail;
    for (size_t i = 0; i < next->mark_count; i++) {
        thingsos_location_t* m = next->marks + i;
        m->book_id = get(&w, 8);
        m->chapter = (uint32_t)get(&w, 4);
        m->byte_offset = (uint32_t)get(&w, 4);
        if (!thingsos_records_book(next, m->book_id, NULL, NULL)) goto fail;
        for (size_t j = 0; j < i; j++)
            if (next->marks[j].book_id == m->book_id && next->marks[j].chapter == m->chapter &&
                next->marks[j].byte_offset == m->byte_offset)
                goto fail;
    }
    uint64_t dated_sum = 0;
    for (size_t i = 0; i < next->day_count; i++) {
        next->days[i].date = (uint32_t)get(&w, 4);
        next->days[i].milliseconds = get(&w, 8);
        if (!thingsos_date_valid(next->days[i].date) || next->days[i].milliseconds > next->total_ms)
            goto fail;
        for (size_t j = 0; j < i; j++)
            if (next->days[j].date == next->days[i].date) goto fail;
        if (UINT64_MAX - dated_sum < next->days[i].milliseconds) goto fail;
        dated_sum += next->days[i].milliseconds;
    }
    if (dated_sum > next->total_ms - next->undated_ms) goto fail;
    if (!w.ok || w.at != n) goto fail;
    *r = *next;
    free(next);
    return true;
fail:
    free(next);
    return false;
}

uint64_t thingsos_book_time(
    const thingsos_records_t* r, uint64_t id, uint32_t first, uint32_t last) {
    if (!r) return 0;
    uint64_t n = 0;
    if (!first && !last) {
        for (size_t i = 0; i < r->book_count; ++i)
            if (r->books[i].id == id) return r->books[i].milliseconds;
        return 0;
    }
    for (size_t i = 0; i < r->book_day_count; ++i)
        if (r->book_days[i].id == id && r->book_days[i].date >= first &&
            r->book_days[i].date <= last)
            n += r->book_days[i].milliseconds;
    return n;
}
/* Same-generation CRC sidecar preserves the rollback-compatible base layout.
   Write sidecar first; a torn base write cannot make future data visible. */
static bool stats_read(thingsos_records_t* r, const char* root) {
    char path[512];
    snprintf(path, sizeof(path), "%s/.thingsos/stats-%u.dat", root, (unsigned)(r->generation & 1));
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    unsigned char* data = thingsos_psram_alloc(36 + THINGSOS_BOOK_DAYS * 20);
    if (!data) {
        fclose(f);
        return false;
    }
    size_t n = fread(data, 1, 36 + THINGSOS_BOOK_DAYS * 20, f);
    bool ok = !ferror(f);
    fclose(f);
    wire_t w = { data, 8, n, true };
    uint64_t gen = get(&w, 8);
    size_t bytes = (size_t)get(&w, 4);
    uint32_t sum = (uint32_t)get(&w, 4);
    uint64_t card = get(&w, 8);
    size_t count = (size_t)get(&w, 4);
    ok = ok && n >= 36 && !memcmp(data, "THSTATS1", 8) && gen == r->generation && bytes == n &&
         sum == crc(data, n) && card == r->card_id && count <= THINGSOS_BOOK_DAYS &&
         n == 36 + count * 20;
    for (size_t i = 0; ok && i < count; ++i) {
        thingsos_book_day_t d;
        d.id = get(&w, 8);
        d.date = (uint32_t)get(&w, 4);
        d.milliseconds = get(&w, 8);
        thingsos_book_record_t* b = thingsos_records_book(r, d.id, NULL, NULL);
        if (!b || !thingsos_date_valid(d.date) || d.milliseconds > b->milliseconds)
            ok = false;
        else
            r->book_days[i] = d;
    }
    r->book_day_count = ok ? count : 0;
    free(data);
    return ok;
}
static bool stats_write(const thingsos_records_t* r, const char* root, uint64_t gen) {
    if (r->book_day_count > THINGSOS_BOOK_DAYS) return false;
    size_t bytes = 36 + r->book_day_count * 20;
    unsigned char* data = thingsos_psram_calloc(1, bytes);
    if (!data) return false;
    wire_t w = { data, 0, bytes, true };
    text_put(&w, "THSTATS1", 8);
    put(&w, gen, 8);
    put(&w, bytes, 4);
    put(&w, 0, 4);
    put(&w, r->card_id, 8);
    put(&w, r->book_day_count, 4);
    for (size_t i = 0; i < r->book_day_count; ++i) {
        put(&w, r->book_days[i].id, 8);
        put(&w, r->book_days[i].date, 4);
        put(&w, r->book_days[i].milliseconds, 8);
    }
    w.at = 20;
    put(&w, crc(data, bytes), 4);
    char path[512];
    snprintf(path, sizeof(path), "%s/.thingsos/stats-%u.dat", root, (unsigned)(gen & 1));
    FILE* f = fopen(path, "wb");
    bool ok = f != NULL;
    if (f) {
        if (fwrite(data, 1, bytes, f) != bytes || fflush(f) || sync_file(f)) ok = false;
        if (fclose(f)) ok = false;
    }
    if (ok) {
        f = fopen(path, "rb");
        if (!f)
            ok = false;
        else {
            unsigned char* check = thingsos_psram_alloc(bytes);
            ok = check && fread(check, 1, bytes, f) == bytes && !memcmp(check, data, bytes);
            free(check);
            fclose(f);
        }
    }
    free(data);
    return ok;
}

/* Separate same-generation sidecar: old state and book/day formats are unchanged. */
#define HOURS_BYTES (36+24*8)
static bool hours_write(const thingsos_records_t* r,const char* root,uint64_t gen) {
    unsigned char data[HOURS_BYTES]={0},check[HOURS_BYTES];
    wire_t w={data,0,sizeof(data),true};
    text_put(&w,"THHOURS1",8);put(&w,gen,8);put(&w,sizeof(data),4);put(&w,0,4);
    put(&w,r->card_id,8);put(&w,r->hour_date,4);
    for(unsigned i=0;i<24;++i)put(&w,r->hour_ms[i],8);
    w.at=20;put(&w,crc(data,sizeof(data)),4);
    char path[512];snprintf(path,sizeof(path),"%s/.thingsos/hours-%u.dat",root,(unsigned)(gen&1));
    FILE* f=fopen(path,"wb");if(!f)return false;
    bool ok=fwrite(data,1,sizeof(data),f)==sizeof(data);
    if(fflush(f) || sync_file(f))ok=false;
    if(fclose(f))ok=false;
    if(!ok)return false;
    f=fopen(path,"rb");if(!f)return false;
    ok=fread(check,1,sizeof(check),f)==sizeof(check) && !memcmp(data,check,sizeof(data));
    fclose(f);return ok;
}
static void hours_read(thingsos_records_t* r,const char* root) {
    r->hour_date=0;memset(r->hour_ms,0,sizeof(r->hour_ms));
    char path[512];snprintf(path,sizeof(path),"%s/.thingsos/hours-%u.dat",root,(unsigned)(r->generation&1));
    FILE* f=fopen(path,"rb");if(!f)return;
    unsigned char data[HOURS_BYTES];size_t n=fread(data,1,sizeof(data),f);
    bool ok=n==sizeof(data) && !ferror(f) && fgetc(f)==EOF;fclose(f);
    if(!ok || memcmp(data,"THHOURS1",8))return;
    wire_t w={data,8,sizeof(data),true};
    uint64_t gen=get(&w,8);size_t size=(size_t)get(&w,4);uint32_t sum=(uint32_t)get(&w,4);
    uint64_t card=get(&w,8);uint32_t date=(uint32_t)get(&w,4);
    if(gen!=r->generation || card!=r->card_id || size!=sizeof(data) || sum!=crc(data,sizeof(data)) ||
       (date && !thingsos_date_valid(date)))return;
    uint64_t total=0;
    for(unsigned i=0;i<24;++i) {uint64_t v=get(&w,8);if(UINT64_MAX-total<v)return;total+=v;}
    uint64_t daily=0;
    for(size_t i=0;i<r->day_count;++i)if(r->days[i].date==date)daily=r->days[i].milliseconds;
    if(total>daily || (!date && total))return;
    r->hour_date=date;w.at=36;for(unsigned i=0;i<24;++i)r->hour_ms[i]=get(&w,8);
}

static int read_slot(thingsos_records_t* r, const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return errno == ENOENT ? 0 : -2;
    if (fseek(f, 0, SEEK_END)) {
        fclose(f);
        return -2;
    }
    long n = ftell(f);
    if (n < 24 || n > THINGSOS_STORE_BYTES) {
        fclose(f);
        return -1;
    }
    rewind(f);
    unsigned char* data = thingsos_psram_alloc((size_t)n);
    if (!data) {
        fclose(f);
        return -2;
    }
    bool read = fread(data, 1, (size_t)n, f) == (size_t)n;
    fclose(f);
    bool valid = read && thingsos_records_decode(r, data, (size_t)n) && r->generation && r->card_id;
    free(data);
    return valid ? 1 : read ? -1 : -2;
}
thingsos_store_result_t thingsos_store_load(thingsos_records_t* r, const char* root) {
    if (!r || !root || strlen(root) > 400) return THINGSOS_STORE_IO_ERROR;
    thingsos_records_t *a = thingsos_psram_calloc(1, sizeof(*a)), *b = thingsos_psram_calloc(1, sizeof(*b));
    if (!a || !b) {
        free(a);
        free(b);
        return THINGSOS_STORE_IO_ERROR;
    }
    char path[512];
    snprintf(path, sizeof(path), "%s/.thingsos/state-0.dat", root);
    int first = read_slot(a, path);
    snprintf(path, sizeof(path), "%s/.thingsos/state-1.dat", root);
    int second = read_slot(b, path);
    thingsos_store_result_t result;
    if (first == 1 || second == 1) {
        *r = first == 1 && (second != 1 || a->generation >= b->generation) ? *a : *b;
        result = first < 0 || second < 0 ? THINGSOS_STORE_RECOVERED : THINGSOS_STORE_OK;
    } else if (!first && !second) {
        thingsos_records_init(r);
        result = THINGSOS_STORE_MISSING;
    } else
        result = first == -2 || second == -2 ? THINGSOS_STORE_IO_ERROR : THINGSOS_STORE_CORRUPT;
    free(a);
    free(b);
    if (result == THINGSOS_STORE_OK || result == THINGSOS_STORE_RECOVERED) {stats_read(r, root);hours_read(r,root);}
    return result;
}
static bool save(thingsos_records_t* r, const char* root, bool compatible) {
    if (!r || !root || strlen(root) > 400 || !r->card_id || r->generation == UINT64_MAX)
        return false;
    char path[512];
    snprintf(path, sizeof(path), "%s/.thingsos", root);
    if (make_dir(path) && errno != EEXIST) return false;
    unsigned char* data = thingsos_psram_calloc(1, THINGSOS_STORE_BYTES);
    thingsos_records_t* check = thingsos_psram_alloc(sizeof(*check));
    if (!data || !check) {
        free(data);
        free(check);
        return false;
    }
    uint64_t generation = r->generation;
    r->generation++;
    size_t bytes;
    bool ok = stats_write(r, root, r->generation) && hours_write(r,root,r->generation) &&
              encode(r, data, THINGSOS_STORE_BYTES, &bytes, compatible);
    r->generation = generation;
    snprintf(
        path, sizeof(path), "%s/.thingsos/state-%u.dat", root, (unsigned)((generation + 1) & 1));
    FILE* f = ok ? fopen(path, "wb") : NULL;
    if (!f) ok = false;
    if (f) {
        if (fwrite(data, 1, bytes, f) != bytes) ok = false;
        if (fflush(f)) ok = false;
        if (sync_file(f)) ok = false;
        if (fclose(f)) ok = false;
    }
    if (ok)
        ok = read_slot(check, path) == 1 && check->generation == generation + 1 &&
             check->card_id == r->card_id;
    if (ok) r->generation = generation + 1;
    free(data);
    free(check);
    return ok;
}
bool thingsos_store_save(thingsos_records_t* r, const char* root) {
    return save(r, root, false);
}
bool thingsos_store_save_compatible(thingsos_records_t* r, const char* root) {
    return save(r, root, true);
}
