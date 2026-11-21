/* SPDX-License-Identifier: Apache-2.0
 * Executes the actual device transport through an injected ESP HTTP boundary.
 * No server calls or account credentials; tests durable dispatch order.
 */
#include "cJSON.h"
#include "thingsos/mem.h"
#include "thingsos/weidu.h"
#include "thingsos/weidu_readtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <ctype.h>
#ifdef _WIN32
#include <direct.h>
#define mkdir_test(p) _mkdir(p)
#else
#define mkdir_test(p) mkdir(p, 0775)
#endif
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "transport line %d: %s\n", __LINE__, #x);                              \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
static bool abort_requested;
static thingsos_weidu_client_t object;
static thingsos_weidu_client_t *client = &object;
static struct {
    struct {
        char owner[33];
    } reading;
} workspace;
#define work (&workspace)
typedef int esp_err_t;
enum { ESP_OK = 0, ESP_FAIL = -1, HTTP_METHOD_GET, HTTP_METHOD_POST };
enum { MBEDTLS_ERR_SSL_ALLOC_FAILED = -141 };
typedef struct {
    void *user_data;
} esp_http_client_event_t;
typedef struct {
    const char *url;
    int timeout_ms, method;
    bool disable_auto_redirect;
    void *crt_bundle_attach;
    esp_err_t (*event_handler)(esp_http_client_event_t *);
    void *user_data;
} esp_http_client_config_t;
typedef void *esp_http_client_handle_t;
static void *esp_crt_bundle_attach;
static unsigned opens, writes, closed, initializations;
static int fail_open, fail_write, http_status, reply_offset;
static const char *reply;
static bool posted;
static esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config) {
    CHECK(config->disable_auto_redirect);
    ++initializations;
    posted = config->method == HTTP_METHOD_POST;
    return (void *)1;
}
static int esp_http_client_set_header(void *h, const char *key, const char *value) {
    (void)h;
    (void)value;
    CHECK(strcmp(key, "Cookie"));
    return 0;
}
static int esp_http_client_open(void *h, int length) {
    (void)h;
    (void)length;
    ++opens;
    return fail_open ? -1 : 0;
}
static esp_err_t esp_http_client_get_and_clear_last_tls_error(void *h,int *tls_error,int *tls_flags) {
    (void)h;
    CHECK(tls_error && !tls_flags);
    *tls_error=fail_open==2?-MBEDTLS_ERR_SSL_ALLOC_FAILED:0;
    return ESP_OK;
}
static int esp_http_client_write(void *h, const char *body, int length) {
    (void)h;
    (void)body;
    CHECK(posted);
    ++writes;
    /* Every body byte must be preceded by persisted send intent. */
    thingsos_rt_summary_t status;
    CHECK(thingsos_rt_summary(client->root, work->reading.owner, &status));
    CHECK(status.unknown && !status.pending);
    return fail_write ? -1 : length;
}
static int esp_http_client_fetch_headers(void *h) {
    (void)h;
    reply_offset = 0;
    return (int)strlen(reply);
}
static int esp_http_client_read(void *h, char *out, int cap) {
    (void)h;
    int n = (int)strlen(reply) - reply_offset;
    if (n > cap)
        n = cap;
    memcpy(out, reply + reply_offset, (size_t)n);
    reply_offset += n;
    return n;
}
static int esp_http_client_get_status_code(void *h) {
    (void)h;
    return http_status;
}
static void esp_http_client_close(void *h) {
    (void)h;
    ++closed;
}
static void esp_http_client_cleanup(void *h) { (void)h; }
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#include "../firmware/components/thingsos_weidu/eink_readtime_transport.inc"

/* Transactional fake of only the NVS boundary; the actual total/ACK logic
   below is the same include used by the device uploader. */
typedef unsigned nvs_handle_t;
enum {NVS_READONLY,NVS_READWRITE,ESP_ERR_NVS_NOT_FOUND=10};
static struct {char space[16];unsigned char blob[1024],staged[1024],legacy[112];size_t bytes,staged_bytes,legacy_bytes;} total_nvs[4];
static bool total_commit_fail,total_set_fail;
static unsigned total_commits;
static esp_err_t nvs_open(const char* space,int mode,nvs_handle_t* out) {
    for(unsigned i=0;i<4;++i)if(!strcmp(total_nvs[i].space,space)){*out=i;return ESP_OK;}
    if(mode==NVS_READONLY)return ESP_ERR_NVS_NOT_FOUND;
    for(unsigned i=0;i<4;++i)if(!total_nvs[i].space[0]){strcpy(total_nvs[i].space,space);*out=i;return ESP_OK;}
    return ESP_FAIL;
}
static esp_err_t nvs_get_blob(nvs_handle_t h,const char* key,void* out,size_t* size) {
    bool legacy=!strcmp(key,"total-v1");CHECK(legacy||!strcmp(key,"total-v2"));
    size_t bytes=legacy?total_nvs[h].legacy_bytes:total_nvs[h].bytes;
    if(!bytes)return ESP_ERR_NVS_NOT_FOUND;
    if(*size<bytes)return ESP_FAIL;
    *size=bytes;memcpy(out,legacy?total_nvs[h].legacy:total_nvs[h].blob,bytes);return ESP_OK;
}
static esp_err_t nvs_set_blob(nvs_handle_t h,const char* key,const void* in,size_t size) {
    CHECK(!strcmp(key,"total-v2")&&size<=1024);
    if(total_set_fail)return ESP_FAIL;
    memcpy(total_nvs[h].staged,in,size);total_nvs[h].staged_bytes=size;return ESP_OK;
}
static esp_err_t nvs_commit(nvs_handle_t h) {
    ++total_commits;if(total_commit_fail)return ESP_FAIL;
    total_nvs[h].bytes=total_nvs[h].staged_bytes;
    memcpy(total_nvs[h].blob,total_nvs[h].staged,total_nvs[h].bytes);return ESP_OK;
}
static void nvs_close(nvs_handle_t h){total_nvs[h].staged_bytes=0;}
#include "../firmware/components/thingsos_weidu/eink_readtime_totals.inc"

static void uploaded_total_test(void) {
    const char* a="2123456789abcdef0123456789abcdef";
    const char* b="3123456789abcdef0123456789abcdef";
    const char* collision="2123456789afffff0123456789abcdef";
    uint64_t total=99;char last[65];
    CHECK(rt_uploaded_load(a,&total,last)&&!total&&!last[0]);
    thingsos_rt_batch_t* batch=calloc(1,sizeof(*batch));CHECK(batch);
    batch->count=2;for(unsigned i=0;i<2;++i){batch->rows[i].hour=1794002400;batch->rows[i].zone=480;}batch->rows[0].id=99;batch->rows[0].seconds=30;
    batch->rows[1].id=100;batch->rows[1].seconds=369;
    const char* rejected[]={"{}","{\"succ\":false}","{\"succ\":0}","{\"succ\":\"1\"}"};
    for(unsigned i=0;i<4;++i) {
        cJSON* ack=cJSON_Parse(rejected[i]);CHECK(ack);
        CHECK(rt_uploaded_commit(a,batch,ack,&total)==WD_UNCONFIRMED);cJSON_Delete(ack);
    }
    CHECK(!total_commits);
    cJSON* ack=cJSON_Parse("{\"succ\":true}");CHECK(ack);
    CHECK(rt_uploaded_commit(a,batch,ack,&total)==WD_OK && total==399);
    /* A new reader after restart reads the committed total, not RAM state. */
    total=0;CHECK(rt_uploaded_load(a,&total,last)&&total==399);
    unsigned commits=total_commits;
    thingsos_rt_row_t swap=batch->rows[0];batch->rows[0]=batch->rows[1];batch->rows[1]=swap;
    CHECK(rt_uploaded_commit(a,batch,ack,&total)==WD_OK && total==399 && total_commits==commits);
    CHECK(rt_uploaded_load(b,&total,last)&&!total); /* separate account */
    CHECK(!rt_uploaded_load(collision,&total,last)); /* full owner guards namespace prefix */
    batch->rows[0].id=101;
    total_commit_fail=true;
    CHECK(rt_uploaded_commit(a,batch,ack,&total)==WD_STORAGE);
    total_commit_fail=false;
    CHECK(rt_uploaded_load(a,&total,last)&&total==399);
    total_set_fail=true;CHECK(rt_uploaded_commit(a,batch,ack,&total)==WD_STORAGE);
    total_set_fail=false;
    CHECK(rt_uploaded_commit(a,batch,ack,&total)==WD_OK&&total==798);
    CHECK(rt_uploaded_commit(b,batch,ack,&total)==WD_OK&&total==399);
    /* Counter survives beyond 32-bit seconds and fails closed on corruption. */
    unsigned h=0;while(strcmp(total_nvs[h].space,"rtu-2123456789a"))++h;
    rt_total_put(total_nvs[h].blob+4,(uint64_t)UINT32_MAX+100,8);
    rt_total_put(total_nvs[h].blob+RT_TOTAL_BYTES-4,crc32(0,total_nvs[h].blob,RT_TOTAL_BYTES-4),4);
    batch->rows[0].id=102;
    CHECK(rt_uploaded_commit(a,batch,ack,&total)==WD_OK&&total==(uint64_t)UINT32_MAX+499);
    total_nvs[h].blob[4]^=1;
    CHECK(!rt_uploaded_load(a,&total,last));
    CHECK(rt_uploaded_commit(a,batch,ack,&total)==WD_STORAGE);
    free(batch);cJSON_Delete(ack);memset(total_nvs,0,sizeof(total_nvs));total_commits=0;
    puts("PASS: acknowledged-only cumulative totals, account isolation, restart, idempotent receipt and NVS failure");
}

static void daily_total_test(void) {
    const char* owner="4123456789abcdef0123456789abcdef";
    uint32_t hour=1794002400,day=rt_reading_day(hour,480),midnight=day*86400-28800;
    uint64_t web,native,total;char last[65];
    CHECK(rt_daily_read(owner,hour,&web,&native)&&!web&&!native);
    thingsos_rt_batch_t* batch=calloc(1,sizeof(*batch));CHECK(batch);
    batch->count=2;
    batch->rows[0]=(thingsos_rt_row_t){.id=1,.hour=hour-86400,.seconds=40,.zone=480};
    batch->rows[1]=(thingsos_rt_row_t){.id=2,.hour=hour,.seconds=120,.zone=480};
    cJSON* ack=cJSON_Parse("{\"succ\":1}");CHECK(ack);
    CHECK(rt_uploaded_commit(owner,batch,ack,&total)==WD_OK&&total==160);
    CHECK(rt_daily_read(owner,hour,&web,&native)&&native==120&&!web);
    CHECK(rt_daily_read(owner,hour-86400,&web,&native)&&native==40&&!web);
    CHECK(rt_web_commit(owner,"99","session-A",hour+90,60)==WD_OK);
    CHECK(rt_daily_read(owner,hour,&web,&native)&&web==60&&native==120);
    unsigned commits=total_commits;
    CHECK(rt_web_commit(owner,"99","session-A",hour+90,60)==WD_OK&&total_commits==commits);
    CHECK(rt_web_commit(owner,"99","session-B",midnight+20,60)==WD_OK);
    CHECK(rt_daily_read(owner,hour,&web,&native)&&web==80&&native==120);
    CHECK(rt_daily_read(owner,hour-86400,&web,&native)&&web==40&&native==40);
    total_commit_fail=true;
    CHECK(rt_web_commit(owner,"99","session-C",hour+180,30)==WD_STORAGE);
    total_commit_fail=false;
    CHECK(rt_daily_read(owner,hour,&web,&native)&&web==80&&native==120);
    CHECK(rt_uploaded_load(owner,&total,last)&&total==160);
    CHECK(rt_web_commit(owner,"99","u32-boundary",UINT32_MAX,10)==WD_OK);
    CHECK(rt_daily_read(owner,UINT32_MAX,&web,&native)&&web==10&&!native);
    /* A legacy total is preserved, never invented as today's reading. */
    const char* old="5123456789abcdef0123456789abcdef";nvs_handle_t h;
    CHECK(nvs_open("rtu-5123456789a",NVS_READWRITE,&h)==ESP_OK);
    unsigned char* wire=total_nvs[h].legacy;memcpy(wire,"WRS1",4);rt_total_put(wire+4,9999,8);
    memcpy(wire+12,old,32);memset(wire+44,'0',64);rt_total_put(wire+108,crc32(0,wire,108),4);total_nvs[h].legacy_bytes=112;
    CHECK(rt_uploaded_load(old,&total,last)&&total==9999);
    CHECK(rt_daily_read(old,hour,&web,&native)&&!web&&!native);
    CHECK(rt_uploaded_commit(old,batch,ack,&total)==WD_OK&&total==10159);
    CHECK(rt_daily_read(old,hour,&web,&native)&&native==120&&!web);
    /* More than a month's historic receipts do not evict a newer day. */
    rt_total_state_t state={0};CHECK(rt_day_add(&state,day,123,false));
    for(unsigned i=1;i<50;++i)CHECK(rt_day_add(&state,day-i,1,false));
    bool found=false;for(unsigned i=0;i<RT_DAY_SLOTS;++i)if(state.days[i].day==day){CHECK(state.days[i].native==123);found=true;}
    CHECK(found);
    cJSON_Delete(ack);free(batch);memset(total_nvs,0,sizeof(total_nvs));total_commits=0;
    puts("PASS: online + Native daily totals, historical-date backfill, midnight splitting, dedupe, restart, legacy migration and failed storage");
}

static void reset(void) {
    opens = writes = closed = initializations = 0;
    fail_open = fail_write = 0;
    http_status = 200;
    reply = "{\"succ\":1}";
    abort_requested = false;
}
int main(int argc, char **argv) {
    CHECK(argc == 2);
    uploaded_total_test();
    daily_total_test();
    snprintf(client->root, sizeof(client->root), "%s/rt-transport-%llu", argv[1],
             (unsigned long long)time(NULL));
    mkdir_test(client->root);
    strcpy(work->reading.owner, "0123456789abcdef0123456789abcdef");
    thingsos_rt_batch_t *batch = calloc(1, sizeof(*batch));
    CHECK(batch);
    batch->count = 1;
    thingsos_rt_row_t *row = &batch->rows[0];
    row->id = 1;
    row->hour = 1794002400;
    row->seconds = 30;
    row->until = row->hour + 30;
    row->zone = 480;
    strcpy(row->book, "99");
    CHECK(thingsos_rt_put(client->root, work->reading.owner, row));
    thingsos_rt_summary_t status;
    cJSON *result = NULL;
    /* The actual SDK endpoint includes a base64 QR image (~68 KiB), even
       though the device needs only uuid and renders its own QR locally. */
    const char* qr_prefix="{\"errcode\":0,\"qrcode\":{\"image\":\"";
    const char* qr_suffix="\"},\"uuid\":\"test_qr_uuid\"}";
    size_t image_bytes=70*1024,large_bytes=strlen(qr_prefix)+image_bytes+strlen(qr_suffix);
    char* large_qr=malloc(large_bytes+1);CHECK(large_qr);
    memcpy(large_qr,qr_prefix,strlen(qr_prefix));memset(large_qr+strlen(qr_prefix),'A',image_bytes);
    strcpy(large_qr+strlen(qr_prefix)+image_bytes,qr_suffix);
    reset();reply=large_qr;
    CHECK(rt_http("https://open.weixin.qq.com/connect/sdk/qrconnect?fixture=1",NULL,NULL,NULL,&result)==WD_OK);
    CHECK(!strcmp(cJSON_GetObjectItemCaseSensitive(result,"uuid")->valuestring,"test_qr_uuid"));
    CHECK(opens==1&&closed==1&&!writes);cJSON_Delete(result);result=NULL;
    reset();reply=large_qr;
    CHECK(rt_http("https://i.weread.qq.com/wxticket?fixture=1",NULL,NULL,NULL,&result)==WD_LIMIT);
    CHECK(!result&&opens==1&&closed==1&&!writes); /* Ordinary budget is unchanged. */
    free(large_qr);
    size_t over_bytes=128*1024+1024;
    char* oversized=malloc(over_bytes+1);CHECK(oversized);memset(oversized,'A',over_bytes);oversized[over_bytes]=0;
    reset();reply=oversized;
    CHECK(rt_http("https://open.weixin.qq.com/connect/sdk/qrconnect?fixture=1",NULL,NULL,NULL,&result)==WD_LIMIT);
    CHECK(!result&&opens==1&&closed==1&&!writes);free(oversized);
    reset();
    abort_requested = true;
    CHECK(rt_http("https://i.weread.qq.com/book/batchUploadProgress", "{}", NULL, batch, &result) ==
          WD_CANCEL);
    CHECK(!initializations && !writes);
    reset();
    CHECK(rt_http("https://untrusted.example/", "{}", NULL, batch, &result) == WD_PROTOCOL &&
          !initializations);
    reset();
    fail_open = 1;
    CHECK(rt_http("https://i.weread.qq.com/book/batchUploadProgress", "{}", NULL, batch, &result) ==
          WD_NETWORK);
    CHECK(thingsos_rt_summary(client->root, work->reading.owner, &status) && status.pending == 30 &&
          !status.unknown && !writes);
    reset();
    fail_open = 2;
    CHECK(rt_http("https://i.weread.qq.com/book/batchUploadProgress", "{}", NULL, batch, &result) ==
          WD_MEMORY);
    CHECK(thingsos_rt_summary(client->root, work->reading.owner, &status) && status.pending == 30 &&
          !status.unknown && !writes);
    reset();
    fail_write = 1;
    CHECK(rt_http("https://i.weread.qq.com/book/batchUploadProgress", "{}", NULL, batch, &result) ==
          WD_NETWORK);
    CHECK(writes == 1 && opens == 1 && closed == 1);
    CHECK(thingsos_rt_summary(client->root, work->reading.owner, &status) && status.unknown == 30 &&
          !status.pending);
    reset();
    CHECK(rt_http("https://i.weread.qq.com/book/batchUploadProgress", "{}", NULL, batch, &result) ==
              WD_STORAGE &&
          !writes);
    /* No reply, rejection, rate-limit and success are never automatically
       retried. Only the explicit success path may acknowledge the source. */
    CHECK(thingsos_rt_confirm(client->root, work->reading.owner, batch));
    for (unsigned i = 0; i < 4; ++i) {
        row->id = 2 + i;
        CHECK(thingsos_rt_put(client->root, work->reading.owner, row));
        reset();
        if (i == 0)
            reply = "{\"succ\":false}";
        if (i == 1)
            http_status = 429;
        if (i == 2) {
            http_status = 401;
            reply = "{\"errCode\":-2012}";
        }
        thingsos_weidu_error_t e =
            rt_http("https://i.weread.qq.com/book/batchUploadProgress", "{}", NULL, batch, &result);
        CHECK(opens == 1 && writes == 1);
        CHECK(e == (i == 1 ? WD_LIMIT : i == 2 ? WD_AUTH : WD_OK));
        CHECK(thingsos_rt_summary(client->root, work->reading.owner, &status) &&
              status.unknown == 30 && !status.pending);
        uint64_t uploaded=0;char receipt[65];
        if(e==WD_OK)CHECK(rt_uploaded_commit(work->reading.owner,batch,result,&uploaded)==
                          (i==3?WD_OK:WD_UNCONFIRMED));
        CHECK(rt_uploaded_load(work->reading.owner,&uploaded,receipt)&&uploaded==(i==3?30:0));
        if (i == 0)
            CHECK(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(result, "succ")));
        cJSON_Delete(result);
        result = NULL;
        CHECK(thingsos_rt_confirm(client->root, work->reading.owner,
                                  batch)); /* explicit test cleanup */
    }
    free(batch);
    puts("PASS: real E-Ink transport claims before body, preserves unsent connect failures, never "
         "retries uncertain POSTs or follows redirects");
    return 0;
}
