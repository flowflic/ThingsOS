/* SPDX-License-Identifier: Apache-2.0
 * Native E-Ink transport/auth adapter. Endpoint contracts reference
 * teng-lin/weread-omni@88bd2e095d7d7ee423eaadf8f40653e72c5be6d4 (MIT).
 * MiuRead #184/#187 inform send-time claiming; no Web fallback or POST retry.
 * Protocol implementation and its licenses are recorded separately.
 */
#include "cJSON.h"
#include <ctype.h>
#include <sys/time.h>
#include "eink_readtime_totals.inc"
static uint64_t rt_wall_ms(void){
    struct timeval stamp;gettimeofday(&stamp,NULL);
    return (uint64_t)stamp.tv_sec*1000+(uint32_t)stamp.tv_usec/1000;
}
static thingsos_rt_device_status_t rt_public;
static uint32_t rt_next;
static bool rt_loaded;
static bool rt_floor(const char *owner, uint32_t *floor, bool write) {
    if (!owner || strlen(owner) != 32)
        return false;
    char space[16];
    snprintf(space, sizeof(space), "rt-%.12s", owner);
    nvs_handle_t h;
    esp_err_t e = nvs_open(space, write ? NVS_READWRITE : NVS_READONLY, &h);
    if (e == ESP_ERR_NVS_NOT_FOUND && !write) {
        *floor = 0;
        return true;
    }
    if (e != ESP_OK)
        return false;
    char saved[33] = {0};
    size_t size = sizeof(saved);
    esp_err_t identity = nvs_get_str(h, "owner", saved, &size);
    bool ok = (identity == ESP_ERR_NVS_NOT_FOUND) || (identity == ESP_OK && !strcmp(saved, owner));
    uint32_t prior = 0;
    esp_err_t read = nvs_get_u32(h, "floor", &prior);
    if (read != ESP_OK && read != ESP_ERR_NVS_NOT_FOUND)
        ok = false;
    if (write && ok) {
        if (*floor < prior)
            *floor = prior;
        ok = nvs_set_str(h, "owner", owner) == ESP_OK &&
             nvs_set_u32(h, "floor", *floor) == ESP_OK && nvs_commit(h) == ESP_OK;
    } else if (!write)
        *floor = prior;
    nvs_close(h);
    return ok;
}
static void rt_publish(const thingsos_rt_device_status_t *s) {
    portENTER_CRITICAL(&guard);
    rt_public = *s;
    portEXIT_CRITICAL(&guard);
}
static void rt_load(void);
static void rt_daily_refresh(thingsos_rt_device_status_t* s) {
    uint32_t stamp=unix_time(NULL);
    s->today_day=rt_reading_day(stamp,480);
    s->today_ready=rt_daily_read(work->reading.owner,stamp,&s->today_online,&s->today_native);
    if(s->error==WD_STORAGE)s->today_ready=false;
}
void thingsos_wd_rt_status(thingsos_rt_device_status_t *s) {
    /* A settings visit can precede enabling uploads or opening a book. Restore
       the current account's saved Native grant before inviting another scan;
       an active worker already loaded it and owns all ledger access. */
    if(client&&!active&&!rt_loaded)rt_load();
    portENTER_CRITICAL(&guard);
    *s = rt_public;
    portEXIT_CRITICAL(&guard);
    if(client&&!active && (!s->today_ready||s->today_day!=rt_reading_day(unix_time(NULL),480))) {
        rt_daily_refresh(s);rt_publish(s);
    }
}
static bool rt_store_raw(thingsos_rt_credentials_t *c, bool write) {
    nvs_handle_t h;
    if (nvs_open("thingsos-eink", write ? NVS_READWRITE : NVS_READONLY, &h) != ESP_OK)
        return false;
    size_t size = sizeof(*c);
    esp_err_t e =
        write ? nvs_set_blob(h, "credentials", c, size) : nvs_get_blob(h, "credentials", c, &size);
    if (write && e == ESP_OK)
        e = nvs_commit(h);
    nvs_close(h);
    if (e != ESP_OK || size != sizeof(*c))
        return false;
    return memchr(c->vid, 0, sizeof(c->vid)) && memchr(c->access, 0, sizeof(c->access)) &&
           memchr(c->refresh, 0, sizeof(c->refresh)) && memchr(c->device, 0, sizeof(c->device)) &&
           c->access[0] && c->refresh[0] && c->device[0] && !strcmp(c->vid, client->session.vid);
}
static bool rt_io_take(void) {
    if (!thingsos_io_lock || xSemaphoreGetMutexHolder(thingsos_io_lock) == xTaskGetCurrentTaskHandle())
        return false;
    xSemaphoreTake(thingsos_io_lock, portMAX_DELAY);
    return true;
}
static void rt_io_give(bool taken) {
    if (taken && thingsos_io_lock)
        xSemaphoreGive(thingsos_io_lock);
}
static bool rt_store(thingsos_rt_credentials_t *c, bool write) {
    bool locked = write ? rt_io_take() : false;
    bool ok = rt_store_raw(c, write);
    rt_io_give(locked);
    return ok;
}
static void rt_cooldown(uint32_t next, bool blocked) {
    rt_next = next;
    nvs_handle_t h;
    if (nvs_open("thingsos-eink", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u32(h, "next", next);
        nvs_set_u8(h, "blocked", blocked);
        nvs_commit(h);
        nvs_close(h);
    }
}
static void rt_summary_update(thingsos_rt_device_status_t *s) {
    rt_daily_refresh(s);
    if (!thingsos_rt_summary(client->root, work->reading.owner, &s->queue))
        s->error = WD_STORAGE;
    if (s->queue.damaged || s->queue.full)
        s->error = s->queue.full ? WD_LIMIT : WD_STORAGE;
}
static void rt_load(void) {
    if (rt_loaded)
        return;
    rt_loaded = true;
    nvs_handle_t h;
    uint8_t blocked = 0;
    if (nvs_open("thingsos-eink", NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u32(h, "next", &rt_next);
        nvs_get_u8(h, "blocked", &blocked);
        nvs_close(h);
    }
    thingsos_rt_device_status_t s = {0};
    s.blocked = blocked != 0;
    thingsos_rt_credentials_t *c = thingsos_psram_calloc(1, sizeof(*c));
    if (c) {
        s.authorized = rt_store(c, false);
        free(c);
    }
    if (work->reading.owner[0]) {
        rt_summary_update(&s);
        char last[65];
        s.uploaded_ready=rt_uploaded_load(work->reading.owner,&s.uploaded,last);
        if(!s.uploaded_ready)s.error=WD_STORAGE;
        if (!rt_floor(work->reading.owner, &s.capture_floor, false))
            s.error = WD_STORAGE;
    }
    rt_publish(&s);
}
bool thingsos_wd_rt_flush(thingsos_rt_capture_t *capture) {
    if (active || !capture || !thingsos_weidu_device_init())
        return false;
    rt_load();
    mkdir("/sdcard/.thingsos", 0775);
    mkdir(client->root, 0775);
    uint32_t before = 0, after = 0, saved = capture->saved_rows;
    bool same_owner = true, ok = true;
    for (unsigned i = 0; i < capture->count; ++i) {
        thingsos_rt_capture_row_t *row = &capture->rows[i];
        if (strcmp(row->owner, work->reading.owner))
            same_owner = false;
        else
            before += row->milliseconds / 1000;
        uint32_t floor = row->floor;
        if (!rt_floor(row->owner, &floor, true))
            ok = false;
    }
    if (ok)
        ok = thingsos_rt_flush(capture, client->root);
    thingsos_rt_device_status_t status;
    thingsos_wd_rt_status(&status);
    for (unsigned i = 0; i < capture->count; ++i)
        if (!strcmp(capture->rows[i].owner, work->reading.owner))
            after += capture->rows[i].milliseconds / 1000;
    status.queue.pending += before - after;
    status.queue.rows += capture->saved_rows - saved;
    if (!ok || !same_owner)
        rt_summary_update(&status);
    status.queue.full = status.queue.rows >= WD_RT_CAPACITY;
    if (!ok)
        status.error = status.queue.full ? WD_LIMIT : WD_STORAGE;
    else if (status.error == WD_STORAGE && !status.queue.damaged)
        status.error = WD_OK;
    if (work->reading.owner[0] && !rt_floor(work->reading.owner, &status.capture_floor, false)) {
        status.error = WD_STORAGE;
        ok = false;
    }
    rt_publish(&status);
    return ok;
}
bool thingsos_wd_rt_resume(void) {
    if (active)
        return false;
    thingsos_rt_device_status_t s;
    thingsos_wd_rt_status(&s);
    s.blocked = false;
    rt_cooldown(rt_next, false);
    rt_publish(&s);
    return true;
}
void thingsos_wd_rt_poll(bool enabled, bool idle) {
    if (!idle || active || !client)
        return;
    rt_load();
    if (!enabled || !thingsos_network_connected() || unix_time(NULL) < WD_SYNC_MIN_TIME ||
        unix_time(NULL) < rt_next || !thingsos_weidu_background_ready())
        return;
    thingsos_rt_device_status_t s;
    thingsos_wd_rt_status(&s);
    if (s.authorized && !s.expired && !s.blocked && s.queue.pending && !s.queue.damaged){
        if(!thingsos_weidu_start(WD_DEVICE_RT_UPLOAD,NULL))rt_next=unix_time(NULL)+5;
    }
}
#include "eink_readtime_transport.inc"
static const char *rt_string(cJSON *value, const char *key) {
    cJSON *p = cJSON_GetObjectItemCaseSensitive(value, key);
    return cJSON_IsString(p) ? p->valuestring : NULL;
}
static bool rt_copy(cJSON *value, const char *key, char *out, size_t size) {
    const char *p = rt_string(value, key);
    if (!p || !*p || strlen(p) >= size)
        return false;
    for (const unsigned char *q = (const unsigned char *)p; *q; ++q)
        if (*q < 32 || *q == 127)
            return false;
    strcpy(out, p);
    return true;
}
static bool rt_tokens(cJSON *data, thingsos_rt_credentials_t *c) {
    char vid[128];
    const char *v = rt_string(data, "vid");
    cJSON *number = cJSON_GetObjectItemCaseSensitive(data, "vid");
    if (!v && cJSON_IsNumber(number)) {
        snprintf(vid, sizeof(vid), "%.0f", number->valuedouble);
        v = vid;
    }
    if (!v || strcmp(v, client->session.vid))
        return false;
    strcpy(c->vid, v);
    if (!rt_copy(data, "accessToken", c->access, sizeof(c->access)))
        return false;
    if (rt_string(data, "refreshToken") &&
        !rt_copy(data, "refreshToken", c->refresh, sizeof(c->refresh)))
        return false;
    return c->refresh[0] && c->device[0];
}
static void rt_digits(char *p, unsigned n) {
    for (unsigned i = 0; i < n; ++i)
        p[i] = (char)('0' + esp_random() % 10);
    p[n] = 0;
}
static cJSON *rt_login_body(const thingsos_rt_credentials_t *c) {
    uint64_t time = rt_wall_ms();
    unsigned random = esp_random() % 999 + 1;
    char seed[192], sig[65];
    snprintf(seed, sizeof(seed), "%llu%s%u", (unsigned long long)time, c->device, random);
    if (!thingsos_rt_sha256(seed, sig))
        return NULL;
    cJSON *b = cJSON_CreateObject();
    cJSON_AddStringToObject(b, "deviceId", c->device);
    cJSON_AddStringToObject(b, "deviceName", "BOOX");
    cJSON_AddNumberToObject(b, "deviceType", 3);
    cJSON_AddNumberToObject(b, "timestamp", (double)time);
    cJSON_AddNumberToObject(b, "random", random);
    cJSON_AddStringToObject(b, "trackId", "");
    cJSON_AddStringToObject(b, "signature", sig);
    return b;
}
static thingsos_weidu_error_t rt_login(cJSON *body, thingsos_rt_credentials_t *c) {
    if (!body)
        return WD_MEMORY;
    char *text = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    if (!text)
        return WD_MEMORY;
    cJSON *result = NULL;
    thingsos_weidu_error_t e = rt_http("https://i.weread.qq.com/login", text, NULL, NULL, &result);
    free(text);
    if (e == WD_OK && !rt_tokens(result, c))
        e = WD_AUTH;
    cJSON_Delete(result);
    if (e == WD_OK && !rt_store(c, true))
        e = WD_STORAGE;
    return e;
}
static thingsos_weidu_error_t rt_get(const char *path, thingsos_rt_credentials_t *c,
                                     cJSON **result) {
    char *url = thingsos_psram_alloc(512);
    if (!url)
        return WD_MEMORY;
    snprintf(url, 512, "https://i.weread.qq.com%s", path);
    thingsos_weidu_error_t e = rt_http(url, NULL, c, NULL, result);
    if (e != WD_AUTH) {
        free(url);
        return e;
    }
    cJSON_Delete(*result);
    *result = NULL;
    cJSON *b = rt_login_body(c);
    cJSON_AddStringToObject(b, "refreshToken", c->refresh);
    cJSON_AddNumberToObject(b, "inBackground", 0);
    cJSON_AddNumberToObject(b, "kickType", 1);
    cJSON_AddStringToObject(b, "refCgi", "");
    e = rt_login(b, c);
    if (e == WD_OK)
        e = rt_http(url, NULL, c, NULL, result);
    free(url);
    return e;
}
static bool rt_url_token(const char *s) {
    if (!s || !*s || strlen(s) > 256)
        return false;
    for (; *s; ++s)
        if (!isalnum((unsigned char)*s) && *s != '_' && *s != '-')
            return false;
    return true;
}
typedef struct {
    char url[768];
    char uuid[129];
    char code[512];
} rt_auth_scratch_t;
static thingsos_weidu_error_t rt_authorize_code(rt_auth_scratch_t *sc) {
    cJSON *ticket = NULL;
    thingsos_weidu_error_t e =
        rt_http("https://i.weread.qq.com/wxticket?nonceStr=weread", NULL, NULL, NULL, &ticket);
    const char *signature = rt_string(ticket, "signature");
    cJSON *stamp = cJSON_GetObjectItemCaseSensitive(ticket, "timeStamp");
    char stamp_text[32] = {0};
    if (cJSON_IsNumber(stamp))
        snprintf(stamp_text, sizeof(stamp_text), "%.0f", stamp->valuedouble);
    else if (cJSON_IsString(stamp) && strlen(stamp->valuestring) < sizeof(stamp_text))
        strcpy(stamp_text, stamp->valuestring);
    if (e == WD_OK && (!rt_url_token(signature) || !stamp_text[0] ||
                       strspn(stamp_text, "0123456789") != strlen(stamp_text)))
        e = WD_PROTOCOL;
    if (e == WD_OK)
        snprintf(sc->url, sizeof(sc->url),
                 "https://open.weixin.qq.com/connect/sdk/"
                 "qrconnect?appid=wxab9b71ad2b90ff34&noncestr=weread&timestamp=%s&scope=snsapi_"
                 "userinfo%%2Csnsapi_timeline%%2Csnsapi_friend&signature=%s",
                 stamp_text, signature);
    cJSON_Delete(ticket);
    if (e != WD_OK)
        return e;
    cJSON *qr = NULL;
    e = rt_http(sc->url, NULL, NULL, NULL, &qr);
    if (e == WD_OK && (!rt_copy(qr, "uuid", sc->uuid, sizeof(sc->uuid)) || !rt_url_token(sc->uuid)))
        e = WD_PROTOCOL;
    cJSON_Delete(qr);
    if (e != WD_OK)
        return e;
    snprintf(client->status.qr, sizeof(client->status.qr),
             "https://open.weixin.qq.com/connect/confirm?uuid=%s", sc->uuid);
    client->status.phase = WD_QR;
    strlcpy(client->status.message, "使用当前账号授权离线阅读时长", sizeof(client->status.message));
    ++client->status.serial;
    publish(NULL, &client->status);
    uint64_t deadline = now_ms(NULL) + 180000;
    int last = 0;
    while (now_ms(NULL) < deadline && !abort_requested) {
        snprintf(sc->url, sizeof(sc->url),
                 "https://long.open.weixin.qq.com/connect/l/qrconnect?f=json&uuid=%s&last=%d", sc->uuid,
                 last);
        cJSON *poll = NULL;
        e = rt_http(sc->url, NULL, NULL, NULL, &poll);
        cJSON *status = cJSON_GetObjectItemCaseSensitive(poll, "wx_errcode");
        int n = cJSON_IsNumber(status) ? status->valueint : 0;
        if (e == WD_OK && n == 405) {
            if (!rt_copy(poll, "wx_code", sc->code, sizeof(sc->code)))
                e = WD_PROTOCOL;
            cJSON_Delete(poll);
            break;
        }
        cJSON_Delete(poll);
        if(e==WD_NETWORK){wait_ms(NULL,250);continue;}
        if (e != WD_OK)
            return e;
        if (n == 402)
            return WD_CANCEL;
        if (n != 404 && n != 408)
            return WD_CANCEL;
        last = n;
        wait_ms(NULL, 250);
    }
    if (abort_requested)
        return WD_CANCEL;
    return sc->code[0] ? WD_OK : WD_CANCEL;
}
static thingsos_weidu_error_t rt_authorize(thingsos_rt_credentials_t *c) {
    if (!thingsos_weidu_session_valid(&client->session))
        return WD_AUTH;
    rt_auth_scratch_t *sc = thingsos_psram_calloc(1, sizeof(*sc));
    if (!sc)
        return WD_MEMORY;
    thingsos_weidu_error_t e = rt_authorize_code(sc);
    if (e != WD_OK) {
        memset(sc, 0, sizeof(*sc));
        free(sc);
        return e;
    }
    if (!rt_store(c, false)) {
        memset(c, 0, sizeof(*c));
        strcpy(c->device, "eink334691225");
        rt_digits(c->device + strlen(c->device), 19);
    }
    cJSON *b = rt_login_body(c);
    cJSON_AddStringToObject(b, "code", sc->code);
    memset(sc, 0, sizeof(*sc));
    free(sc);
    cJSON_AddNumberToObject(b, "appFirstInstall", 1);
    char install[40] = "eink31";
    rt_digits(install + 6, 26);
    cJSON_AddStringToObject(b, "installId", install);
    cJSON_AddNumberToObject(b, "isAutoLogout", 0);
    cJSON_AddNumberToObject(b, "isFromQrcode", 1);
    return rt_login(b, c);
}
static bool rt_position(cJSON *value, char uid[64], uint32_t *offset) {
    for (unsigned i = 0; i < 4 && value; ++i) {
        cJSON *u = cJSON_GetObjectItemCaseSensitive(value, "chapterUid");
        cJSON *o = cJSON_GetObjectItemCaseSensitive(value, "chapterOffset");
        if (u && cJSON_IsNumber(o) && o->valuedouble >= 0 && o->valuedouble <= UINT32_MAX) {
            if (cJSON_IsString(u)) {
                if (strlen(u->valuestring) >= 64)
                    return false;
                strcpy(uid, u->valuestring);
            } else if (cJSON_IsNumber(u))
                snprintf(uid, 64, "%.0f", u->valuedouble);
            else
                return false;
            *offset = (uint32_t)o->valuedouble;
            return true;
        }
        cJSON *next = cJSON_GetObjectItemCaseSensitive(value, "book");
        if (!next)
            next = cJSON_GetObjectItemCaseSensitive(value, "data");
        if (!next)
            next = cJSON_GetObjectItemCaseSensitive(value, "result");
        value = next;
    }
    return false;
}
static thingsos_weidu_error_t rt_upload(thingsos_rt_credentials_t *c,
                                        thingsos_rt_device_status_t *status) {
    char last[65];
    bool locked=rt_io_take();
    status->uploaded_ready=rt_uploaded_load(work->reading.owner,&status->uploaded,last);
    rt_io_give(locked);
    if(!status->uploaded_ready)return WD_STORAGE; /* Keep pending if the total is unreadable. */
    thingsos_rt_batch_t *b = thingsos_psram_calloc(1, sizeof(*b));
    if (!b)
        return WD_MEMORY;
    bool ok = thingsos_rt_peek(client->root, work->reading.owner, unix_time(NULL), b);
    if (!ok || !b->count) {
        free(b);
        return ok ? WD_OK : WD_STORAGE;
    }
    cJSON *config = NULL, *feature = NULL, *before = NULL, *after = NULL, *ack = NULL;
    char *body = thingsos_psram_alloc(8192);
    thingsos_weidu_error_t e = body ? rt_get("/config?token=1", c, &config) : WD_MEMORY;
    if (e == WD_OK)
        e = rt_get("/feature?synckey=0", c, &feature);
    char path[128], uid[64], newuid[64];
    uint32_t offset = 0, newoffset = 0;
    snprintf(path, sizeof(path), "/book/getProgress?bookId=%s", b->rows[0].book);
    if (e == WD_OK)
        e = rt_get(path, c, &before);
    if (e == WD_OK && !rt_position(before, uid, &offset))
        e = WD_PROTOCOL;
    const char *token = rt_string(config, "token");
    cJSON *features = cJSON_GetObjectItemCaseSensitive(feature, "feature");
    cJSON *guest_item = cJSON_GetObjectItemCaseSensitive(features, "guest_token");
    const char *guest = rt_string(features, "guest_token");
    if (!guest || !*guest)
        guest = "5ecdcfd7f";
    if (e == WD_OK && guest_item && !cJSON_IsString(guest_item))
        e = WD_PROTOCOL;
    if (e == WD_OK && !thingsos_rt_body(c, b, guest, token,
                                      rt_wall_ms(),
                                        esp_random() % 1000, esp_random() % 1000, 480, body, 8192))
        e = WD_PROTOCOL;
    if (e == WD_OK)
        e = rt_http("https://i.weread.qq.com/book/batchUploadProgress", body, c, b, &ack);
    if (e == WD_OK) {
        /* Persist the successful lifetime total before deleting any source.
           Failed/uncertain requests never reach this commit; failed storage
           retains .sent and cannot cause an automatic network replay. */
        bool locked=rt_io_take();
        e=rt_uploaded_commit(work->reading.owner,b,ack,&status->uploaded);
        rt_io_give(locked);
        if(e==WD_OK) {
            status->uploaded_ready=true;
            ok = thingsos_rt_confirm(client->root, work->reading.owner, b);
            if (!ok)
                e = WD_STORAGE;
            else {
                status->confirmed = 0;
                for (unsigned i = 0; i < b->count; ++i)
                    status->confirmed += b->rows[i].seconds;
                status->confirmed_at = unix_time(NULL);
            }
            /* Confirmation precedes optional position verification. */
            if (rt_get(path, c, &after) != WD_OK || !rt_position(after, newuid, &newoffset) ||
                strcmp(uid, newuid) || offset != newoffset) {
                status->blocked = true;
            }
        }
    }
    free(body);
    free(b);
    cJSON_Delete(config);
    cJSON_Delete(feature);
    cJSON_Delete(before);
    cJSON_Delete(after);
    cJSON_Delete(ack);
    return e;
}
static thingsos_weidu_error_t rt_worker(void) {
    thingsos_rt_device_status_t status;
    thingsos_wd_rt_status(&status);
    thingsos_rt_credentials_t *c = thingsos_psram_calloc(1, sizeof(*c));
    if (!c) {
        status.error=WD_MEMORY;rt_publish(&status);
        return WD_MEMORY;
    }
    thingsos_weidu_error_t e;
    if (unix_time(NULL) < WD_SYNC_MIN_TIME)
        e = WD_CLOCK;
    else if (requested_action == WD_DEVICE_RT_AUTH) {
        e = rt_authorize(c);
        if (e == WD_OK) {
            status.authorized = true;
            status.expired = false;
        }
    } else if (!rt_store(c, false))
        e = WD_AUTH;
    else
        e = rt_upload(c, &status);
    memset(c, 0, sizeof(*c));
    free(c);
    status.error = e;
    if (e == WD_AUTH)
        status.expired = true;
    bool locked=rt_io_take();
    rt_cooldown(unix_time(NULL) + (e == WD_OK ? 10 : e == WD_LIMIT ? 1800 : 60), status.blocked);
    rt_io_give(locked);
    rt_summary_update(&status);
    rt_publish(&status);
    return e;
}
