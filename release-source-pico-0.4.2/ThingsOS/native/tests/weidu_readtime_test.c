/* SPDX-License-Identifier: Apache-2.0
 * Functional regression tests for independently authored spool/capture code.
 * Signature vector from AnkioTomas/moon app_spec.lua (GPL-3.0), identified in
 * licenses/READTIME-SOURCES.md. Vector is factual protocol test data.
 */
#include "cJSON.h"
#include "thingsos/runtime.h"
#include "thingsos/weidu_readtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#ifdef _WIN32
#include <direct.h>
#define mkdir_test(p) _mkdir(p)
#else
#define mkdir_test(p) mkdir(p, 0775)
#endif
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "readtime line %d: %s\n", __LINE__, #x);                               \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
static const char *owner = "0123456789abcdef0123456789abcdef";
static const char *other = "1123456789abcdef0123456789abcdef";
static const uint32_t hour = 1794002400u; /* integer hour, independent of local TZ */
static unsigned hook_ms;
static void interval(void *context, uint64_t start, uint32_t ms) {
    (void)context;
    (void)start;
    hook_ms += ms;
}
static thingsos_rt_row_t row(uint64_t id, uint32_t h, uint32_t seconds) {
    thingsos_rt_row_t r = {0};
    r.id = id;
    r.hour = h;
    r.seconds = seconds;
    r.until = h + seconds;
    r.zone = 480;
    strcpy(r.book, "99");
    return r;
}
static void write_copy(const char *from, const char *to) {
    FILE *f = fopen(from, "rb"), *g = fopen(to, "wb");
    CHECK(f && g);
    int c;
    while ((c = fgetc(f)) != EOF)
        CHECK(fputc(c, g) != EOF);
    fclose(f);
    CHECK(!fclose(g));
}
int main(int argc, char **argv) {
    CHECK(argc == 2);
    mkdir_test(argv[1]);
    CHECK(hour % 3600 == 0);
    char root[640];
    snprintf(root, sizeof(root), "%s/rt-run-%llu", argv[1], (unsigned long long)time(NULL));
    mkdir_test(root);
    thingsos_rt_batch_t *b = calloc(1, sizeof(*b));
    CHECK(b);
    thingsos_rt_summary_t summary;
    CHECK(thingsos_rt_summary(root, owner, &summary) && !summary.rows);
    thingsos_rt_row_t r = row(1, hour, 30);
    CHECK(thingsos_rt_put(root, owner, &r));
    CHECK(thingsos_rt_put(root, owner, &r));
    r.seconds = 31;
    CHECK(!thingsos_rt_put(root, owner, &r));
    r.seconds = 30;
    CHECK(thingsos_rt_summary(root, owner, &summary) && summary.pending == 30 && summary.rows == 1);
    CHECK(thingsos_rt_summary(root, other, &summary) && !summary.rows);
    CHECK(!thingsos_rt_put(root, "../../bad", &r));
    CHECK(thingsos_rt_peek(root, owner, hour + 20, b) && !b->count);
    CHECK(thingsos_rt_peek(root, owner, hour + 100, b) && b->count == 1);
    /* No claim for local cooldown / preflight failure: a fresh reader can
       still select exactly the same pending record. */
    CHECK(thingsos_rt_peek(root, owner, hour + 100, b) && b->count == 1);
    CHECK(thingsos_rt_claim(root, owner, b));
    CHECK(thingsos_rt_summary(root, owner, &summary) && !summary.pending && summary.unknown == 30);
    thingsos_rt_batch_t *again = calloc(1, sizeof(*again));
    CHECK(again);
    CHECK(thingsos_rt_peek(root, owner, hour + 100, again) && !again->count);
    /* Simulate interrupted state transition: both names survive. .sent wins,
       including after constructing a completely new batch/caller. */
    char sent[800], pending[800];
    snprintf(sent, sizeof(sent), "%s/readtime-%s/%016llx.sent", root, owner, 1ull);
    snprintf(pending, sizeof(pending), "%s/readtime-%s/%016llx.pending", root, owner, 1ull);
    write_copy(sent, pending);
    CHECK(thingsos_rt_peek(root, owner, hour + 100, again) && !again->count);
    CHECK(thingsos_rt_summary(root, owner, &summary) && summary.unknown == 30 && !summary.pending);
    CHECK(thingsos_rt_confirm(root, owner, b));
    CHECK(thingsos_rt_summary(root, owner, &summary) && !summary.rows);

    /* #184/#187: 3690 seconds / 123 fragments must be one historical batch,
       never the obsolete 600-second / 100-fragment cap. */
    for (unsigned i = 0; i < 123; ++i) {
        r = row(100 + i, hour + (i / 120) * 3600, 30);
        CHECK(thingsos_rt_put(root, owner, &r));
    }
    CHECK(thingsos_rt_peek(root, owner, hour + 7200, b) && b->count == 123);
    unsigned sum = 0;
    for (unsigned i = 0; i < b->count; ++i)
        sum += b->rows[i].seconds;
    CHECK(sum == 3690);
    thingsos_rt_credentials_t credentials = {0};
    strcpy(credentials.vid, "123");
    strcpy(credentials.device, "eink-test");
    char body[8192], hash[65];
    CHECK(thingsos_rt_body(&credentials, b, "5ecdcfd7f", "test-config-token",
                           ((uint64_t)hour + 7200) * 1000 + 123, 821, 44, 480, body, sizeof(body)));
    cJSON *json = cJSON_Parse(body);
    CHECK(json);
    cJSON *book = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(json, "books"), 0);
    CHECK(cJSON_GetObjectItemCaseSensitive(book, "readingTime")->valueint == 3690);
    CHECK(!cJSON_GetObjectItemCaseSensitive(book, "chapterUid") &&
          !cJSON_GetObjectItemCaseSensitive(book, "chapterOffset"));
    CHECK(!cJSON_GetObjectItemCaseSensitive(json, "recordCreateTimeZone"));
    CHECK(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(book, "finish")));
    CHECK(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(book, "hours")) == 2);
    cJSON_Delete(json);
    CHECK(thingsos_rt_claim(root, owner, b) && thingsos_rt_confirm(root, owner, b));

    /* Extreme fragmentation is bounded without consuming the legitimate
       remainder or classifying unsent fragments as unknown. */
    for (unsigned i = 0; i < 520; ++i) {
        r = row(1000 + i, hour, 1);
        CHECK(thingsos_rt_put(root, owner, &r));
    }
    CHECK(thingsos_rt_peek(root, owner, hour + 7200, b) && b->count == WD_RT_BATCH);
    CHECK(thingsos_rt_claim(root, owner, b) && thingsos_rt_confirm(root, owner, b));
    CHECK(thingsos_rt_peek(root, owner, hour + 7200, b) && b->count == 8);
    CHECK(thingsos_rt_claim(root, owner, b) && thingsos_rt_confirm(root, owner, b));

    /* Capture keeps subsecond remainder and splits midnight/hour crossings. */
    thingsos_rt_capture_t capture = {0};
    capture.nonce = 42;
    CHECK(thingsos_rt_capture(&capture, owner, "99", ((uint64_t)hour + 3599) * 1000 + 500, 1000,
                              480, 0));
    CHECK(capture.count == 2 && capture.rows[0].milliseconds == 500 &&
          capture.rows[1].milliseconds == 500);
    CHECK(thingsos_rt_flush(&capture, root) && capture.count == 2);
    CHECK(thingsos_rt_capture(&capture, owner, "99", ((uint64_t)hour + 3600) * 1000 + 500, 500, 480,
                              0));
    CHECK(thingsos_rt_flush(&capture, root));
    CHECK(thingsos_rt_summary(root, owner, &summary) && summary.pending == 1);
    CHECK(capture.count == 1 && capture.rows[0].milliseconds == 500);
    CHECK(thingsos_rt_peek(root, owner, hour + 7200, b) && b->count == 1);
    CHECK(thingsos_rt_claim(root, owner, b) && thingsos_rt_confirm(root, owner, b));
    CHECK(!thingsos_rt_capture(&capture, owner, "99", 100, 1000, 480, 0));
    CHECK(!thingsos_rt_capture(&capture, owner, "99", (uint64_t)hour * 1000, 6000, 480, 0));

    /* Future intervals in the same hour must also wait for clock recovery. */
    r = row(8000, hour, 10);
    r.until = hour + 3000;
    CHECK(thingsos_rt_put(root, owner, &r));
    CHECK(thingsos_rt_peek(root, owner, hour + 100, b) && !b->count);
    CHECK(thingsos_rt_peek(root, owner, hour + 7200, b) && b->count == 1);
    CHECK(thingsos_rt_claim(root, owner, b) && thingsos_rt_confirm(root, owner, b));
    /* The later upstream overlap repair retains anomalous source and lets
       healthy records continue rather than stalling every book. */
    for (unsigned i = 0; i < 61; ++i) {
        r = row(8100 + i, hour, 60);
        CHECK(thingsos_rt_put(root, owner, &r));
    }
    CHECK(thingsos_rt_peek(root, owner, hour + 7200, b) && b->count == 60);
    CHECK(thingsos_rt_summary(root, owner, &summary) && summary.quarantined == 60 &&
          summary.pending == 3600);
    CHECK(thingsos_rt_claim(root, owner, b) && thingsos_rt_confirm(root, owner, b));
    CHECK(thingsos_rt_peek(root, owner, hour + 7200, b) && !b->count);
    /* Multi-day backlog respects both the hour-group and 24h span bounds. */
    for (unsigned i = 0; i < 25; ++i) {
        r = row(8200 + i, hour + i * 86400, 60);
        CHECK(thingsos_rt_put(root, owner, &r));
    }
    unsigned days = 0, total_days = 0;
    do {
        CHECK(thingsos_rt_peek(root, owner, hour + 30 * 86400, b));
        if (!b->count)
            break;
        CHECK(b->count <= WD_RT_HOURS);
        for (unsigned i = 0; i < b->count; ++i)
            total_days += b->rows[i].seconds;
        CHECK(thingsos_rt_claim(root, owner, b) && thingsos_rt_confirm(root, owner, b));
        ++days;
    } while (days < 30);
    CHECK(days == 25 && total_days == 1500);
    /* Private uploads use CB_... IDs, not only decimal book identifiers. */
    r=row(8700,hour,60);strcpy(r.book,"CB_00K1XI1aSDlZ76f75hG9n7JG");
    CHECK(thingsos_rt_put(root,owner,&r));
    CHECK(thingsos_rt_peek(root,owner,hour+7200,b) && b->count==1);
    CHECK(thingsos_rt_body(&credentials,b,"5ecdcfd7f","test-config-token",((uint64_t)hour+7200)*1000,821,44,480,body,sizeof(body)));
    CHECK(strstr(body,"CB_00K1XI1aSDlZ76f75hG9n7JG"));
    CHECK(thingsos_rt_claim(root,owner,b) && thingsos_rt_confirm(root,owner,b));
    /* Preserve corrupt source and fail closed; never reset the outbox. */
    r = row(9000, hour, 1);
    CHECK(thingsos_rt_put(root, owner, &r));
    snprintf(pending, sizeof(pending), "%s/readtime-%s/%016llx.pending", root, owner, 9000ull);
    FILE *bad = fopen(pending, "wb");
    CHECK(bad);
    fputs("damaged", bad);
    fclose(bad);
    CHECK(thingsos_rt_summary(root, owner, &summary) && summary.damaged);
    CHECK(!thingsos_rt_peek(root, owner, hour + 7200, b));
    r.id = 9001;
    CHECK(thingsos_rt_put(root, owner, &r)); /* a separate file cannot overwrite damaged source */
    CHECK(thingsos_rt_summary(root, owner, &summary) && summary.damaged);

    CHECK(thingsos_rt_sha256("abc", hash) &&
          !strcmp(hash, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    const char *parts[] = {
        "5ecdcfd7f", "821", "1792318161",
        "393727764_35645832171399234841676621530100678335_35645832171399234841676621530100678335_"
        "CB_00K1XI1aSDlZ76f75hG9n7JG_0_+08:00_81_0_1792317600_+08:00_81_0"};
    CHECK(thingsos_rt_sign(parts, 4, hash) &&
          !strcmp(hash, "dfd6f2ceb1e4a5d1152c6d19fd9c9f84115cad69989ee799faec377d05d8a833"));
    thingsos_runtime_t runtime;
    thingsos_runtime_init(&runtime);
    runtime.reading_interval = interval;
    thingsos_runtime_navigate(&runtime, THINGSOS_READING, 1000);
    thingsos_runtime_tick(&runtime, 2000);
    thingsos_runtime_overlay(&runtime, true, 2500);
    thingsos_runtime_tick(&runtime, 4500);
    CHECK(hook_ms == 1500 && runtime.reading_ms == hook_ms);
    thingsos_runtime_overlay(&runtime, false, 5000);
    thingsos_runtime_tick(&runtime, 100000);
    CHECK(hook_ms == 1500); /* blocked/sleep gap excluded */
    free(b);
    free(again);
    puts("PASS: durable offline time, send intent, #184/#187 long batches, binary signature vector "
         "and active intervals");
    return 0;
}
