/* SPDX-License-Identifier: Apache-2.0 */
#include "weidu_device.h"
#include "thingsos/diagnostics.h"
#include "thingsos/memory_trace.h"
#include <stdatomic.h>
#include <errno.h>
#include "lwip/dns.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "lwip/tcpip.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_transport.h"
#include "esp_transport_ssl.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_tls_errors.h"
#include "esp_wifi.h"
#include "mbedtls/ssl.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/idf_additions.h"
#include "network.h"
#include "nvs.h"
#include "board.h"
#include "thingsos/mem.h"
static portMUX_TYPE guard = portMUX_INITIALIZER_UNLOCKED;
static thingsos_weidu_client_t* client;
typedef struct {
    bool valid;
    char owner[33];
    uint64_t generation;
    thingsos_wd_upload_t upload;
    thingsos_wd_read_session_t session;
} deferred_read_t;
/* Per-cover retry bookkeeping: at most three attempts per session, 30 s apart. */
typedef struct { uint32_t key; uint8_t tries; int64_t at; } cover_attempt_t;
/* Ordinary business data is never used by display/DMA/interrupt code. */
typedef struct {
    thingsos_weidu_status_t public_status;
    thingsos_weidu_book_t requested_book,cover_books[9];
    cover_attempt_t cover_attempts[64];
    char runtime_cookie[2048], fingerprint[16];
    char search_query[97], online_search_query[97];
    thingsos_weidu_book_t online_search_books[20];
    unsigned online_search_count;
    bool online_search_member[20];
    bool online_search_ready;
    char reader_pc[128],reader_token[128];
    char previous_ps[128];
    bool have_previous_ps;
    unsigned online_index,online_count;
    bool online_prepare_cover;
    char mp_ticket[512],mp_wrpa[256];
    thingsos_wd_device_status_t reading;
    thingsos_wd_diagnostic_t request_diagnostic;
    thingsos_wd_upload_t upload, pending;
    thingsos_wd_read_session_t read_session;
    thingsos_wd_thought_query_t thought_query;
    thingsos_annotation_t annotation;
    uint64_t annotation_book;
    char annotation_owner[33];
    char pending_owner[33];
    bool pending_valid;
    deferred_read_t deferred[8];
    uint64_t queued_generation, queue_retry_at;
    char queued_owner[33];
    thingsos_wd_read_session_t queued_session;
    /* Worker-only scratch (one worker at a time): keeps a session copy and
       path buffers off the 12 KiB internal worker stack. */
    thingsos_weidu_session_t logout_previous;
    char worker_path[640];
} weidu_work_t;
static weidu_work_t* work;
static thingsos_weidu_action_t requested_action;
static size_t cover_count;
static unsigned cover_serial;
unsigned thingsos_weidu_device_cover_serial(void){return cover_serial;}
static unsigned cover_attempt_next;
static atomic_bool abort_requested, active;
static atomic_uint cancel_requested_at;
static bool reading_action_is(thingsos_weidu_action_t a){return (a>=WD_DEVICE_NOTES && a<=WD_DEVICE_THOUGHTS) || a==WD_DEVICE_ENTER || a==WD_DEVICE_REPORT || a==WD_DEVICE_PERSONAL || a==WD_DEVICE_NOTE_WRITE;}
/* 公众号正文落在独立目录，与 books/fonts/wallpapers 同级且同为英文名。 */
#define MP_OUTPUT "/sdcard/articles"
/* The coordinator and the reading path share the global Web connection. The
   two prefetch workers pass a prefetch_conn_t through fetch()'s context so each
   worker gets its own TLS client without disturbing the reading session. */
#define PREFETCH_CONN_MAGIC 0x57444650u
typedef struct {
    uint32_t magic;
    esp_http_client_handle_t connection;
    esp_transport_handle_t transport;
    atomic_bool* stop;
} prefetch_conn_t;
static prefetch_conn_t download_channel={.magic=PREFETCH_CONN_MAGIC};
static bool channel_stopped(void* context) {
    prefetch_conn_t* c=context;return c->stop && atomic_load(c->stop);
}
static void channel_close(prefetch_conn_t* c) {
    if(c->connection){esp_http_client_cleanup(c->connection);c->connection=NULL;}
    if(c->transport){esp_transport_destroy(c->transport);c->transport=NULL;}
}
/* runtime_cookie is shared by the coordinator and both prefetch readers. */
static SemaphoreHandle_t cookie_lock;
static bool reading_context_active;
static void reset_dns_cache(void* ignored) {(void)ignored;dns_clear_cache();}
/* DigiCert Global Root G2, from ESP-IDF's trusted Mozilla CA store. Use a real
   CA certificate and the standard chain verifier, not the compressed callback. */
extern const char weread_root_pem[] asm("_binary_weread_root_pem_start");
#include "radio_policy.inc"
static void publish(void* context, const thingsos_weidu_status_t* s) {
    (void)context;
    portENTER_CRITICAL(&guard);
    work->public_status = *s;
    work->public_status.busy = active;
    portEXIT_CRITICAL(&guard);
}
static bool session(void* context, thingsos_weidu_session_t* s, bool save) {
    (void)context;
    nvs_handle_t h;
    if (nvs_open("thingsos-weidu", save ? NVS_READWRITE : NVS_READONLY, &h) != ESP_OK) return false;
    bool ok;
    if (save) {
        esp_err_t e =
            s->vid[0] ? nvs_set_blob(h, "session", s, sizeof(*s)) : nvs_erase_key(h, "session");
        ok = (e == ESP_OK || e == ESP_ERR_NVS_NOT_FOUND) && nvs_commit(h) == ESP_OK;
    } else {
        size_t n = sizeof(*s);
        ok = nvs_get_blob(h, "session", s, &n) == ESP_OK && n == sizeof(*s) &&
             thingsos_weidu_session_valid(s);
        if (!ok) memset(s, 0, sizeof(*s));
    }
    nvs_close(h);
    return ok;
}
static bool cancelled(void* context) {
    (void)context;
    return abort_requested;
}
static uint64_t now_ms(void* context) {
    (void)context;
    return (uint64_t)esp_timer_get_time() / 1000;
}
#include "reader_transport.inc"
static uint32_t unix_time(void* context) {
    (void)context;
    time_t now = time(NULL);
    return now >= 1577836800 && now <= 4102444799 ? (uint32_t)now : 0;
}
static uint32_t random_value(void* context) {
    (void)context;
    return esp_random();
}
/* Core owns I/O; release it only while waiting or doing TLS/network work. */
static void wait_ms(void* context, unsigned ms) {
    (void)context;
    bool owned=thingsos_io_lock && xSemaphoreGetMutexHolder(thingsos_io_lock)==xTaskGetCurrentTaskHandle();
    if(owned)xSemaphoreGive(thingsos_io_lock);
    uint64_t until=now_ms(NULL)+ms;
    while(!abort_requested && now_ms(NULL)<until)vTaskDelay(1);
    if(owned)xSemaphoreTake(thingsos_io_lock,portMAX_DELAY);
}
/* Wait for the station to regain an IP before an idempotent replay. */
static bool link_wait(unsigned max_ms, atomic_bool* stop) {
    uint64_t until=now_ms(NULL)+max_ms;
    while(!thingsos_network_connected()) {
        if(abort_requested || (stop && atomic_load(stop)) || now_ms(NULL)>=until)return false;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return true;
}
static void wait_link(void* context, unsigned max_ms) {
    (void)context;
    if(thingsos_network_connected())return;
    bool owned=thingsos_io_lock && xSemaphoreGetMutexHolder(thingsos_io_lock)==xTaskGetCurrentTaskHandle();
    if(owned)xSemaphoreGive(thingsos_io_lock);
    bool back=link_wait(max_ms,NULL);
    ESP_LOGI("ThingsOS_weidu","Retry link wait restored=%d",back);
    if(owned)xSemaphoreTake(thingsos_io_lock,portMAX_DELAY);
}
static bool cookie_replace_locked(const char* key, const char* value) {
    if (strchr(value, '\r') || strchr(value, '\n') || strchr(value, ';')) return false;
    char* next = heap_caps_malloc(sizeof(work->runtime_cookie),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if (!next) return false;
    size_t used = 0;
    const char* p = work->runtime_cookie;
    size_t keylen = strlen(key);
    while (*p) {
        const char* end = strchr(p, ';');
        if (!end) end = p + strlen(p);
        if (strncmp(p, key, keylen) || p[keylen] != '=') {
            size_t n = (size_t)(end - p);
            if (used + n + 3 >= sizeof(work->runtime_cookie)) {
                free(next);
                return false;
            }
            if (used) {
                next[used++] = ';';
                next[used++] = ' ';
            }
            memcpy(next + used, p, n);
            used += n;
        }
        p = *end ? end + 1 : end;
        while (*p == ' ') ++p;
    }
    if (*value) {
        int n = snprintf(
            next + used, sizeof(work->runtime_cookie) - used, "%s%s=%s", used ? "; " : "", key, value);
        if (n < 0 || (size_t)n >= sizeof(work->runtime_cookie) - used) {
            free(next);
            return false;
        }
        used += (size_t)n;
    }
    next[used] = 0;
    memcpy(work->runtime_cookie, next, used + 1);
    free(next);
    return true;
}
static bool cookie_replace(const char* key, const char* value) {
    if (cookie_lock) xSemaphoreTake(cookie_lock, portMAX_DELAY);
    bool ok = cookie_replace_locked(key, value);
    if (cookie_lock) xSemaphoreGive(cookie_lock);
    return ok;
}
#include "reader_http_body.inc"
#include "reader_http_pump.inc"
#include "reader_dns.inc"
#include "cache_http_pump.inc"
#include "cache_dns.inc"
#define IMAGE_URL_BYTES 1152u
#define IMAGE_MAX_REDIRECTS 3u
static esp_err_t event(esp_http_client_event_t* e) {
    transfer_t* t = e->user_data;
    if(!t)return ESP_OK;
    if (abort_requested || transfer_stopped(t)) {
        t->error = WD_CANCEL;
        return ESP_FAIL;
    }
    if(t->image_redirect_ready)return ESP_OK;
    if(e->event_id==HTTP_EVENT_ON_CONNECTED)t->phase=3;
    if(e->event_id==HTTP_EVENT_HEADERS_SENT)t->phase=4;
    if(e->event_id==HTTP_EVENT_ON_HEADER || e->event_id==HTTP_EVENT_ON_HEADERS_COMPLETE)t->phase=5;
    if(e->event_id==HTTP_EVENT_ON_DATA)t->phase=6;
    if(t->image_redirect && e->event_id==HTTP_EVENT_ON_HEADERS_COMPLETE) {
        int status=esp_http_client_get_status_code(e->client);
        /* The new request needs only Location. Leave the redirect body unread. */
        t->image_redirect_ready=status==301 || status==302 || status==303 || status==307 || status==308;
        return ESP_OK;
    }
    if(t->stream && e->event_id==HTTP_EVENT_ON_DATA && e->data_len>0){
        reader_http_data(t,e->data,(size_t)e->data_len,esp_http_client_get_status_code(e->client));
        return t->error==WD_OK?ESP_OK:ESP_FAIL;
    }
    if(t->image_redirect && e->event_id==HTTP_EVENT_ON_HEADER && !strcasecmp(e->header_key,"Location") &&
       !thingsos_weidu_image_redirect_url(t->image_url,e->header_value,t->image_redirect,IMAGE_URL_BYTES))
        t->image_redirect[0]=0;
    if(e->event_id==HTTP_EVENT_ON_HEADER && t->session && t->mp_renewal &&
       (!strcasecmp(e->header_key,"x-wr-ticket") || !strcasecmp(e->header_key,"x-wrpa-0"))) {
        char* dst=!strcasecmp(e->header_key,"x-wr-ticket")?work->mp_ticket:work->mp_wrpa;
        size_t cap=dst==work->mp_ticket?sizeof(work->mp_ticket):sizeof(work->mp_wrpa);
        size_t n=strlen(e->header_value);
        if(n>=cap || strchr(e->header_value,'\r') || strchr(e->header_value,'\n')){t->error=WD_PROTOCOL;return ESP_FAIL;}
        memcpy(dst,e->header_value,n+1);
    }
    if (e->event_id == HTTP_EVENT_ON_HEADER && !strcasecmp(e->header_key, "Set-Cookie")) {
        if(!t->session)return ESP_OK;
        const char* equal = strchr(e->header_value, '=');
        if (!equal) return ESP_OK;
        size_t n = (size_t)(equal - e->header_value);
        if (n > 31) return ESP_OK;
        char key[32], value[512];
        memcpy(key, e->header_value, n);
        key[n] = 0;
        const char* end = strchr(equal + 1, ';');
        if (!end) end = equal + 1 + strlen(equal + 1);
        n = (size_t)(end - equal - 1);
        if (n >= sizeof(value)) {
            t->error = WD_PROTOCOL;
            return ESP_FAIL;
        }
        memcpy(value, equal + 1, n);
        value[n] = 0;
        if (strncmp(key, "wr_", 3)) return ESP_OK;
        if (!cookie_replace(key, value)) {
            t->error = WD_PROTOCOL;
            return ESP_FAIL;
        }
        char* dst = NULL;
        size_t cap = 0;
        if (!strcmp(key, "wr_vid")) {
            dst = t->session->vid;
            cap = sizeof(t->session->vid);
        }
        if (!strcmp(key, "wr_skey")) {
            dst = t->session->skey;
            cap = sizeof(t->session->skey);
        }
        if (!strcmp(key, "wr_rt")) {
            dst = t->session->rt;
            cap = sizeof(t->session->rt);
        }
        if (dst) {
            if (n >= cap) {
                t->error = WD_PROTOCOL;
                return ESP_FAIL;
            }
            memcpy(dst, value, n + 1);
        }
    }
    return ESP_OK;
}
static atomic_uint diagnostic_request_sequence;
static unsigned diagnostic_endpoint(const char* path) {
    if(strstr(path,"/web/book/chapter/"))return 1;
    if(strstr(path,"/web/reader/"))return 2;
    if(!strcmp(path,"/web/book/chapterInfos"))return 3;
    if(!strcmp(path,"/web/shelf/sync"))return 4;
    if(strstr(path,"/web/search/"))return 5;
    if(strstr(path,"/web/mp/"))return 6;
    if(!strcmp(path,"/web/book/read"))return 7;
    if(strstr(path,"/web/book/getProgress"))return 8;
    if(strstr(path,"/api/agent/"))return 9;
    return 0;
}
/* Per-request scratch kept in PSRAM, off the 12 KiB worker stack (and the
   prefetch stacks): one per call because prefetch workers fetch concurrently. */
typedef struct { char url[640]; thingsos_weidu_ps_t ps, pc, token; } fetch_scratch_t;
static thingsos_weidu_error_t fetch_impl_with(
    fetch_scratch_t* scratch,
    void* context, const char* method, const char* path, const char* body, const char* referer,
    const char* destination, size_t limit, thingsos_weidu_session_t* s, char* memory, size_t* bytes,
    bool extract_reader,const char* authorization) {
    char* url = scratch->url;
    thingsos_weidu_ps_t *ps = &scratch->ps, *pc = &scratch->pc, *token = &scratch->token;
    unsigned diagnostic_id=atomic_fetch_add(&diagnostic_request_sequence,1)+1;
    unsigned diagnostic_stage=diagnostic_endpoint(path);
    wifi_ap_record_t diagnostic_ap={0};
    int diagnostic_wifi=esp_wifi_sta_get_ap_info(&diagnostic_ap);
    thingsos_diag_event(DIAG_NET_WIFI,4,diagnostic_id,(unsigned)requested_action,diagnostic_wifi==ESP_OK?diagnostic_ap.rssi:0,diagnostic_wifi);
    prefetch_conn_t* pconn = context ? (prefetch_conn_t*)context : NULL;
    if (pconn && pconn->magic != PREFETCH_CONN_MAGIC) pconn = NULL;
    prefetch_conn_t* channel=pconn?pconn:&download_channel;
    esp_http_client_handle_t* held = &channel->connection;
    if (path[0] != '/' || abort_requested) return abort_requested ? WD_CANCEL : WD_PROTOCOL;
    bool mp_renewal=!strcmp(path,"/web/login/renewal");
    /* Never mix a previous response's ticket with a new response's wrpa value.
       Reader/thought endpoints can return headers with these same names. */
    if(mp_renewal)work->mp_ticket[0]=work->mp_wrpa[0]=0;
    if (s && (!cookie_replace("wr_fp",work->fingerprint) || !cookie_replace("wr_vid", s->vid) ||
              !cookie_replace("wr_skey", s->skey) || !cookie_replace("wr_rt", s->rt)))
        return WD_PROTOCOL;
    bool personal_marks=!strcmp(path,"/api/agent/gateway");
    if(personal_marks && !authorization)return WD_ACCESS;
    /* Only the explicit official notes gateway uses the API key. Web cookies
       stay on their Web origin; gateway response cookies are ignored. */
    int n = snprintf(url, sizeof(scratch->url), "%s%s",personal_marks?"https://i.weread.qq.com":"https://weread.qq.com",path);
    if (n < 0 || (size_t)n >= sizeof(scratch->url)) return WD_LIMIT;
    bool serialized_reading=reading_action_is(requested_action);
    bool cache_async=requested_action==WD_DEVICE_DOWNLOAD || requested_action==WD_DEVICE_CHAPTER || requested_action==WD_DEVICE_RESTORE;
    if(cache_async) {
        thingsos_weidu_error_t dns=cache_dns_ready(personal_marks?"i.weread.qq.com":"weread.qq.com",channel_stopped,channel);
        thingsos_diag_event(DIAG_NET_DNS,diagnostic_stage,diagnostic_id,0,dns,0);
        if(dns!=WD_OK)return dns;
    }
    if (serialized_reading) {
        work->request_diagnostic.phase=1;
        thingsos_weidu_error_t dns = reader_dns_ready(personal_marks ? "i.weread.qq.com" : "weread.qq.com");
        thingsos_diag_event(DIAG_NET_DNS,diagnostic_stage,diagnostic_id,0,dns,0);
        if (dns != WD_OK) return dns;
    }
    transfer_t transfer = { .phase=2,.stream=serialized_reading || cache_async,.extract_reader=extract_reader,.memory=memory,
                            .cancelled=channel_stopped,.cancel_context=channel,
                            .file = destination ? fopen(destination, "wb") : NULL,
                            .limit = limit,
                            .budget_ms = cache_async && !strncmp(path,"/web/search/global?",sizeof("/web/search/global?")-1)?5000:0,
                            .error = WD_OK,
                            .session = personal_marks?NULL:s, .mp_renewal=mp_renewal };
    if (!memory && !transfer.file) return WD_STORAGE;
    char* file_buffer=transfer.file?heap_caps_malloc(16384,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT):NULL;
    if(file_buffer && setvbuf(transfer.file,file_buffer,_IOFBF,16384)){free(file_buffer);file_buffer=NULL;}
    int timeout_ms = !strncmp(path,"/api/auth/getLoginInfo?",sizeof("/api/auth/getLoginInfo?")-1) ||
                     !strcmp(path,"/web/shelf/sync") ? 60000 : 15000;
    /* Give every TOC attempt 25 s so a slow but valid response is not cut off. */
    if (!strcmp(path,"/web/book/chapterInfos")) timeout_ms = 25000;
    bool reuse=(requested_action==WD_DEVICE_DOWNLOAD || requested_action==WD_DEVICE_CHAPTER) &&
        (!strncmp(path,"/web/book/chapter/",18) || !strcmp(path,"/web/book/chapterInfos") || !strncmp(path,"/web/reader/",12));
    bool reused=reuse && *held;
    esp_transport_handle_t transport=reused?channel->transport:
        (serialized_reading || cache_async)?reader_transport_create(personal_marks):NULL;
    if((serialized_reading || cache_async) && !transport) {
        if(transfer.file)fclose(transfer.file);
        free(file_buffer);return WD_MEMORY;
    }
    transfer.transport=transport;
    esp_http_client_config_t config = { .url = url,
                                        .transport = transport,
                                        /* Login confirmation is a server long poll;
                                           a 15 s timeout aborts a valid QR session.
                                           A several-thousand-book shelf also takes
                                           the server longer than 15 s to build. */
                                        .timeout_ms = timeout_ms,
                                        .is_async = serialized_reading || cache_async,
                                        .event_handler = event,
                                        .user_data = &transfer,
                                        .cert_pem = personal_marks?NULL:weread_root_pem,
                                        .crt_bundle_attach = personal_marks?esp_crt_bundle_attach:NULL,
                                        .disable_auto_redirect = true,
                                        .addr_type = HTTP_ADDR_TYPE_INET,
                                        .buffer_size = cache_async?4096:2048,
                                        .buffer_size_tx = 4096,
                                        .user_agent = WD_USER_AGENT };
    /* The coordinator owns thingsos_io_lock; a prefetch worker does not. Only
       release and re-take it when this task is actually the mutex holder. */
    bool io_owned = thingsos_io_lock &&
                    xSemaphoreGetMutexHolder(thingsos_io_lock) == xTaskGetCurrentTaskHandle();
    if(io_owned && !serialized_reading)xSemaphoreGive(thingsos_io_lock);
    /* Chapter transfers may reuse a Web connection. Notes/auth/gateway must
       use isolated clients so Cookie/Bearer headers and TLS roots cannot leak. */
    esp_http_client_handle_t connection=reused?*held:esp_http_client_init(&config);
    if(reused) {
        esp_http_client_set_user_data(connection,&transfer);
        if(esp_http_client_set_url(connection,url)!=ESP_OK) {
            esp_http_client_cleanup(connection);
            *held=NULL;
            channel->transport=NULL;
            connection=NULL;
            transfer.error=WD_MEMORY;
        } else {
            esp_http_client_set_timeout_ms(connection,config.timeout_ms);
        }
    }
    uint64_t request_started=now_ms(NULL);
    esp_err_t result = ESP_FAIL;
    int status = 0;
    int tls_error = 0, tls_flags = 0;
    esp_err_t transport_error = ESP_OK;
    bool ps_reported = false;
    transfer.ps=ps;transfer.pc=pc;transfer.token=token;
    if(extract_reader && reading_context_active)work->reader_pc[0]=work->reader_token[0]=0;
    if (connection) {
        esp_http_client_set_method(
            connection, !strcmp(method, "POST") ? HTTP_METHOD_POST : HTTP_METHOD_GET);
        esp_http_client_set_header(connection, "Origin", "https://weread.qq.com");
        esp_http_client_set_header(
            connection, "Referer", referer ? referer : "https://weread.qq.com/");
        esp_http_client_set_header(connection, "Accept", "application/json, text/plain, */*");
        esp_http_client_set_header(connection, "Accept-Encoding", "identity");
        if (transfer.session && work->runtime_cookie[0]) {
            if (cookie_lock) xSemaphoreTake(cookie_lock, portMAX_DELAY);
            esp_http_client_set_header(connection, "Cookie", work->runtime_cookie);
            if (cookie_lock) xSemaphoreGive(cookie_lock);
        }
        if(personal_marks)esp_http_client_set_header(connection,"Authorization",authorization);
        if(!personal_marks && !strncmp(path,"/web/mp/",8)) {
            ESP_LOGI("ThingsOS_weidu","MP auth headers ticket_bytes=%u wrpa_bytes=%u",(unsigned)strlen(work->mp_ticket),(unsigned)strlen(work->mp_wrpa));
            if(work->mp_ticket[0])esp_http_client_set_header(connection,"x-wr-ticket",work->mp_ticket);
            if(work->mp_wrpa[0])esp_http_client_set_header(connection,"x-wrpa-0",work->mp_wrpa);
        }
        esp_http_client_set_post_field(connection,NULL,0);
        if (body) {
            esp_http_client_set_header(
                connection, "Content-Type", "application/json;charset=UTF-8");
            esp_http_client_set_post_field(connection, body, (int)strlen(body));
        }
        if(serialized_reading || cache_async){
            result=cache_async?cache_http_perform(connection,&transfer):reader_http_perform(connection,&transfer);
            status=esp_http_client_get_status_code(connection);
            if(esp_http_client_get_content_length(connection)>(int64_t)limit)transfer.error=WD_LIMIT;
            if(result==ESP_OK && !transfer.reader_ready && !esp_http_client_is_complete_data_received(connection))result=ESP_FAIL;
        }else{
        unsigned char* block = heap_caps_malloc(8192, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        uint64_t started = now_ms(NULL);
        /* RAM responses stay small; a multi-megabyte shelf on a weak link
           outlives the old 60 s cap and used to fail as a network error. */
        const uint64_t budget = memory ? 60000 : 300000;
        if (!block)
            transfer.error = WD_MEMORY;
        else {
            result = esp_http_client_open(connection, body ? (int)strlen(body) : 0);
            if (result == ESP_OK && body) {
                size_t length = strlen(body), sent = 0;
                while (sent < length && !abort_requested) {
                    int written =
                        esp_http_client_write(connection, body + sent, (int)(length - sent));
                    if (written <= 0) {
                        result = ESP_FAIL;
                        break;
                    }
                    sent += (size_t)written;
                }
            }
            int64_t length = result == ESP_OK ? esp_http_client_fetch_headers(connection) : -1;
            status = esp_http_client_get_status_code(connection);
            if (length < 0) result = ESP_FAIL;
            if (length > (int64_t)limit) transfer.error = WD_LIMIT;
            if(personal_marks && result==ESP_OK && status!=200) {
                /* Only a numeric protocol error is useful for diagnostics.
                   Never persist/log the error body or any account fields. */
                int got=esp_http_client_read(connection,(char*)block,1023);
                if(got>0) {
                    block[got]=0;char* code=strstr((char*)block,"\"errcode\"");
                    if(code && (code=strchr(code,':'))) {
                        char* end;long value=strtol(code+1,&end,10);
                        if(end!=code+1 && value>-100000 && value<100000)
                            ESP_LOGI("ThingsOS_weidu","Personal marks protocol error=%ld",value);
                    }
                }
            }
            while (result == ESP_OK && transfer.error == WD_OK && status == 200 &&
                   !abort_requested) {
                if (now_ms(NULL) - started > budget) {
                    result = ESP_ERR_TIMEOUT;
                    break;
                }
                int got = esp_http_client_read(connection, (char*)block, 8192);
                if (got < 0) {
                    result = ESP_FAIL;
                    break;
                }
                if (!got) {
                    if (!esp_http_client_is_complete_data_received(connection)) result = ESP_FAIL;
                    break;
                }
                if ((size_t)got > limit - transfer.bytes) {
                    transfer.error = WD_LIMIT;
                    break;
                }
                bool ok = true;
                if (extract_reader) {
                    if(reading_context_active){
                        if(pc->state!=4 && !thingsos_weidu_field_feed(pc,"\"pclts\"",block,(size_t)got)){pc->state=4;pc->value[0]=0;}
                        if(token->state!=4 && !thingsos_weidu_field_feed(token,"\"token\"",block,(size_t)got)){transfer.error=WD_PROTOCOL;break;}
                        if(pc->state==4)strlcpy(work->reader_pc,pc->value,sizeof(work->reader_pc));
                        if(token->state==4)strlcpy(work->reader_token,token->value,sizeof(work->reader_token));
                    }
                    if (ps->state!=4 && !thingsos_weidu_ps_feed(ps, block, (size_t)got)) {
                        transfer.error = WD_PROTOCOL;
                        break;
                    }
                    if (ps->state == 4) {
                        strcpy(memory, ps->value);
                        if (!ps_reported && !reading_context_active) {
                            ESP_LOGI("ThingsOS_weidu","Reader token offset=%u block_end=%u",
                                     (unsigned)ps->offset,
                                     (unsigned)(transfer.bytes + (size_t)got));
                            ps_reported = true;
                        }
                        /* Drain this bounded response before reusing its TLS socket. */
                    }
                } else if (memory)
                    memcpy(memory + transfer.bytes, block, (size_t)got);
                else {
                    xSemaphoreTake(thingsos_io_lock, portMAX_DELAY);
                    ok = fwrite(block, 1, (size_t)got, transfer.file) == (size_t)got;
                    xSemaphoreGive(thingsos_io_lock);
                }
                if (!ok) {
                    transfer.error = WD_STORAGE;
                    break;
                }
                transfer.bytes += (size_t)got;
            }
            free(block);
        }
        }
        int request_errno=errno;
        transport_error = transport
            ?esp_tls_get_and_clear_last_error(esp_transport_get_error_handle(reader_transport_data(transport)->tls),&tls_error,&tls_flags)
            :esp_http_client_get_and_clear_last_tls_error(connection, &tls_error, &tls_flags);
        thingsos_diag_event(DIAG_NET_HTTP,diagnostic_stage,diagnostic_id,(unsigned)status,result,request_errno);
        thingsos_diag_event(DIAG_NET_METRIC,diagnostic_stage,diagnostic_id,(unsigned)transfer.bytes,(int)(now_ms(NULL)-request_started),(int)transfer.phase);
        if(result!=ESP_OK || tls_error || tls_flags || transfer.error!=WD_OK || status!=200) {
            thingsos_diag_event(DIAG_NET_TLS,diagnostic_stage,diagnostic_id,(unsigned)tls_flags,tls_error,transport_error);
            thingsos_diag_event(DIAG_FAILURE,2,diagnostic_id,(unsigned)transfer.phase,transfer.error,0);
        }
        /* Never log request URLs, cookies, QR identifiers or response bodies. */
        ESP_LOGI("ThingsOS_weidu", "HTTP %s status=%d result=0x%x TLS=%d flags=0x%x bytes=%u",
                 method, status, (unsigned)result, tls_error, (unsigned)tls_flags,
                 (unsigned)transfer.bytes);
        const char* stage=!strcmp(path,"/web/book/read")?"read-upload":
            !strcmp(path,"/web/getConfig")?"read-config":
            !strncmp(path,"/web/book/getProgress",21)?"read-progress":
            !strcmp(path,"/web/login/renewal")?"renewal":
            !strcmp(path,"/web/book/chapterInfos")?"toc":strstr(path,"/web/reader/")?"reader":strstr(path,"/web/book/chapter/")?"shard":"session";
        ESP_LOGI("ThingsOS_weidu","Transport stage=%s elapsed=%u ms reused=%d early_reader=%d esp_error=0x%x errno=%d",stage,(unsigned)(now_ms(NULL)-request_started),reused,transfer.reader_ready,(unsigned)transport_error,errno);
        if(serialized_reading) {
            work->request_diagnostic.http_status=status;
            work->request_diagnostic.transport_error=result;
            work->request_diagnostic.tls_error=tls_error;
            work->request_diagnostic.tls_flags=(unsigned)tls_flags;
            work->request_diagnostic.socket_error=request_errno;
            work->request_diagnostic.phase=transfer.phase;
        }
        /* A reader prefix is sufficient once all required fields arrived.
           Discard its undrained socket; only complete responses may be reused. */
        if(reuse && result==ESP_OK && transfer.error==WD_OK && status==200 && !abort_requested && !transfer_stopped(&transfer) &&
           esp_http_client_is_complete_data_received(connection) && esp_http_client_is_persistent_connection(connection)) {
            esp_http_client_set_user_data(connection,NULL);
            *held=connection;
            channel->transport=transport;transport=NULL;
        } else {
            esp_http_client_cleanup(connection);
            if(connection==*held){*held=NULL;channel->transport=NULL;}
        }
        if(!cache_async && !abort_requested && result!=ESP_OK && !status && !tls_error && !tls_flags &&
           transport_error!=ESP_ERR_ESP_TLS_CANNOT_RESOLVE_HOSTNAME) {
            /* Resolver answers can point at an unavailable edge. Refresh through
               lwIP's owning task; retain weread.qq.com for SNI and verification. */
            struct addrinfo hints={.ai_family=AF_INET,.ai_socktype=SOCK_STREAM},*answer=NULL;
            if(getaddrinfo("weread.qq.com",NULL,&hints,&answer)==0 && answer) {
                struct sockaddr_in* peer=(struct sockaddr_in*)answer->ai_addr;
                ESP_LOGI("ThingsOS_weidu","Connect failure resolved edge=%s",inet_ntoa(peer->sin_addr));
            }
            if(answer)freeaddrinfo(answer);
            tcpip_callback(reset_dns_cache,NULL);
        }
    }
    if(io_owned && !serialized_reading)xSemaphoreTake(thingsos_io_lock, portMAX_DELAY);
    if(transport)esp_transport_destroy(transport);
    if (transfer.file) {
        if (!abort_requested && !transfer_stopped(&transfer) && !thingsos_weidu_flush(transfer.file)) transfer.error = WD_STORAGE;
        if (fclose(transfer.file) && transfer.error == WD_OK) transfer.error = WD_STORAGE;
    }
    free(file_buffer);
    if (memory && !extract_reader) {
        memory[transfer.bytes] = 0;
        if (bytes) *bytes = transfer.bytes;
    }
    if (abort_requested || transfer_stopped(&transfer)) transfer.error = WD_CANCEL;
    if (transfer.error != WD_OK) {
        if (destination) remove(destination);
        return transfer.error;
    }
    if (result != ESP_OK) {
        if (destination) remove(destination);
        if (transport_error == ESP_ERR_ESP_TLS_CANNOT_RESOLVE_HOSTNAME) return WD_DNS;
        if (tls_error == -MBEDTLS_ERR_SSL_ALLOC_FAILED || tls_error == MBEDTLS_ERR_SSL_ALLOC_FAILED) return WD_MEMORY;
        /* A Wi-Fi drop surfaces as an mbedTLS socket error (NET_RECV_FAILED)
           with no verification flags; that is a replayable link loss. */
        return thingsos_weidu_transport_error(tls_error,tls_flags);
    }
    if (status == 401) return personal_marks ? WD_ACCESS : WD_AUTH;
    if (status == 403) return WD_ACCESS;
    if (extract_reader && ps->state != 4) return WD_ACCESS;
    return status == 200 ? WD_OK : WD_NETWORK;
}
static thingsos_weidu_error_t fetch_impl(
    void* context, const char* method, const char* path, const char* body, const char* referer,
    const char* destination, size_t limit, thingsos_weidu_session_t* s, char* memory, size_t* bytes,
    bool extract_reader,const char* authorization) {
    fetch_scratch_t* scratch = heap_caps_calloc(1, sizeof(*scratch), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!scratch) return WD_MEMORY;
    thingsos_weidu_error_t e = fetch_impl_with(scratch, context, method, path, body, referer, destination,
                                               limit, s, memory, bytes, extract_reader, authorization);
    /* Token values can hold reader credentials: wipe before freeing. */
    memset(scratch, 0, sizeof(*scratch));
    free(scratch);
    return e;
}
static thingsos_weidu_error_t fetch(
    void* context,const char* method,const char* path,const char* body,const char* referer,
    const char* destination,size_t limit,thingsos_weidu_session_t* session,char* memory,size_t* bytes,
    bool extract_reader,const char* authorization) {
    bool diagnose=reading_action_is(requested_action);
    if(diagnose) {
        memset(&work->request_diagnostic,0,sizeof(work->request_diagnostic));
        const char* stage=!strcmp(path,"/web/book/read")?"POST":
            !strcmp(path,"/web/getConfig")?"CONFIG":
            !strncmp(path,"/web/book/getProgress",21)?"PULL":
            !strcmp(path,"/web/login/renewal")?"AUTH":
            !strncmp(path,"/web/reader/",12)?"READER":"OTHER";
        strlcpy(work->request_diagnostic.stage,stage,sizeof(work->request_diagnostic.stage));
    }
    thingsos_weidu_error_t e=fetch_impl(context,method,path,body,referer,destination,limit,session,memory,bytes,extract_reader,authorization);
    thingsos_diag_event(DIAG_TASK,2,(unsigned)requested_action,diagnostic_endpoint(path),e,0);
    if(diagnose)work->request_diagnostic.error=e;
    return e;
}
typedef struct {char response[1024],authorization[272];} marks_key_t;
static thingsos_weidu_error_t http(
    void* context, const char* method, const char* endpoint, const char* body, const char* referer,
    const char* destination, size_t limit, thingsos_weidu_session_t* s) {
    if(!strcmp(endpoint,"/api/agent/gateway")) {
        /* Authentication material exists only in a short-lived PSRAM block,
           never in a response file, log, NVS export or computer-side request. */
        marks_key_t* key=heap_caps_calloc(1,sizeof(*key),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if(!key)return WD_MEMORY;
        size_t bytes=0;
        thingsos_weidu_error_t e=fetch(context,"GET","/api/skills/apikeyGet",NULL,NULL,NULL,
            sizeof(key->response)-1,s,key->response,&bytes,false,NULL);
        if(e==WD_OK && !thingsos_wd_key_header(key->response,bytes,key->authorization,sizeof(key->authorization)))e=WD_ACCESS;
        if(e==WD_AUTH)e=WD_ACCESS; /* A notes route cannot invalidate the working Web shelf. */
        if(e==WD_OK)e=fetch(context,method,endpoint,body,referer,destination,limit,s,NULL,NULL,false,key->authorization);
        memset(key,0,sizeof(*key));free(key);return e;
    }
    return fetch(context, method, endpoint, body, referer, destination, limit, s, NULL, NULL, false,NULL);
}
static thingsos_weidu_error_t auth(
    void* context, const char* method, const char* endpoint, const char* body, char* data,
    size_t capacity, size_t* bytes, thingsos_weidu_session_t* s) {
    if (!capacity) return WD_LIMIT;
    return fetch(context, method, endpoint, body, NULL, NULL, capacity - 1, s, data, bytes, false,NULL);
}
static thingsos_weidu_error_t reader(
    void* context, const char* referer, char* token, size_t capacity, thingsos_weidu_session_t* s) {
    const char* host = "https://weread.qq.com";
    size_t n = strlen(host);
    if (capacity < 128 || strncmp(referer, host, n) || referer[n] != '/') return WD_PROTOCOL;
    token[0] = 0;
    return fetch(
        context, "GET", referer + n, NULL, referer, NULL, 2u * 1024u * 1024u, s, token, NULL, true,NULL);
}
static thingsos_weidu_error_t reading_exchange(void* context,const char* method,const char* endpoint,const char* body,const char* ref,char* data,size_t capacity,size_t* bytes,thingsos_weidu_session_t* session){
    if(!capacity)return WD_LIMIT;
    return fetch(context,method,endpoint,body,ref,NULL,capacity-1,session,data,bytes,false,NULL);
}
enum { CATALOG_RESPONSE_BYTES = 64u*1024u };
static thingsos_weidu_error_t fetch_catalog(const char* query,char* response,size_t* bytes) {
    char endpoint[616]="/web/search/global?keyword=";
    static const char suffix[]="&count=20";
    size_t used=strlen(endpoint);
    static const char hex[]="0123456789ABCDEF";
    for(const unsigned char* p=(const unsigned char*)query;*p;p++) {
        unsigned char ch=*p;bool plain=(ch>='a'&&ch<='z')||(ch>='A'&&ch<='Z')||
            (ch>='0'&&ch<='9')||ch=='-'||ch=='_'||ch=='.'||ch=='~';
        size_t n=plain?1:3;if(used+n+sizeof(suffix)>sizeof(endpoint))return WD_LIMIT;
        if(plain)endpoint[used++]=(char)ch;
        else {endpoint[used++]='%';endpoint[used++]=hex[ch>>4];endpoint[used++]=hex[ch&15];}
    }
    memcpy(endpoint+used,suffix,sizeof(suffix));
    /* An expired Web cookie can turn a valid public catalog search into errcode -2010. */
    return fetch(NULL,"GET",endpoint,NULL,NULL,NULL,CATALOG_RESPONSE_BYTES-1,NULL,response,bytes,false,NULL);
}
static thingsos_weidu_error_t online_search(thingsos_weidu_client_t* c,const char* query) {
    char* response=heap_caps_malloc(CATALOG_RESPONSE_BYTES,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!response)return WD_MEMORY;
    size_t bytes=0;
    thingsos_weidu_error_t e=fetch_catalog(query,response,&bytes);
    unsigned count=0;
    if(e==WD_OK)e=thingsos_weidu_catalog_results(response,bytes,work->online_search_books,
        sizeof(work->online_search_books)/sizeof(work->online_search_books[0]),&count);
    free(response);
    if(e==WD_OK) {
        if(count)thingsos_weidu_shelf_membership(c->root,&c->session,work->online_search_books,count,work->online_search_member);
        work->online_search_count=count;
        strlcpy(work->online_search_query,query,sizeof(work->online_search_query));
        work->online_search_ready=true;
    }
    ESP_LOGI("ThingsOS_weidu","Online catalog search result=%d books=%u",e,work->online_search_count);
    return e;
}
static void online_prepare_cover(thingsos_weidu_client_t* c,const thingsos_weidu_book_t* book) {
    if(abort_requested || !book->title[0] || !thingsos_weidu_stream_needs_cover(c->root,book->id))return;
    char* response=heap_caps_malloc(CATALOG_RESPONSE_BYTES,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!response)return;
    size_t bytes=0;
    char url[640];
    thingsos_weidu_error_t e=fetch_catalog(book->title,response,&bytes);
    bool found=e==WD_OK && thingsos_weidu_catalog_cover(response,bytes,book->id,url,sizeof(url));
    free(response);
    bool saved=found && thingsos_weidu_cache_cover(c,book->id,url);
    ESP_LOGI("ThingsOS_weidu","Online cover id=%s catalog=%d matched=%d cached=%d",book->id,e,found,saved);
}
static thingsos_weidu_error_t reading_context(void* context,const char* ref,char ps[128],char pc[128],char token[128],thingsos_weidu_session_t* session){
    reading_context_active=true;
    thingsos_weidu_error_t e=reader(context,ref,ps,128,session);
    reading_context_active=false;
    if(e==WD_OK){strlcpy(pc,work->reader_pc,128);strlcpy(token,work->reader_token,128);}
    return e;
}
static bool image_deadline_reached(void* context) {
    return now_ms(NULL)>=*(const uint64_t*)context;
}
/* Use a fresh public transport per hop, with no account headers or cookies. */
static thingsos_weidu_error_t image(void* context,const char* url,const char* destination,size_t limit) {
    (void)context;
    char (*urls)[IMAGE_URL_BYTES]=thingsos_psram_alloc(2*sizeof(*urls));
    if(!urls)return WD_MEMORY;
    if(!thingsos_weidu_cover_url(url,urls[0],sizeof(urls[0]))){free(urls);return WD_PROTOCOL;}
    bool owned=thingsos_io_lock && xSemaphoreGetMutexHolder(thingsos_io_lock)==xTaskGetCurrentTaskHandle();
    if(owned)xSemaphoreGive(thingsos_io_lock);
    FILE* file=fopen(destination,"wb");
    thingsos_weidu_error_t error=file?WD_NETWORK:WD_STORAGE;
    unsigned request_budget=limit==THINGSOS_MEMBER_BYTES || requested_action==WD_DEVICE_CHAPTER?6000:300000;
    uint64_t deadline=now_ms(NULL)+request_budget+5000;
    unsigned hop=0;
    int status=0;
    size_t bytes=0;
    for(;file;++hop) {
        char* checked=urls[hop%2];
        char* redirect=urls[(hop+1)%2];redirect[0]=0;
        const char* slash=strchr(checked+8,'/');char host[128];
        snprintf(host,sizeof(host),"%.*s",(int)(slash-(checked+8)),checked+8);
        error=cache_dns_ready(host,image_deadline_reached,&deadline);
        if(error==WD_CANCEL && !abort_requested)error=WD_NETWORK;
        thingsos_diag_event(DIAG_NET_DNS,10,hop,0,error,0);
        if(error!=WD_OK)break;
        uint64_t started=now_ms(NULL);
        if(started>=deadline){error=WD_NETWORK;break;}
        unsigned remaining=(unsigned)(deadline-started);
        esp_transport_handle_t transport=reader_transport_create(true);
        if(!transport){error=WD_MEMORY;break;}
        transfer_t transfer={.file=file,.limit=limit,.stream=true,.transport=transport,
            .budget_ms=remaining<request_budget?remaining:request_budget,.image_url=checked,.image_redirect=redirect};
        esp_http_client_config_t config={.url=checked,.timeout_ms=15000,.is_async=true,.transport=transport,
            .disable_auto_redirect=true,.crt_bundle_attach=esp_crt_bundle_attach,.buffer_size=4096,.buffer_size_tx=1024,
            .event_handler=event,.user_data=&transfer,.addr_type=HTTP_ADDR_TYPE_INET};
        esp_http_client_handle_t connection=esp_http_client_init(&config);
        esp_err_t result=connection?cache_http_perform(connection,&transfer):ESP_FAIL;
        status=connection?esp_http_client_get_status_code(connection):0;
        int image_errno=errno,tls_code=0,tls_flags=0;
        esp_err_t tls_result=esp_tls_get_and_clear_last_error(esp_transport_get_error_handle(reader_transport_data(transport)->tls),&tls_code,&tls_flags);
        thingsos_diag_event(DIAG_NET_HTTP,10,hop,(unsigned)status,result,image_errno);
        thingsos_diag_event(DIAG_NET_METRIC,10,hop,(unsigned)transfer.bytes,(int)(now_ms(NULL)-started),(int)transfer.phase);
        if(result!=ESP_OK || tls_code || tls_flags)thingsos_diag_event(DIAG_NET_TLS,10,hop,(unsigned)tls_flags,tls_code,tls_result);
        bool complete=status==200 && result==ESP_OK && esp_http_client_is_complete_data_received(connection);
        if(connection)esp_http_client_cleanup(connection);
        esp_transport_destroy(transport);
        bytes=transfer.bytes;
        error=transfer.error;
        if(error!=WD_OK || complete)break;
        error=WD_NETWORK;
        if(!transfer.image_redirect_ready || !redirect[0] || !strcmp(checked,redirect) || hop==IMAGE_MAX_REDIRECTS)break;
    }
    if(abort_requested)error=WD_CANCEL;
    if(file) {
        if(error==WD_OK && !thingsos_weidu_flush(file))error=WD_STORAGE;
        if(fclose(file))error=WD_STORAGE;
        if(error!=WD_OK)remove(destination);
    }
    if(owned)xSemaphoreTake(thingsos_io_lock,portMAX_DELAY);
    free(urls);
    ESP_LOGI("ThingsOS_weidu","Public image status=%d bytes=%u error=%d redirects=%u",status,(unsigned)bytes,error,hop);
    return error;
}
static void finish_network(void* ignored) {
    (void)ignored;
    channel_close(&download_channel);
    ESP_LOGI("ThingsOS_weidu","Storage phase DMA free=%u largest=%u stack margin=%u",
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL),
        (unsigned)uxTaskGetStackHighWaterMark(NULL));
}
/* Two reader workers prefetch chapter psvts values while the coordinator
   writes the previous chapter. Only HTTPS token fetches run here; all SD
   writes, decode and ordering stay on the download worker. */
#define PREFETCH_RING 4
#define PREFETCH_STACK 12288
#define WEIDU_WORKER_STACK (12u * 1024u)
typedef struct {
    char ps[128];
    thingsos_weidu_error_t error;
    unsigned sequence;
    atomic_uint state; /* 0 empty, 2 ready */
} prefetch_slot_t;
typedef struct prefetch_t prefetch_t;
typedef struct {
    prefetch_t* p;
    unsigned index;
    TaskHandle_t handle;
    prefetch_conn_t conn;
} prefetch_task_t;
struct prefetch_t {
    char toc[700], book[128];
    const unsigned* indices;
    unsigned count;
    atomic_uint next;
    atomic_bool stop;
    prefetch_slot_t slots[PREFETCH_RING];
    SemaphoreHandle_t gate, done;
    prefetch_task_t tasks[2];
};
static prefetch_t* prefetch;
static void prefetch_worker(void* argument) {
    prefetch_task_t* task = argument;
    prefetch_t* p = task->p;
    xSemaphoreTake(p->gate, portMAX_DELAY);
    for (;;) {
        if (atomic_load(&p->stop)) break;
        unsigned sequence = atomic_fetch_add(&p->next, 1);
        if (sequence >= p->count) break;
        unsigned index = p->indices[sequence];
        thingsos_weidu_chapter_t chapter;
        bool ok = false;
        if (thingsos_io_lock && xSemaphoreTake(thingsos_io_lock, portMAX_DELAY) == pdTRUE) {
            FILE* f = fopen(p->toc, "rb");
            ok = f && !fseek(f, (long)(index * sizeof(chapter)), SEEK_SET) &&
                 fread(&chapter, sizeof(chapter), 1, f) == 1;
            if (f) fclose(f);
            xSemaphoreGive(thingsos_io_lock);
        }
        thingsos_weidu_error_t e = ok ? WD_OK : WD_STORAGE;
        char b[160] = {0}, ch[160] = {0}, referer[400] = {0}, ps[128] = {0};
        if (e == WD_OK &&
            (!thingsos_weidu_encode_id(p->book, b) || !thingsos_weidu_encode_id(chapter.uid, ch)))
            e = WD_PROTOCOL;
        if (e == WD_OK) {
            snprintf(referer, sizeof(referer), "https://weread.qq.com/web/reader/%sk%s", b, ch);
            for (unsigned attempt = 0; attempt < 3; ++attempt) {
                if (atomic_load(&p->stop) || abort_requested) { e = WD_CANCEL; break; }
                e = fetch(&task->conn, "GET", referer + strlen("https://weread.qq.com"), NULL, referer,
                          NULL, 2u * 1024u * 1024u, &client->session, ps, NULL, true, NULL);
                if ((e != WD_NETWORK && e != WD_DNS) || attempt == 2) break;
                uint64_t until=now_ms(NULL)+500u*(attempt+1);
                while(!atomic_load(&p->stop) && !abort_requested && now_ms(NULL)<until)vTaskDelay(1);
                link_wait(6000,&p->stop);
            }
            if (e == WD_OK && !thingsos_weidu_token(ps, sizeof(ps))) e = WD_ACCESS;
        }
        prefetch_slot_t* slot = &p->slots[sequence % PREFETCH_RING];
        while (atomic_load_explicit(&slot->state, memory_order_acquire) != 0) {
            if (atomic_load(&p->stop)) { e = WD_CANCEL; break; }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        if (e == WD_CANCEL && atomic_load(&p->stop)) break;
        strlcpy(slot->ps, ps, sizeof(slot->ps));
        slot->error = e;
        slot->sequence = sequence;
        atomic_store_explicit(&slot->state, 2, memory_order_release);
    }
    channel_close(&task->conn);
    xSemaphoreGive(p->done);
    vTaskDeleteWithCaps(NULL);
}
static bool prefetch_start(void* context, const char* toc, const char* book_id,
                           const unsigned* indices, unsigned count) {
    (void)context;
    if (prefetch || !toc || !book_id || !indices || !count) return false;
    /* Interactive chapter windows share memory with live display scans.
       Keep one TLS connection for the coordinator's context and body requests. */
    if (requested_action == WD_DEVICE_CHAPTER) {
        ESP_LOGI("ThingsOS_weidu", "Online chapter window uses serial HTTPS (%u missing)", count);
        return false;
    }
    /* 栈优先放 PSRAM，内部 SRAM 留给 WiFi/DMA；PSRAM 不够时退回内部 RAM。
       / Prefer PSRAM stacks so internal SRAM stays available for WiFi/DMA,
       falling back to internal RAM when PSRAM cannot hold both stacks. */
    bool psram_stacks =
        heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) >= PREFETCH_STACK &&
        heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) >= 2u * PREFETCH_STACK + 65536u;
    if (!psram_stacks &&
        (heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) <
             2u * PREFETCH_STACK + 8192u ||
         heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) < PREFETCH_STACK))
        return false;
    const uint32_t stack_caps = psram_stacks ? MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
                                             : MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    ESP_LOGI("ThingsOS_weidu","Prefetch stacks: %s",psram_stacks?"PSRAM":"internal RAM");
    prefetch_t* p = heap_caps_calloc(1, sizeof(*p), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!p) return false;
    strlcpy(p->toc, toc, sizeof(p->toc));
    strlcpy(p->book, book_id, sizeof(p->book));
    p->indices = indices;
    p->count = count;
    p->gate = xSemaphoreCreateCounting(2, 0);
    p->done = xSemaphoreCreateCounting(2, 0);
    if (!p->gate || !p->done) {
        if (p->gate) vSemaphoreDelete(p->gate);
        if (p->done) vSemaphoreDelete(p->done);
        free(p);
        return false;
    }
    atomic_init(&p->next, 0);
    atomic_init(&p->stop, false);
    for (unsigned i = 0; i < PREFETCH_RING; ++i) atomic_init(&p->slots[i].state, 0);
    for (unsigned i = 0; i < 2; ++i) {
        p->tasks[i].p = p;
        p->tasks[i].index = i;
        p->tasks[i].conn.magic = PREFETCH_CONN_MAGIC;
        p->tasks[i].conn.connection = NULL;
        p->tasks[i].conn.stop=&p->stop;
    }
    prefetch = p;
    if (xTaskCreateWithCaps(prefetch_worker, "weidu_prefetch", PREFETCH_STACK, &p->tasks[0], 2,
                            &p->tasks[0].handle, stack_caps) != pdPASS) {
        prefetch = NULL;
        vSemaphoreDelete(p->gate);
        vSemaphoreDelete(p->done);
        free(p);
        return false;
    }
    if (xTaskCreateWithCaps(prefetch_worker, "weidu_prefetch", PREFETCH_STACK, &p->tasks[1], 2,
                            &p->tasks[1].handle, stack_caps) != pdPASS) {
        atomic_store(&p->stop, true);
        xSemaphoreGive(p->gate);
        xSemaphoreTake(p->done, portMAX_DELAY);
        prefetch = NULL;
        vSemaphoreDelete(p->gate);
        vSemaphoreDelete(p->done);
        free(p);
        return false;
    }
    xSemaphoreGive(p->gate);
    xSemaphoreGive(p->gate);
    return true;
}
static thingsos_weidu_error_t prefetch_get(void* context, unsigned sequence, char* ps,
                                           size_t capacity, unsigned timeout_ms) {
    (void)context;
    prefetch_t* p = prefetch;
    if (!p || sequence >= p->count || !ps || !capacity) return WD_PROTOCOL;
    bool io_owned = thingsos_io_lock &&
        xSemaphoreGetMutexHolder(thingsos_io_lock) == xTaskGetCurrentTaskHandle();
    if (io_owned) xSemaphoreGive(thingsos_io_lock);
    thingsos_weidu_error_t result = WD_NETWORK;
    uint64_t started = now_ms(NULL);
    for (;;) {
        prefetch_slot_t* slot = &p->slots[sequence % PREFETCH_RING];
        if (atomic_load_explicit(&slot->state, memory_order_acquire) == 2 &&
            slot->sequence == sequence) {
            result = slot->error;
            if (result == WD_OK) strlcpy(ps, slot->ps, capacity);
            else ps[0] = 0;
            atomic_store_explicit(&slot->state, 0, memory_order_release);
            break;
        }
        if (abort_requested) { result = WD_CANCEL; break; }
        if (now_ms(NULL) - started >= timeout_ms) { result = WD_NETWORK; break; }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (io_owned) xSemaphoreTake(thingsos_io_lock, portMAX_DELAY);
    return result;
}
#include "prefetch_join.inc"
static void prefetch_stop(void* context) {
    (void)context;
    prefetch_t* p = prefetch;
    if (!p) return;
    prefetch_join(p);
    prefetch = NULL;
    vSemaphoreDelete(p->gate);
    vSemaphoreDelete(p->done);
    free(p);
}
bool thingsos_weidu_device_init(void) {
    if (client) return true;
    work=heap_caps_calloc(1,sizeof(*work),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!work)return false;
    client = heap_caps_calloc(1, sizeof(*client), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!client) {free(work);work=NULL;return false;}
    cookie_lock = xSemaphoreCreateMutex();
    if (!cookie_lock) {free(client);client=NULL;free(work);work=NULL;return false;}
    strcpy(client->root, "/sdcard/.thingsos/weidu");
    strcpy(client->output, "/sdcard/books");
    client->io =
        (thingsos_weidu_io_t){ NULL,         http,    session, cancelled, now_ms, unix_time,
                               random_value, wait_ms, publish, auth,      reader, image, finish_network, reading_exchange, reading_context,
                               0, prefetch_start, prefetch_get, prefetch_stop, wait_link };
    nvs_handle_t fp_handle;uint32_t fingerprint=0;
    if(nvs_open("thingsos-weidu",NVS_READWRITE,&fp_handle)==ESP_OK){
        if(nvs_get_u32(fp_handle,"fingerprint",&fingerprint)!=ESP_OK || !fingerprint){
            fingerprint=esp_random();if(!fingerprint)fingerprint=1;
            if(nvs_set_u32(fp_handle,"fingerprint",fingerprint)!=ESP_OK || nvs_commit(fp_handle)!=ESP_OK)fingerprint=0;
        }nvs_close(fp_handle);
    }
    if(!fingerprint){free(client);client=NULL;free(work);work=NULL;return false;}
    snprintf(work->fingerprint,sizeof(work->fingerprint),"%u",(unsigned)fingerprint);
    work->reading.pull=work->reading.progress=work->reading.duration=work->reading.notes=WD_CANCEL;
    session(NULL, &client->session, false);
    if(client->session.vid[0])thingsos_weidu_md5(client->session.vid,strlen(client->session.vid),work->reading.owner);
    client->status.authenticated = thingsos_weidu_session_valid(&client->session);
    size_t count, total;
    thingsos_weidu_shelf_page(client->root, &client->session, 0, NULL, 0, &count, &total);
    client->status.count = (unsigned)total;
    if(thingsos_wd_pending_load(client->root,work->reading.owner,&work->pending)){
        uint64_t generation;
        if(thingsos_wd_queue_put(client->root,work->reading.owner,&work->pending,&generation))thingsos_wd_pending_clear(client->root);
        else {work->pending_valid=true;strlcpy(work->pending_owner,work->reading.owner,sizeof(work->pending_owner));}
    }
    client->status.phase = WD_IDLE;
    publish(NULL, &client->status);
    return true;
}
void thingsos_weidu_state(thingsos_weidu_status_t* out) {
    portENTER_CRITICAL(&guard);
    if(work)*out=work->public_status;
    else memset(out,0,sizeof(*out));
    portEXIT_CRITICAL(&guard);
}
bool thingsos_weidu_busy(void) {
    return active;
}
void thingsos_weidu_cancel(void) {
    if(!atomic_exchange(&abort_requested,true) && active) {
        atomic_store(&cancel_requested_at,(unsigned)now_ms(NULL));
        ESP_LOGI("ThingsOS_weidu","Cancel requested action=%d",requested_action);
    }
}
static bool same_progress(const thingsos_wd_upload_t* a,const thingsos_wd_upload_t* b){
    return !strcmp(a->id,b->id)&&a->chapter==b->chapter&&a->offset==b->offset&&a->percent==b->percent;
}
static thingsos_weidu_error_t reading_fetch(thingsos_weidu_client_t* c,void* context) {
    thingsos_wd_device_status_t* reading=context;
    if(requested_action==WD_DEVICE_THOUGHTS)return thingsos_wd_thought_refresh(c,&work->thought_query);
    if(requested_action==WD_DEVICE_NOTES)return thingsos_wd_notes_refresh(c,reading->book);
    if(requested_action==WD_DEVICE_PERSONAL)return thingsos_wd_personal_refresh(c,reading->book);
    return thingsos_wd_pull(c,reading->book,&reading->cloud);
}
static thingsos_weidu_error_t reading_pull_queued(thingsos_weidu_client_t* c,void* context) {
    return thingsos_wd_pull(c,work->upload.id,(thingsos_wd_progress_t*)context);
}
static const char* device_sync_owner(const char* owner) {
    if(owner && strlen(owner)==32)return owner;
    return work?work->reading.owner:"";
}
static void device_record_uploaded(const char* id,unsigned chapter,uint32_t offset,const char* owner,uint32_t at) {
    const char* resolved=device_sync_owner(owner);
    if(!client || !resolved[0])return;
    thingsos_wd_sync_t sync={0};
    thingsos_wd_sync_load(client->root,resolved,id,&sync);
    thingsos_weidu_chapter_t toc={0};
    if(thingsos_wd_toc(client->root,id,chapter,&toc))strlcpy(sync.uploaded_uid,toc.uid,sizeof(sync.uploaded_uid));
    else sync.uploaded_uid[0]=0;
    sync.uploaded_offset=offset;
    sync.uploaded_updated=at;
    if(at>sync.local_updated)sync.local_updated=at;
    sync.conflict_pending=false;
    memset(&sync.conflict_cloud,0,sizeof(sync.conflict_cloud));
    thingsos_wd_sync_save(client->root,resolved,id,&sync);
}
static bool device_queued_conflict(thingsos_weidu_client_t* c,const thingsos_wd_upload_t* up,
    const char* owner,const thingsos_wd_progress_t* cloud) {
    thingsos_wd_sync_t sync={0};
    thingsos_wd_sync_load(c->root,owner,up->id,&sync);
    thingsos_weidu_chapter_t toc={0};
    bool has_toc=thingsos_wd_toc(c->root,up->id,up->chapter,&toc);
    if(thingsos_wd_progress_compare(cloud,has_toc?toc.uid:NULL,up->offset,has_toc,sync.local_updated,&sync)!=WD_SYNC_CLOUD_NEWER)
        return false;
    sync.conflict_pending=true;
    sync.conflict_cloud=*cloud;
    thingsos_wd_sync_save(c->root,owner,up->id,&sync);
    return true;
}
#include "eink_readtime.inc"
static void worker(void* unused) {
    (void)unused;
    /* result: stack region (1 main SRAM, 2 slow LP RAM, 3 PSRAM). */
    { int here=0; thingsos_memory_trace("weidu-worker",(int)requested_action,thingsos_memory_region(&here)); }
    /* Peak main-SRAM draw of this job (local-minimum window; false if another
       window, e.g. BLE init, is open). */
    const uint32_t peak_caps=MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA|MALLOC_CAP_8BIT;
    size_t peak_before=heap_caps_get_free_size(peak_caps);
    bool peak_window=heap_caps_monitor_local_minimum_free_size_start()==ESP_OK;
    xSemaphoreTake(thingsos_io_lock, portMAX_DELAY);
    uint64_t operation_started = now_ms(NULL);
    bool network_exchange = radio_needs_awake(requested_action,
        requested_action == WD_DEVICE_RESTORE && thingsos_network_connected());
    weidu_radio_t radio = {0};
    unsigned link_losses_at_start = thingsos_network_link_losses();
    if (network_exchange) radio = radio_awake();
    mkdir("/sdcard/.thingsos", 0775);
    mkdir(client->root, 0775);
    mkdir(client->output, 0775);
    thingsos_weidu_error_t e = WD_OK;
    thingsos_wd_device_status_t reading=work->reading;
    memset(&work->request_diagnostic,0,sizeof(work->request_diagnostic));
    bool reading_action=reading_action_is(requested_action);reading.action=requested_action;
    thingsos_wd_read_session_t* read_session=work->queued_generation?&work->queued_session:&work->read_session;
    bool cache_job=requested_action==WD_DEVICE_DOWNLOAD || requested_action==WD_DEVICE_CHAPTER || requested_action==WD_DEVICE_RESTORE;
    if(cache_job)xSemaphoreGive(thingsos_io_lock);
    bool rt_job=requested_action==WD_DEVICE_RT_AUTH || requested_action==WD_DEVICE_RT_UPLOAD;
    if(rt_job){
        xSemaphoreGive(thingsos_io_lock);
        e=rt_worker();
        xSemaphoreTake(thingsos_io_lock,portMAX_DELAY);
    } else if (requested_action == WD_DEVICE_LOGOUT) {
        memset(&work->read_session,0,sizeof(work->read_session));
        work->logout_previous = client->session;
        memset(&client->session, 0, sizeof(client->session));
        memset(work->runtime_cookie, 0, sizeof(work->runtime_cookie));
        memset(work->mp_ticket,0,sizeof(work->mp_ticket));memset(work->mp_wrpa,0,sizeof(work->mp_wrpa));
        if (!session(NULL, &client->session, true)) {
            client->session = work->logout_previous;
            memset(&work->logout_previous,0,sizeof(work->logout_previous));
            e = WD_STORAGE;
        } else {
            nvs_handle_t rt_handle;
            if(nvs_open("thingsos-eink",NVS_READWRITE,&rt_handle)==ESP_OK){nvs_erase_key(rt_handle,"credentials");nvs_commit(rt_handle);nvs_close(rt_handle);}
            rt_loaded=false;memset(&rt_public,0,sizeof(rt_public));
            snprintf(work->worker_path, sizeof(work->worker_path), "%s/shelf.dat", client->root);
            remove(work->worker_path);
            client->status.count = 0;
            memset(&reading,0,sizeof(reading));reading.pull=reading.progress=reading.duration=reading.notes=WD_CANCEL;
            work->pending_valid=false;thingsos_wd_pending_clear(client->root);
            client->status.qr[0] = client->status.path[0] = 0;
            memset(&work->logout_previous,0,sizeof(work->logout_previous));
        }
    } else if(requested_action==WD_DEVICE_SEARCH) {
        e=online_search(client,work->search_query);
        client->status.phase=e==WD_OK?WD_READY:WD_ERROR;
        strlcpy(client->status.message,e==WD_OK?"线上书籍搜索完成":"线上书籍搜索失败，书架结果仍可用",
            sizeof(client->status.message));
    } else if(requested_action==WD_DEVICE_SHELF_ADD) {
        e=thingsos_weidu_shelf_add(client,&work->requested_book);
        if(e==WD_AUTH && client->session.rt[0] && thingsos_weidu_renew(client)==WD_OK)
            e=thingsos_weidu_shelf_add(client,&work->requested_book);
        if(e==WD_OK)thingsos_weidu_shelf_membership(client->root,&client->session,
            work->online_search_books,work->online_search_count,work->online_search_member);
    } else if(requested_action==WD_DEVICE_CHAPTER) {
        /* This source job touches only private files and its network client.
           It never uses font/display state; publish complete chapter files by
           rename so the main task can keep turning pages during TOC/SD work. */
        e=thingsos_weidu_stream_window(client,&work->requested_book,work->online_index,work->online_count);
        if(e==WD_AUTH && client->session.rt[0] && thingsos_weidu_renew(client)==WD_OK)
            e=thingsos_weidu_stream_window(client,&work->requested_book,work->online_index,work->online_count);
        if(e==WD_OK && work->online_prepare_cover)online_prepare_cover(client,&work->requested_book);
    } else if(requested_action>=WD_DEVICE_MP_ACCOUNTS && requested_action<=WD_DEVICE_MP_CONTENT) {
        e=requested_action==WD_DEVICE_MP_CONTENT?
            thingsos_mp_download(client,&work->cover_books[0],&work->requested_book,MP_OUTPUT):
            thingsos_mp_refresh(client,requested_action==WD_DEVICE_MP_ARTICLES?&work->requested_book:NULL);
        ESP_LOGI("ThingsOS_weidu","MP action=%d account=%s error=%d",
            (int)requested_action,work->requested_book.id,e);
        client->status.phase=e==WD_OK?WD_READY:WD_ERROR;
        /* The progress card shows the account being refreshed, not the book that
           happened to be read before. */
        strlcpy(client->status.title,
            requested_action==WD_DEVICE_MP_ACCOUNTS?"公众号":
            work->requested_book.title[0]?work->requested_book.title:"公众号",
            sizeof(client->status.title));
        strlcpy(client->status.message,e==WD_OK?
            (requested_action==WD_DEVICE_MP_CONTENT?"文章已缓存到公众号文章目录":
             requested_action==WD_DEVICE_MP_ACCOUNTS?"公众号列表已刷新":"文章列表已刷新"):
            e==WD_PROTOCOL?"公众号数据格式不兼容，保留了上次缓存，可稍后重试":
            e==WD_AUTH?"公众号需要重新登录微读账号":
            e==WD_ACCESS?"公众号访问验证未通过，请稍后重试；旧缓存保留":
            e==WD_VERIFY?"公众号需官网安全验证；旧缓存保留":
            e==WD_NETWORK?"公众号刷新失败，网络未连通":
            "公众号刷新失败，保留上次缓存",sizeof(client->status.message));
    } else if(reading_action) {
        strlcpy(reading.book,work->requested_book.id,sizeof(reading.book));
        reading.conflict=false;
        if(requested_action==WD_DEVICE_UPLOAD || requested_action==WD_DEVICE_REPORT)reading.duration=WD_CANCEL;
        if(requested_action==WD_DEVICE_ENTER) {
            e=thingsos_wd_begin(client,&work->upload,&work->read_session);
            reading.progress=e;
            if(e==WD_OK){reading.expired=false;device_record_uploaded(work->upload.id,work->upload.chapter,work->upload.offset,reading.owner,unix_time(NULL));}
            ESP_LOGI("ThingsOS_weidu","Reading session book=%s entered=%d result=%d",work->upload.id,work->read_session.entered,e);
        }else if(requested_action==WD_DEVICE_NOTE_WRITE) {
            unsigned state=THINGSOS_NOTE_FAILED;
            reading.note_book=work->annotation_book;reading.note_id=work->annotation.id;
            reading.note_content_crc=thingsos_annotation_crc(work->annotation.thought,strlen(work->annotation.thought));
            strlcpy(reading.note_owner,work->annotation_owner,sizeof(reading.note_owner));
            e=thingsos_wd_annotation_push(client,reading.book,work->annotation_owner,&work->annotation,&state);
            reading.note_error=e;reading.note_state=state;
            strlcpy(reading.note_bookmark_id,work->annotation.bookmark_id,sizeof(reading.note_bookmark_id));
            strlcpy(reading.note_review_id,work->annotation.review_id,sizeof(reading.note_review_id));
        }else if(requested_action!=WD_DEVICE_UPLOAD && requested_action!=WD_DEVICE_REPORT){
            bool renewed=false;
            if(requested_action==WD_DEVICE_PULL)reading.pulled_at=unix_time(NULL);
            e=thingsos_wd_authenticated_read(client,reading.expired,reading_fetch,&reading,&renewed);
            if(e==WD_OK)reading.expired=false;
            ESP_LOGI("ThingsOS_weidu","Reading auth action=%d renewed=%d result=%d",requested_action,renewed,e);
        }else if(reading.expired)e=WD_AUTH;
        else {
            bool conflict_withheld=false;
            const char* sync_owner=work->queued_generation?work->queued_owner:reading.owner;
            if(work->queued_generation) {
                thingsos_wd_progress_t pulled={0};
                bool renewed=false;
                thingsos_weidu_error_t pe=thingsos_wd_authenticated_read(client,reading.expired,reading_pull_queued,&pulled,&renewed);
                if(pe==WD_OK) {
                    reading.expired=false;reading.cloud=pulled;reading.pull=WD_OK;reading.pulled_at=unix_time(NULL);
                    if(device_queued_conflict(client,&work->upload,work->queued_owner,&pulled)) {
                        reading.conflict=true;conflict_withheld=true;e=WD_OK;reading.progress=WD_OK;
                    }
                } else {
                    conflict_withheld=true;e=pe;reading.progress=pe;
                }
            }
            if(!conflict_withheld) {
                reading.uploaded_at=unix_time(NULL);
                e=thingsos_wd_upload_interval(client,&work->upload,&reading.duration,read_session,requested_action==WD_DEVICE_UPLOAD);reading.progress=e;
                if(e==WD_OK)device_record_uploaded(work->upload.id,work->upload.chapter,work->upload.offset,sync_owner,reading.uploaded_at);
                if(reading.duration==WD_OK && read_session->last_seconds){
                    reading.duration_seconds=read_session->last_seconds;reading.duration_at=reading.uploaded_at;
                    /* Serialized reading jobs already own thingsos_io_lock; re-taking
                       a non-recursive mutex here self-deadlocks both worker and UI. */
                    bool io_owned=thingsos_io_lock &&
                        xSemaphoreGetMutexHolder(thingsos_io_lock)==xTaskGetCurrentTaskHandle();
                    if(!io_owned && thingsos_io_lock)xSemaphoreTake(thingsos_io_lock,portMAX_DELAY);
                    thingsos_weidu_error_t recorded=rt_web_commit(reading.owner,reading.book,read_session->ps,
                        read_session->last_end,read_session->last_seconds);
                    if(!io_owned && thingsos_io_lock)xSemaphoreGive(thingsos_io_lock);
                    portENTER_CRITICAL(&guard);
                    rt_public.today_ready=false;rt_public.today_day=0;
                    if(recorded!=WD_OK)rt_public.error=recorded;
                    portEXIT_CRITICAL(&guard);
                    /* A local accounting failure must never replay an accepted POST. */
                    if(recorded!=WD_OK)reading.duration=recorded;
                }
            }
        }
        if(requested_action==WD_DEVICE_NOTES)reading.notes=e;
        if(requested_action==WD_DEVICE_PERSONAL)reading.personal=e;
        if(requested_action==WD_DEVICE_THOUGHTS){reading.thoughts=e;reading.thought_query=work->thought_query;}
        if(requested_action==WD_DEVICE_PULL)reading.pull=e;
        reading.session_ready=read_session->entered && !read_session->consumed;
        if(requested_action==WD_DEVICE_UPLOAD || requested_action==WD_DEVICE_REPORT){
            reading.progress=e;
            if(e==WD_OK && work->pending_valid && same_progress(&work->pending,&work->upload)){
                work->pending_valid=false;thingsos_wd_pending_clear(client->root);
            }
        }
        if(e==WD_AUTH || reading.duration==WD_AUTH)reading.expired=true;
    } else if(requested_action==WD_DEVICE_RESTORE) {
        if(network_exchange) {
            e=thingsos_wd_cache_bundle(client,&work->requested_book,true,&reading.notes);
            strlcpy(reading.book,work->requested_book.id,sizeof(reading.book));
        }else {
            e=thingsos_weidu_restore(client,&work->requested_book);reading.notes=WD_NETWORK;
            if(e==WD_OK)strlcpy(client->status.message,"正文已恢复，联网后可刷新划线书评",sizeof(client->status.message));
        }
    } else {
        if (requested_action == WD_DEVICE_LOGIN) {
            memset(&work->read_session,0,sizeof(work->read_session));
            memset(work->runtime_cookie, 0, sizeof(work->runtime_cookie));
            memset(work->mp_ticket,0,sizeof(work->mp_ticket));memset(work->mp_wrpa,0,sizeof(work->mp_wrpa));
        }
        /* SNTP is already owned by network.c; do not install a second time service. */
        for (unsigned i = 0; i < 100 && !unix_time(NULL) && !abort_requested; ++i)
            wait_ms(NULL, 100);
        if (abort_requested)
            e = WD_CANCEL;
        else if (!unix_time(NULL))
            e = WD_CLOCK;
        else
            e = requested_action == WD_DEVICE_COVERS ? thingsos_weidu_covers(client,work->cover_books,cover_count)
                : requested_action == WD_DEVICE_LOGIN ? thingsos_weidu_login(client)
                : requested_action == WD_DEVICE_SYNC
                    ? thingsos_weidu_sync(client)
                    : thingsos_wd_cache_bundle(client, &work->requested_book,false,&reading.notes);
        if (e == WD_AUTH && (requested_action==WD_DEVICE_SYNC || requested_action==WD_DEVICE_DOWNLOAD) && client->session.rt[0]) {
            thingsos_weidu_error_t renewed = thingsos_weidu_renew(client);
            if (renewed == WD_OK)
                e = requested_action == WD_DEVICE_SYNC
                        ? thingsos_weidu_sync(client)
                        : thingsos_wd_cache_bundle(client, &work->requested_book,false,&reading.notes);
            else
                e = renewed;
        }
        if (e == WD_AUTH && requested_action != WD_DEVICE_LOGIN) {
            memset(&client->session, 0, sizeof(client->session));
            session(NULL, &client->session, true);
        } else if (e != WD_OK && requested_action == WD_DEVICE_LOGIN)
            session(NULL, &client->session, false);
    }
    if(cache_job)xSemaphoreTake(thingsos_io_lock,portMAX_DELAY);
    if(abort_requested && cache_job)e=WD_CANCEL;
    if (e == WD_OK && requested_action == WD_DEVICE_DOWNLOAD &&
        !session(NULL, &client->session, true))
        e = WD_STORAGE;
    if(requested_action==WD_DEVICE_DOWNLOAD || requested_action==WD_DEVICE_RESTORE) {
        strlcpy(reading.book,work->requested_book.id,sizeof(reading.book));
        ESP_LOGI("ThingsOS_weidu","Bundle completed book=%s book_error=%d notes_error=%d",reading.book,e,reading.notes);
    }
    if(!reading_action)ESP_LOGI("ThingsOS_weidu","Operation completed action=%d error=%d chapters=%u/%u cached=%d",requested_action,e,client->status.completed,client->status.total,client->status.path[0]!=0);
    else ESP_LOGI("ThingsOS_weidu","Reading completed action=%d error=%d duration=%d",requested_action,e,reading.duration);
    if(requested_action==WD_DEVICE_LOGIN && e==WD_OK){
        char owner[33];thingsos_weidu_md5(client->session.vid,strlen(client->session.vid),owner);
        if(strcmp(owner,reading.owner)){memset(&reading,0,sizeof(reading));reading.pull=reading.progress=reading.duration=reading.notes=WD_CANCEL;}
        strlcpy(reading.owner,owner,sizeof(reading.owner));reading.expired=false;
        rt_loaded=false;
        if(work->pending_valid && !strcmp(work->pending_owner,owner)){
            thingsos_weidu_error_t ignored;reading.progress=thingsos_wd_upload(client,&work->pending,&ignored);reading.uploaded_at=unix_time(NULL);
            if(reading.progress==WD_OK){work->pending_valid=false;thingsos_wd_pending_clear(client->root);}
            if(reading.progress==WD_AUTH)reading.expired=true;
        }else {work->pending_valid=false;thingsos_wd_pending_clear(client->root);}
    }
    client->status.authenticated = thingsos_weidu_session_valid(&client->session) && !reading.expired;
    if(work->queued_generation) {
        if(e==WD_OK)thingsos_wd_queue_ack(client->root,work->queued_owner,work->upload.id,work->queued_generation);
        work->queue_retry_at=now_ms(NULL)+(e==WD_OK?2000:60000);
        work->queued_generation=0;
    }
    if (e != WD_OK) {
        bool detailed=client->status.phase==WD_ERROR;
        /* A confirmed QR that failed during the first shelf sync left a valid
           saved session; report the sync instead of a login or Wi-Fi failure. */
        bool sync_failed=requested_action==WD_DEVICE_LOGIN && client->status.phase==WD_SHELF &&
                         thingsos_weidu_session_valid(&client->session);
        client->status.phase = e == WD_CANCEL ? WD_CANCELLED : WD_ERROR;
        if(!detailed) strlcpy(
            client->status.message, thingsos_weidu_error_text(e), sizeof(client->status.message));
        if(sync_failed && e != WD_CANCEL) strlcpy(
            client->status.message, "登录已成功，书架同步未完成，请点同步重试",
            sizeof(client->status.message));
        /* The station dropped during this job and replays did not recover:
           name the real cause instead of a generic timeout or TLS failure. */
        else if((e==WD_NETWORK || e==WD_DNS) && thingsos_network_link_losses()!=link_losses_at_start)
            strlcpy(client->status.message, thingsos_weidu_link_lost_text(),
                    sizeof(client->status.message));
    } else if (requested_action == WD_DEVICE_LOGOUT) {
        client->status.phase = WD_IDLE;
        strcpy(client->status.message, "已退出登录，离线图书保留在书库");
    } else if(reading_action || rt_job) {
        client->status.phase=WD_READY;
        client->status.message[0]=0;
    }
    if(requested_action==WD_DEVICE_COVERS){
        bool changed=false;
        for(size_t i=0;i<cover_count;++i) {
            snprintf(work->worker_path,sizeof(work->worker_path),"%s/covers/%.96s.jpg",client->root,work->cover_books[i].id);
            struct stat st;if(!stat(work->worker_path,&st) && st.st_size>0)changed=true;
        }
        if(changed)++cover_serial;
        client->status.phase=WD_READY;client->status.message[0]=0;
    }
    channel_close(&download_channel);
    radio_restore(&radio);
    ESP_LOGI("ThingsOS_weidu", "Exchange completed action=%d network=%d error=%d elapsed=%u ms",
             requested_action, network_exchange, e, (unsigned)(now_ms(NULL)-operation_started));
    thingsos_network_external_end();
    unsigned cancel_at=atomic_load(&cancel_requested_at);
    if(cancel_at)ESP_LOGI("ThingsOS_weidu","Cancel cleanup elapsed=%u ms",(unsigned)now_ms(NULL)-cancel_at);
    ESP_LOGI("ThingsOS_weidu", "Worker stack margin=%u heap internal=%u largest=%u action=%d stack=%u",
             (unsigned)uxTaskGetStackHighWaterMark(NULL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             requested_action, WEIDU_WORKER_STACK);
    if(peak_window) {
        size_t low=heap_caps_get_minimum_free_size(peak_caps);
        heap_caps_monitor_local_minimum_free_size_stop();
        ESP_LOGI("ThingsOS_weidu","Job peak action=%d main_before=%u main_low=%u peak_use=%u",
                 requested_action,(unsigned)peak_before,(unsigned)low,(unsigned)(peak_before>low?peak_before-low:0));
        thingsos_memory_trace("weidu-peak",(int)requested_action,(int)(peak_before>low?peak_before-low:0));
    }
    thingsos_memory_trace("weidu-finished",(int)requested_action,(int)e);
    thingsos_memory_trace("weidu-stack",(int)requested_action,(int)uxTaskGetStackHighWaterMark(NULL));
    ESP_LOGI("ThingsOS_weidu","DMA free=%u largest=%u PSRAM free=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    if(requested_action==WD_DEVICE_PULL || requested_action==WD_DEVICE_UPLOAD ||
       requested_action==WD_DEVICE_REPORT || requested_action==WD_DEVICE_ENTER) {
        reading.diagnostic=work->request_diagnostic;
        reading.diagnostic.error=e;
        reading.diagnostic.action=(unsigned)requested_action;
    }
    portENTER_CRITICAL(&guard);reading.serial=work->reading.serial+1;work->reading=reading;portEXIT_CRITICAL(&guard);
    active = false;
    client->status.busy = false;
    ++client->status.serial;
    publish(NULL, &client->status);
    xSemaphoreGive(thingsos_io_lock);
    vTaskDelete(NULL);
}
bool thingsos_weidu_background_ready(void) {
    const uint32_t caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT;
    return thingsos_wd_background_memory_ready(
        heap_caps_get_free_size(caps), heap_caps_get_largest_free_block(caps));
}

static thingsos_weidu_start_result_t start_job(thingsos_weidu_action_t action, const thingsos_weidu_book_t* book) {
    if (active) return WD_START_BUSY;
    if ((action == WD_DEVICE_DOWNLOAD || action==WD_DEVICE_RESTORE || action==WD_DEVICE_SHELF_ADD) && !book)
        return WD_START_INVALID;
    if (!thingsos_weidu_device_init()) return WD_START_INIT;
    if(action==WD_DEVICE_RT_AUTH || action==WD_DEVICE_RT_UPLOAD)rt_load();
    if(action==WD_DEVICE_RT_UPLOAD && (unix_time(NULL)<rt_next || rt_public.blocked || rt_public.expired || !rt_public.authorized))return WD_START_INVALID;
    thingsos_sd_info_t sd;
    if (thingsos_board_sd_info(&sd) != ESP_OK || !sd.mounted) return WD_START_STORAGE;
    if (action != WD_DEVICE_LOGOUT && action!=WD_DEVICE_RESTORE && !thingsos_network_connected()) return WD_START_OFFLINE;
    if(reading_action_is(action) && !thingsos_wd_memory_ready(
        heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
        heap_caps_get_free_size(MALLOC_CAP_SPIRAM),heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM))){
        client->status.background=reading_action_is(action);
        client->status.phase=WD_ERROR;strlcpy(client->status.message,thingsos_weidu_error_text(WD_MEMORY),sizeof(client->status.message));
        thingsos_memory_trace("weidu-refused",(int)action,WD_MEMORY);
        ++client->status.serial;publish(NULL,&client->status);return WD_START_MEMORY;
    }
    if (!thingsos_network_external_begin()) return WD_START_BUSY;
    thingsos_memory_trace("weidu-request",(int)action,0);
    abort_requested = false;
    atomic_store(&cancel_requested_at,0);
    if(action==WD_DEVICE_SYNC)memset(work->cover_attempts,0,sizeof(work->cover_attempts));
    client->status.background=action==WD_DEVICE_COVERS || action==WD_DEVICE_CHAPTER ||
        action==WD_DEVICE_SEARCH || action==WD_DEVICE_RT_UPLOAD || reading_action_is(action);
    requested_action = action;
    if (book) work->requested_book = *book;
    client->status.path[0] = client->status.qr[0] = 0;
    client->status.completed = client->status.total = 0;
    client->status.phase = (action == WD_DEVICE_LOGIN || action==WD_DEVICE_RT_AUTH) ? WD_LOGIN
                           : (action == WD_DEVICE_DOWNLOAD || action==WD_DEVICE_RESTORE || action==WD_DEVICE_CHAPTER) ? WD_DOWNLOAD
                                                          : WD_SHELF;
    strlcpy(client->status.message, action==WD_DEVICE_RT_AUTH?"正在获取时长授权二维码":action==WD_DEVICE_CHAPTER?"正在加载阅读章节":
        action==WD_DEVICE_SEARCH?"正在搜索线上书籍":action==WD_DEVICE_SHELF_ADD?"正在加入书架":reading_action_is(action)?"正在后台同步阅读状态":
        action>=WD_DEVICE_MP_ACCOUNTS && action<=WD_DEVICE_MP_CONTENT?"正在获取公众号数据":"正在准备微读服务",
        sizeof(client->status.message));
    active = true;
    ++client->status.serial;
    publish(NULL, &client->status);
    if (xTaskCreate(worker, "weidu", WEIDU_WORKER_STACK, NULL, 2, NULL) != pdPASS) {
        thingsos_memory_trace("weidu-start-failed",(int)action,WD_MEMORY);
        ESP_LOGW("ThingsOS_weidu", "Worker start failed action=%d internal=%u largest=%u", action,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        active = false;
        thingsos_network_external_end();
        client->status.phase = WD_ERROR;
        strlcpy(
            client->status.message, thingsos_weidu_error_text(WD_MEMORY),
            sizeof(client->status.message));
        ++client->status.serial;
        publish(NULL, &client->status);
        return WD_START_MEMORY;
    }
    return WD_START_OK;
}
bool thingsos_weidu_start(thingsos_weidu_action_t action, const thingsos_weidu_book_t* book) {
    return start_job(action, book) == WD_START_OK;
}
bool thingsos_weidu_device_shelf(
    size_t start, thingsos_weidu_book_t* books, size_t cap, size_t* count, size_t* total) {
    if (!client || (active && requested_action != WD_DEVICE_COVERS && requested_action < WD_DEVICE_NOTES)) {
        *count = *total = 0;
        return false;
    }
    return thingsos_weidu_shelf_page(
        client->root, &client->session, start, books, cap, count, total);
}
bool thingsos_weidu_device_search(size_t start,thingsos_weidu_book_t* books,size_t cap,size_t* count,size_t* total,const char* query) {
    /* Notes/progress only touch their own files. The committed shelf remains
       available while those background requests release the SD/UI lock. */
    if(!client || (active && requested_action!=WD_DEVICE_COVERS && requested_action!=WD_DEVICE_SEARCH && requested_action<WD_DEVICE_NOTES)) {*count=*total=0;return false;}
    size_t local_count=0,local_total=0;
    if(!thingsos_weidu_shelf_search(client->root,&client->session,start,books,cap,&local_count,&local_total,query))return false;
    /* Shelf results precede online results; suppress the same ID appearing
       twice, including immediately after Add to shelf. */
    thingsos_weidu_book_t* remote=heap_caps_calloc(20,sizeof(*remote),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(!remote)return false;
    size_t remote_count=0;
    if(query && work->online_search_ready && !strcmp(query,work->online_search_query))
        for(unsigned i=0;i<work->online_search_count;++i) {
            if(work->online_search_member[i] && strstr(work->online_search_books[i].title,query))continue;
            remote[remote_count++]=work->online_search_books[i];
        }
    if(total)*total=local_total+remote_count;
    *count=local_count;
    if(start+local_count>=local_total && remote_count && *count<cap) {
        size_t offset=start>local_total?start-local_total:0;
        if(offset<remote_count) {
            size_t n=remote_count-offset;if(n>cap-*count)n=cap-*count;
            memcpy(books+*count,remote+offset,n*sizeof(*books));*count+=n;
        }
    } else if(start>=local_total && remote_count) {
        size_t offset=start-local_total;
        *count=offset<remote_count?remote_count-offset:0;if(*count>cap)*count=cap;
        if(*count)memcpy(books,remote+offset,*count*sizeof(*books));
    }
    free(remote);
    return true;
}
bool thingsos_weidu_device_membership(const thingsos_weidu_book_t* books,size_t count,bool* members) {
    return client && thingsos_weidu_shelf_membership(client->root,&client->session,books,count,members);
}
bool thingsos_weidu_device_search_online(const char* query) {
    if(!query || !query[0] || !thingsos_weidu_device_init() ||
       !thingsos_weidu_session_valid(&client->session))return false;
    if(work->online_search_ready && !strcmp(query,work->online_search_query))return true;
    if(active)return false;
    strlcpy(work->search_query,query,sizeof(work->search_query));
    work->online_search_ready=false;work->online_search_count=0;
    return thingsos_weidu_start(WD_DEVICE_SEARCH,NULL);
}
bool thingsos_weidu_device_cached(const char* id, char* path, size_t cap) {
    return thingsos_weidu_cached("/sdcard/books", id, path, cap);
}
bool thingsos_mp_device_request(thingsos_weidu_action_t action,const thingsos_weidu_book_t* account,const thingsos_weidu_book_t* article) {
    if(action<WD_DEVICE_MP_ACCOUNTS || action>WD_DEVICE_MP_CONTENT || active || !thingsos_weidu_device_init())return false;
    if(action!=WD_DEVICE_MP_ACCOUNTS && (!account || !thingsos_mp_account_id(account->id)))return false;
    if(action==WD_DEVICE_MP_CONTENT){if(!article)return false;work->cover_books[0]=*account;}
    return thingsos_weidu_start(action,action==WD_DEVICE_MP_CONTENT?article:account);
}
bool thingsos_mp_device_page(const char* id,size_t start,thingsos_weidu_book_t* books,size_t cap,size_t* count,size_t* total) {
    if(!client){*count=*total=0;return false;}
    return thingsos_mp_page(client->root,&client->session,id,start,books,cap,count,total);
}
bool thingsos_mp_device_cached(const thingsos_weidu_book_t* account,const thingsos_weidu_book_t* article,char* path,size_t cap) {
    return client && thingsos_mp_cached(MP_OUTPUT,&client->session,account,article,path,cap);
}

void thingsos_weidu_device_covers(const thingsos_weidu_book_t* books,size_t count) {
    if(active || !client || !thingsos_network_connected() || !count || count>9)return;
    cover_count=0;
    const int64_t started_ms=(int64_t)(esp_timer_get_time()/1000);
    for(size_t i=0;i<count;++i) {
        uint32_t key=2166136261u;for(const char* p=books[i].id;*p;++p)key=(key^(unsigned char)*p)*16777619u;
        if(!key)key=1;
        char path[640];snprintf(path,sizeof(path),"%s/covers/%.96s.jpg",client->root,books[i].id);
        FILE* f=fopen(path,"rb");if(f){fclose(f);continue;}
        cover_attempt_t* slot=NULL;
        for(unsigned j=0;j<64;++j)if(work->cover_attempts[j].key==key)slot=&work->cover_attempts[j];
        /* A transient connect failure must not strand the cover for the
           session: retry 30 s later, at most three times, then give up. */
        if(slot && (slot->tries>=3 || started_ms-slot->at<30000))continue;
        if(!slot){slot=&work->cover_attempts[cover_attempt_next++%64];
            slot->key=key;slot->tries=0;slot->at=0;}
        if(slot->tries)ESP_LOGI("ThingsOS_weidu","Cover retry %u/3",(unsigned)slot->tries+1);
        slot->tries++;slot->at=started_ms;
        work->cover_books[cover_count++]=books[i];
    }
    if(cover_count)thingsos_weidu_start(WD_DEVICE_COVERS,NULL);
}

bool thingsos_weidu_device_partial(const char* id) {unsigned count;return client && thingsos_weidu_partial(client->root,id,&count);}
bool thingsos_weidu_device_chapter(const thingsos_weidu_book_t* book,unsigned index) {
    return thingsos_weidu_device_chapters(book,index,1,false)==WD_START_OK;
}
thingsos_weidu_start_result_t thingsos_weidu_device_chapters(const thingsos_weidu_book_t* book,unsigned index,unsigned count,bool prepare_cover) {
    if(active)return WD_START_BUSY;
    if(!book || !count || count>3)return WD_START_INVALID;
    if(!thingsos_weidu_device_init())return WD_START_INIT;
    work->online_index=index;work->online_count=count;work->online_prepare_cover=prepare_cover;
    return start_job(WD_DEVICE_CHAPTER,book);
}

void thingsos_wd_device_status(thingsos_wd_device_status_t* out){
    portENTER_CRITICAL(&guard);if(work)*out=work->reading;else {memset(out,0,sizeof(*out));out->pull=out->progress=out->duration=out->notes=WD_CANCEL;}portEXIT_CRITICAL(&guard);
}
bool thingsos_wd_device_request(thingsos_weidu_action_t action,const char* id,const thingsos_wd_upload_t* upload){
    if((action!=WD_DEVICE_ENTER && action!=WD_DEVICE_REPORT && action!=WD_DEVICE_PERSONAL && (action<WD_DEVICE_NOTES || action>WD_DEVICE_UPLOAD)) || active || !thingsos_weidu_device_init())return false;
    if(!id || strlen(id)>=sizeof(work->requested_book.id))return false;
    memset(&work->requested_book,0,sizeof(work->requested_book));strlcpy(work->requested_book.id,id,sizeof(work->requested_book.id));
    if(upload)work->upload=*upload;
    if(!thingsos_weidu_start(action,&work->requested_book))return false;
    return true;
}
bool thingsos_wd_device_notes(const char* id,thingsos_wd_notes_info_t* info){return client && thingsos_wd_notes_view(client->root,&client->session,id,info);}
bool thingsos_wd_device_personal_info(const char* id,unsigned* count){*count=0;return client && thingsos_wd_personal_info(client->root,&client->session,id,count);}
bool thingsos_wd_device_personal(const char* id,unsigned index,thingsos_wd_personal_t* out){return client && thingsos_wd_personal(client->root,&client->session,id,index,out);}
bool thingsos_wd_device_personal_find(const char* id,const char* uid,uint32_t begin,uint32_t end,thingsos_wd_personal_t* out){return client && thingsos_wd_personal_find(client->root,&client->session,id,uid,begin,end,out);}
bool thingsos_wd_device_annotation_request(const char* id,uint64_t book,const char* owner,const thingsos_annotation_t* note) {
    if(!id || !book || !owner || strlen(owner)!=32 || !note || active || !thingsos_weidu_device_init())return false;
    work->annotation=*note;work->annotation_book=book;strlcpy(work->annotation_owner,owner,sizeof(work->annotation_owner));
    memset(&work->requested_book,0,sizeof(work->requested_book));strlcpy(work->requested_book.id,id,sizeof(work->requested_book.id));
    return thingsos_weidu_start(WD_DEVICE_NOTE_WRITE,&work->requested_book);
}
bool thingsos_wd_device_note(const char* id,unsigned kind,unsigned index,thingsos_wd_note_t* note){return client && thingsos_wd_note(client->root,&client->session,id,kind,index,note);}
bool thingsos_wd_device_thought_request(const thingsos_wd_thought_query_t* q){
    if(!q || active || !thingsos_weidu_device_init())return false;
    work->thought_query=*q;memset(&work->requested_book,0,sizeof(work->requested_book));
    strlcpy(work->requested_book.id,q->id,sizeof(work->requested_book.id));
    return thingsos_weidu_start(WD_DEVICE_THOUGHTS,&work->requested_book);
}
bool thingsos_wd_device_thought_info(const thingsos_wd_thought_query_t* q,thingsos_wd_thought_info_t* info){return client && thingsos_wd_thought_info(client->root,&client->session,q,info);}
bool thingsos_wd_device_thought(const thingsos_wd_thought_query_t* q,unsigned i,thingsos_wd_note_t* n){return client && thingsos_wd_thought(client->root,&client->session,q,i,n);}
bool thingsos_wd_device_line_note(const char* id,unsigned chapter,const thingsos_wd_map_t* map,const thingsos_chapter_t* text,size_t begin,size_t end,thingsos_wd_note_t* n){
    thingsos_weidu_chapter_t toc;return client && thingsos_wd_toc(client->root,id,chapter,&toc) && thingsos_wd_line_note(client->root,&client->session,id,toc.uid,map,text,begin,end,n);
}

bool thingsos_wd_device_defer(const thingsos_wd_upload_t* in){
    if(!thingsos_weidu_device_init())return false;
    if(!in){work->pending_valid=false;thingsos_wd_pending_clear(client->root);return true;}
    if(!work->reading.owner[0])return false;
    if(!thingsos_wd_pending_save(client->root,work->reading.owner,in))return false;
    work->pending=*in;work->pending.seconds=0;work->pending.report=false;
    strlcpy(work->pending_owner,work->reading.owner,sizeof(work->pending_owner));work->pending_valid=true;
    return true;
}
bool thingsos_wd_device_schedule(const thingsos_wd_upload_t* in) {
    if(!in || !thingsos_weidu_device_init() || !work->reading.owner[0])return false;
    uint64_t generation=0;
    if(!thingsos_wd_queue_put(client->root,work->reading.owner,in,&generation))return false;
    unsigned slot=8;
    for(unsigned i=0;i<8;++i) {
        deferred_read_t* q=&work->deferred[i];
        if(q->valid && !strcmp(q->owner,work->reading.owner) && !strcmp(q->upload.id,in->id)){slot=i;break;}
        if(!q->valid && slot==8)slot=i;
    }
    if(slot<8) {
        deferred_read_t* q=&work->deferred[slot];
        q->valid=true;q->upload=*in;q->session=work->read_session;q->generation=generation;
        strlcpy(q->owner,work->reading.owner,sizeof(q->owner));
        /* Consume the live session now. A later book never inherits its time. */
        /* A report/enter already in flight may still borrow the live session.
           Its caller has accounted for that interval; never reset its storage. */
        if(!active && !strcmp(work->read_session.id,in->id))memset(&work->read_session,0,sizeof(work->read_session));
    }
    if(!active)work->queue_retry_at=0;
    return true; /* Progress is durable even if the optional time snapshot is unavailable. */
}
void thingsos_wd_device_poll(bool idle) {
    if(!idle || !work || active || !thingsos_network_connected() || now_ms(NULL)<work->queue_retry_at)return;
    if(!thingsos_weidu_background_ready())return;
    thingsos_wd_upload_t in;uint64_t generation=0;
    if(!thingsos_wd_queue_peek(client->root,work->reading.owner,&in,&generation)) {
        work->queue_retry_at=now_ms(NULL)+5000;return;
    }
    unsigned chosen=8;
    memset(&work->queued_session,0,sizeof(work->queued_session));
    for(unsigned i=0;i<8;++i) {
        deferred_read_t* q=&work->deferred[i];
        if(q->valid && q->generation==generation && !strcmp(q->owner,work->reading.owner) && !strcmp(q->upload.id,in.id)) {
            in=q->upload;work->queued_session=q->session;chosen=i;break;
        }
    }
    /* The persisted retry has seconds=0/report=false. A duration snapshot is
       consumed once when started, even if its response is lost. */
    work->queued_generation=generation;
    strlcpy(work->queued_owner,work->reading.owner,sizeof(work->queued_owner));
    if(thingsos_wd_device_request(WD_DEVICE_UPLOAD,in.id,&in)) {
        if(chosen<8)work->deferred[chosen].valid=false;
    } else {
        /* Start refused (e.g. an audio session holds the network claim). Back
           off: retrying every UI pass re-read the SD queue ~25 times/s. */
        work->queued_generation=0;
        work->queue_retry_at=now_ms(NULL)+5000;
    }
}
bool thingsos_wd_device_sync_load(const char* id,const char* owner,thingsos_wd_sync_t* out) {
    if(!out)return false;
    memset(out,0,sizeof(*out));
    const char* resolved=device_sync_owner(owner);
    return client && resolved[0] && thingsos_wd_sync_load(client->root,resolved,id,out);
}
bool thingsos_wd_device_sync_save(const char* id,const char* owner,const thingsos_wd_sync_t* in) {
    const char* resolved=device_sync_owner(owner);
    return client && resolved[0] && thingsos_wd_sync_save(client->root,resolved,id,in);
}

bool thingsos_wd_device_lines(const char* id,unsigned chapter,const thingsos_wd_map_t* map,const thingsos_chapter_t* text,thingsos_text_underline_t* lines,size_t* count,size_t* total){
    thingsos_weidu_chapter_t toc;if(!client||!thingsos_wd_toc(client->root,id,chapter,&toc))return false;
    return thingsos_wd_lines(client->root,&client->session,id,toc.uid,map,text,lines,WD_DRAW_LIMIT,count,total);
}
