// SPDX-License-Identifier: Apache-2.0
/* flomo service: NVS token, LAN pairing server, read-only remote MCP client
   (https://flomoapp.com/mcp) and SD list/detail cache. */
#include "thingsos/flomo.h"
#include "network.h"
#include "secure.h"
#include "cJSON.h"

#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char* TAG = "ThingsOS_flomo";

#define FLOMO_ROOT "/sdcard/.thingsos/flomo"
#define FLOMO_MEMO_ROOT "/sdcard/.thingsos/flomo/m"
#define FLOMO_LIST_CACHE "/sdcard/.thingsos/flomo/list.dat"

#define FLOMO_PAIR_PORT 8083
#define FLOMO_PAIR_CTRL_PORT 32771
#define FLOMO_PAIR_TTL_US (300LL * 1000000LL)
#define FLOMO_PAIR_MAX_FAILS 5
#define FLOMO_MEMOS_MAX 200
#define FLOMO_TAGS_MAX 300
#define FLOMO_RESPONSE_MAX (512u * 1024u)
#define FLOMO_DETAIL_MAX (120u * 1024u)
#define FLOMO_REQUEST_MAX 1024

extern const uint8_t _binary_flomo_html_start[];
extern const uint8_t _binary_flomo_html_end[];

typedef enum { JOB_VERIFY = 1, JOB_LIST, JOB_MORE, JOB_DETAIL, JOB_TAGS } flomo_job_t;

typedef struct {
    SemaphoreHandle_t lock;
    thingsos_flomo_status_t status;
    flomo_config_t config;
    flomo_config_t pending;
    thingsos_flomo_memo_t* memos;
    thingsos_flomo_tag_t* tags;
    thingsos_flomo_memo_t related[THINGSOS_FLOMO_RELATED_MAX];
    thingsos_flomo_memo_t detail;
    char* detail_content;
    size_t detail_len;
    bool detail_valid;
    bool detail_complete;
    unsigned rpc_id;

    httpd_handle_t server;
    int64_t pair_deadline_us;
    int64_t pair_stop_after_us;
    unsigned pair_failures;
    unsigned pair_handlers_active;

    TaskHandle_t task;
    bool task_done;
    bool external_claimed;
    bool pending_verify;
    volatile bool cancel;
    flomo_job_t job;
    char job_id[THINGSOS_FLOMO_ID_LEN];
} flomo_state_t;

static flomo_state_t* s;
static bool (*network_yield_locked)(void);

void thingsos_flomo_set_network_yield(bool (*yield_locked)(void)) {
    network_yield_locked = yield_locked;
}

static void set_message(const char* text) {
    if (!s) return;
    xSemaphoreTake(s->lock, portMAX_DELAY);
    strlcpy(s->status.message, text ? text : "", sizeof(s->status.message));
    ++s->status.serial;
    xSemaphoreGive(s->lock);
}

static bool ensure_memos_locked(void) {
    if (!s) return false;
    if (!s->memos)
        s->memos = heap_caps_calloc(FLOMO_MEMOS_MAX, sizeof(*s->memos), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return s->memos != NULL;
}

static bool ensure_tags_locked(void) {
    if (!s) return false;
    if (!s->tags)
        s->tags = heap_caps_calloc(FLOMO_TAGS_MAX, sizeof(*s->tags), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return s->tags != NULL;
}

static bool dir_exists(const char* path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static bool ensure_dir(const char* path) {
    return (mkdir(path, 0775) == 0 || errno == EEXIST) && dir_exists(path);
}

/* Caller holds thingsos_io_lock. */
static bool ensure_flomo_dirs_locked(void) {
    return dir_exists("/sdcard") && ensure_dir("/sdcard/.thingsos") && ensure_dir(FLOMO_ROOT) &&
           ensure_dir(FLOMO_MEMO_ROOT);
}

static bool memo_text_path(const char* id, char* out, size_t cap) {
    return thingsos_flomo_id_valid(id) && snprintf(out, cap, "%s/%s.txt", FLOMO_MEMO_ROOT, id) < (int)cap;
}

/* Stop reading once one complete SSE event with a JSON payload has arrived:
   streamable HTTP may keep the stream open after the reply. */
static bool sse_reply_complete(const char* buf, size_t used) {
    const char* data = NULL;
    for (const char* p = buf; (p = strstr(p, "data:")) != NULL; p += 5) data = p;
    if (!data) return false;
    const char* json = data + 5;
    while (*json == ' ') ++json;
    if (*json != '{') return false;
    return strstr(json, "\n\n") || strstr(json, "\r\n\r\n") || (size_t)(json - buf) > used;
}

/* One read-only tools/call. Returns the parsed result object (cJSON*), or NULL
   with the status message set. */
static cJSON* mcp_call(const char* token, const char* body) {
    if (!token || !token[0] || !body) return NULL;
    char* response = heap_caps_calloc(1, FLOMO_RESPONSE_MAX + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!response) {
        set_message("flomo 请求内存不足");
        return NULL;
    }
    esp_http_client_config_t cfg = {
        .url = "https://" THINGSOS_FLOMO_HOST THINGSOS_FLOMO_PATH,
        .method = HTTP_METHOD_POST,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 20000,
        .disable_auto_redirect = true,
        .max_authorization_retries = -1,
        .buffer_size = 4096,
        .buffer_size_tx = 2048,
    };
    esp_http_client_handle_t http = esp_http_client_init(&cfg);
    esp_err_t err = http ? ESP_OK : ESP_ERR_NO_MEM;
    size_t body_len = strlen(body), used = 0;
    int status = 0;
    bool complete = false;
    if (http) {
        char auth[THINGSOS_FLOMO_TOKEN_LEN + 16];
        snprintf(auth, sizeof(auth), "Bearer %s", token);
        err = esp_http_client_set_header(http, "Authorization", auth);
        flomo_wipe(auth, sizeof(auth));
        if (err == ESP_OK) err = esp_http_client_set_header(http, "Content-Type", "application/json");
        if (err == ESP_OK) err = esp_http_client_set_header(http, "Accept", "application/json, text/event-stream");
        if (err == ESP_OK) err = esp_http_client_set_header(http, "Mcp-Protocol-Version", "2025-06-18");
        if (err == ESP_OK) err = esp_http_client_set_header(http, "User-Agent", "ThingsOS-flomo/1.0");
        if (err == ESP_OK) err = esp_http_client_open(http, (int)body_len);
        for (size_t sent = 0; err == ESP_OK && sent < body_len;) {
            int n = s->cancel ? -1 : esp_http_client_write(http, body + sent, (int)(body_len - sent));
            if (n <= 0) err = ESP_FAIL;
            else sent += (size_t)n;
        }
        if (err == ESP_OK) {
            if (esp_http_client_fetch_headers(http) < 0) err = ESP_FAIL;
            else status = esp_http_client_get_status_code(http);
        }
        while (err == ESP_OK) {
            if (s->cancel) {
                err = ESP_FAIL;
                break;
            }
            size_t rem = FLOMO_RESPONSE_MAX - used;
            if (!rem) {
                err = ESP_ERR_INVALID_SIZE;
                break;
            }
            int n = esp_http_client_read(http, response + used, (int)(rem > 4096 ? 4096 : rem));
            if (n < 0) {
                err = ESP_FAIL;
                break;
            }
            if (n == 0) {
                complete = esp_http_client_is_complete_data_received(http) ||
                           (status == 200 && sse_reply_complete(response, used));
                if (!complete) err = ESP_FAIL;
                break;
            }
            used += (size_t)n;
            response[used] = 0;
            if (status == 200 && sse_reply_complete(response, used)) {
                complete = true;
                break;
            }
        }
        esp_http_client_close(http);
        esp_http_client_cleanup(http);
    }
    response[used] = 0;

    char msg[THINGSOS_FLOMO_MESSAGE_LEN] = {0};
    cJSON* result = NULL;
    if (err == ESP_OK && complete && status == 200) {
        if (thingsos_flomo_unwrap_sse(response, used, response, FLOMO_RESPONSE_MAX + 1)) {
            void* parsed = NULL;
            char remote[128] = {0};
            if (thingsos_flomo_parse_result(response, strlen(response), &parsed, remote, sizeof(remote)))
                result = (cJSON*)parsed;
            else
                snprintf(msg, sizeof(msg), "flomo：%s", remote[0] ? remote : "返回错误");
        } else {
            strlcpy(msg, "flomo 返回内容无法识别", sizeof(msg));
        }
    } else if (s->cancel) {
        strlcpy(msg, "flomo 操作已取消", sizeof(msg));
    } else if (status == 401 || status == 403) {
        snprintf(msg, sizeof(msg), "flomo Token 无效、已注销或会员权限不足 (HTTP %d)", status);
    } else if (status == 429) {
        strlcpy(msg, "flomo 请求过于频繁，请稍后重试", sizeof(msg));
    } else if (status >= 400) {
        snprintf(msg, sizeof(msg), "flomo 服务返回 HTTP %d", status);
    } else if (err == ESP_ERR_INVALID_SIZE) {
        strlcpy(msg, "flomo 返回内容超过 512 KB 限制", sizeof(msg));
    } else {
        snprintf(msg, sizeof(msg), "连接 flomo 失败 (%s)", esp_err_to_name(err));
    }
    if (!result) {
        ESP_LOGW(TAG, "MCP call failed: HTTP %d %s", status, esp_err_to_name(err));
        set_message(msg);
    }
    free(response);
    return result;
}

static unsigned next_rpc_id(void) {
    xSemaphoreTake(s->lock, portMAX_DELAY);
    unsigned id = ++s->rpc_id;
    xSemaphoreGive(s->lock);
    return id;
}

static void save_list_cache(void) {
    thingsos_flomo_memo_t* copy =
        heap_caps_calloc(FLOMO_MEMOS_MAX, sizeof(*copy), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!copy) return;
    xSemaphoreTake(s->lock, portMAX_DELAY);
    unsigned count = s->memos ? s->status.memo_count : 0;
    if (count) memcpy(copy, s->memos, count * sizeof(*copy));
    xSemaphoreGive(s->lock);
    xSemaphoreTake(thingsos_io_lock, portMAX_DELAY);
    if (ensure_flomo_dirs_locked()) thingsos_flomo_save_memos(FLOMO_LIST_CACHE, copy, count);
    xSemaphoreGive(thingsos_io_lock);
    free(copy);
}

/* First page (append=false) or the next older page (append=true). */
static bool fetch_list_sync(const char* token, bool append) {
    xSemaphoreTake(s->lock, portMAX_DELAY);
    if (!ensure_memos_locked()) {
        xSemaphoreGive(s->lock);
        set_message("flomo 列表内存不足");
        return false;
    }
    thingsos_flomo_mode_t mode = s->status.mode;
    char tag[THINGSOS_FLOMO_TAG_LEN], query[97], end_date[16] = "";
    strlcpy(tag, mode == THINGSOS_FLOMO_MODE_TAG ? s->status.tag : "", sizeof(tag));
    strlcpy(query, s->status.query, sizeof(query));
    if (append && s->status.memo_count)
        strlcpy(end_date, s->memos[s->status.memo_count - 1].created_at, 11);
    xSemaphoreGive(s->lock);

    /* Keyword results are relevance-ordered: one page of up to 50, no paging. */
    unsigned limit = query[0] ? THINGSOS_FLOMO_SEARCH_MAX
                   : append ? THINGSOS_FLOMO_SEARCH_MAX : THINGSOS_FLOMO_PAGE_SIZE;
    thingsos_flomo_memo_t* batch =
        heap_caps_calloc(THINGSOS_FLOMO_SEARCH_MAX, sizeof(*batch), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    char* body = malloc(FLOMO_REQUEST_MAX);
    if (!batch || !body) {
        free(batch);
        free(body);
        set_message("flomo 列表内存不足");
        return false;
    }
    bool ok = false;
    unsigned added = 0, got = 0;
    /* Paging uses end_date (inclusive day). If a whole page repeats memos we
       already have, step back a day; at most three attempts per tap. */
    for (unsigned attempt = 0; attempt < 3 && !s->cancel; ++attempt) {
        bool built = mode == THINGSOS_FLOMO_MODE_REVIEW
                         ? thingsos_flomo_build_daily_review(next_rpc_id(), body, FLOMO_REQUEST_MAX)
                         : thingsos_flomo_build_search(next_rpc_id(), query, tag, end_date, limit, body, FLOMO_REQUEST_MAX);
        if (!built) {
            set_message("构造 flomo 请求失败");
            break;
        }
        cJSON* result = mcp_call(token, body);
        if (!result) break;
        got = thingsos_flomo_read_memos(result, batch, THINGSOS_FLOMO_SEARCH_MAX);
        cJSON_Delete(result);
        ok = true;

        xSemaphoreTake(s->lock, portMAX_DELAY);
        if (!append) s->status.memo_count = 0;
        for (unsigned i = 0; i < got && s->status.memo_count < FLOMO_MEMOS_MAX; ++i) {
            bool dup = false;
            for (unsigned j = 0; j < s->status.memo_count && !dup; ++j) {
                if (!strcmp(s->memos[j].id, batch[i].id)) {
                    s->memos[j] = batch[i];
                    dup = true;
                }
            }
            if (!dup) {
                s->memos[s->status.memo_count++] = batch[i];
                ++added;
            }
        }
        xSemaphoreGive(s->lock);
        if (!append || added || got < limit || !end_date[0]) break;
        char prev[16];
        if (!thingsos_flomo_day_before(end_date, prev, sizeof(prev))) break;
        strlcpy(end_date, prev, sizeof(end_date));
    }
    free(batch);
    free(body);
    if (!ok) return false;

    xSemaphoreTake(s->lock, portMAX_DELAY);
    s->status.has_more = mode != THINGSOS_FLOMO_MODE_REVIEW && !query[0] && got >= limit &&
                         (added || !append) && s->status.memo_count < FLOMO_MEMOS_MAX;
    s->status.offline_mode = false;
    if (s->status.memo_count)
        snprintf(s->status.message, sizeof(s->status.message), "已加载 %u 条笔记", s->status.memo_count);
    else
        strlcpy(s->status.message,
                mode == THINGSOS_FLOMO_MODE_REVIEW ? "今天没有每日回顾" : "没有符合条件的笔记",
                sizeof(s->status.message));
    ++s->status.serial;
    bool home = mode == THINGSOS_FLOMO_MODE_RECENT && !query[0];
    xSemaphoreGive(s->lock);
    if (home) save_list_cache();
    return true;
}

static bool fetch_tags_sync(const char* token) {
    char body[256];
    if (!thingsos_flomo_build_tag_tree(next_rpc_id(), FLOMO_TAGS_MAX, body, sizeof(body))) return false;
    cJSON* result = mcp_call(token, body);
    if (!result) return false;
    thingsos_flomo_tag_t* tags =
        heap_caps_calloc(FLOMO_TAGS_MAX, sizeof(*tags), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!tags) {
        cJSON_Delete(result);
        set_message("flomo 标签内存不足");
        return false;
    }
    unsigned count = thingsos_flomo_read_tags(result, tags, FLOMO_TAGS_MAX);
    cJSON_Delete(result);
    xSemaphoreTake(s->lock, portMAX_DELAY);
    if (!ensure_tags_locked()) {
        xSemaphoreGive(s->lock);
        free(tags);
        set_message("flomo 标签内存不足");
        return false;
    }
    memcpy(s->tags, tags, count * sizeof(*tags));
    s->status.tag_count = count;
    snprintf(s->status.message, sizeof(s->status.message), count ? "已同步 %u 个标签" : "flomo 中还没有标签", count);
    ++s->status.serial;
    xSemaphoreGive(s->lock);
    free(tags);
    return true;
}

static bool fetch_detail_sync(const char* token, const char* id) {
    char body[256];
    if (!thingsos_flomo_build_batch_get(next_rpc_id(), id, body, sizeof(body))) return false;
    cJSON* result = mcp_call(token, body);
    if (!result) return false;
    char* content = heap_caps_calloc(1, FLOMO_DETAIL_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    thingsos_flomo_memo_t memo;
    size_t len = 0;
    bool ok = content && thingsos_flomo_read_full(result, id, &memo, content, FLOMO_DETAIL_MAX, &len);
    cJSON_Delete(result);
    if (!ok) {
        free(content);
        set_message(content ? "flomo 中找不到这条笔记" : "flomo 详情内存不足");
        return false;
    }

    char path[96];
    if (memo_text_path(id, path, sizeof(path))) {
        char tmp[104];
        snprintf(tmp, sizeof(tmp), "%s.tmp", path);
        xSemaphoreTake(thingsos_io_lock, portMAX_DELAY);
        if (ensure_flomo_dirs_locked()) {
            FILE* f = fopen(tmp, "wb");
            bool wrote = f && fwrite(content, 1, len, f) == len && fflush(f) == 0;
            if (f && fclose(f) != 0) wrote = false;
            if (wrote) {
                remove(path);
                if (rename(tmp, path) != 0) wrote = false;
            }
            if (!wrote) remove(tmp);
        }
        xSemaphoreGive(thingsos_io_lock);
    }

    xSemaphoreTake(s->lock, portMAX_DELAY);
    free(s->detail_content);
    s->detail_content = content;
    s->detail_len = len;
    s->detail = memo;
    s->detail_valid = true;
    s->detail_complete = !memo.truncated;
    s->status.related_count = 0;
    s->status.related_for[0] = 0;
    if (s->memos) {
        for (unsigned i = 0; i < s->status.memo_count; ++i) {
            if (!strcmp(s->memos[i].id, id)) {
                memo.truncated = s->memos[i].truncated;
                strlcpy(memo.preview, s->memos[i].preview, sizeof(memo.preview));
                s->memos[i] = memo;
                break;
            }
        }
    }
    strlcpy(s->status.message, "已获取笔记全文", sizeof(s->status.message));
    ++s->status.serial;
    xSemaphoreGive(s->lock);

    /* Related notes are optional; a failure keeps the full text. */
    if (s->cancel || !thingsos_flomo_build_recommended(next_rpc_id(), id, 5, body, sizeof(body))) return true;
    result = mcp_call(token, body);
    if (!result) return true;
    thingsos_flomo_memo_t* related =
        heap_caps_calloc(THINGSOS_FLOMO_RELATED_MAX, sizeof(*related), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    unsigned count = related ? thingsos_flomo_read_memos(result, related, THINGSOS_FLOMO_RELATED_MAX) : 0;
    cJSON_Delete(result);
    xSemaphoreTake(s->lock, portMAX_DELAY);
    if (related) memcpy(s->related, related, count * sizeof(*related));
    s->status.related_count = count;
    strlcpy(s->status.related_for, id, sizeof(s->status.related_for));
    strlcpy(s->status.message, count ? "已获取笔记全文与相关笔记" : "已获取笔记全文", sizeof(s->status.message));
    ++s->status.serial;
    xSemaphoreGive(s->lock);
    free(related);
    return true;
}

static void worker_task(void* arg) {
    (void)arg;
    xSemaphoreTake(s->lock, portMAX_DELAY);
    flomo_job_t job = s->job;
    flomo_config_t cfg = job == JOB_VERIFY ? s->pending : s->config;
    char id[THINGSOS_FLOMO_ID_LEN];
    strlcpy(id, s->job_id, sizeof(id));
    xSemaphoreGive(s->lock);

    if (job == JOB_VERIFY) {
        xSemaphoreTake(s->lock, portMAX_DELAY);
        s->status.mode = THINGSOS_FLOMO_MODE_RECENT;
        s->status.tag[0] = 0;
        s->status.query[0] = 0;
        xSemaphoreGive(s->lock);
        bool ok = fetch_list_sync(cfg.token, false);
        bool saved = ok && flomo_config_save(&cfg);
        xSemaphoreTake(s->lock, portMAX_DELAY);
        if (saved) {
            s->config = cfg;
            s->status.configured = true;
            s->status.config_phase = THINGSOS_FLOMO_CONFIG_READY;
            ++s->status.connection_serial;
            strlcpy(s->status.message, "flomo 已连接", sizeof(s->status.message));
        } else {
            s->status.config_phase = THINGSOS_FLOMO_CONFIG_FAILED;
            if (ok) strlcpy(s->status.message, "保存 flomo Token 失败", sizeof(s->status.message));
        }
        ++s->status.serial;
        flomo_wipe(&s->pending, sizeof(s->pending));
        xSemaphoreGive(s->lock);
    } else if (job == JOB_LIST || job == JOB_MORE) {
        fetch_list_sync(cfg.token, job == JOB_MORE);
    } else if (job == JOB_DETAIL) {
        fetch_detail_sync(cfg.token, id);
    } else if (job == JOB_TAGS) {
        fetch_tags_sync(cfg.token);
    }
    flomo_wipe(&cfg, sizeof(cfg));
    ESP_LOGI(TAG, "Worker stack margin=%u bytes", (unsigned)uxTaskGetStackHighWaterMark(NULL));

    xSemaphoreTake(s->lock, portMAX_DELAY);
    s->status.busy = false;
    s->status.op = THINGSOS_FLOMO_OP_NONE;
    s->status.active_id[0] = 0;
    s->task_done = true;
    ++s->status.serial;
    xSemaphoreGive(s->lock);
    vTaskDelete(NULL);
}

static void reap_worker_locked(void) {
    if (s->task != NULL && s->task_done) {
        s->task = NULL;
        s->task_done = false;
        if (s->external_claimed) {
            thingsos_network_external_end();
            s->external_claimed = false;
        }
    }
}

static bool launch_job(flomo_job_t job, thingsos_flomo_op_t op, const char* id, const char* init_msg) {
    if (!s) return false;
    if (!thingsos_network_connected()) {
        set_message("请先连接 WiFi");
        return false;
    }
    const uint32_t internal = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    size_t int_free = heap_caps_get_free_size(internal);
    size_t int_largest = heap_caps_get_largest_free_block(internal);
    size_t ps_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t ps_largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    if (int_free < 34u * 1024u ||
        int_largest < 16u * 1024u ||
        ps_free < 1024u * 1024u ||
        ps_largest < 544u * 1024u) {
        ESP_LOGW(
            TAG,
            "Job %d refused: internal_free=%u largest=%u psram_free=%u psram_largest=%u",
            (int)job, (unsigned)int_free, (unsigned)int_largest,
            (unsigned)ps_free, (unsigned)ps_largest);
        set_message("flomo 内存不足，请退出阅读或传输后重试");
        return false;
    }
    xSemaphoreTake(s->lock, portMAX_DELAY);
    reap_worker_locked();
    bool ready = !s->status.busy && !s->task && (job == JOB_VERIFY || s->status.configured);
    bool claimed = ready && thingsos_network_external_begin();
    /* Like Cubox: let the UI park a paused song that holds the network claim.
       JOB_VERIFY runs on the HTTP server task and must not touch UI state. */
    if (ready && !claimed && job != JOB_VERIFY && network_yield_locked) {
        xSemaphoreGive(s->lock);
        bool yielded = network_yield_locked();
        xSemaphoreTake(s->lock, portMAX_DELAY);
        reap_worker_locked();
        ready = !s->status.busy && !s->task && s->status.configured;
        claimed = ready && yielded && thingsos_network_external_begin();
    }
    if (ready && !claimed) {
        xSemaphoreGive(s->lock);
        set_message("请先等待当前传输任务结束");
        return false;
    }
    if (ready) {
        s->external_claimed = true;
        s->cancel = false;
        s->task_done = false;
        s->job = job;
        strlcpy(s->job_id, id ? id : "", sizeof(s->job_id));
        strlcpy(s->status.active_id, id ? id : "", sizeof(s->status.active_id));
        s->status.busy = true;
        s->status.op = op;
        if (init_msg) strlcpy(s->status.message, init_msg, sizeof(s->status.message));
        ++s->status.serial;
    }
    xSemaphoreGive(s->lock);
    if (!ready) return false;
    if (xTaskCreate(worker_task, "flomo_work", 12288, NULL, 4, &s->task) != pdPASS) {
        xSemaphoreTake(s->lock, portMAX_DELAY);
        s->task = NULL;
        s->status.busy = false;
        s->status.op = THINGSOS_FLOMO_OP_NONE;
        if (s->external_claimed) {
            thingsos_network_external_end();
            s->external_claimed = false;
        }
        strlcpy(s->status.message, "无法启动 flomo 网络任务", sizeof(s->status.message));
        ++s->status.serial;
        xSemaphoreGive(s->lock);
        return false;
    }
    return true;
}

/* ---- LAN pairing ---- */

static bool host_header_allowed(httpd_req_t* req) {
    char host[128] = {0}, ip[96] = {0}, expected[128];
    if (httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host)) != ESP_OK) return false;
    thingsos_network_address(false, ip, sizeof(ip));
    const char* addr = strstr(ip, "://");
    addr = addr ? addr + 3 : ip;
    snprintf(expected, sizeof(expected), "%s:%u", addr, FLOMO_PAIR_PORT);
    return !strcmp(host, expected);
}

static void pair_handler_count(int delta) {
    if (!s) return;
    xSemaphoreTake(s->lock, portMAX_DELAY);
    if (delta > 0) ++s->pair_handlers_active;
    else if (s->pair_handlers_active > 0) --s->pair_handlers_active;
    xSemaphoreGive(s->lock);
}

static esp_err_t pair_page_get(httpd_req_t* req) {
    if (!host_header_allowed(req)) {
        httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Forbidden host");
        return ESP_OK;
    }
    pair_handler_count(1);
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "Content-Security-Policy",
                       "default-src 'self'; style-src 'unsafe-inline'; script-src 'unsafe-inline'");
    httpd_resp_send(req, (const char*)_binary_flomo_html_start,
                    (ssize_t)(_binary_flomo_html_end - _binary_flomo_html_start));
    pair_handler_count(-1);
    return ESP_OK;
}

static esp_err_t send_json_reply(httpd_req_t* req, const char* status, bool ok, const char* msg) {
    cJSON* root = cJSON_CreateObject();
    char buf[320];
    bool printed = false;
    if (root) {
        cJSON_AddBoolToObject(root, "ok", ok);
        cJSON_AddStringToObject(root, "message", msg ? msg : "");
        printed = cJSON_PrintPreallocated(root, buf, sizeof(buf), false) != 0;
        cJSON_Delete(root);
    }
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_sendstr(req, printed ? buf : "{\"ok\":false,\"message\":\"Error\"}");
    return ESP_OK;
}

static esp_err_t pair_submit_post(httpd_req_t* req) {
    if (!host_header_allowed(req)) return send_json_reply(req, "403 Forbidden", false, "非法主机头请求");
    pair_handler_count(1);
    esp_err_t ret = ESP_OK;
    cJSON* json = NULL;
    char* body = NULL;
    flomo_config_t cand = {0};

    if (req->content_len <= 0 || req->content_len > 2048) {
        ret = send_json_reply(req, "400 Bad Request", false, "请求内容长度无效");
        goto done;
    }
    body = calloc(1, (size_t)req->content_len + 1);
    if (!body) {
        ret = send_json_reply(req, "500 Internal Server Error", false, "内存不足");
        goto done;
    }
    for (size_t got = 0; got < (size_t)req->content_len;) {
        int n = httpd_req_recv(req, body + got, (size_t)req->content_len - got);
        if (n <= 0) {
            ret = send_json_reply(req, "400 Bad Request", false, "接收请求失败");
            goto done;
        }
        got += (size_t)n;
    }
    json = cJSON_Parse(body);
    flomo_wipe(body, (size_t)req->content_len);
    if (!json) {
        ret = send_json_reply(req, "400 Bad Request", false, "JSON 格式无效");
        goto done;
    }
    const cJSON* jcode = cJSON_GetObjectItemCaseSensitive(json, "code");
    const cJSON* jcred = cJSON_GetObjectItemCaseSensitive(json, "credential");
    const char* code = cJSON_IsString(jcode) ? jcode->valuestring : "";
    const char* cred = cJSON_IsString(jcred) ? jcred->valuestring : "";

    xSemaphoreTake(s->lock, portMAX_DELAY);
    bool active = s->status.pairing && esp_timer_get_time() < s->pair_deadline_us;
    bool locked_out = s->pair_failures >= FLOMO_PAIR_MAX_FAILS;
    bool code_ok = active && !locked_out && strlen(code) == 8 && !strcmp(code, s->status.code);
    if (!code_ok && active && !locked_out && ++s->pair_failures >= FLOMO_PAIR_MAX_FAILS) {
        s->status.pairing = false;
        s->pair_stop_after_us = esp_timer_get_time() + 250000LL;
        strlcpy(s->status.message, "配对码错误次数过多，请刷新配对码", sizeof(s->status.message));
        ++s->status.serial;
    }
    xSemaphoreGive(s->lock);
    if (!active || locked_out) {
        ret = send_json_reply(req, "403 Forbidden", false, "配对已过期或失败次数过多，请在设备刷新配对码");
        goto done;
    }
    if (!code_ok) {
        ret = send_json_reply(req, "403 Forbidden", false, "配对码不正确");
        goto done;
    }
    bool valid = thingsos_flomo_parse_token(cred, cand.token, sizeof(cand.token));
    if (cJSON_IsString(jcred)) flomo_wipe(jcred->valuestring, strlen(jcred->valuestring));
    if (!valid) {
        ret = send_json_reply(req, "400 Bad Request", false, "无法识别 flomo Token，请核对后重新提交");
        goto done;
    }

    xSemaphoreTake(s->lock, portMAX_DELAY);
    reap_worker_locked();
    if (s->status.busy || s->task || s->pending_verify) {
        xSemaphoreGive(s->lock);
        ret = send_json_reply(req, "409 Conflict", false, "设备正在处理另一项任务，请稍后重试");
        goto done;
    }
    s->pending = cand;
    s->pending_verify = true;
    s->status.pairing = false;
    s->pair_stop_after_us = esp_timer_get_time() + 150000LL;
    s->status.config_phase = THINGSOS_FLOMO_CONFIG_VERIFYING;
    strlcpy(s->status.message, "正在向 flomo 验证 Token…", sizeof(s->status.message));
    ++s->status.serial;
    xSemaphoreGive(s->lock);

    ret = send_json_reply(req, "200 OK", true, "已接收 Token，正在验证 flomo…");
done:
    flomo_wipe(&cand, sizeof(cand));
    free(body);
    if (json) cJSON_Delete(json);
    pair_handler_count(-1);
    return ret;
}

static void stop_pair_server(void) {
    if (!s || !s->server) return;
    httpd_stop(s->server);
    s->server = NULL;
}

/* ---- Public API ---- */

void thingsos_flomo_init(void) {
    if (s) return;
    s = heap_caps_calloc(1, sizeof(*s), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s) {
        ESP_LOGE(TAG, "PSRAM allocation failed");
        return;
    }
    s->lock = xSemaphoreCreateMutex();
    s->status.mode = THINGSOS_FLOMO_MODE_RECENT;
    if (flomo_config_load(&s->config)) {
        s->status.configured = true;
        s->status.config_phase = THINGSOS_FLOMO_CONFIG_READY;
    }
    ++s->status.serial;
}

void thingsos_flomo_status(thingsos_flomo_status_t* out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    if (!s) return;
    xSemaphoreTake(s->lock, portMAX_DELAY);
    if (s->status.pairing) {
        int64_t rem = (s->pair_deadline_us - esp_timer_get_time()) / 1000000LL;
        s->status.pair_seconds = rem > 0 ? (unsigned)rem : 0;
    } else {
        s->status.pair_seconds = 0;
    }
    *out = s->status;
    xSemaphoreGive(s->lock);
}

bool thingsos_flomo_active(void) {
    if (!s) return false;
    xSemaphoreTake(s->lock, portMAX_DELAY);
    bool active = s->status.busy || s->task != NULL;
    xSemaphoreGive(s->lock);
    return active;
}

bool thingsos_flomo_pairing(void) {
    if (!s) return false;
    xSemaphoreTake(s->lock, portMAX_DELAY);
    bool p = s->status.pairing;
    xSemaphoreGive(s->lock);
    return p;
}

bool thingsos_flomo_pair(bool enabled) {
    if (!s) return false;
    if (!enabled) {
        xSemaphoreTake(s->lock, portMAX_DELAY);
        s->status.pairing = false;
        s->status.pair_seconds = 0;
        s->pair_stop_after_us = 0;
        ++s->status.serial;
        xSemaphoreGive(s->lock);
        stop_pair_server();
        return true;
    }
    if (!thingsos_network_connected()) {
        set_message("请先连接 WiFi");
        return false;
    }
    const uint32_t internal = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    if (heap_caps_get_free_size(internal) < 34u * 1024u ||
        heap_caps_get_largest_free_block(internal) < 16u * 1024u) {
        set_message("flomo 配对内存不足，请退出传输后重试");
        return false;
    }
    if (s->server) stop_pair_server();
    xSemaphoreTake(s->lock, portMAX_DELAY);
    s->pending_verify = false;
    flomo_wipe(&s->pending, sizeof(s->pending));
    xSemaphoreGive(s->lock);

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = FLOMO_PAIR_PORT;
    cfg.ctrl_port = FLOMO_PAIR_CTRL_PORT;
    cfg.stack_size = 6144;
    cfg.max_uri_handlers = 4;
    cfg.max_open_sockets = 2;
    cfg.lru_purge_enable = true;
    cfg.recv_wait_timeout = 5;
    cfg.max_req_hdr_len = 2048;
    esp_err_t start_err = httpd_start(&s->server, &cfg);
    if (start_err != ESP_OK) {
        s->server = NULL;
        set_message(
            (start_err == ESP_ERR_HTTPD_ALLOC_MEM || start_err == ESP_ERR_HTTPD_TASK)
                ? "flomo 配对内存不足，请退出传输后重试"
                : "无法启动手机配对服务");
        return false;
    }
    httpd_uri_t get_root = {.uri = "/", .method = HTTP_GET, .handler = pair_page_get};
    httpd_uri_t post_pair = {.uri = "/api/flomo/pair", .method = HTTP_POST, .handler = pair_submit_post};
    httpd_register_uri_handler(s->server, &get_root);
    httpd_register_uri_handler(s->server, &post_pair);

    xSemaphoreTake(s->lock, portMAX_DELAY);
    s->status.pairing = true;
    s->pair_stop_after_us = 0;
    s->status.config_phase = THINGSOS_FLOMO_CONFIG_IDLE;
    snprintf(s->status.code, sizeof(s->status.code), "%08lu", (unsigned long)(esp_random() % 100000000u));
    s->pair_deadline_us = esp_timer_get_time() + FLOMO_PAIR_TTL_US;
    s->pair_failures = 0;
    s->status.pair_seconds = (unsigned)(FLOMO_PAIR_TTL_US / 1000000LL);
    strlcpy(s->status.message, "请使用手机扫码输入 flomo Token", sizeof(s->status.message));
    ++s->status.serial;
    xSemaphoreGive(s->lock);
    return true;
}

void thingsos_flomo_pair_poll(void) {
    if (!s) return;
    int64_t now_us = esp_timer_get_time();
    bool connected = thingsos_network_connected();
    xSemaphoreTake(s->lock, portMAX_DELAY);
    reap_worker_locked();
    if (s->status.pairing && (now_us >= s->pair_deadline_us || !connected)) {
        s->status.pairing = false;
        s->status.pair_seconds = 0;
        s->pair_stop_after_us = 0;
        strlcpy(s->status.message, connected ? "配对二维码已过期，请刷新" : "WiFi 已断开，配对服务已停止",
                sizeof(s->status.message));
        ++s->status.serial;
    }
    bool need_stop = s->server && !s->status.pairing && s->pair_handlers_active == 0 &&
                     (s->pair_stop_after_us == 0 || now_us >= s->pair_stop_after_us);
    xSemaphoreGive(s->lock);
    if (need_stop) {
        stop_pair_server();
        /* Reclaim the deleted httpd stack/TCB before checking internal SRAM. */
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    bool launch_verify = false;
    xSemaphoreTake(s->lock, portMAX_DELAY);
    if (!s->server && s->pending_verify && !s->status.busy && !s->task) {
        s->pending_verify = false;
        launch_verify = true;
    }
    xSemaphoreGive(s->lock);

    if (launch_verify &&
        !launch_job(JOB_VERIFY, THINGSOS_FLOMO_OP_VERIFY, "", "正在向 flomo 验证 Token…")) {
        xSemaphoreTake(s->lock, portMAX_DELAY);
        s->status.config_phase = THINGSOS_FLOMO_CONFIG_FAILED;
        flomo_wipe(&s->pending, sizeof(s->pending));
        ++s->status.serial;
        xSemaphoreGive(s->lock);
    }
}

bool thingsos_flomo_quiesce_locked(void) {
    if (!s) return true;
    s->cancel = true;
    xSemaphoreTake(s->lock, portMAX_DELAY);
    s->status.pairing = false;
    s->pending_verify = false;
    bool idle = !s->status.busy && s->task == NULL;
    xSemaphoreGive(s->lock);
    stop_pair_server();
    xSemaphoreTake(s->lock, portMAX_DELAY);
    s->pending_verify = false;
    flomo_wipe(&s->pending, sizeof(s->pending));
    xSemaphoreGive(s->lock);
    return idle;
}

bool thingsos_flomo_disconnect(void) {
    if (!s || !thingsos_flomo_quiesce_locked()) return false;
    flomo_config_clear();
    xSemaphoreTake(s->lock, portMAX_DELAY);
    flomo_wipe(&s->config, sizeof(s->config));
    s->status.configured = false;
    s->status.config_phase = THINGSOS_FLOMO_CONFIG_IDLE;
    s->status.memo_count = 0;
    s->status.tag_count = 0;
    s->status.related_count = 0;
    s->status.has_more = false;
    s->detail_valid = false;
    strlcpy(s->status.message, "已断开 flomo 连接", sizeof(s->status.message));
    ++s->status.serial;
    xSemaphoreGive(s->lock);
    return true;
}

bool thingsos_flomo_memo(unsigned index, thingsos_flomo_memo_t* out) {
    if (!s || !out) return false;
    xSemaphoreTake(s->lock, portMAX_DELAY);
    bool ok = s->memos && index < s->status.memo_count;
    if (ok) *out = s->memos[index];
    xSemaphoreGive(s->lock);
    return ok;
}

bool thingsos_flomo_tag(unsigned index, thingsos_flomo_tag_t* out) {
    if (!s || !out) return false;
    xSemaphoreTake(s->lock, portMAX_DELAY);
    bool ok = s->tags && index < s->status.tag_count;
    if (ok) *out = s->tags[index];
    xSemaphoreGive(s->lock);
    return ok;
}

bool thingsos_flomo_related(unsigned index, thingsos_flomo_memo_t* out) {
    if (!s || !out) return false;
    xSemaphoreTake(s->lock, portMAX_DELAY);
    bool ok = index < s->status.related_count;
    if (ok) *out = s->related[index];
    xSemaphoreGive(s->lock);
    return ok;
}

static bool find_memo_locked(const char* id, thingsos_flomo_memo_t* out) {
    if (s->memos) {
        for (unsigned i = 0; i < s->status.memo_count; ++i)
            if (!strcmp(s->memos[i].id, id)) return *out = s->memos[i], true;
    }
    for (unsigned i = 0; i < s->status.related_count; ++i)
        if (!strcmp(s->related[i].id, id)) return *out = s->related[i], true;
    return false;
}

bool thingsos_flomo_detail_copy(
    const char* id, thingsos_flomo_memo_t* memo, char* content, size_t cap, size_t* len_out,
    bool* complete_out) {
    if (len_out) *len_out = 0;
    if (complete_out) *complete_out = false;
    if (!s || !memo || !content || cap < 2 || !thingsos_flomo_id_valid(id)) return false;
    xSemaphoreTake(s->lock, portMAX_DELAY);
    if (s->detail_valid && !strcmp(s->detail.id, id) && s->detail_content) {
        *memo = s->detail;
        size_t n = s->detail_len < cap - 1 ? s->detail_len : cap - 1;
        memcpy(content, s->detail_content, n);
        content[n] = 0;
        if (len_out) *len_out = n;
        if (complete_out) *complete_out = s->detail_complete && n == s->detail_len;
        xSemaphoreGive(s->lock);
        return true;
    }
    bool found = find_memo_locked(id, memo);
    xSemaphoreGive(s->lock);
    if (!found) return false;

    /* Previously fetched full text cached on SD (caller holds thingsos_io_lock). */
    char path[96];
    size_t n = 0;
    FILE* f = memo_text_path(id, path, sizeof(path)) ? fopen(path, "rb") : NULL;
    if (f) {
        n = fread(content, 1, cap - 1, f);
        fclose(f);
    }
    if (n) {
        content[n] = 0;
        if (complete_out) *complete_out = n < cap - 1;
    } else {
        n = strlen(memo->preview);
        if (n >= cap) n = cap - 1;
        memcpy(content, memo->preview, n);
        content[n] = 0;
        if (complete_out) *complete_out = !memo->truncated;
    }
    if (len_out) *len_out = n;
    return true;
}

bool thingsos_flomo_set_filter(thingsos_flomo_mode_t mode, const char* tag, const char* query, bool fetch_online) {
    if (!s) return false;
    xSemaphoreTake(s->lock, portMAX_DELAY);
    s->status.mode = mode;
    strlcpy(s->status.tag, mode == THINGSOS_FLOMO_MODE_TAG && tag ? tag : "", sizeof(s->status.tag));
    strlcpy(s->status.query, mode == THINGSOS_FLOMO_MODE_REVIEW || !query ? "" : query, sizeof(s->status.query));
    if (mode == THINGSOS_FLOMO_MODE_TAG && !s->status.tag[0]) s->status.mode = THINGSOS_FLOMO_MODE_RECENT;
    ++s->status.serial;
    bool configured = s->status.configured;
    xSemaphoreGive(s->lock);
    if (fetch_online && configured && thingsos_network_connected()) return thingsos_flomo_refresh_list(false);
    return true;
}

bool thingsos_flomo_refresh_list(bool more) {
    return launch_job(more ? JOB_MORE : JOB_LIST, more ? THINGSOS_FLOMO_OP_MORE : THINGSOS_FLOMO_OP_LIST, "",
                      more ? "正在加载更早的笔记…" : "正在加载 flomo 笔记…");
}

bool thingsos_flomo_refresh_tags(void) {
    return launch_job(JOB_TAGS, THINGSOS_FLOMO_OP_TAGS, "", "正在同步 flomo 标签…");
}

bool thingsos_flomo_fetch_detail(const char* id) {
    if (!thingsos_flomo_id_valid(id)) return false;
    return launch_job(JOB_DETAIL, THINGSOS_FLOMO_OP_DETAIL, id, "正在获取笔记全文…");
}

void thingsos_flomo_load_offline(void) {
    if (!s) return;
    thingsos_flomo_memo_t* list =
        heap_caps_calloc(FLOMO_MEMOS_MAX, sizeof(*list), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!list) return;
    unsigned count = 0;
    if (thingsos_flomo_load_memos(FLOMO_LIST_CACHE, list, FLOMO_MEMOS_MAX, &count) && count) {
        xSemaphoreTake(s->lock, portMAX_DELAY);
        if (!s->status.memo_count && ensure_memos_locked()) {
            memcpy(s->memos, list, count * sizeof(*list));
            s->status.memo_count = count;
            s->status.has_more = false;
            s->status.offline_mode = true;
            ++s->status.serial;
        }
        xSemaphoreGive(s->lock);
    }
    free(list);
}

void thingsos_flomo_cancel(void) {
    if (s) s->cancel = true;
}
