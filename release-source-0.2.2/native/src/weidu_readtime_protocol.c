/* SPDX-License-Identifier: AGPL-3.0-only
 * C adaptation of MiuRead readtime_protocol.lua, revision
 * 418380a0638381fcd8e2bad8d1f23be79fddd7c4 (miumiupy98-art/miuread-koreader).
 * Signature transformation/table derived from AnkioTomas/moon (GPL-3.0),
 * 43b2181da6eb5285b62114da85e1413d2f8464e4,
 * book.koplugin/source/wechat/app.lua; GPL terms retained for that material.
 * Native endpoint contracts: teng-lin/weread-omni (MIT),
 * 88bd2e095d7d7ee423eaadf8f40653e72c5be6d4.
 * Changes: bounded C binary-string sorting, explicit clock/random inputs,
 * cJSON serialization, no Lua/KOReader runtime or credentials embedded.
 * See licenses/READTIME-SOURCES.md and the accompanying full license texts.
 */
#include "cJSON.h"
#include "thingsos/mem.h"
#include "thingsos/weidu_readtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static const unsigned char remap[256] = {
    52,  202, 85,  64,  29,  182, 147, 198, 49,  48,  41,  53,  50,  167, 184, 17,  194, 181, 22,
    250, 139, 177, 36,  164, 16,  144, 4,   233, 8,   248, 59,  138, 156, 140, 68,  249, 188, 92,
    105, 226, 161, 218, 210, 211, 117, 137, 247, 30,  45,  80,  86,  215, 114, 83,  191, 34,  251,
    32,  15,  1,   46,  69,  135, 110, 102, 72,  242, 224, 205, 254, 103, 169, 67,  244, 148, 81,
    206, 165, 74,  238, 19,  38,  142, 204, 170, 51,  20,  93,  14,  57,  187, 207, 145, 43,  129,
    77,  234, 153, 236, 26,  44,  133, 197, 217, 54,  116, 75,  24,  225, 241, 61,  157, 65,  159,
    180, 23,  13,  214, 76,  190, 220, 175, 151, 40,  119, 240, 98,  255, 113, 193, 200, 39,  143,
    108, 104, 168, 155, 230, 89,  28,  27,  18,  9,   152, 78,  63,  6,   55,  0,   186, 31,  10,
    25,  47,  201, 213, 208, 87,  73,  111, 253, 37,  228, 97,  12,  66,  203, 150, 100, 95,  219,
    173, 96,  35,  141, 154, 109, 195, 196, 94,  62,  185, 146, 106, 189, 91,  7,   127, 118, 149,
    237, 79,  171, 132, 122, 128, 231, 120, 199, 229, 235, 115, 131, 107, 252, 56,  70,  125, 71,
    101, 179, 82,  99,  58,  5,   209, 239, 163, 166, 222, 158, 60,  2,   174, 178, 123, 160, 246,
    243, 42,  192, 172, 134, 3,   90,  84,  11,  245, 130, 212, 126, 227, 223, 176, 216, 221, 33,
    232, 124, 136, 162, 121, 88,  112, 183, 21};
typedef struct {
    unsigned char *p;
    size_t n;
} chunk_t;
static int compare(const void *a, const void *b) {
    const chunk_t *x = a;
    const chunk_t *y = b;
    int c = memcmp(x->p, y->p, x->n < y->n ? x->n : y->n);
    return c ? c : x->n < y->n ? -1 : x->n > y->n ? 1 : 0;
}
static void rotate(unsigned char *p, size_t n) {
    unsigned sum = 0;
    for (size_t i = 0; i < n; ++i)
        sum ^= p[i];
    size_t k = (sum % 11) % n;
    unsigned char tail[10];
    memcpy(tail, p + n - k, k);
    memmove(p + k, p, n - k);
    memcpy(p, tail, k);
}
bool thingsos_rt_sign(const char *const *parts, size_t count, char out[65]) {
    if (!parts || count > 8)
        return false;
    unsigned char *memory = thingsos_psram_alloc(8192);
    if (!memory)
        return false;
    chunk_t chunks[9];
    size_t total = 5;
    memcpy(memory, "5a6f1", 5);
    chunks[0] = (chunk_t){memory, 5};
    bool ok = true;
    for (size_t i = 0; i < count; ++i) {
        if (!parts[i]) {
            ok = false;
            break;
        }
        size_t n = strlen(parts[i]);
        if (n > 4096 - total) {
            ok = false;
            break;
        }
        chunks[i + 1] = (chunk_t){memory + total, n};
        for (size_t j = 0; j < n; ++j)
            memory[total + j] = remap[(unsigned char)parts[i][j]];
        total += n;
    }
    if (ok) {
        qsort(chunks, count + 1, sizeof(chunks[0]), compare);
        unsigned char *joined = memory + 4096;
        size_t at = 0;
        for (size_t i = 0; i <= count; ++i) {
            memcpy(joined + at, chunks[i].p, chunks[i].n);
            at += chunks[i].n;
        }
        rotate(joined, total);
        char hash[65];
        ok = thingsos_rt_hash(joined, total, hash);
        if (ok) {
            rotate((unsigned char *)hash, 64);
            ok = thingsos_rt_hash(hash, 64, out);
        }
    }
    free(memory);
    return ok;
}
static bool zone_text(int32_t zone, char out[8]) {
    if (zone < -840 || zone > 840)
        return false;
    unsigned a = (unsigned)(zone < 0 ? -zone : zone);
    snprintf(out, 8, "%c%02u:%02u", zone < 0 ? '-' : '+', a / 60, a % 60);
    return true;
}
bool thingsos_rt_body(const thingsos_rt_credentials_t *credentials,
                      const thingsos_rt_batch_t *batch, const char *guest, const char *token,
                      uint64_t now, uint32_t random, uint32_t outer, int32_t zone, char *out,
                      size_t cap) {
    if (!credentials || !batch || !batch->count || batch->count > WD_RT_BATCH || !guest ||
        !*guest || !token || !*token || now / 1000 < 1790812800u || now / 1000 > UINT32_MAX ||
        random > 999 || outer > 999)
        return false;
    for (size_t i = 0; i < sizeof(credentials->vid); ++i) {
        if (!credentials->vid[i])
            break;
        if (credentials->vid[i] < '0' || credentials->vid[i] > '9')
            return false;
    }
    if (!memchr(credentials->vid, 0, sizeof(credentials->vid)) || !credentials->vid[0] ||
        !memchr(credentials->device, 0, sizeof(credentials->device)) || !credentials->device[0])
        return false;
    typedef struct {
        uint32_t hour, seconds;
        int32_t zone;
    } hour_t;
    hour_t hours[WD_RT_HOURS];
    unsigned count = 0;
    uint32_t total = 0, risk = 0;
    char tz[8];
    if (!zone_text(zone, tz))
        return false;
    for (unsigned i = 0; i < batch->count; ++i) {
        const thingsos_rt_row_t *r = &batch->rows[i];
        if (!memchr(r->book, 0, sizeof(r->book)) || !r->book[0] ||
            strspn(r->book, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-") != strlen(r->book))
            return false;
        if (strcmp(r->book, batch->rows[0].book) || r->version != batch->rows[0].version ||
            !r->seconds || r->seconds > 3600 || r->hour % 3600 || r->hour > now / 1000 ||
            r->seconds > now / 1000 - r->hour || r->until > now / 1000 ||
            r->until < r->hour + r->seconds || r->until > r->hour + 3600 || r->zone < -840 ||
            r->zone > 840)
            return false;
        unsigned j = 0;
        for (; j < count; ++j)
            if (hours[j].hour == r->hour && hours[j].zone == r->zone)
                break;
        if (j == count) {
            if (count == WD_RT_HOURS)
                return false;
            hours[count++] = (hour_t){r->hour, 0, r->zone};
        }
        hours[j].seconds += r->seconds;
        if (hours[j].seconds > 3600)
            return false;
        total += r->seconds;
        risk |= r->risk;
    }
    for (unsigned i = 0; i < count; ++i)
        for (unsigned j = i + 1; j < count; ++j)
            if (hours[j].hour < hours[i].hour ||
                (hours[j].hour == hours[i].hour && hours[j].zone < hours[i].zone)) {
                hour_t t = hours[i];
                hours[i] = hours[j];
                hours[j] = t;
            }
    if (hours[count - 1].hour - hours[0].hour >= 86400)
        return false;
    cJSON *root = cJSON_CreateObject();
    cJSON *books = cJSON_AddArrayToObject(root, "books");
    cJSON *book = cJSON_CreateObject();
    if (!root || !books || !book) {
        cJSON_Delete(root);
        cJSON_Delete(book);
        return false;
    }
    cJSON_AddItemToArray(books, book);
    cJSON_AddStringToObject(book, "bookId", batch->rows[0].book);
    cJSON_AddNumberToObject(book, "bookVersion", batch->rows[0].version);
    cJSON_AddStringToObject(book, "appId", "");
    cJSON_AddStringToObject(book, "deviceId", credentials->device);
    cJSON_AddStringToObject(book, "installId", "");
    cJSON_AddStringToObject(book, "summary", "");
    const char *zeros[] = {"progress",    "currentProgress", "autoTime",  "isLecture",
                           "lectureTime", "lectureTextTime", "novalTime", "ttsTime"};
    for (unsigned i = 0; i < sizeof(zeros) / sizeof(*zeros); ++i)
        cJSON_AddNumberToObject(book, zeros[i], 0);
    cJSON_AddNumberToObject(book, "isResendReadingInfo", 1);
    cJSON_AddBoolToObject(book, "finish", false);
    cJSON_AddNumberToObject(book, "risk", risk);
    cJSON_AddNumberToObject(book, "readingTime", total);
    cJSON_AddStringToObject(book, "recordCreateTimeZone", tz);
    cJSON_AddNumberToObject(book, "timestamp", (double)(now / 1000));
    cJSON_AddNumberToObject(book, "random", random);
    cJSON *array = cJSON_AddArrayToObject(book, "hours");
    char payload[1536], stamp[32], rnd[16], sig[65];
    int n = snprintf(payload, sizeof(payload), "%s_%s__%s_%u_%s_%u_0", credentials->vid,
                     credentials->device, batch->rows[0].book, (unsigned)risk, tz, (unsigned)total);
    bool ok = n > 0 && (size_t)n < sizeof(payload) && array;
    for (unsigned i = 0; i < count && ok; ++i) {
        char z[8];
        zone_text(hours[i].zone, z);
        cJSON *h = cJSON_CreateObject();
        if (!h) {
            ok = false;
            break;
        }
        cJSON_AddItemToArray(array, h);
        cJSON_AddNumberToObject(h, "startOfHour", hours[i].hour);
        cJSON_AddStringToObject(h, "timeZone", z);
        cJSON_AddNumberToObject(h, "readingTime", hours[i].seconds);
        cJSON_AddNumberToObject(h, "ttsTime", 0);
        size_t used = strlen(payload);
        n = snprintf(payload + used, sizeof(payload) - used, "_%u_%s_%u_0", (unsigned)hours[i].hour,
                     z, (unsigned)hours[i].seconds);
        ok = n > 0 && (size_t)n < sizeof(payload) - used;
    }
    snprintf(stamp, sizeof(stamp), "%llu", (unsigned long long)(now / 1000));
    snprintf(rnd, sizeof(rnd), "%u", (unsigned)random);
    const char *parts[] = {guest, rnd, stamp, payload};
    if (ok)
        ok = thingsos_rt_sign(parts, 4, sig);
    if (ok)
        cJSON_AddStringToObject(book, "signature", sig);
    char envelope[1024];
    n = snprintf(envelope, sizeof(envelope), "%llu%s%u", (unsigned long long)now, token,
                 (unsigned)outer);
    if (ok)
        ok = n > 0 && (size_t)n < sizeof(envelope) && thingsos_rt_sha256(envelope, sig);
    if (ok) {
        cJSON_AddNumberToObject(root, "timestamp", (double)now);
        cJSON_AddNumberToObject(root, "random", outer);
        cJSON_AddStringToObject(root, "signature", sig);
        ok = cap <= INT32_MAX && cJSON_PrintPreallocated(root, out, (int)cap, false);
    }
    if (ok) {
        ok = cJSON_GetArraySize(root) == 4 && cJSON_GetArraySize(book) == 23 &&
             cJSON_GetArraySize(array) == (int)count;
        for (unsigned i = 0; i < count && ok; ++i)
            ok = cJSON_GetArraySize(cJSON_GetArrayItem(array, (int)i)) == 4;
    }
    cJSON_Delete(root);
    return ok;
}
