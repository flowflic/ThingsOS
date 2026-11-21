// SPDX-License-Identifier: Apache-2.0
/* Cubox service: NVS credentials, LAN pairing server, HTTPS client, and SD cache. */
#include "thingsos/cubox.h"
#include "thingsos/content.h"
#include "thingsos/ui.h"
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

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char* TAG = "ThingsOS_cubox";

#define CUBOX_ROOT "/sdcard/.thingsos/cubox"
#define CUBOX_CARD_ROOT "/sdcard/.thingsos/cubox/c"
#define CUBOX_FOLDERS_CACHE "/sdcard/.thingsos/cubox/folders.dat"
#define CUBOX_LIST_CACHE "/sdcard/.thingsos/cubox/list.dat"

#define CUBOX_PAIR_PORT 8082
#define CUBOX_PAIR_CTRL_PORT 32770
#define CUBOX_PAIR_TTL_US (300LL * 1000000LL)
#define CUBOX_PAIR_MAX_FAILS 5
#define CUBOX_CARDS_MAX 200
#define CUBOX_RESPONSE_MAX (768u * 1024u)
#define CUBOX_DETAIL_MAGIC 0x43424431u

extern const uint8_t _binary_cubox_html_start[];
extern const uint8_t _binary_cubox_html_end[];

typedef enum {
    JOB_VERIFY = 1,
    JOB_FOLDERS,
    JOB_LIST,
    JOB_MORE,
    JOB_DETAIL,
    JOB_DOWNLOAD,
    JOB_UPDATE,
    JOB_ARCHIVE
} cubox_job_t;

/* List state of the inactive tab slot (0 = recent, 1 = folder). */
typedef struct {
    thingsos_cubox_filter_t filter;
    unsigned card_count;
    unsigned page;
    bool has_more;
    bool offline_mode;
    char folder_id[THINGSOS_CUBOX_ID_LEN];
    char folder_name[THINGSOS_CUBOX_NAME_LEN];
    char query[97];
} cubox_slot_t;

typedef struct {
    SemaphoreHandle_t lock;
    thingsos_cubox_status_t status;
    cubox_config_t config;
    cubox_config_t pending;
    thingsos_cubox_folder_t folders[THINGSOS_CUBOX_FOLDER_MAX];
    thingsos_cubox_card_t* cards;     /* active slot */
    thingsos_cubox_card_t* cards_alt; /* inactive slot */
    cubox_slot_t alt;
    thingsos_cubox_detail_t detail;
    bool detail_valid;
    char last_card_id[THINGSOS_CUBOX_ID_LEN];

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
    cubox_job_t job;
    char job_card_id[THINGSOS_CUBOX_ID_LEN];
    int job_read;
    int job_starred;
} cubox_state_t;

static cubox_state_t* s;

static thingsos_cubox_card_t* card_for_id(thingsos_cubox_card_t* list,unsigned count,const char* id) {
    if(list)for(unsigned i=0;i<count;++i)if(!strcmp(list[i].id,id))return list+i;
    return NULL;
}


static void set_message(const char* text) {
    if (!s) return;
    xSemaphoreTake(s->lock, portMAX_DELAY);
    strlcpy(s->status.message, text ? text : "", sizeof(s->status.message));
    ++s->status.serial;
    xSemaphoreGive(s->lock);
}

static bool sd_dir_exists(const char* path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static bool sd_file_size(const char* path, uint32_t* size_out) {
    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) return false;
    if (size_out) *size_out = (uint32_t)st.st_size;
    return true;
}

static bool ensure_dir_locked(const char* path) {
    if (mkdir(path, 0775) == 0 || errno == EEXIST) return sd_dir_exists(path);
    return false;
}

static bool ensure_cubox_dirs_locked(void) {
    return sd_dir_exists("/sdcard") && ensure_dir_locked("/sdcard/.thingsos") &&
           ensure_dir_locked(CUBOX_ROOT) && ensure_dir_locked(CUBOX_CARD_ROOT);
}

static bool card_dir_path(const char* card_id, char* out, size_t cap) {
    if (!thingsos_cubox_id_valid(card_id)) return false;
    return snprintf(out, cap, "%s/%s", CUBOX_CARD_ROOT, card_id) < (int)cap;
}

static bool card_article_path_raw(const char* card_id, char* out, size_t cap) {
    if (!thingsos_cubox_id_valid(card_id)) return false;
    return snprintf(out, cap, "%s/%s/article.md", CUBOX_CARD_ROOT, card_id) < (int)cap;
}

static bool card_detail_path(const char* card_id, char* out, size_t cap) {
    if (!thingsos_cubox_id_valid(card_id)) return false;
    return snprintf(out, cap, "%s/%s/detail.dat", CUBOX_CARD_ROOT, card_id) < (int)cap;
}

typedef struct {
    uint32_t magic;
    uint32_t size;
    uint32_t crc32;
    thingsos_cubox_detail_t detail;
} cubox_detail_blob_t;

static bool save_detail_blob_locked(const char* card_id, const thingsos_cubox_detail_t* detail) {
    char p[160], tmp[176];
    if (!card_detail_path(card_id, p, sizeof(p))) return false;
    if (snprintf(tmp, sizeof(tmp), "%s.tmp", p) >= (int)sizeof(tmp)) return false;
    cubox_detail_blob_t* blob = heap_caps_calloc(1, sizeof(*blob), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!blob) return false;
    blob->magic = CUBOX_DETAIL_MAGIC;
    blob->size = (uint32_t)sizeof(blob->detail);
    blob->detail = *detail;
    blob->crc32 = thingsos_cubox_crc32(&blob->detail, sizeof(blob->detail));
    FILE* f = fopen(tmp, "wb");
    bool ok = f && fwrite(blob, sizeof(*blob), 1, f) == 1 && fflush(f) == 0;
    if (f && fclose(f) != 0) ok = false;
    free(blob);
    if (!ok) {
        remove(tmp);
        return false;
    }
    remove(p);
    if (rename(tmp, p) != 0) {
        remove(tmp);
        return false;
    }
    return true;
}

static bool load_detail_blob_locked(const char* card_id, thingsos_cubox_detail_t* out) {
    char p[160];
    if (!card_detail_path(card_id, p, sizeof(p))) return false;
    cubox_detail_blob_t* blob = heap_caps_calloc(1, sizeof(*blob), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!blob) return false;
    FILE* f = fopen(p, "rb");
    bool ok = f && fread(blob, sizeof(*blob), 1, f) == 1;
    if (f) fclose(f);
    ok = ok && blob->magic == CUBOX_DETAIL_MAGIC && blob->size == sizeof(blob->detail) &&
         thingsos_cubox_crc32(&blob->detail, sizeof(blob->detail)) == blob->crc32;
    if (ok && out) *out = blob->detail;
    free(blob);
    return ok;
}

static void annotate_card_cache_locked(thingsos_cubox_card_t* c) {
    char md[160];
    uint32_t sz = 0;
    if (card_article_path_raw(c->id, md, sizeof(md)) && sd_file_size(md, &sz)) {
        c->cached = true;
        c->cached_bytes = sz;
    } else {
        c->cached = false;
        c->cached_bytes = 0;
        c->cached_images = 0;
    }
}

static bool remove_card_dir_locked(const char* card_id) {
    char dirp[160];
    if (!card_dir_path(card_id, dirp, sizeof(dirp))) return false;
    DIR* d = opendir(dirp);
    if (!d) return errno == ENOENT;
    struct dirent* ent;
    char child[420];
    while ((ent = readdir(d)) != NULL) {
        if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, "..")) continue;
        if (snprintf(child, sizeof(child), "%s/%s", dirp, ent->d_name) < (int)sizeof(child)) {
            unlink(child);
        }
    }
    closedir(d);
    return rmdir(dirp) == 0 || errno == ENOENT;
}

static char* cubox_api_request(
    const char* server,
    const char* token,
    esp_http_client_method_t method,
    const char* path_and_query,
    const char* body,
    int* http_status_out) {
    if (http_status_out) *http_status_out = 0;
    if (!thingsos_cubox_server_valid(server) || !token || !token[0] || !path_and_query) return NULL;

    char url[256];
    if (snprintf(url, sizeof(url), "https://%s%s", server, path_and_query) >= (int)sizeof(url))
        return NULL;

    size_t capacity=8192;
    char* response=heap_caps_malloc(capacity+1,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if (!response) {
        set_message("Cubox 请求内存不足");
        return NULL;
    }

    esp_http_client_config_t cfg = {
        .url = url,
        .method = method,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 20000,
        .disable_auto_redirect = true,
        .max_authorization_retries = -1,
        .buffer_size = 4096,
        .buffer_size_tx = 2048,
    };

    esp_http_client_handle_t http = esp_http_client_init(&cfg);
    esp_err_t err = http ? ESP_OK : ESP_ERR_NO_MEM;
    size_t body_len = body ? strlen(body) : 0;
    int status = 0;
    size_t used = 0;
    bool complete = false;

    if (http) {
        char auth[300];
        snprintf(auth, sizeof(auth), "Bearer %s", token);
        err = esp_http_client_set_header(http, "Authorization", auth);
        cubox_wipe(auth, sizeof(auth));
        if (err == ESP_OK) err = esp_http_client_set_header(http, "User-Agent", "ThingsOS-Cubox/1.0");
        if (err == ESP_OK && body) {
            err = esp_http_client_set_header(http, "Content-Type", "application/json; charset=utf-8");
        }
        if (err == ESP_OK) err = esp_http_client_open(http, (int)body_len);
        for (size_t sent = 0; err == ESP_OK && sent < body_len;) {
            if (s->cancel) {
                err = ESP_FAIL;
                break;
            }
            int n = esp_http_client_write(http, body + sent, (int)(body_len - sent));
            if (n <= 0) {
                err = ESP_FAIL;
                break;
            }
            sent += (size_t)n;
        }
        if (err == ESP_OK) {
            int64_t length=esp_http_client_fetch_headers(http);
            status=esp_http_client_get_status_code(http);
            if(length<0 && !esp_http_client_is_chunked_response(http))err=ESP_FAIL;
            else if(length>CUBOX_RESPONSE_MAX)err=ESP_ERR_INVALID_SIZE;
            else if(length>(int64_t)capacity) {
                char* expanded=heap_caps_realloc(response,(size_t)length+1,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
                if(expanded){response=expanded;capacity=(size_t)length;}
                else err=ESP_ERR_NO_MEM;
            }
        }
        while (err == ESP_OK) {
            if (s->cancel) {
                err = ESP_FAIL;
                break;
            }
            char overflow = 0;
            if(used==capacity && capacity<CUBOX_RESPONSE_MAX &&
               !esp_http_client_is_complete_data_received(http)) {
                size_t next=capacity*2;
                if(next>CUBOX_RESPONSE_MAX)next=CUBOX_RESPONSE_MAX;
                char* expanded=heap_caps_realloc(response,next+1,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
                if(!expanded){err=ESP_ERR_NO_MEM;break;}
                response=expanded;capacity=next;
            }
            size_t rem=capacity-used;
            int n = esp_http_client_read(
                http, rem ? response + used : &overflow,
                (int)(rem > 4096 ? 4096 : (rem ? rem : 1)));
            if (n < 0) {
                err = ESP_FAIL;
                break;
            }
            if (n == 0) {
                complete = esp_http_client_is_complete_data_received(http);
                if (!complete) err = ESP_FAIL;
                break;
            }
            if (!rem) {
                err = ESP_ERR_INVALID_SIZE;
                break;
            }
            used += (size_t)n;
        }
        esp_http_client_close(http);
        esp_http_client_cleanup(http);
    }

    if (http_status_out) *http_status_out = status;
    response[used] = 0;

    if (err == ESP_OK && complete && status == 200) {
        return response;
    }

    char msg[THINGSOS_CUBOX_MESSAGE_LEN];
    if (s->cancel) {
        snprintf(msg, sizeof(msg), "Cubox 操作已取消");
    } else if (status == 401 || status == 403) {
        snprintf(msg, sizeof(msg), "Cubox Token 无效或权限不足 (HTTP %d)", status);
    } else if (status == 429) {
        snprintf(msg, sizeof(msg), "Cubox 请求过于频繁，请稍后重试");
    } else if (status >= 400) {
        int code = -1;
        char remote_msg[96] = {0};
        thingsos_cubox_parse_envelope(response, used, &code, remote_msg, sizeof(remote_msg));
        if (remote_msg[0])
            snprintf(msg, sizeof(msg), "Cubox 错误 (HTTP %d): %s", status, remote_msg);
        else
            snprintf(msg, sizeof(msg), "Cubox 服务返回 HTTP %d", status);
    } else if (err == ESP_ERR_INVALID_SIZE) {
        snprintf(msg, sizeof(msg), "文章响应超过 768 KB 限制");
    } else if(err==ESP_ERR_NO_MEM) {
        snprintf(msg,sizeof(msg),"Cubox 响应内存不足，请关闭文章后重试");
    } else {
        snprintf(msg, sizeof(msg), "连接 Cubox 失败 (%s)", esp_err_to_name(err));
    }
    set_message(msg);
    free(response);
    return NULL;
}

typedef struct {
    char location[THINGSOS_CUBOX_URL_LEN];
} img_hdr_ctx_t;

typedef struct {
    char url[THINGSOS_CUBOX_URL_LEN];
    char next[THINGSOS_CUBOX_URL_LEN];
    img_hdr_ctx_t hctx;
} img_dl_scratch_t;

static esp_err_t image_http_event(esp_http_client_event_t* evt) {
    if (evt->event_id == HTTP_EVENT_ON_HEADER && evt->header_key && evt->header_value &&
        evt->user_data) {
        img_hdr_ctx_t* ctx = (img_hdr_ctx_t*)evt->user_data;
        if (!strcasecmp(evt->header_key, "Location")) {
            strlcpy(ctx->location, evt->header_value, sizeof(ctx->location));
        }
    }
    return ESP_OK;
}

static bool resolve_redirect(const char* current, const char* loc, char* out, size_t cap) {
    if (!loc || !loc[0]) return false;
    if (!strncmp(loc, "https://", 8) || !strncmp(loc, "http://", 7)) {
        strlcpy(out, loc, cap);
        return thingsos_cubox_http_url_valid(out);
    }
    if (!strncmp(loc, "//", 2)) {
        const char* scheme = !strncmp(current, "https://", 8) ? "https:" : "http:";
        return snprintf(out, cap, "%s%s", scheme, loc) < (int)cap && thingsos_cubox_http_url_valid(out);
    }
    if (loc[0] == '/') {
        const char* p = strstr(current, "://");
        if (!p) return false;
        const char* slash = strchr(p + 3, '/');
        size_t origin_len = slash ? (size_t)(slash - current) : strlen(current);
        return origin_len + strlen(loc) < cap &&
               snprintf(out, cap, "%.*s%s", (int)origin_len, current, loc) < (int)cap &&
               thingsos_cubox_http_url_valid(out);
    }
    return false;
}

static bool download_image_to_sd(const char* dir_path, unsigned index, thingsos_cubox_image_ref_t* ref) {
    if (!ref || !thingsos_cubox_http_url_valid(ref->url)) return false;
    char temporary[256];
    if(snprintf(temporary,sizeof(temporary),"%s/img-%02u.part",dir_path,index)>=(int)sizeof(temporary))
        return false;
    const size_t block_size=4096;
    uint8_t* buf=heap_caps_malloc(block_size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    img_dl_scratch_t* scratch=heap_caps_calloc(1,sizeof(*scratch),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if (!buf || !scratch) {
        free(buf);
        free(scratch);
        return false;
    }

    strlcpy(scratch->url, ref->url, sizeof(scratch->url));

    bool ok = false;
    size_t downloaded = 0;
    unsigned char signature[32]={0};

    for (unsigned hop = 0; hop < 4 && !s->cancel; ++hop) {
        memset(&scratch->hctx, 0, sizeof(scratch->hctx));
        esp_http_client_config_t cfg = {
            .url = scratch->url,
            .method = HTTP_METHOD_GET,
            .crt_bundle_attach = esp_crt_bundle_attach,
            .timeout_ms = 15000,
            .disable_auto_redirect = true,
            .max_authorization_retries = -1,
            .buffer_size = 2048,
            .event_handler = image_http_event,
            .user_data = &scratch->hctx,
        };
        esp_http_client_handle_t http = esp_http_client_init(&cfg);
        if (!http) break;
        esp_http_client_set_header(
            http, "User-Agent",
            "Mozilla/5.0 (Linux; Android 13) AppleWebKit/537.36 ThingsOS-Cubox/1.0");
        esp_http_client_set_header(http, "Accept", "image/*,*/*;q=0.8");

        esp_err_t err = esp_http_client_open(http, 0);
        int status = 0;
        if (err == ESP_OK) {
            int64_t length=esp_http_client_fetch_headers(http);
            status=esp_http_client_get_status_code(http);
            if(length<0 && !esp_http_client_is_chunked_response(http))err=ESP_FAIL;
            if(status==200 && length>THINGSOS_MEMBER_BYTES)err=ESP_ERR_INVALID_SIZE;
        }

        if (err == ESP_OK &&
            (status == 301 || status == 302 || status == 303 || status == 307 || status == 308)) {
            bool resolved = resolve_redirect(
                scratch->url, scratch->hctx.location, scratch->next, sizeof(scratch->next));
            esp_http_client_close(http);
            esp_http_client_cleanup(http);
            if (!resolved) break;
            strlcpy(scratch->url, scratch->next, sizeof(scratch->url));
            continue;
        }

        downloaded = 0;
        bool complete = false;
        if (err == ESP_OK && status == 200) {
            xSemaphoreTake(thingsos_io_lock,portMAX_DELAY);
            FILE* f=fopen(temporary,"wb");
            xSemaphoreGive(thingsos_io_lock);
            if(!f)err=ESP_FAIL;
            while(err==ESP_OK && !s->cancel) {
                size_t rem=THINGSOS_MEMBER_BYTES-downloaded;
                size_t want=rem>block_size?block_size:rem?rem:1;
                int n=esp_http_client_read(http,(char*)buf,(int)want);
                if (n < 0) {
                    err = ESP_FAIL;
                    break;
                }
                if (n == 0) {
                    complete = esp_http_client_is_complete_data_received(http);
                    if (!complete) err = ESP_FAIL;
                    break;
                }
                if (!rem) {
                    err = ESP_ERR_INVALID_SIZE;
                    break;
                }
                if(downloaded<sizeof(signature)) {
                    size_t take=sizeof(signature)-downloaded;
                    if(take>(size_t)n)take=(size_t)n;
                    memcpy(signature+downloaded,buf,take);
                }
                xSemaphoreTake(thingsos_io_lock,portMAX_DELAY);
                bool wrote=fwrite(buf,1,(size_t)n,f)==(size_t)n;
                xSemaphoreGive(thingsos_io_lock);
                if(!wrote){err=ESP_FAIL;break;}
                downloaded += (size_t)n;
            }
            if(f) {
                xSemaphoreTake(thingsos_io_lock,portMAX_DELAY);
                if(fflush(f))err=ESP_FAIL;
                if(fclose(f))err=ESP_FAIL;
                xSemaphoreGive(thingsos_io_lock);
            }
        }
        esp_http_client_close(http);
        esp_http_client_cleanup(http);

        ok=err==ESP_OK && !s->cancel && complete && status==200 && downloaded>=16;
        break;
    }

    size_t prefix=downloaded<sizeof(signature)?downloaded:sizeof(signature);
    const char* ext=ok?thingsos_cubox_image_ext(signature,prefix):NULL;
    xSemaphoreTake(thingsos_io_lock,portMAX_DELAY);
    ok=false;
    if(ext && !s->cancel) {
        snprintf(ref->filename, sizeof(ref->filename), "img_%02u%s", index, ext);
        char final_path[240],backup[256];
        if(snprintf(final_path,sizeof(final_path),"%s/%s",dir_path,ref->filename)<(int)sizeof(final_path)) {
            snprintf(backup,sizeof(backup),"%s.bak",final_path);
            struct stat info;
            bool exists=!stat(final_path,&info);
            if(!exists && !rename(backup,final_path))exists=true;
            if(exists)remove(backup);
            if(!exists || !rename(final_path,backup)) {
                ok=!rename(temporary,final_path);
                if(ok)remove(backup);
                else if(exists)rename(backup,final_path);
            }
        }
    }
    if(!ok)remove(temporary);
    xSemaphoreGive(thingsos_io_lock);
    ref->downloaded=ok;
    free(scratch);
    free(buf);
    return ok;
}

static bool fetch_folders_sync(const char* server, const char* token) {
    int http_status = 0;
    char* resp = cubox_api_request(server, token, HTTP_METHOD_GET, "/c/api/cli/folder/list", NULL, &http_status);
    if (!resp) return false;

    thingsos_cubox_folder_t* parsed = heap_caps_calloc(
        THINGSOS_CUBOX_FOLDER_MAX, sizeof(*parsed), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!parsed) {
        free(resp);
        set_message("目录缓存内存不足");
        return false;
    }

    unsigned count = 0;
    int code = -1;
    char remote_msg[96] = {0};
    bool ok = thingsos_cubox_parse_folders(
        resp, strlen(resp), parsed, THINGSOS_CUBOX_FOLDER_MAX, &count, &code, remote_msg,
        sizeof(remote_msg));
    free(resp);

    if (!ok) {
        free(parsed);
        char msg[THINGSOS_CUBOX_MESSAGE_LEN];
        if (code == 401 || code == 403)
            snprintf(msg, sizeof(msg), "Cubox Token 无效 (code %d)", code);
        else if (remote_msg[0])
            snprintf(msg, sizeof(msg), "Cubox 目录错误 (%d): %s", code, remote_msg);
        else
            snprintf(msg, sizeof(msg), "解析 Cubox 目录失败 (code %d)", code);
        set_message(msg);
        return false;
    }

    xSemaphoreTake(thingsos_io_lock, portMAX_DELAY);
    if (ensure_cubox_dirs_locked()) {
        thingsos_cubox_save_folders(CUBOX_FOLDERS_CACHE, parsed, count);
    }
    xSemaphoreGive(thingsos_io_lock);

    xSemaphoreTake(s->lock, portMAX_DELAY);
    memcpy(s->folders, parsed, count * sizeof(*parsed));
    s->status.folder_count = count;
    ++s->status.serial;
    xSemaphoreGive(s->lock);

    free(parsed);
    return true;
}

static bool fetch_cards_sync(const char* server, const char* token, bool append_more) {
    xSemaphoreTake(s->lock, portMAX_DELAY);
    thingsos_cubox_filter_t filter = s->status.filter;
    char folder_id[THINGSOS_CUBOX_ID_LEN];
    char query[97];
    char last_id[THINGSOS_CUBOX_ID_LEN];
    strlcpy(folder_id, s->status.folder_id, sizeof(folder_id));
    strlcpy(query, s->status.query, sizeof(query));
    unsigned page = append_more ? (s->status.page + 1) : 1;
    if (append_more && !query[0] && s->status.card_count > 0) {
        strlcpy(last_id, s->cards[s->status.card_count - 1].id, sizeof(last_id));
    } else {
        last_id[0] = 0;
    }
    xSemaphoreGive(s->lock);

    char body[512];
    if (!thingsos_cubox_build_filter_request(
            filter, folder_id, query, last_id, page, THINGSOS_CUBOX_LIST_PAGE_SIZE, body,
            sizeof(body))) {
        set_message("构造筛选请求失败");
        return false;
    }

    int http_status = 0;
    char* resp = cubox_api_request(server, token, HTTP_METHOD_POST, "/c/api/cli/card/filter", body, &http_status);
    if (!resp) return false;

    thingsos_cubox_card_t* batch = heap_caps_calloc(
        THINGSOS_CUBOX_LIST_PAGE_SIZE, sizeof(*batch), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!batch) {
        free(resp);
        set_message("列表内存不足");
        return false;
    }

    unsigned got = 0;
    int code = -1;
    char remote_msg[96] = {0};
    bool ok = thingsos_cubox_parse_cards(
        resp, strlen(resp), batch, THINGSOS_CUBOX_LIST_PAGE_SIZE, &got, &code, remote_msg,
        sizeof(remote_msg));
    free(resp);

    if (!ok) {
        free(batch);
        char msg[THINGSOS_CUBOX_MESSAGE_LEN];
        if (remote_msg[0])
            snprintf(msg, sizeof(msg), "Cubox 列表错误 (%d): %s", code, remote_msg);
        else
            snprintf(msg, sizeof(msg), "解析 Cubox 文章列表失败 (code %d)", code);
        set_message(msg);
        return false;
    }

    xSemaphoreTake(thingsos_io_lock, portMAX_DELAY);
    for (unsigned i = 0; i < got; ++i) annotate_card_cache_locked(&batch[i]);
    xSemaphoreGive(thingsos_io_lock);

    xSemaphoreTake(s->lock, portMAX_DELAY);
    if (!append_more) {
        s->status.card_count = 0;
        s->status.page = 1;
    } else {
        s->status.page = page;
    }
    for (unsigned i = 0; i < got && s->status.card_count < CUBOX_CARDS_MAX; ++i) {
        bool dup = false;
        for (unsigned j = 0; j < s->status.card_count; ++j) {
            if (!strcmp(s->cards[j].id, batch[i].id)) {
                s->cards[j] = batch[i];
                dup = true;
                break;
            }
        }
        if (!dup) s->cards[s->status.card_count++] = batch[i];
    }
    s->status.has_more = (got >= THINGSOS_CUBOX_LIST_PAGE_SIZE && s->status.card_count < CUBOX_CARDS_MAX);
    s->status.offline_mode = false;
    snprintf(
        s->status.message, sizeof(s->status.message),
        s->status.card_count ? "已加载 %u 篇收藏" : "当前筛选下没有收藏",
        s->status.card_count);
    ++s->status.serial;
    unsigned total = s->status.card_count;
    bool cacheable=s->status.slot==0 && !s->status.query[0];
    xSemaphoreGive(s->lock);

    free(batch);

    xSemaphoreTake(thingsos_io_lock, portMAX_DELAY);
    if (cacheable && ensure_cubox_dirs_locked()) {
        thingsos_cubox_save_cards(CUBOX_LIST_CACHE, s->cards, total);
    }
    xSemaphoreGive(thingsos_io_lock);
    return true;
}

static bool fetch_detail_sync(
    const char* server,
    const char* token,
    const char* card_id,
    bool download_article) {
    if (!thingsos_cubox_id_valid(card_id)) return false;

    char path[128];
    snprintf(path, sizeof(path), "/c/api/cli/card/detail?id=%s", card_id);

    int http_status = 0;
    char* resp = cubox_api_request(server, token, HTTP_METHOD_GET, path, NULL, &http_status);
    if (!resp) return false;

    thingsos_cubox_detail_t* det = heap_caps_calloc(1, sizeof(*det), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    char* raw_md = heap_caps_calloc(1, THINGSOS_XML_BYTES + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!det || !raw_md) {
        free(det);
        free(raw_md);
        free(resp);
        set_message("文章详情内存不足");
        return false;
    }

    size_t raw_len = 0;
    int code = -1;
    char remote_msg[96] = {0};
    bool ok = thingsos_cubox_parse_detail(
        resp, strlen(resp), det, raw_md, THINGSOS_XML_BYTES + 1, &raw_len, &code, remote_msg,
        sizeof(remote_msg));
    free(resp);

    if (!ok) {
        free(det);
        free(raw_md);
        char msg[THINGSOS_CUBOX_MESSAGE_LEN];
        if (remote_msg[0])
            snprintf(msg, sizeof(msg), "获取详情失败 (%d): %s", code, remote_msg);
        else
            snprintf(msg, sizeof(msg), "解析文章详情失败 (code %d)", code);
        set_message(msg);
        return false;
    }

    unsigned img_ok_count = 0;
    if (download_article) {
        char card_dir[160], md_path[180], md_tmp[192];
        card_dir_path(card_id, card_dir, sizeof(card_dir));
        card_article_path_raw(card_id, md_path, sizeof(md_path));
        snprintf(md_tmp, sizeof(md_tmp), "%s.tmp", md_path);

        xSemaphoreTake(thingsos_io_lock, portMAX_DELAY);
        bool dir_ok = ensure_cubox_dirs_locked() && ensure_dir_locked(card_dir);
        xSemaphoreGive(thingsos_io_lock);

        if (!dir_ok) {
            free(det);
            free(raw_md);
            set_message("无法创建 SD 卡 Cubox 缓存目录");
            return false;
        }

        thingsos_cubox_image_ref_t* refs = heap_caps_calloc(
            THINGSOS_IMAGE_LIMIT, sizeof(*refs), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        char* final_md = heap_caps_calloc(1, THINGSOS_XML_BYTES + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!refs || !final_md) {
            free(refs);
            free(final_md);
            free(det);
            free(raw_md);
            set_message("排版缓存内存不足");
            return false;
        }

        unsigned ref_count = thingsos_cubox_extract_images(raw_md, refs, THINGSOS_IMAGE_LIMIT);
        xSemaphoreTake(s->lock, portMAX_DELAY);
        s->status.dl_done = 0;
        s->status.dl_total = ref_count;
        snprintf(
            s->status.message, sizeof(s->status.message),
            ref_count ? "正在下载图片 (0/%u)…" : "正在生成阅读排版…", ref_count);
        ++s->status.serial;
        xSemaphoreGive(s->lock);

        for (unsigned i = 0; i < ref_count && !s->cancel; ++i) {
            if (download_image_to_sd(card_dir, i, &refs[i])) ++img_ok_count;
            xSemaphoreTake(s->lock, portMAX_DELAY);
            s->status.dl_done = i + 1;
            snprintf(
                s->status.message, sizeof(s->status.message), "正在下载图片 (%u/%u)…", i + 1,
                ref_count);
            ++s->status.serial;
            xSemaphoreGive(s->lock);
        }

        if (s->cancel) {
            free(refs);
            free(final_md);
            free(det);
            free(raw_md);
            set_message("已取消缓存下载");
            return false;
        }

        size_t written = 0;
        bool built = thingsos_cubox_build_article_markdown(
            det, raw_md, refs, ref_count, final_md, THINGSOS_XML_BYTES + 1, &written);
        free(refs);

        bool saved = false;
        if (built && written > 0) {
            det->card.cached_images = (uint16_t)img_ok_count;
            det->card.cached = true;
            det->card.cached_bytes = (uint32_t)written;

            xSemaphoreTake(thingsos_io_lock, portMAX_DELAY);
            FILE* f = fopen(md_tmp, "wb");
            saved = f && fwrite(final_md, 1, written, f) == written && fflush(f) == 0;
            if (f && fclose(f) != 0) saved = false;
            if (saved) {
                remove(md_path);
                if (rename(md_tmp, md_path) != 0) saved = false;
            }
            if (!saved) remove(md_tmp);
            if (saved) save_detail_blob_locked(card_id, det);
            xSemaphoreGive(thingsos_io_lock);
        }
        free(final_md);

        if (!saved) {
            free(det);
            free(raw_md);
            set_message("写入 SD 卡文章失败");
            return false;
        }
    }

    free(raw_md);

    xSemaphoreTake(thingsos_io_lock, portMAX_DELAY);
    annotate_card_cache_locked(&det->card);
    if (det->card.cached) {
        if (img_ok_count) det->card.cached_images = (uint16_t)img_ok_count;
        save_detail_blob_locked(card_id, det);
    }
    xSemaphoreGive(thingsos_io_lock);

    xSemaphoreTake(s->lock, portMAX_DELAY);
    s->detail = *det;
    s->detail_valid = true;
    thingsos_cubox_card_t* active=card_for_id(s->cards,s->status.card_count,det->card.id);
    thingsos_cubox_card_t* other=card_for_id(s->cards_alt,s->alt.card_count,det->card.id);
    if(active)*active=det->card;
    if(other)*other=det->card;
    if (download_article) {
        snprintf(
            s->status.message, sizeof(s->status.message),
            det->card.cached_images ? "已缓存正文及 %u 张图片" : "正文已缓存到 SD 卡",
            det->card.cached_images);
    } else {
        strlcpy(s->status.message, "已更新文章详情", sizeof(s->status.message));
    }
    ++s->status.serial;
    xSemaphoreGive(s->lock);

    free(det);
    return true;
}

static bool update_card_sync(const char* server, const char* token, const char* card_id, int set_read, int set_starred) {
    char body[160];
    if (!thingsos_cubox_build_update_request(card_id, set_read, set_starred, body, sizeof(body)))
        return false;
    int http_status = 0;
    char* resp = cubox_api_request(server, token, HTTP_METHOD_POST, "/c/api/cli/card/update", body, &http_status);
    if (!resp) return false;

    int code = -1;
    char remote_msg[96] = {0};
    bool ok = thingsos_cubox_parse_envelope(resp, strlen(resp), &code, remote_msg, sizeof(remote_msg));
    free(resp);
    if (!ok) {
        char msg[THINGSOS_CUBOX_MESSAGE_LEN];
        snprintf(msg, sizeof(msg), "更新状态失败 (%d): %s", code, remote_msg[0] ? remote_msg : "未知错误");
        set_message(msg);
        return false;
    }

    xSemaphoreTake(s->lock, portMAX_DELAY);
    if (s->detail_valid && !strcmp(s->detail.card.id, card_id)) {
        if (set_read >= 0) s->detail.card.read = set_read != 0;
        if (set_starred >= 0) s->detail.card.starred = set_starred != 0;
    }
    thingsos_cubox_card_t* matches[]={
        card_for_id(s->cards,s->status.card_count,card_id),card_for_id(s->cards_alt,s->alt.card_count,card_id)};
    for(unsigned i=0;i<2;++i)if(matches[i]) {
        if(set_read>=0)matches[i]->read=set_read!=0;
        if(set_starred>=0)matches[i]->starred=set_starred!=0;
    }
    strlcpy(s->status.message, "已同步 Cubox 状态", sizeof(s->status.message));
    ++s->status.serial;
    xSemaphoreGive(s->lock);
    return true;
}

static bool archive_card_sync(const char* server, const char* token, const char* card_id) {
    char body[128];
    if (!thingsos_cubox_build_archive_request(card_id, body, sizeof(body))) return false;
    int http_status = 0;
    char* resp = cubox_api_request(server, token, HTTP_METHOD_POST, "/c/api/cli/cards/archiving", body, &http_status);
    if (!resp) return false;

    int code = -1;
    char remote_msg[96] = {0};
    bool ok = thingsos_cubox_parse_envelope(resp, strlen(resp), &code, remote_msg, sizeof(remote_msg));
    free(resp);
    if (!ok) {
        char msg[THINGSOS_CUBOX_MESSAGE_LEN];
        snprintf(msg, sizeof(msg), "归档失败 (%d): %s", code, remote_msg[0] ? remote_msg : "未知错误");
        set_message(msg);
        return false;
    }

    xSemaphoreTake(s->lock, portMAX_DELAY);
    if (s->detail_valid && !strcmp(s->detail.card.id, card_id)) {
        s->detail.card.archived = true;
    }
    for (unsigned i = 0; i < s->status.card_count; ++i) {
        if (!strcmp(s->cards[i].id, card_id)) {
            for (unsigned j = i; j + 1 < s->status.card_count; ++j) s->cards[j] = s->cards[j + 1];
            --s->status.card_count;
            break;
        }
    }
    thingsos_cubox_card_t* other=card_for_id(s->cards_alt,s->alt.card_count,card_id);
    if(other) {
        size_t tail=s->alt.card_count-(size_t)(other-s->cards_alt)-1;
        memmove(other,other+1,tail*sizeof(*other));
        --s->alt.card_count;
    }
    strlcpy(s->status.message, "已在 Cubox 归档", sizeof(s->status.message));
    ++s->status.serial;
    xSemaphoreGive(s->lock);
    return true;
}

static void worker_task(void* arg) {
    (void)arg;
    xSemaphoreTake(s->lock, portMAX_DELAY);
    cubox_job_t job = s->job;
    cubox_config_t cfg = (job == JOB_VERIFY) ? s->pending : s->config;
    char card_id[THINGSOS_CUBOX_ID_LEN];
    strlcpy(card_id, s->job_card_id, sizeof(card_id));
    int req_read = s->job_read;
    int req_starred = s->job_starred;
    xSemaphoreGive(s->lock);

    if (job == JOB_VERIFY) {
        bool ok = fetch_folders_sync(cfg.server, cfg.token);
        if (ok) {
            if (cubox_config_save(&cfg)) {
                xSemaphoreTake(s->lock, portMAX_DELAY);
                if(strcmp(s->config.server,cfg.server) || strcmp(s->config.token,cfg.token)) {
                    memset(&s->alt,0,sizeof(s->alt));
                    s->status.card_count=0;
                }
                s->config = cfg;
                s->status.configured = true;
                s->status.config_phase = THINGSOS_CUBOX_CONFIG_READY;
                strlcpy(s->status.server, cfg.server, sizeof(s->status.server));
                ++s->status.connection_serial;
                strlcpy(s->status.message, "Cubox 已连接", sizeof(s->status.message));
                ++s->status.serial;
                xSemaphoreGive(s->lock);
                fetch_cards_sync(cfg.server, cfg.token, false);
            } else {
                set_message("保存 Cubox 凭证失败");
                xSemaphoreTake(s->lock, portMAX_DELAY);
                s->status.config_phase = THINGSOS_CUBOX_CONFIG_FAILED;
                ++s->status.serial;
                xSemaphoreGive(s->lock);
            }
        } else {
            xSemaphoreTake(s->lock, portMAX_DELAY);
            s->status.config_phase = THINGSOS_CUBOX_CONFIG_FAILED;
            ++s->status.serial;
            xSemaphoreGive(s->lock);
        }
        cubox_wipe(&s->pending, sizeof(s->pending));
    } else if (job == JOB_FOLDERS) {
        fetch_folders_sync(cfg.server, cfg.token);
    } else if (job == JOB_LIST || job == JOB_MORE) {
        fetch_cards_sync(cfg.server, cfg.token, job == JOB_MORE);
    } else if (job == JOB_DETAIL) {
        fetch_detail_sync(cfg.server, cfg.token, card_id, false);
    } else if (job == JOB_DOWNLOAD) {
        fetch_detail_sync(cfg.server, cfg.token, card_id, true);
    } else if (job == JOB_UPDATE) {
        update_card_sync(cfg.server, cfg.token, card_id, req_read, req_starred);
    } else if (job == JOB_ARCHIVE) {
        archive_card_sync(cfg.server, cfg.token, card_id);
    }

    cubox_wipe(&cfg, sizeof(cfg));
    ESP_LOGI(TAG, "Worker stack margin=%u bytes", (unsigned)uxTaskGetStackHighWaterMark(NULL));

    xSemaphoreTake(s->lock, portMAX_DELAY);
    s->status.busy = false;
    s->status.op = THINGSOS_CUBOX_OP_NONE;
    s->status.active_card_id[0] = 0;
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

static bool launch_job(
    cubox_job_t job,
    thingsos_cubox_op_t op,
    const char* card_id,
    int set_read,
    int set_starred,
    const char* init_msg) {
    if (!s) return false;
    if (!thingsos_network_connected()) {
        set_message("请先连接 WiFi");
        return false;
    }
    const uint32_t internal = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    const bool heavy_psram = (job == JOB_DETAIL || job == JOB_DOWNLOAD);
    const size_t min_psram = heavy_psram ? (1280u * 1024u) : (576u * 1024u);
    const size_t min_psram_block = heavy_psram ? (256u * 1024u) : (128u * 1024u);
    size_t int_free = heap_caps_get_free_size(internal);
    size_t int_largest = heap_caps_get_largest_free_block(internal);
    size_t ps_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t ps_largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    if (int_free < 34u * 1024u ||
        int_largest < 16u * 1024u ||
        ps_free < min_psram ||
        ps_largest < min_psram_block) {
        ESP_LOGW(
            TAG,
            "Job %d refused: internal_free=%u largest=%u psram_free=%u psram_largest=%u",
            (int)job, (unsigned)int_free, (unsigned)int_largest,
            (unsigned)ps_free, (unsigned)ps_largest);
        set_message("Cubox 内存不足，请退出阅读或传输后重试");
        return false;
    }

    xSemaphoreTake(s->lock, portMAX_DELAY);
    reap_worker_locked();
    bool ready = !s->status.busy && !s->task &&
                 (job == JOB_VERIFY || s->status.configured);
    bool claimed = ready && thingsos_network_external_begin();
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
        strlcpy(s->job_card_id, card_id ? card_id : "", sizeof(s->job_card_id));
        strlcpy(s->status.active_card_id, card_id ? card_id : "", sizeof(s->status.active_card_id));
        s->job_read = set_read;
        s->job_starred = set_starred;
        s->status.busy = true;
        s->status.op = op;
        s->status.dl_done = 0;
        s->status.dl_total = 0;
        if (init_msg) strlcpy(s->status.message, init_msg, sizeof(s->status.message));
        ++s->status.serial;
    }
    xSemaphoreGive(s->lock);
    if (!ready) return false;

    if (xTaskCreate(worker_task, "cubox_work", 14336, NULL, 4, &s->task) != pdPASS) {
        xSemaphoreTake(s->lock, portMAX_DELAY);
        s->task = NULL;
        s->status.busy = false;
        s->status.op = THINGSOS_CUBOX_OP_NONE;
        if (s->external_claimed) {
            thingsos_network_external_end();
            s->external_claimed = false;
        }
        strlcpy(s->status.message, "无法启动 Cubox 网络任务", sizeof(s->status.message));
        ++s->status.serial;
        xSemaphoreGive(s->lock);
        return false;
    }
    return true;
}

static bool host_header_allowed(httpd_req_t* req) {
    char host[128] = {0};
    char ip[96] = {0};
    if (httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host)) != ESP_OK) return false;
    thingsos_network_address(false, ip, sizeof(ip));
    const char* addr = strstr(ip, "://");
    addr = addr ? addr + 3 : ip;
    char expected[128];
    snprintf(expected, sizeof(expected), "%s:%u", addr, CUBOX_PAIR_PORT);
    return !strcmp(host, expected);
}

static void pair_handler_enter(void) {
    if (!s) return;
    xSemaphoreTake(s->lock, portMAX_DELAY);
    ++s->pair_handlers_active;
    xSemaphoreGive(s->lock);
}

static void pair_handler_leave(void) {
    if (!s) return;
    xSemaphoreTake(s->lock, portMAX_DELAY);
    if (s->pair_handlers_active > 0) --s->pair_handlers_active;
    xSemaphoreGive(s->lock);
}

static esp_err_t pair_page_get(httpd_req_t* req) {
    if (!host_header_allowed(req)) {
        httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Forbidden host");
        return ESP_OK;
    }
    pair_handler_enter();
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(
        req, "Content-Security-Policy",
        "default-src 'self'; style-src 'unsafe-inline'; script-src 'unsafe-inline'");
    httpd_resp_send(
        req, (const char*)_binary_cubox_html_start,
        (ssize_t)(_binary_cubox_html_end - _binary_cubox_html_start));
    pair_handler_leave();
    return ESP_OK;
}

static esp_err_t send_json_reply(httpd_req_t* req, const char* status, bool ok, const char* msg) {
    cJSON* root = cJSON_CreateObject();
    if (!root) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
        return ESP_OK;
    }
    cJSON_AddBoolToObject(root, "ok", ok);
    cJSON_AddStringToObject(root, "message", msg ? msg : "");
    char buf[256];
    bool printed = cJSON_PrintPreallocated(root, buf, sizeof(buf), false) != 0;
    cJSON_Delete(root);
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_sendstr(req, printed ? buf : "{\"ok\":false,\"message\":\"Error\"}");
    return ESP_OK;
}

static esp_err_t pair_submit_post(httpd_req_t* req) {
    if (!host_header_allowed(req)) {
        return send_json_reply(req, "403 Forbidden", false, "非法主机头请求");
    }
    pair_handler_enter();
    esp_err_t ret = ESP_OK;
    cJSON* json = NULL;

    if (req->content_len <= 0 || req->content_len > 1536) {
        ret = send_json_reply(req, "400 Bad Request", false, "请求内容长度无效");
        goto done;
    }

    char* body = calloc(1, (size_t)req->content_len + 1);
    if (!body) {
        ret = send_json_reply(req, "500 Internal Server Error", false, "内存不足");
        goto done;
    }

    size_t got = 0;
    while (got < (size_t)req->content_len) {
        int n = httpd_req_recv(req, body + got, (size_t)req->content_len - got);
        if (n <= 0) {
            free(body);
            ret = send_json_reply(req, "400 Bad Request", false, "接收请求失败");
            goto done;
        }
        got += (size_t)n;
    }
    body[got] = 0;

    json = cJSON_Parse(body);
    cubox_wipe(body, got);
    free(body);
    if (!json) {
        ret = send_json_reply(req, "400 Bad Request", false, "JSON 格式无效");
        goto done;
    }

    const cJSON* jcode = cJSON_GetObjectItemCaseSensitive(json, "code");
    const cJSON* jserver = cJSON_GetObjectItemCaseSensitive(json, "server");
    const cJSON* jcred = cJSON_GetObjectItemCaseSensitive(json, "credential");
    const char* code = cJSON_IsString(jcode) ? jcode->valuestring : "";
    const char* server_hint = cJSON_IsString(jserver) ? jserver->valuestring : "cubox.pro";
    const char* cred = cJSON_IsString(jcred) ? jcred->valuestring : "";

    xSemaphoreTake(s->lock, portMAX_DELAY);
    bool active = s->status.pairing && esp_timer_get_time() < s->pair_deadline_us;
    bool locked_out = s->pair_failures >= CUBOX_PAIR_MAX_FAILS;
    bool code_ok = active && !locked_out && strlen(code) == 8 && !strcmp(code, s->status.code);
    if (!code_ok && active && !locked_out) {
        ++s->pair_failures;
        if (s->pair_failures >= CUBOX_PAIR_MAX_FAILS) {
            s->status.pairing = false;
            s->pair_stop_after_us = esp_timer_get_time() + 250000LL;
            strlcpy(
                s->status.message, "配对码错误次数过多，请刷新配对码",
                sizeof(s->status.message));
            ++s->status.serial;
        }
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

    cubox_config_t cand = {0};
    bool valid = thingsos_cubox_parse_credentials(
        cred, server_hint, cand.server, sizeof(cand.server), cand.token, sizeof(cand.token));
    cJSON_Delete(json);
    json = NULL;

    if (!valid) {
        cubox_wipe(&cand, sizeof(cand));
        ret = send_json_reply(
            req, "400 Bad Request", false,
            "无法识别 Cubox API 链接或 Token，请核对后重新提交");
        goto done;
    }

    xSemaphoreTake(s->lock, portMAX_DELAY);
    reap_worker_locked();
    if (s->status.busy || s->task || s->pending_verify) {
        xSemaphoreGive(s->lock);
        cubox_wipe(&cand, sizeof(cand));
        ret = send_json_reply(req, "409 Conflict", false, "设备正在处理另一项任务，请稍后重试");
        goto done;
    }
    s->pending = cand;
    s->pending_verify = true;
    s->status.pairing = false;
    s->pair_stop_after_us = esp_timer_get_time() + 150000LL;
    s->status.config_phase = THINGSOS_CUBOX_CONFIG_VERIFYING;
    strlcpy(s->status.message, "正在向 Cubox 验证凭证…", sizeof(s->status.message));
    ++s->status.serial;
    xSemaphoreGive(s->lock);
    cubox_wipe(&cand, sizeof(cand));

    ret = send_json_reply(req, "200 OK", true, "已接收凭证，正在验证 Cubox API…");
done:
    if (json) cJSON_Delete(json);
    pair_handler_leave();
    return ret;
}

static void stop_pair_server(void) {
    if (!s || !s->server) return;
    httpd_stop(s->server);
    s->server = NULL;
}

static void new_pair_code_locked(void) {
    uint32_t v = esp_random() % 100000000u;
    snprintf(s->status.code, sizeof(s->status.code), "%08lu", (unsigned long)v);
    s->pair_deadline_us = esp_timer_get_time() + CUBOX_PAIR_TTL_US;
    s->pair_failures = 0;
    s->status.pair_seconds = (unsigned)(CUBOX_PAIR_TTL_US / 1000000LL);
}

static void swap_slot_locked(void) {
    cubox_slot_t cur = {
        .filter = s->status.filter,
        .card_count = s->status.card_count,
        .page = s->status.page,
        .has_more = s->status.has_more,
        .offline_mode = s->status.offline_mode,
    };
    strlcpy(cur.folder_id, s->status.folder_id, sizeof(cur.folder_id));
    strlcpy(cur.folder_name, s->status.folder_name, sizeof(cur.folder_name));
    strlcpy(cur.query, s->status.query, sizeof(cur.query));
    s->status.filter = s->alt.filter;
    s->status.card_count = s->alt.card_count;
    s->status.page = s->alt.page;
    s->status.has_more = s->alt.has_more;
    s->status.offline_mode = s->alt.offline_mode;
    strlcpy(s->status.folder_id, s->alt.folder_id, sizeof(s->status.folder_id));
    strlcpy(s->status.folder_name, s->alt.folder_name, sizeof(s->status.folder_name));
    strlcpy(s->status.query, s->alt.query, sizeof(s->status.query));
    if(s->cards_alt) {
        thingsos_cubox_card_t* previous=s->cards;
        s->cards=s->cards_alt;
        s->cards_alt=previous;
    } else {
        /* Keep the controller usable with one PSRAM list. Reload an evicted tab. */
        s->status.card_count=cur.card_count=0;
        s->status.page=cur.page=0;
        s->status.has_more=cur.has_more=false;
    }
    s->alt=cur;
    s->status.slot ^= 1u;
    ++s->status.serial;
}

bool thingsos_cubox_select_slot(unsigned slot) {
    if (!s || slot > 1u) return false;
    xSemaphoreTake(s->lock, portMAX_DELAY);
    bool ok = true;
    if (s->status.slot != slot) {
        /* A running list job writes into the active slot; never swap under it. */
        if (s->status.busy) {
            ok = false;
        } else {
            size_t bytes=CUBOX_CARDS_MAX*sizeof(*s->cards);
            if(!s->cards_alt && heap_caps_get_free_size(MALLOC_CAP_SPIRAM)>=bytes+512u*1024u)
                s->cards_alt=heap_caps_calloc(CUBOX_CARDS_MAX,sizeof(*s->cards_alt),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
            swap_slot_locked();
        }
    }
    xSemaphoreGive(s->lock);
    return ok;
}

void thingsos_cubox_init(void) {
    if (s) return;
    s = heap_caps_calloc(1, sizeof(*s), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s) {
        ESP_LOGE(TAG, "PSRAM allocation failed");
        return;
    }
    s->cards=heap_caps_calloc(CUBOX_CARDS_MAX,sizeof(*s->cards),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    s->lock=xSemaphoreCreateMutex();
    if(!s->lock || !s->cards) {
        if(s->lock)vSemaphoreDelete(s->lock);
        free(s->cards);free(s);s=NULL;return;
    }
    strlcpy(s->status.server, "cubox.pro", sizeof(s->status.server));
    strlcpy(s->status.folder_name, "全部文件夹", sizeof(s->status.folder_name));
    s->status.filter=THINGSOS_CUBOX_FILTER_ALL;
    s->alt.filter=THINGSOS_CUBOX_FILTER_ALL;
    strlcpy(s->alt.folder_name,"全部文件夹",sizeof(s->alt.folder_name));
    if (cubox_config_load(&s->config)) {
        s->status.configured = true;
        s->status.config_phase = THINGSOS_CUBOX_CONFIG_READY;
        strlcpy(s->status.server, s->config.server, sizeof(s->status.server));
    }
    ++s->status.serial;
}

void thingsos_cubox_status(thingsos_cubox_status_t* out) {
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

bool thingsos_cubox_active(void) {
    if (!s) return false;
    xSemaphoreTake(s->lock, portMAX_DELAY);
    bool active = s->status.busy || s->task != NULL;
    xSemaphoreGive(s->lock);
    return active;
}

bool thingsos_cubox_pairing(void) {
    if (!s) return false;
    xSemaphoreTake(s->lock, portMAX_DELAY);
    bool p = s->status.pairing;
    xSemaphoreGive(s->lock);
    return p;
}

bool thingsos_cubox_pair(bool enabled) {
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
    if (s->server) stop_pair_server();

    xSemaphoreTake(s->lock, portMAX_DELAY);
    s->pending_verify = false;
    cubox_wipe(&s->pending, sizeof(s->pending));
    xSemaphoreGive(s->lock);

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = CUBOX_PAIR_PORT;
    cfg.ctrl_port = CUBOX_PAIR_CTRL_PORT;
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
                ? "Cubox 配对内存不足，请退出阅读或传输后重试"
                : "无法启动手机配对服务");
        return false;
    }

    httpd_uri_t get_root = {.uri = "/", .method = HTTP_GET, .handler = pair_page_get};
    httpd_uri_t post_pair = {.uri = "/api/cubox/pair", .method = HTTP_POST, .handler = pair_submit_post};
    httpd_register_uri_handler(s->server, &get_root);
    httpd_register_uri_handler(s->server, &post_pair);

    xSemaphoreTake(s->lock, portMAX_DELAY);
    s->status.pairing = true;
    s->pair_stop_after_us = 0;
    s->status.config_phase = THINGSOS_CUBOX_CONFIG_IDLE;
    new_pair_code_locked();
    strlcpy(s->status.message, "请使用手机扫码输入 Cubox API 链接", sizeof(s->status.message));
    ++s->status.serial;
    xSemaphoreGive(s->lock);
    return true;
}

void thingsos_cubox_pair_poll(void) {
    if (!s) return;

    int64_t now_us = esp_timer_get_time();
    bool connected = thingsos_network_connected();
    xSemaphoreTake(s->lock, portMAX_DELAY);
    reap_worker_locked();
    bool pairing = s->status.pairing;
    bool expired = pairing && (now_us >= s->pair_deadline_us || !connected);
    if (expired) {
        s->status.pairing = false;
        s->status.pair_seconds = 0;
        s->pair_stop_after_us = 0;
        strlcpy(
            s->status.message,
            connected ? "配对二维码已过期，请刷新" : "WiFi 已断开，配对服务已停止",
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
        !launch_job(JOB_VERIFY, THINGSOS_CUBOX_OP_FOLDERS, "", -1, -1, "正在向 Cubox 验证凭证…")) {
        xSemaphoreTake(s->lock, portMAX_DELAY);
        s->status.config_phase = THINGSOS_CUBOX_CONFIG_FAILED;
        ++s->status.serial;
        xSemaphoreGive(s->lock);
        cubox_wipe(&s->pending, sizeof(s->pending));
    }
}

bool thingsos_cubox_quiesce_locked(void) {
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
    cubox_wipe(&s->pending, sizeof(s->pending));
    xSemaphoreGive(s->lock);
    return idle;
}

bool thingsos_cubox_disconnect(void) {
    if (!s) return false;
    if (!thingsos_cubox_quiesce_locked()) return false;
    cubox_config_clear();
    xSemaphoreTake(s->lock, portMAX_DELAY);
    cubox_wipe(&s->config, sizeof(s->config));
    s->status.configured = false;
    s->status.config_phase = THINGSOS_CUBOX_CONFIG_IDLE;
    s->status.folder_count = 0;
    memset(&s->alt,0,sizeof(s->alt));
    s->status.card_count = 0;
    s->status.has_more = false;
    s->detail_valid = false;
    strlcpy(s->status.message, "已断开 Cubox 连接", sizeof(s->status.message));
    ++s->status.serial;
    xSemaphoreGive(s->lock);
    return true;
}

bool thingsos_cubox_folder(unsigned index, thingsos_cubox_folder_t* out) {
    if (!s || !out) return false;
    xSemaphoreTake(s->lock, portMAX_DELAY);
    bool ok = index < s->status.folder_count;
    if (ok) *out = s->folders[index];
    xSemaphoreGive(s->lock);
    return ok;
}

bool thingsos_cubox_card(unsigned index, thingsos_cubox_card_t* out) {
    if (!s || !out) return false;
    xSemaphoreTake(s->lock, portMAX_DELAY);
    bool ok = index < s->status.card_count;
    if (ok) *out = s->cards[index];
    xSemaphoreGive(s->lock);
    return ok;
}

bool thingsos_cubox_article_path(const char* card_id, char* out, size_t cap) {
    if (!out || !cap || !thingsos_cubox_id_valid(card_id)) return false;
    if (!card_article_path_raw(card_id, out, cap)) return false;
    return sd_file_size(out, NULL);
}

bool thingsos_cubox_detail_copy(const char* card_id, thingsos_cubox_detail_t* out) {
    if (!s || !out || !thingsos_cubox_id_valid(card_id)) return false;
    xSemaphoreTake(s->lock, portMAX_DELAY);
    if (s->detail_valid && !strcmp(s->detail.card.id, card_id)) {
        *out = s->detail;
        xSemaphoreGive(s->lock);
        return true;
    }
    xSemaphoreGive(s->lock);

    if (load_detail_blob_locked(card_id, out)) {
        annotate_card_cache_locked(&out->card);
        return true;
    }

    xSemaphoreTake(s->lock, portMAX_DELAY);
    bool found = false;
    for (unsigned i = 0; i < s->status.card_count; ++i) {
        if (!strcmp(s->cards[i].id, card_id)) {
            memset(out, 0, sizeof(*out));
            out->card = s->cards[i];
            found = true;
            break;
        }
    }
    xSemaphoreGive(s->lock);
    return found;
}

bool thingsos_cubox_set_filter(
    thingsos_cubox_filter_t filter,
    const char* folder_id,
    const char* folder_name,
    const char* query,
    bool fetch_online) {
    if (!s) return false;
    if (folder_id && folder_id[0] && !thingsos_cubox_id_valid(folder_id)) return false;

    xSemaphoreTake(s->lock, portMAX_DELAY);
    if(s->status.slot==1 && strcmp(s->status.folder_id,folder_id?folder_id:"")) {
        s->status.card_count=0;
        s->status.page=0;
        s->status.has_more=false;
    }
    s->status.filter = filter;
    strlcpy(s->status.folder_id, folder_id ? folder_id : "", sizeof(s->status.folder_id));
    strlcpy(
        s->status.folder_name,
        (folder_id && folder_id[0] && folder_name && folder_name[0]) ? folder_name : "全部文件夹",
        sizeof(s->status.folder_name));
    strlcpy(s->status.query, query ? query : "", sizeof(s->status.query));
    ++s->status.serial;
    xSemaphoreGive(s->lock);

    if (fetch_online && thingsos_network_connected() && s->status.configured) {
        return thingsos_cubox_refresh_list(false);
    }
    return true;
}

bool thingsos_cubox_refresh_folders(void) {
    return launch_job(JOB_FOLDERS, THINGSOS_CUBOX_OP_FOLDERS, "", -1, -1, "正在同步 Cubox 文件夹…");
}

bool thingsos_cubox_refresh_list(bool more) {
    return launch_job(
        more ? JOB_MORE : JOB_LIST,
        more ? THINGSOS_CUBOX_OP_MORE : THINGSOS_CUBOX_OP_LIST,
        "",
        -1,
        -1,
        more ? "正在加载下一页…" : "正在加载 Cubox 收藏…");
}

bool thingsos_cubox_fetch_detail(const char* card_id) {
    return launch_job(JOB_DETAIL, THINGSOS_CUBOX_OP_DETAIL, card_id, -1, -1, "正在获取文章详情…");
}

bool thingsos_cubox_download(const char* card_id) {
    return launch_job(JOB_DOWNLOAD, THINGSOS_CUBOX_OP_DOWNLOAD, card_id, -1, -1, "正在准备下载文章与图片…");
}

bool thingsos_cubox_mark(const char* card_id, int set_read, int set_starred) {
    return launch_job(JOB_UPDATE, THINGSOS_CUBOX_OP_UPDATE, card_id, set_read, set_starred, "正在同步状态…");
}

bool thingsos_cubox_archive(const char* card_id) {
    return launch_job(JOB_ARCHIVE, THINGSOS_CUBOX_OP_ARCHIVE, card_id, -1, -1, "正在归档…");
}

bool thingsos_cubox_delete_cache(const char* card_id) {
    if (!s || !thingsos_cubox_id_valid(card_id)) return false;
    bool ok = remove_card_dir_locked(card_id);
    if (ok) {
        xSemaphoreTake(s->lock, portMAX_DELAY);
        thingsos_cubox_card_t* other=card_for_id(s->cards_alt,s->alt.card_count,card_id);
        if(other) {
            other->cached=false;
            other->cached_bytes=0;
            other->cached_images=0;
        }
        for (unsigned i = 0; i < s->status.card_count; ++i) {
            if (!strcmp(s->cards[i].id, card_id)) {
                s->cards[i].cached = false;
                s->cards[i].cached_bytes = 0;
                s->cards[i].cached_images = 0;
            }
        }
        if (s->detail_valid && !strcmp(s->detail.card.id, card_id)) {
            s->detail.card.cached = false;
            s->detail.card.cached_bytes = 0;
            s->detail.card.cached_images = 0;
        }
        strlcpy(s->status.message, "已清除本地缓存", sizeof(s->status.message));
        ++s->status.serial;
        xSemaphoreGive(s->lock);
    }
    return ok;
}

void thingsos_cubox_load_offline(void) {
    if (!s) return;
    thingsos_cubox_folder_t* fl = heap_caps_calloc(
        THINGSOS_CUBOX_FOLDER_MAX, sizeof(*fl), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    thingsos_cubox_card_t* cl = heap_caps_calloc(
        CUBOX_CARDS_MAX, sizeof(*cl), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!fl || !cl) {
        free(fl);
        free(cl);
        return;
    }

    unsigned fc = 0, cc = 0;
    thingsos_cubox_load_folders(CUBOX_FOLDERS_CACHE, fl, THINGSOS_CUBOX_FOLDER_MAX, &fc);
    if (!thingsos_cubox_load_cards(CUBOX_LIST_CACHE, cl, CUBOX_CARDS_MAX, &cc) || cc == 0) {
        DIR* d = opendir(CUBOX_CARD_ROOT);
        if (d) {
            struct dirent* ent;
            thingsos_cubox_detail_t* tmp = heap_caps_calloc(1, sizeof(*tmp), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            while (tmp && cc < CUBOX_CARDS_MAX && (ent = readdir(d)) != NULL) {
                if (!thingsos_cubox_id_valid(ent->d_name)) continue;
                char md[160];
                uint32_t sz = 0;
                if (!card_article_path_raw(ent->d_name, md, sizeof(md)) || !sd_file_size(md, &sz))
                    continue;
                if (load_detail_blob_locked(ent->d_name, tmp)) {
                    tmp->card.cached = true;
                    tmp->card.cached_bytes = sz;
                    cl[cc++] = tmp->card;
                }
            }
            free(tmp);
            closedir(d);
        }
    } else {
        for (unsigned i = 0; i < cc; ++i) annotate_card_cache_locked(&cl[i]);
    }

    xSemaphoreTake(s->lock, portMAX_DELAY);
    if (fc > 0) {
        memcpy(s->folders, fl, fc * sizeof(*fl));
        s->status.folder_count = fc;
    }
    if(s->status.slot!=0)swap_slot_locked();
    memcpy(s->cards, cl, cc * sizeof(*cl));
    s->status.card_count = cc;
    s->status.has_more = false;
    s->status.offline_mode = true;
    ++s->status.serial;
    xSemaphoreGive(s->lock);

    free(fl);
    free(cl);
}

void thingsos_cubox_cancel(void) {
    if (!s) return;
    s->cancel = true;
}
