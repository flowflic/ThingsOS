/* SPDX-License-Identifier: Apache-2.0
 * Independently authored bounded C spool and capture adapter.
 * Behavioral reference: MiuRead readtime_outbox, including #184/#187:
 * pending/unknown isolation, send-time claim and historical-hour grouping.
 * No Lua/SQLite source is incorporated into this module.
 */
#include "thingsos/weidu_readtime.h"
#include "thingsos/weidu.h"
#include "zlib.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#define rt_mkdir(p) _mkdir(p)
#else
#include <dirent.h>
#define rt_mkdir(p) mkdir(p, 0775)
#endif

static bool owner_ok(const char *s) {
    if (!s || strlen(s) != 32)
        return false;
    for (unsigned i = 0; i < 32; ++i)
        if (!isxdigit((unsigned char)s[i]))
            return false;
    return true;
}
static bool book_ok(const char *s) {
    if (!s || !memchr(s, 0, 64) || !*s)
        return false;
    for (; *s; ++s)
        if (!isalnum((unsigned char)*s) && *s!='_' && *s!='-')
            return false;
    return true;
}
static bool valid(const thingsos_rt_row_t *r) {
    return r && r->id && book_ok(r->book) && r->hour >= 1790812800u && r->hour % 3600 == 0 &&
           r->seconds && r->seconds <= 3600 && r->zone >= -840 && r->zone <= 840 &&
           r->until >= r->hour + r->seconds && r->until <= r->hour + 3600;
}
static bool directory(const char *root, const char *owner, char out[640]) {
    return root && owner_ok(owner) && snprintf(out, 640, "%s/readtime-%s", root, owner) < 640;
}
static bool path_for(const char *dir, uint64_t id, const char *state, char out[720]) {
    return snprintf(out, 720, "%s/%016llx.%s", dir, (unsigned long long)id, state) < 720;
}
static void put32(unsigned char *p, uint32_t n) {
    for (unsigned i = 0; i < 4; ++i)
        p[i] = (unsigned char)(n >> (i * 8));
}
static uint32_t get32(const unsigned char *p) {
    uint32_t n = 0;
    for (unsigned i = 0; i < 4; ++i)
        n |= (uint32_t)p[i] << (i * 8);
    return n;
}
static void encode(const thingsos_rt_row_t *r, unsigned char b[128]) {
    memset(b, 0, 128);
    memcpy(b, "WRT1", 4);
    put32(b + 4, (uint32_t)r->id);
    put32(b + 8, (uint32_t)(r->id >> 32));
    put32(b + 12, r->hour);
    put32(b + 16, r->seconds);
    put32(b + 20, r->version);
    put32(b + 24, r->risk);
    put32(b + 28, (uint32_t)r->zone);
    memcpy(b + 32, r->book, strlen(r->book));
    put32(b + 96, r->until);
    put32(b + 124, crc32(0, b, 124));
}
static bool read_row(const char *path, thingsos_rt_row_t *r) {
    unsigned char b[128];
    FILE *f = fopen(path, "rb");
    bool ok = f && fread(b, 1, sizeof(b), f) == sizeof(b) && fgetc(f) == EOF && !ferror(f);
    if (f)
        fclose(f);
    if (!ok || memcmp(b, "WRT1", 4) || get32(b + 124) != crc32(0, b, 124))
        return false;
    memset(r, 0, sizeof(*r));
    r->id = get32(b + 4) | ((uint64_t)get32(b + 8) << 32);
    r->hour = get32(b + 12);
    r->seconds = get32(b + 16);
    r->version = get32(b + 20);
    r->risk = get32(b + 24);
    r->zone = (int32_t)get32(b + 28);
    memcpy(r->book, b + 32, 64);
    r->until = get32(b + 96);
    return valid(r);
}
static bool same(const thingsos_rt_row_t *a, const thingsos_rt_row_t *b) {
    return a->id == b->id && !strcmp(a->book, b->book) && a->hour == b->hour &&
           a->seconds == b->seconds && a->version == b->version && a->risk == b->risk &&
           a->zone == b->zone && a->until == b->until;
}
typedef bool (*visitor_t)(const char *, const char *, void *);
static bool walk(const char *dir, visitor_t visit, void *context) {
#ifdef _WIN32
    char pattern[720];
    snprintf(pattern, sizeof(pattern), "%s/*", dir);
    WIN32_FIND_DATAA data;
    HANDLE h = FindFirstFileA(pattern, &data);
    if (h == INVALID_HANDLE_VALUE)
        return GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND;
    bool ok = true;
    do {
        if (!visit(dir, data.cFileName, context)) {
            ok = false;
            break;
        }
    } while (FindNextFileA(h, &data));
    if (ok && GetLastError() != ERROR_NO_MORE_FILES)
        ok = false;
    FindClose(h);
    return ok;
#else
    DIR *d = opendir(dir);
    if (!d)
        return errno == ENOENT;
    bool ok = true;
    struct dirent *e;
    errno = 0;
    while ((e = readdir(d)) != NULL)
        if (!visit(dir, e->d_name, context)) {
            ok = false;
            break;
        }
    /* Visitors perform file IO and can change errno; readdir errors are rare,
       but the next complete scan will never promote an unread record. */
    closedir(d);
    return ok;
#endif
}
static int state(const char *name) {
    size_t n = strlen(name);
    if (n != 24 && n != 21)
        return 0;
    for (unsigned i = 0; i < 16; ++i)
        if (!isxdigit((unsigned char)name[i]))
            return 0;
    return !strcmp(name + 16, ".pending") ? 1
           : !strcmp(name + 16, ".sent")  ? 2
           : !strcmp(name + 16, ".held")  ? 3
                                          : 0;
}
static bool summary_visit(const char *dir, const char *name, void *context) {
    int s = state(name);
    if (!s)
        return true;
    if (s == 1) {
        char claimed[720];
        snprintf(claimed, sizeof(claimed), "%s/%.16s.sent", dir, name);
        FILE *f = fopen(claimed, "rb");
        if (f) {
            fclose(f);
            return true;
        }
    }
    thingsos_rt_summary_t *sum = context;
    char p[720];
    snprintf(p, sizeof(p), "%s/%s", dir, name);
    thingsos_rt_row_t r;
    ++sum->rows;
    if (!read_row(p, &r)) {
        sum->damaged = true;
        return true;
    }
    char expected[32];
    snprintf(expected, sizeof(expected), "%016llx.%s", (unsigned long long)r.id,
             s == 1   ? "pending"
             : s == 2 ? "sent"
                      : "held");
    if (strcmp(expected, name)) {
        sum->damaged = true;
        return true;
    }
    if (s == 1)
        sum->pending += r.seconds;
    else if (s == 2)
        sum->unknown += r.seconds;
    else
        sum->quarantined += r.seconds;
    return true;
}
bool thingsos_rt_summary(const char *root, const char *owner, thingsos_rt_summary_t *sum) {
    memset(sum, 0, sizeof(*sum));
    char dir[640];
    if (!directory(root, owner, dir))
        return false;
    bool ok = walk(dir, summary_visit, sum);
    sum->full = sum->rows >= WD_RT_CAPACITY;
    return ok;
}
static bool count_visit(const char *dir, const char *name, void *context) {
    (void)dir;
    unsigned *count = context;
    if (state(name))
        ++*count;
    return *count < WD_RT_CAPACITY;
}
bool thingsos_rt_put(const char *root, const char *owner, const thingsos_rt_row_t *row) {
    char dir[640], p[720], sent[720], tmp[720];
    if (!valid(row) || !directory(root, owner, dir))
        return false;
    if (rt_mkdir(dir) && errno != EEXIST)
        return false;
    path_for(dir, row->id, "pending", p);
    path_for(dir, row->id, "sent", sent);
    path_for(dir, row->id, "tmp", tmp);
    thingsos_rt_row_t existing;
    FILE *f = fopen(sent, "rb");
    if (f) {
        fclose(f);
        return false;
    }
    f = fopen(p, "rb");
    if (f) {
        fclose(f);
        return read_row(p, &existing) && same(row, &existing);
    }
    unsigned count = 0;
    if (!walk(dir, count_visit, &count) || count >= WD_RT_CAPACITY)
        return false;
    unsigned char b[128];
    encode(row, b);
    f = fopen(tmp, "wb");
    bool ok = f && fwrite(b, 1, sizeof(b), f) == sizeof(b) && thingsos_weidu_flush(f);
    if (f && fclose(f))
        ok = false;
    if (ok)
        ok = read_row(tmp, &existing) && same(row, &existing) && !rename(tmp, p);
    if (!ok)
        remove(tmp);
    return ok;
}
typedef struct {
    thingsos_rt_batch_t *batch;
    uint32_t now, first, last;
    struct {
        uint32_t hour, seconds;
        int32_t zone;
    } hours[WD_RT_HOURS];
    unsigned groups;
    bool bad;
} peek_t;
static bool peek_visit(const char *dir, const char *name, void *context) {
    peek_t *v = context;
    if (state(name) != 1 || v->batch->count == WD_RT_BATCH)
        return true;
    char path[720], sent[720];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    snprintf(sent, sizeof(sent), "%s/%.16s.sent", dir, name);
    FILE *claimed = fopen(sent, "rb");
    if (claimed) {
        fclose(claimed);
        return true;
    }
    thingsos_rt_row_t r;
    if (!read_row(path, &r)) {
        v->bad = true;
        return true;
    }
    if (r.until > v->now)
        return true;
    thingsos_rt_batch_t *batch = v->batch;
    if (batch->count &&
        (strcmp(batch->rows[0].book, r.book) || batch->rows[0].version != r.version))
        return true;
    uint32_t first = batch->count && v->first < r.hour ? v->first : r.hour;
    uint32_t last = batch->count && v->last > r.hour ? v->last : r.hour;
    if (last - first >= 86400)
        return true;
    unsigned g = 0;
    while (g < v->groups && (v->hours[g].hour != r.hour || v->hours[g].zone != r.zone))
        ++g;
    if (g == v->groups) {
        if (g == WD_RT_HOURS)
            return true;
        v->hours[g].hour = r.hour;
        v->hours[g].zone = r.zone;
        v->hours[g].seconds = 0;
        ++v->groups;
    }
    if (v->hours[g].seconds + r.seconds > 3600) {
        char held[720];
        path_for(dir, r.id, "held", held);
        if (rename(path, held))
            v->bad = true;
        return true; /* retain excessive source; healthy rows can proceed */
    }
    v->hours[g].seconds += r.seconds;
    v->first = first;
    v->last = last;
    batch->rows[batch->count++] = r;
    return true;
}
bool thingsos_rt_peek(const char *root, const char *owner, uint32_t now,
                      thingsos_rt_batch_t *batch) {
    batch->count = 0;
    char dir[640];
    if (!directory(root, owner, dir))
        return false;
    peek_t context = {0};
    context.batch = batch;
    context.now = now;
    bool ok = walk(dir, peek_visit, &context);
    if (context.bad)
        batch->count = 0;
    return ok && !context.bad;
}
bool thingsos_rt_claim(const char *root, const char *owner, const thingsos_rt_batch_t *b) {
    char dir[640], p[720], s[720];
    if (!b || !b->count || b->count > WD_RT_BATCH || !directory(root, owner, dir))
        return false;
    /* Validate the entire batch before changing state. Partial rename failure
       leaves already-claimed rows unknown; do not dispatch the partial batch. */
    for (unsigned i = 0; i < b->count; ++i) {
        thingsos_rt_row_t check;
        path_for(dir, b->rows[i].id, "pending", p);
        if (!read_row(p, &check) || !same(&check, &b->rows[i]))
            return false;
    }
    for (unsigned i = 0; i < b->count; ++i) {
        path_for(dir, b->rows[i].id, "pending", p);
        path_for(dir, b->rows[i].id, "sent", s);
        FILE *f = fopen(s, "rb");
        if (f) {
            fclose(f);
            return false;
        }
        /* Flush a separate durable intent BEFORE removing pending. If power
           fails with both names present, .sent always wins during scans. */
        unsigned char bytes[128];
        encode(&b->rows[i], bytes);
        f = fopen(s, "wb");
        bool ok =
            f && fwrite(bytes, 1, sizeof(bytes), f) == sizeof(bytes) && thingsos_weidu_flush(f);
        if (f && fclose(f))
            ok = false;
        if (!ok)
            return false;
        thingsos_rt_row_t check;
        if (!read_row(s, &check) || !same(&check, &b->rows[i]))
            return false;
        if (remove(p))
            return false;
    }
    return true;
}
bool thingsos_rt_confirm(const char *root, const char *owner, const thingsos_rt_batch_t *b) {
    char dir[640], p[720], pending[720];
    if (!b || !directory(root, owner, dir))
        return false;
    bool ok = true;
    for (unsigned i = 0; i < b->count; ++i) {
        thingsos_rt_row_t check;
        path_for(dir, b->rows[i].id, "sent", p);
        path_for(dir, b->rows[i].id, "pending", pending);
        FILE *f = fopen(pending, "rb");
        if (f) {
            fclose(f);
            if (remove(pending)) {
                ok = false;
                continue;
            }
        }
        if (!read_row(p, &check) || !same(&check, &b->rows[i]) || remove(p))
            ok = false;
    }
    return ok;
}
bool thingsos_rt_capture(thingsos_rt_capture_t *c, const char *owner, const char *book,
                         uint64_t wall, uint32_t ms, int32_t zone, uint32_t risk) {
    if (!c || !owner_ok(owner) || !book_ok(book) || wall / 1000 < 1790812800u || ms > 5000 ||
        zone < -840 || zone > 840)
        return false;
    while (ms) {
        uint64_t hour = wall / 3600000 * 3600;
        uint32_t part = (uint32_t)(3600000 - wall % 3600000);
        if (part > ms)
            part = ms;
        thingsos_rt_capture_row_t *p = c->count ? &c->rows[c->count - 1] : NULL;
        if (!p || strcmp(p->owner, owner) || strcmp(p->row.book, book) || p->row.hour != hour ||
            p->row.zone != zone || p->row.risk != risk || p->milliseconds + part > 3600000) {
            if (c->count == WD_RT_RAM) {
                /* Wire duration precision is seconds. Retire only a <1s
                   carry from a sealed hour, never pending whole seconds. */
                for (unsigned i = 0; i < c->count; ++i)
                    if (c->rows[i].milliseconds < 1000 && !c->rows[i].row.id &&
                        c->rows[i].row.hour != hour) {
                        memmove(&c->rows[i], &c->rows[i + 1],
                                (c->count - i - 1) * sizeof(c->rows[0]));
                        --c->count;
                        break;
                    }
                if (c->count == WD_RT_RAM) {
                    c->full = true;
                    return false;
                }
            }
            p = &c->rows[c->count++];
            memset(p, 0, sizeof(*p));
            strcpy(p->owner, owner);
            strcpy(p->row.book, book);
            p->row.hour = (uint32_t)hour;
            p->row.zone = zone;
            p->row.risk = risk;
        }
        p->milliseconds += part;
        wall += part;
        ms -= part;
        p->floor = (uint32_t)(wall / 1000);
    }
    return true;
}
bool thingsos_rt_flush(thingsos_rt_capture_t *c, const char *root) {
    if (!c || !c->nonce)
        return false;
    for (unsigned i = 0; i < c->count;) {
        thingsos_rt_capture_row_t *p = &c->rows[i];
        if (p->milliseconds < 1000) {
            ++i;
            continue;
        }
        if (!p->row.id) {
            if (c->sequence == UINT32_MAX)
                return false;
            p->row.id = ((uint64_t)c->nonce << 32) | ++c->sequence;
            p->row.seconds = p->milliseconds / 1000;
            p->row.until = p->floor;
        }
        if (!thingsos_rt_put(root, p->owner, &p->row))
            return false;
        ++c->saved_rows;
        c->saved_seconds += p->row.seconds;
        p->milliseconds -= p->row.seconds * 1000;
        p->row.id = 0;
        p->row.seconds = 0;
        if (p->milliseconds) {
            ++i;
            continue;
        }
        memmove(p, p + 1, (c->count - i - 1) * sizeof(*p));
        --c->count;
    }
    c->full = false;
    return true;
}
