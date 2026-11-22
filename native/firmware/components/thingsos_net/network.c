// SPDX-License-Identifier: Apache-2.0
#include "network.h"
#include "esp_attr.h"
#include "thingsos/mem.h"
#include "single_update.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "board.h"
#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "thingsos/diagnostics.h"
#include "thingsos/memory_trace.h"
#include "thingsos/release.h"
#include <ctype.h>
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_sntp.h"
#include "esp_ota_ops.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "esp_wifi.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "psa/crypto.h"
#include "board.h"
#include "thingsos/manage.h"
#include "thingsos/preview.h"
#include "thingsos/runtime.h"
#include "thingsos/update.h"
#include "thingsos/service.h"
#include "thingsos/audio.h"
#include "thingsos/weidu.h"
/* Normal app supplies this; rescue has no WeRead dependency. */
extern void thingsos_weidu_state(thingsos_weidu_status_t*) __attribute__((weak));
#include "thingsos/network_defaults.h"
static httpd_handle_t server;
static const char ap_ssid[]=THINGSOS_AP_SSID, ap_password[]=THINGSOS_AP_PASSWORD;
static char token[33];
_Static_assert(sizeof(THINGSOS_AP_PASSWORD)-1==8,"AP password must be 8 bytes");
static bool rescue_mode;
static bool startup_healthy;
void thingsos_network_healthy(void) {startup_healthy=true;}
static volatile bool busy;
static volatile bool external_busy;
static bool sntp_started;
static int64_t time_fallback_at;
static const char* time_source_pending;
static void time_from_http(void* arg);
static void time_source_save(const char* source, time_t stamp);
static bool time_enabled = true, service_enabled;
static bool network_ready;
/* The radio is started on demand: booting it while no saved AP is in range made
   the driver retry at ~45 Hz for hours. */
static bool radio_up;
static bool radio_allowed=true;
extern bool thingsos_audio_active(void) __attribute__((weak));
extern void thingsos_audio_status(thingsos_audio_status_t*) __attribute__((weak));
static volatile bool upload_cancel_requested;
static bool wifi_operation_busy(void){return thingsos_network_wifi_busy();}
static thingsos_service_t http_service;
static struct {unsigned sequence,internal_free,largest,dma_free,psram_free;} service_idle;
static char library_path[512] = "/sdcard/books";
static portMUX_TYPE state_guard = portMUX_INITIALIZER_UNLOCKED;
static bool scan_active, scan_ready;
static esp_err_t scan_status;
static int64_t scan_started;
static thingsos_manage_job_t* manage_job;
static unsigned manage_sequence, manage_result_id, manage_done, manage_failed;
static bool manage_claimed;
static uint64_t management_card;
void thingsos_network_card(uint64_t id) {
    portENTER_CRITICAL(&state_guard);
    management_card = id;
    portEXIT_CRITICAL(&state_guard);
}
static thingsos_transfer_state_t transfer;
static char saved_ssid[33], saved_password[64], wifi_failure[96];
static volatile bool reconnect_enabled, user_disconnected;
static volatile bool wifi_suspended;
static volatile bool credentials_pending;
#include "wifi_join.inc"
#include "wifi_tx_power.inc"
static wifi_join_t station_join;
/* Protect driver commands against an HTTP request replacing a connection
   while maintenance is about to retry or cancel it. Events only change state. */
static SemaphoreHandle_t wifi_control;
static bool wifi_station_has_address(void) {
    wifi_ap_record_t record;
    esp_netif_t* sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t info;
    return esp_wifi_sta_get_ap_info(&record) == ESP_OK && sta &&
        esp_netif_get_ip_info(sta, &info) == ESP_OK && info.ip.addr;
}
static void wifi_station_cancel(void) {
    portENTER_CRITICAL(&state_guard);
    station_join = (wifi_join_t){0};
    credentials_pending = false;
    portEXIT_CRITICAL(&state_guard);
}
static void wifi_station_request(void) {
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&state_guard);
    wifi_join_begin(&station_join, now);
    portEXIT_CRITICAL(&state_guard);
    wifi_failure[0] = 0;
}
bool thingsos_network_connecting(void) {
    portENTER_CRITICAL(&state_guard);
    bool joining = station_join.deadline != 0;
    portEXIT_CRITICAL(&state_guard);
    return joining;
}
/* Start the radio only when something actually needs it (transfer page, WeRead,
   OTA, NTP, a user connect). Booting it with no AP in range makes the driver
   retry internally every ~22 ms, which pins the Wi-Fi task on core 0 and floods
   the console. */
static esp_err_t wifi_radio_start(void) {
    if(!radio_allowed)return ESP_ERR_INVALID_STATE;
    if (radio_up) return ESP_OK;
    esp_err_t e = esp_wifi_start();
    if (e != ESP_OK && e != ESP_ERR_WIFI_STATE) {
        ESP_LOGW("ThingsOS_network", "WiFi start failed: %s", esp_err_to_name(e));
        return e;
    }
    e = wifi_tx_power_limit();
    if (e != ESP_OK) {
        esp_wifi_stop();
        radio_up = false;
        return e;
    }
    radio_up = true;
    /* The driver retries esp_wifi_connect() internally every ~22 ms until it
       finds a suitable AP and logs one warning per attempt: 110 lines in 1.2 s
       at boot, and an endless stream while the saved AP stays out of range.
       Its level is capped at errors; association results are logged here. */
    esp_log_level_set("wifi", ESP_LOG_ERROR);
    ESP_LOGI("ThingsOS_network", "WiFi radio started");
    return ESP_OK;
}
#include "network_sleep.inc"
static volatile bool pause_requested, pause_complete, pause_ok;
static void transfer_set(
    const char* name, const char* message, unsigned percent, bool active, bool failed, bool ota) {
    uint64_t at=(uint64_t)esp_timer_get_time()/1000;
    portENTER_CRITICAL(&state_guard);
    if(active)transfer.finished_ms=0;
    else if(transfer.active)transfer.finished_ms=at;
    if (name) strlcpy(transfer.name, name, sizeof(transfer.name));
    if (message) strlcpy(transfer.message, message, sizeof(transfer.message));
    transfer.percent = percent;
    transfer.active = active;
    transfer.failed = failed;
    transfer.ota = ota;
    ++transfer.serial;
    portEXIT_CRITICAL(&state_guard);
}
void thingsos_network_transfer(thingsos_transfer_state_t* out) {
    portENTER_CRITICAL(&state_guard);
    *out = transfer;
    portEXIT_CRITICAL(&state_guard);
}
bool thingsos_network_job(thingsos_manage_job_t* out) {
    portENTER_CRITICAL(&state_guard);
    bool ok = manage_job && !manage_claimed;
    if (ok) {
        *out = *manage_job;
        manage_claimed = true;
    }
    portEXIT_CRITICAL(&state_guard);
    return ok;
}
void thingsos_network_job_done(unsigned job, unsigned done, unsigned failed) {
    portENTER_CRITICAL(&state_guard);
    thingsos_manage_job_t* old = manage_job;
    manage_job = NULL;
    manage_claimed = false;
    manage_result_id = job;
    manage_done = done;
    manage_failed = failed;
    portEXIT_CRITICAL(&state_guard);
    free(old);
}
const char* thingsos_network_failure(void) {
    return wifi_failure;
}
static bool lock(void);
static void unlock(void);
typedef struct {char ssid[33],password[64];} wifi_profile_t;
#define WIFI_SAVED_LIMIT 8
static bool profile_read(const char* ssid,char password[64]) {
    nvs_handle_t h;if(!ssid || nvs_open("thingsos-wifi",NVS_READONLY,&h)!=ESP_OK)return false;
    wifi_profile_t profiles[WIFI_SAVED_LIMIT]={0};size_t bytes=sizeof(profiles);
    bool found=false;
    if(nvs_get_blob(h,"profiles",profiles,&bytes)==ESP_OK && bytes==sizeof(profiles))
        for(unsigned i=0;i<WIFI_SAVED_LIMIT;++i)
            if(memchr(profiles[i].ssid,0,33) && memchr(profiles[i].password,0,64) && !strcmp(ssid,profiles[i].ssid)) {
                if(password)memcpy(password,profiles[i].password,64);
                found=true;break;
            }
    /* Migrate the previously supported single saved station on first use. */
    if(!found) {
        wifi_profile_t station={0};bytes=sizeof(station);
        if(nvs_get_blob(h,"station",&station,&bytes)==ESP_OK && bytes==sizeof(station) &&
           memchr(station.ssid,0,33) && memchr(station.password,0,64) && !strcmp(ssid,station.ssid)) {
            if(password)memcpy(password,station.password,64);
            found=true;
        }
        memset(&station,0,sizeof(station));
    }
    nvs_close(h);memset(profiles,0,sizeof(profiles));return found;
}
bool thingsos_network_saved(const char* ssid) {return profile_read(ssid,NULL);}
esp_err_t thingsos_network_reconnect(const char* ssid) {
    char password[64]={0};if(!profile_read(ssid,password))return ESP_ERR_NOT_FOUND;
    esp_err_t e=thingsos_network_connect(ssid,password);memset(password,0,sizeof(password));return e;
}
static bool credentials(bool erase) {
    if (rescue_mode) return true;
    nvs_handle_t h;
    if (nvs_open("thingsos-wifi", NVS_READWRITE, &h) != ESP_OK) return false;
    wifi_profile_t profiles[WIFI_SAVED_LIMIT]={0};size_t bytes=sizeof(profiles);
    if(nvs_get_blob(h,"profiles",profiles,&bytes)!=ESP_OK || bytes!=sizeof(profiles))memset(profiles,0,sizeof(profiles));
    if(!profiles[0].ssid[0]) {
        bytes=sizeof(profiles[0]);
        if(nvs_get_blob(h,"station",&profiles[0],&bytes)!=ESP_OK || bytes!=sizeof(profiles[0]))memset(&profiles[0],0,sizeof(profiles[0]));
    }
    wifi_profile_t next[WIFI_SAVED_LIMIT]={0};unsigned count=0;
    if(!erase){strlcpy(next[0].ssid,saved_ssid,33);strlcpy(next[0].password,saved_password,64);count=1;}
    for(unsigned i=0;i<WIFI_SAVED_LIMIT && count<WIFI_SAVED_LIMIT;++i)
        if(profiles[i].ssid[0] && memchr(profiles[i].ssid,0,33) && memchr(profiles[i].password,0,64) && strcmp(profiles[i].ssid,saved_ssid))next[count++]=profiles[i];
    esp_err_t result;
    if (erase) {
        result = nvs_erase_key(h, "station");
        if (result == ESP_ERR_NVS_NOT_FOUND) result = ESP_OK;
    } else {
        struct {
            char ssid[33], password[64];
        } station = { 0 };
        strlcpy(station.ssid, saved_ssid, sizeof(station.ssid));
        strlcpy(station.password, saved_password, sizeof(station.password));
        result = nvs_set_blob(h, "station", &station, sizeof(station));
        memset(&station, 0, sizeof(station));
    }
    if (result == ESP_OK) result = nvs_set_blob(h,"profiles",next,sizeof(next));
    if (result == ESP_OK) result = nvs_commit(h);
    memset(profiles,0,sizeof(profiles));memset(next,0,sizeof(next));
    nvs_close(h);
    return result == ESP_OK;
}
const char* thingsos_network_saved_name(void) {
    return saved_ssid;
}
static void wifi_station_poll(int64_t now) {
    if (!wifi_control || xSemaphoreTake(wifi_control, 0) != pdTRUE) return;
    portENTER_CRITICAL(&state_guard);
    enum wifi_join_action action = radio_allowed && !wifi_suspended && reconnect_enabled && !user_disconnected
        ? wifi_join_step(&station_join, now, scan_active && !scan_ready) : WIFI_JOIN_WAIT;
    wifi_join_t state = station_join;
    portEXIT_CRITICAL(&state_guard);
    if (action == WIFI_JOIN_TRY) {
        esp_err_t error = esp_wifi_connect();
        thingsos_diag_event(DIAG_NET_WIFI,0,state.attempts,state.reason,error,0);
        ESP_LOGI("ThingsOS_network", "WiFi connect attempt %u: %s", state.attempts, esp_err_to_name(error));
        if (error != ESP_OK) {
            portENTER_CRITICAL(&state_guard);
            wifi_join_disconnected(&station_join, now, state.reason);
            portEXIT_CRITICAL(&state_guard);
        }
    } else if (action == WIFI_JOIN_END) {
        thingsos_diag_event(DIAG_NET_WIFI,5,state.attempts,state.associated,ESP_ERR_TIMEOUT,(int)state.reason);
        snprintf(wifi_failure, sizeof(wifi_failure), "%s", state.associated ? "已连接路由器，但未获取 IP 地址" : "WiFi 未连接 · 请检查网络和密码");
        ESP_LOGW("ThingsOS_network", "WiFi connect ended: attempts=%u reason=%u waiting_for_ip=%d",
                 state.attempts, state.reason, state.associated);
        esp_wifi_disconnect();
    }
    xSemaphoreGive(wifi_control);
}
void thingsos_network_maintenance(void) {
    static int64_t retry_after;
    int64_t current = esp_timer_get_time();
    if (time_source_pending) {
        const char* source = time_source_pending;
        time_source_pending = NULL;
        time_source_save(source, time(NULL));
    }
    /* 未校时期间一直重试：现场网络是间歇性可用的，只在开机后试几次会错过窗口。 */
    if (time_fallback_at && time_enabled && time(NULL) < 1577836800 && current >= time_fallback_at) {
        time_fallback_at = current + 30000000;
        xTaskCreate(time_from_http, "time_http", 6144, NULL, 2, NULL);
    }
    wifi_station_poll(current);
    if (credentials_pending && current >= retry_after && !rescue_mode && wifi_control &&
        xSemaphoreTake(wifi_control, 0) == pdTRUE) {
        if (credentials_pending && thingsos_network_connected()) {
            retry_after = current + 5000000;
            credentials_pending = !credentials(false);
        }
        xSemaphoreGive(wifi_control);
    }
}
static void network_dns(void) {
    esp_netif_t* sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!sta) return;
    /* Keep DHCP-provided resolvers. Populate only empty backup slots so a
       router DNS failure does not make every HTTPS request fail immediately. */
    static const char* const backups[] = {"223.5.5.5", "223.6.6.6"};
    for (unsigned i = 0; i < 2; ++i) {
        esp_netif_dns_info_t dns = {0};
        esp_netif_dns_type_t type = i ? ESP_NETIF_DNS_FALLBACK : ESP_NETIF_DNS_BACKUP;
        if (esp_netif_get_dns_info(sta, type, &dns) == ESP_OK && ESP_IP_IS_ANY(dns.ip)) {
            dns.ip.type = ESP_IPADDR_TYPE_V4;
            if (esp_netif_str_to_ip4(backups[i], &dns.ip.u_addr.ip4) == ESP_OK)
                esp_netif_set_dns_info(sta, type, &dns);
        }
    }
    for (unsigned i = 0; i < 3; ++i) {
        esp_netif_dns_info_t dns = {0};
        if (esp_netif_get_dns_info(sta, (esp_netif_dns_type_t)i, &dns) == ESP_OK &&
            dns.ip.type == ESP_IPADDR_TYPE_V4)
            ESP_LOGI("ThingsOS_network", "DNS slot=%u server=" IPSTR, i,
                     IP2STR(&dns.ip.u_addr.ip4));
    }
}
static unsigned link_losses;
unsigned thingsos_network_link_losses(void) {
    return __atomic_load_n(&link_losses,__ATOMIC_RELAXED);
}
static void wifi_event(void* arg, esp_event_base_t base, int32_t event, void* data) {
    (void)arg;
    if(base==WIFI_EVENT && event==WIFI_EVENT_SCAN_DONE) {
        const wifi_event_sta_scan_done_t* result=data;
        portENTER_CRITICAL(&state_guard);
        if(scan_active && !scan_ready){scan_status=result && !result->status?ESP_OK:ESP_FAIL;scan_ready=true;}
        portEXIT_CRITICAL(&state_guard);
        return;
    }
    if (base == WIFI_EVENT && event == WIFI_EVENT_STA_CONNECTED) {
        portENTER_CRITICAL(&state_guard);
        if (station_join.deadline && !station_join.retry) station_join.associated = true;
        portEXIT_CRITICAL(&state_guard);
        thingsos_diag_event(DIAG_NET_WIFI,1,0,0,0,0);
        ESP_LOGI("ThingsOS_network", "WiFi STA associated; waiting for IP");
    } else if (base == IP_EVENT && event == IP_EVENT_STA_GOT_IP) {
        portENTER_CRITICAL(&state_guard);
        bool accepted = reconnect_enabled && !user_disconnected && !wifi_suspended &&
                        radio_allowed && wifi_join_got_ip(&station_join);
        if (accepted) credentials_pending = true;
        portEXIT_CRITICAL(&state_guard);
        thingsos_diag_event(DIAG_NET_WIFI,2,accepted,0,0,0);
        if (!rescue_mode) thingsos_memory_trace("wifi-connected",0,accepted);
        if (accepted) network_dns();
    } else if (base == WIFI_EVENT && event == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t* info = data;
        unsigned reason = info ? info->reason : 0;
        int64_t now = esp_timer_get_time();
        portENTER_CRITICAL(&state_guard);
        /* A queued local disconnect from replacing a profile may arrive
           after the next request has already been queued. */
        if (reconnect_enabled && !user_disconnected && !wifi_suspended &&
            (reason != WIFI_REASON_ASSOC_LEAVE || station_join.has_ip))
            wifi_join_disconnected(&station_join, now, reason);
        portEXIT_CRITICAL(&state_guard);
        __atomic_add_fetch(&link_losses,1u,__ATOMIC_RELAXED);
        thingsos_diag_event(DIAG_NET_WIFI,3,reason,0,0,0);
        if (!rescue_mode) thingsos_memory_trace("wifi-disconnected",0,(int)reason);
        ESP_LOGI("ThingsOS_network", "WiFi STA disconnected: reason=%u", reason);
    }
}
/* 校时保底：现场网络会丢弃 UDP/123（0.1.2 报告：NTP 请求已发出但收不到回包），
   NTP 失败后改用 HTTP 响应的 Date 头设置系统时钟。明文 HTTP 只读取时间。 */
static void time_source_save(const char* source, time_t stamp) {
    nvs_handle_t handle;
    if (nvs_open("thingsos-net", NVS_READWRITE, &handle) != ESP_OK) return;
    nvs_set_str(handle, "time-src", source);
    nvs_set_u32(handle, "time-at", (uint32_t)stamp);
    nvs_commit(handle);
    nvs_close(handle);
}
static void time_source_report(void) {
    nvs_handle_t handle;
    char source[16] = { 0 };
    size_t length = sizeof(source);
    uint32_t stamp = 0;
    if (nvs_open("thingsos-net", NVS_READONLY, &handle) != ESP_OK) return;
    if (nvs_get_str(handle, "time-src", source, &length) == ESP_OK &&
        nvs_get_u32(handle, "time-at", &stamp) == ESP_OK)
        ESP_LOGI("ThingsOS_network", "Last time source: %s at %lu", source, (unsigned long)stamp);
    nvs_close(handle);
}
static void time_synced(struct timeval* value) {
    ESP_LOGI("ThingsOS_network", "Time synced by NTP: %lld", (long long)value->tv_sec);
    time_source_pending = "ntp";
}
static esp_err_t http_date_event(esp_http_client_event_t* evt) {
    if (evt->event_id != HTTP_EVENT_ON_HEADER || !evt->header_key || !evt->header_value ||
        !evt->user_data || strcasecmp(evt->header_key, "Date")) return ESP_OK;
    static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    char month[4] = { 0 };
    int day = 0, year = 0, hour = 0, minute = 0, second = 0;
    if (sscanf(evt->header_value, "%*3s, %d %3s %d %d:%d:%d", &day, month, &year, &hour,
               &minute, &second) != 6) return ESP_OK;
    const char* found = strstr(months, month);
    if (!found || (found - months) % 3) return ESP_OK;
    int index = (int)(found - months) / 3;
    int y = year - (index <= 1);
    int era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (unsigned)((153 * (index > 1 ? index - 2 : index + 10) + 2) / 5 + day - 1);
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    time_t stamp = (time_t)((long)era * 146097 + (long)doe - 719468) * 86400 +
                   hour * 3600 + minute * 60 + second;
    if (stamp >= 1577836800 && stamp <= 4102444799) *(time_t*)evt->user_data = stamp;
    return ESP_OK;
}
static void time_from_http(void* arg) {
    (void)arg;
    static const char* const urls[] = { "http://www.baidu.com/", "http://www.aliyun.com/" };
    time_t stamp = 0;
    for (unsigned i = 0; i < sizeof(urls) / sizeof(urls[0]) && !stamp; ++i) {
        esp_http_client_config_t config = { .url = urls[i], .timeout_ms = 5000,
                                            .event_handler = http_date_event,
                                            .user_data = &stamp };
        esp_http_client_handle_t client = esp_http_client_init(&config);
        if (!client) continue;
        if (esp_http_client_open(client, 0) == ESP_OK) {
            esp_http_client_fetch_headers(client);
            esp_http_client_close(client);
        }
        esp_http_client_cleanup(client);
    }
    if (stamp) {
        struct timeval value = { .tv_sec = stamp };
        settimeofday(&value, NULL);
        ESP_LOGI("ThingsOS_network", "Time set from HTTP Date: %lld", (long long)stamp);
        time_source_pending = "http";
    } else {
        ESP_LOGW("ThingsOS_network", "No time: NTP silent and no HTTP Date received");
    }
    vTaskDelete(NULL);
}
/* Cold link: the first outbound TCP connection after association can be refused
   for up to ~90 s, so the release host is warmed in the background as soon as
   the interface has an address. */
static void network_warmup_event(void* arg, esp_event_base_t base, int32_t event, void* data) {
    (void)arg; (void)base; (void)event; (void)data;
    thingsos_network_warmup_start();
}
static void network_time(void* arg, esp_event_base_t base, int32_t event, void* data) {
    if (sntp_started || rescue_mode || !time_enabled) return;
    /* Aliyun 的固定地址先走，避免 DNS 依赖；随后是国内 NTP 池。 */
    static const char* const servers[] = { "203.107.6.88", "ntp.aliyun.com", "cn.pool.ntp.org" };
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(servers[0]);
    config.sync_cb = time_synced;
    /* 槽位数由 sdkconfig 决定（主固件 3、救援固件 1），按实际容量截断。 */
    size_t count = sizeof(servers) / sizeof(servers[0]);
    size_t capacity = sizeof(config.servers) / sizeof(config.servers[0]);
    if (count > capacity) count = capacity;
    config.num_of_servers = (unsigned)count;
    for (size_t i = 0; i < count; ++i) config.servers[i] = servers[i];
    esp_err_t error = esp_netif_sntp_init(&config);
    if (error != ESP_OK) {
        ESP_LOGW("ThingsOS_network", "SNTP start failed: %s", esp_err_to_name(error));
        return;
    }
    sntp_started = true;
    time_fallback_at = esp_timer_get_time() + 10000000;
    time_source_report();
}
SemaphoreHandle_t thingsos_io_lock;
extern const unsigned char web_start[] asm("_binary_web_html_start");
extern const unsigned char web_end[] asm("_binary_web_html_end");
extern const unsigned char sha_start[] asm("_binary_web_sha256_js_start");
extern const unsigned char sha_end[] asm("_binary_web_sha256_js_end");
static esp_err_t sha_script(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/javascript; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, (const char*)sha_start, sha_end - sha_start - 1);
}
extern const unsigned char image_js_start[] asm("_binary_web_image_js_start");
extern const unsigned char image_js_end[] asm("_binary_web_image_js_end");
static esp_err_t image_script(httpd_req_t* req) {
    httpd_resp_set_type(req,"text/javascript; charset=utf-8");
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    return httpd_resp_send(req,(const char*)image_js_start,image_js_end-image_js_start-1);
}
const char* thingsos_network_ssid(void) {
    return ap_ssid;
}
const char* thingsos_network_password(void) {
    return ap_password;
}
const char* thingsos_network_token(void) {
    return token;
}
bool thingsos_network_busy(void) {
    return busy || external_busy;
}
/* An audio session (playing, paused or recording) claims the network only to
   exclude uploads/OTA/USB while a file is open. It is not a transfer, so WiFi
   changes must not be refused as "transfer in progress": the network layer's
   own WiFi operations already use this narrower predicate. */
bool thingsos_network_wifi_busy(void) {
    return busy || (external_busy && !(thingsos_audio_active && thingsos_audio_active()));
}
static bool claim_transfer(void) {
    portENTER_CRITICAL(&state_guard);
    bool ok=!busy && !external_busy && !manage_job;
    if(ok)busy=true;
    portEXIT_CRITICAL(&state_guard);return ok;
}
bool thingsos_network_external_begin(void) {
    portENTER_CRITICAL(&state_guard);
    bool ok=!busy && !external_busy && !manage_job && !rescue_mode;
    if(ok) external_busy=true;
    portEXIT_CRITICAL(&state_guard);
    return ok;
}
void thingsos_network_external_end(void) {
    portENTER_CRITICAL(&state_guard);external_busy=false;portEXIT_CRITICAL(&state_guard);
}
static bool authorized(httpd_req_t* req) {
    /* Normal LAN/device-hotspot service is intentionally code-free. The
     * independent rescue environment retains its device-code protection. */
    if (!rescue_mode) return true;
    char supplied[40] = { 0 };
    if (httpd_req_get_hdr_value_str(req, "X-ThingsOS-Token", supplied, sizeof(supplied)) != ESP_OK)
        return false;
    unsigned mismatch = (unsigned)(strlen(supplied) ^ strlen(token));
    for (size_t i = 0; i < 32; i++)
        mismatch |= (unsigned char)supplied[i] ^ (unsigned char)token[i];
    return mismatch == 0;
}
static esp_err_t reject(httpd_req_t* req, const char* status, const char* message) {
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    return httpd_resp_sendstr(req, message);
}
static bool sd_ready(void) {
    thingsos_sd_info_t sd;
    return !rescue_mode && thingsos_board_sd_info(&sd) == ESP_OK && sd.mounted;
}
static bool lock(void) {
    if(external_busy || !thingsos_io_lock || xSemaphoreTake(thingsos_io_lock,pdMS_TO_TICKS(15000))!=pdTRUE) return false;
    if(external_busy) {xSemaphoreGive(thingsos_io_lock);return false;}
    return true;
}
static void unlock(void) {
    xSemaphoreGive(thingsos_io_lock);
}
static esp_err_t json_response(httpd_req_t* req, cJSON* value) {
    char* s = cJSON_PrintUnformatted(value);
    cJSON_Delete(value);
    if (!s) return reject(req, "500 Internal Server Error", "Out of memory");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t err = httpd_resp_sendstr(req, s);
    free(s);
    return err;
}
static esp_err_t page(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(
        req, "Content-Security-Policy",
        "default-src 'self'; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'; "
        "connect-src 'self'; img-src 'self' blob: data:; frame-ancestors 'none'");
    return httpd_resp_send(req, (const char*)web_start, web_end - web_start - 1);
}
static esp_err_t status(httpd_req_t* req) {
    if (!authorized(req)) return reject(req, "401 Unauthorized", "Enter device token");
    cJSON* root = cJSON_CreateObject();
    if (!root) return ESP_ERR_NO_MEM;
    cJSON_AddStringToObject(root, "system", "ThingsOS");
    cJSON_AddBoolToObject(root, "rescue", rescue_mode);
    cJSON_AddStringToObject(root,"rescue_reason",thingsos_diag_reason_text());
    cJSON_AddBoolToObject(root, "sd", sd_ready());
    cJSON_AddBoolToObject(root, "busy", busy);
    cJSON_AddBoolToObject(root, "service", service_enabled);
    char directory[512], card[17];
    portENTER_CRITICAL(&state_guard);
    strlcpy(directory, library_path, sizeof(directory));
    uint64_t card_id = management_card;
    portEXIT_CRITICAL(&state_guard);
    snprintf(card, sizeof(card), "%016llx", (unsigned long long)card_id);
    cJSON_AddStringToObject(root, "library", directory);
    cJSON_AddStringToObject(root, "card", card);
    cJSON_AddStringToObject(root, "version", esp_app_get_description()->version);
    wifi_ap_record_t station;
    bool online = esp_wifi_sta_get_ap_info(&station) == ESP_OK;
    cJSON_AddBoolToObject(root, "wifi", online);
    const esp_partition_t* running = esp_ota_get_running_partition();
    cJSON_AddStringToObject(root, "partition", running ? running->label : "");
    /* An OTA metadata state can describe an earlier boot. Only this boot's
       display/font/touch health confirmation proves startup completed. */
    bool healthy = !rescue_mode && startup_healthy;
    cJSON_AddStringToObject(root,"layout","thingsos-metalio-single-v1");
    cJSON_AddNumberToObject(root,"app_capacity",THINGSOS_SINGLE_CAPACITY);
    cJSON_AddBoolToObject(root, "healthy", healthy);
    char elf[65];
    for (size_t i = 0; i < 32; ++i)
        snprintf(elf + i * 2, 3, "%02x", esp_app_get_description()->app_elf_sha256[i]);
    cJSON_AddStringToObject(root, "elf", elf);
    portENTER_CRITICAL(&state_guard);
    unsigned job = manage_job ? manage_job->job : manage_result_id, done = manage_done,
             failed = manage_failed;
    bool pending = manage_job != NULL;
    portEXIT_CRITICAL(&state_guard);
    cJSON_AddNumberToObject(root, "job", job);
    cJSON_AddBoolToObject(root, "job_pending", pending);
    cJSON_AddNumberToObject(root, "job_done", done);
    cJSON_AddNumberToObject(root, "job_failed", failed);
    /* Read-only diagnostics: heap pools are dynamic allocation capacity,
       distinct from physical SRAM and ELF section occupancy. */
    cJSON* memory=cJSON_AddObjectToObject(root,"memory");
    static const struct {const char* name;uint32_t caps;} pools[]={
        {"internal",MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT},
        {"psram",MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT},
        {"dma",MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL}};
    for(unsigned i=0;memory && i<3;++i) {
        cJSON* pool=cJSON_AddObjectToObject(memory,pools[i].name);
        if(!pool)continue;
        cJSON_AddNumberToObject(pool,"total",heap_caps_get_total_size(pools[i].caps));
        cJSON_AddNumberToObject(pool,"free",heap_caps_get_free_size(pools[i].caps));
        cJSON_AddNumberToObject(pool,"minimum_free",heap_caps_get_minimum_free_size(pools[i].caps));
        cJSON_AddNumberToObject(pool,"largest_free_block",heap_caps_get_largest_free_block(pools[i].caps));
    }
    unsigned sequence,internal_free,largest,dma_free,psram_free;
    portENTER_CRITICAL(&state_guard);
    sequence=service_idle.sequence;internal_free=service_idle.internal_free;largest=service_idle.largest;
    dma_free=service_idle.dma_free;psram_free=service_idle.psram_free;
    portEXIT_CRITICAL(&state_guard);
    cJSON* idle=memory&&sequence?cJSON_AddObjectToObject(memory,"after_service_stop"):NULL;
    if(idle) {
        cJSON_AddNumberToObject(idle,"sequence",sequence);
        cJSON_AddNumberToObject(idle,"internal_free",internal_free);
        cJSON_AddNumberToObject(idle,"largest_free_block",largest);
        cJSON_AddNumberToObject(idle,"dma_free",dma_free);
        cJSON_AddNumberToObject(idle,"psram_free",psram_free);
    }
    if(thingsos_weidu_state) {
        thingsos_weidu_status_t status;
        thingsos_weidu_state(&status);
        cJSON* wd=cJSON_AddObjectToObject(root,"weidu");
        if(wd) {
            cJSON_AddNumberToObject(wd,"phase",status.phase);
            cJSON_AddBoolToObject(wd,"busy",status.busy);
            cJSON_AddNumberToObject(wd,"completed",status.completed);
            cJSON_AddNumberToObject(wd,"total",status.total);
            /* Error diagnostics only: omit login QR, session, path and title. */
            if(status.phase==WD_ERROR)cJSON_AddStringToObject(wd,"error",status.message);
        }
    }
    cJSON_AddStringToObject(root, "wifi_error", wifi_failure);
    cJSON_AddBoolToObject(root, "development", true);
    if(thingsos_audio_status){thingsos_audio_status_t audio;thingsos_audio_status(&audio);cJSON* a=cJSON_AddObjectToObject(root,"audio");
        if(a){cJSON_AddNumberToObject(a,"state",audio.state);cJSON_AddNumberToObject(a,"position_ms",audio.position_ms);cJSON_AddNumberToObject(a,"duration_ms",audio.duration_ms);cJSON_AddNumberToObject(a,"peak",audio.peak);cJSON_AddNumberToObject(a,"volume",audio.volume);cJSON_AddNumberToObject(a,"error",audio.error);}}
    return json_response(req, root);
}
static int hex(char c) {
    return c >= '0' && c <= '9'   ? c - '0'
           : c >= 'a' && c <= 'f' ? c - 'a' + 10
           : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                  : -1;
}
static int upload_kind(httpd_req_t* req) {
    char query[1024],kind[24];
    if(httpd_req_get_url_query_str(req,query,sizeof(query))!=ESP_OK ||
       httpd_query_key_value(query,"kind",kind,sizeof(kind))!=ESP_OK)return -1;
    return !strcmp(kind,"books")?0:!strcmp(kind,"fonts")?1:!strcmp(kind,"wallpapers")?2:!strcmp(kind,"audio")?3:!strcmp(kind,"music")?4:-1;
}
static const char* upload_directory(int kind) {
    return kind==1?"/sdcard/fonts":kind==2?"/sdcard/wallpapers":kind==3?"/sdcard/audio":kind==4?"/sdcard/music":library_path;
}
/* Decode once, then apply the same SD boundary on every endpoint. */
static bool query_value(httpd_req_t* req,const char* key,char* out,size_t capacity) {
    char query[2048],encoded[1536];
    if(httpd_req_get_url_query_str(req,query,sizeof(query))!=ESP_OK ||
       httpd_query_key_value(query,key,encoded,sizeof(encoded))!=ESP_OK)return false;
    size_t used=0;
    for(size_t i=0;encoded[i];++i) {
        int c=(unsigned char)encoded[i];
        if(c=='%') {
            if(!encoded[i+1] || !encoded[i+2] || hex(encoded[i+1])<0 || hex(encoded[i+2])<0)return false;
            c=hex(encoded[i+1])*16+hex(encoded[i+2]);i+=2;
        } else if(c=='+')c=' ';
        if(c<32 || used+1>=capacity)return false;
        out[used++]=(char)c;
    }
    out[used]=0;return true;
}
static bool query_has(httpd_req_t* req,const char* key) {
    const char* q=strchr(req->uri,'?');size_t n=strlen(key);
    while(q) {++q;if(!strncmp(q,key,n) && q[n]=='=')return true;q=strchr(q,'&');}
    return false;
}
static bool request_directory(httpd_req_t* req,char out[512]) {
    if(query_has(req,"directory")) {
        if(!query_value(req,"directory",out,512))return false;
    } else {
        int kind=upload_kind(req);if(kind<0)return false;
        strlcpy(out,upload_directory(kind),512);
    }
    /* Music is a fixed landing directory shared with the local player. */
    if(upload_kind(req)==4&&strcmp(out,"/sdcard/music"))return false;
    return thingsos_remote_directory(out);
}
static bool number_query(httpd_req_t* req,const char* key,size_t* value) {
    char text[24];*value=0;
    if(!query_has(req,key))return true;
    if(!query_value(req,key,text,sizeof(text)))return false;
    if(!text[0])return false;
    for(const char* p=text;*p;++p)if(*p<'0'||*p>'9')return false;
    char* end;unsigned long n=strtoul(text,&end,10);if(*end || n>64u*1024u*1024u)return false;
    *value=n;return true;
}
static bool filename(httpd_req_t* req, char out[256], int* kind_out) {
    char query[1024], encoded[768];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "name", encoded, sizeof(encoded)) != ESP_OK)
        return false;
    *kind_out=upload_kind(req);
    if(*kind_out<0)return false;
    size_t j = 0;
    for (size_t i = 0; encoded[i]; i++) {
        int c = (unsigned char)encoded[i];
        if (c == '%') {
            if (!encoded[i + 1] || !encoded[i + 2] || hex(encoded[i + 1]) < 0 ||
                hex(encoded[i + 2]) < 0)
                return false;
            c = hex(encoded[i + 1]) * 16 + hex(encoded[i + 2]);
            i += 2;
        } else if (c == '+')
            c = ' ';
        if (c < 32 || c == '/' || c == '\\' || c == ':' || j >= 190) return false;
        out[j++] = (char)c;
    }
    out[j] = 0;
    /* Shared FAT-safe name rule (manage.c); "..." inside a name is allowed. */
    if (!thingsos_upload_name(out)) return false;
    const char* ext = strrchr(out, '.');
    if (!ext) return false;
    return *kind_out==1 ? !strcasecmp(ext, ".ttf")
                  : *kind_out==3||*kind_out==4 ? !strcasecmp(ext,".wav")||!strcasecmp(ext,".mp3")
                  : *kind_out==2 ? !strcasecmp(ext,".png") || !strcasecmp(ext,".jpg") || !strcasecmp(ext,".jpeg") || !strcasecmp(ext,".webp")
                  : !strcasecmp(ext, ".epub") || !strcasecmp(ext, ".txt") || !strcasecmp(ext, ".md") || !strcasecmp(ext, ".markdown") ||
                        !strcasecmp(ext, ".png") || !strcasecmp(ext, ".jpg") ||
                        !strcasecmp(ext, ".jpeg") || !strcasecmp(ext, ".webp");
}
static bool recover_directory(const char* directory);
static esp_err_t list(httpd_req_t* req) {
    if(!authorized(req))return reject(req,"401 Unauthorized","Token required");
    if(!service_enabled)return reject(req,"503 Service Unavailable","Transfer service stopped");
    size_t offset=0;if(!number_query(req,"offset",&offset))return reject(req,"400 Bad Request","Invalid directory page");
    char folder[512];if(!request_directory(req,folder))return reject(req,"403 Forbidden","Protected or invalid SD directory");
    if(!lock())return reject(req,"503 Service Unavailable","SD unavailable or busy");
    if(sd_ready()&&upload_kind(req)==4&&!strcmp(folder,"/sdcard/music")&&mkdir(folder,0775)&&errno!=EEXIST){unlock();return reject(req,"500 Internal Server Error","Cannot create music directory");}
    if(sd_ready() && !recover_directory(folder)){unlock();return reject(req,"500 Internal Server Error","Pending file replacement could not be recovered");}
    DIR* dir=sd_ready()?opendir(folder):NULL;
    if(!dir){unlock();return reject(req,"404 Not Found","SD directory unavailable");}
    cJSON* array=cJSON_CreateArray();
    if(!array){closedir(dir);unlock();return ESP_ERR_NO_MEM;}
    if(!offset && !strcmp(folder,library_path)) {
        cJSON* item=cJSON_CreateObject();cJSON_AddStringToObject(item,"name","Example.txt");
        cJSON_AddBoolToObject(item,"readonly",true);cJSON_AddStringToObject(item,"content","Example Test");
        cJSON_AddItemToArray(array,item);
    }
    struct dirent* entry;size_t count=0,seen=0;bool more=false;
    while((entry=readdir(dir))) {
        if(entry->d_name[0]=='.')continue;
        char path[768];snprintf(path,sizeof(path),"%s/%s",folder,entry->d_name);
        struct stat info;if(stat(path,&info))continue;
        bool directory=S_ISDIR(info.st_mode);
        if(!directory && !S_ISREG(info.st_mode))continue;
        if(seen++<offset)continue;
        if(count==256){more=true;break;}
        cJSON* item=cJSON_CreateObject();if(!item)break;
        cJSON_AddStringToObject(item,"name",entry->d_name);
        cJSON_AddBoolToObject(item,"directory",directory);
        cJSON_AddBoolToObject(item,"locked",!thingsos_remote_directory(path));
        cJSON_AddBoolToObject(item,"readonly",directory || !thingsos_managed_path(path));
        cJSON_AddNumberToObject(item,"bytes",(double)info.st_size);
        cJSON_AddItemToArray(array,item);++count;
    }
    closedir(dir);
    char encoded[1536],card[17];size_t used=0;
    const unsigned char* source=(const unsigned char*)folder;
    static const char digits[]="0123456789ABCDEF";
    while(*source && used+3<sizeof(encoded)) {
        unsigned char c=*source++;
        if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='/'||c=='-'||c=='_')encoded[used++]=(char)c;
        else {encoded[used++]='%';encoded[used++]=digits[c>>4];encoded[used++]=digits[c&15];}
    }
    encoded[used]=0;snprintf(card,sizeof(card),"%016llx",(unsigned long long)management_card);
    httpd_resp_set_hdr(req,"X-ThingsOS-Directory",encoded);httpd_resp_set_hdr(req,"X-ThingsOS-Card",card);
    char next[24];snprintf(next,sizeof(next),"%u",more?(unsigned)(offset+count):0);
    httpd_resp_set_hdr(req,"X-ThingsOS-Next",next);
    unlock();return json_response(req,array);
}
static esp_err_t preview(httpd_req_t* req) {
    if(!authorized(req) || rescue_mode)return reject(req,"403 Forbidden","User files only; token required");
    if(!service_enabled || busy || manage_job)return reject(req,"409 Conflict","Device busy or transfer stopped");
    char folder[512],name[192],path[768];size_t chapter=0,offset=0;
    if(!request_directory(req,folder) || !query_value(req,"name",name,sizeof(name)) ||
       strpbrk(name,"/\\") || !number_query(req,"chapter",&chapter) || !number_query(req,"offset",&offset))
        return reject(req,"400 Bad Request","Invalid preview target");
    snprintf(path,sizeof(path),"%s/%s",folder,name);
    if(!thingsos_managed_path(path))return reject(req,"403 Forbidden","Protected or unsupported file");
    if(!lock())return reject(req,"503 Service Unavailable","SD unavailable or busy");
    struct stat info;
    if(!sd_ready() || stat(path,&info) || !S_ISREG(info.st_mode)){unlock();return reject(req,"404 Not Found","File unavailable");}
    httpd_resp_set_hdr(req,"X-Content-Type-Options","nosniff");httpd_resp_set_hdr(req,"Cache-Control","no-store");
    const char* ext=strrchr(name,'.');
    if(!strcasecmp(ext,".png") || !strcasecmp(ext,".jpg") || !strcasecmp(ext,".jpeg") || !strcasecmp(ext,".webp")) {
        if(info.st_size>8*1024*1024){unlock();return reject(req,"413 Payload Too Large","Image preview limit is 8 MiB");}
        FILE* f=fopen(path,"rb");char* block=heap_caps_malloc(8192,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if(!f || !block){if(f)fclose(f);free(block);unlock();return reject(req,"500 Internal Server Error","Image unavailable");}
        httpd_resp_set_type(req,!strcasecmp(ext,".png")?"image/png":!strcasecmp(ext,".webp")?"image/webp":"image/jpeg");
        esp_err_t result=ESP_OK;size_t n;
        while((n=fread(block,1,8192,f)) && result==ESP_OK)result=httpd_resp_send_chunk(req,block,n);
        if(ferror(f))result=ESP_FAIL;
        fclose(f);free(block);unlock();
        return result==ESP_OK?httpd_resp_send_chunk(req,NULL,0):result;
    }
    thingsos_preview_t result;
    if(!thingsos_preview_read(path,chapter,offset,&result)){unlock();return reject(req,"422 Unprocessable Content","Book cannot be previewed or page is invalid");}
    cJSON* out=cJSON_CreateObject();
    if(out) {
        cJSON_AddStringToObject(out,"text",result.text);cJSON_AddStringToObject(out,"title",result.title);
        cJSON_AddNumberToObject(out,"chapter",result.chapter);cJSON_AddNumberToObject(out,"chapters",result.chapters);
        cJSON_AddNumberToObject(out,"offset",result.offset);cJSON_AddNumberToObject(out,"next",result.next);
    }
    thingsos_preview_close(&result);unlock();
    if(!out)return ESP_ERR_NO_MEM;
    return json_response(req,out);
}

static esp_err_t manage(httpd_req_t* req) {
    if (!service_enabled) return reject(req, "503 Service Unavailable", "Transfer service stopped");
    if (!authorized(req) || rescue_mode)
        return reject(req, "403 Forbidden", "User files only; token required");
    char confirmation[16];
    if (httpd_req_get_hdr_value_str(
            req, "X-ThingsOS-Confirm", confirmation, sizeof(confirmation)) != ESP_OK ||
        strcmp(confirmation, "MANAGE"))
        return reject(req, "400 Bad Request", "Explicit management confirmation required");
    if (!sd_ready() || thingsos_network_busy() || manage_job)
        return reject(req, "409 Conflict", "Device busy or SD unavailable");
    char request_directory[512], request_card[17];
    uint64_t card_id;
    portENTER_CRITICAL(&state_guard);
    strlcpy(request_directory, library_path, sizeof(request_directory));
    card_id = management_card;
    portEXIT_CRITICAL(&state_guard);
    snprintf(request_card, sizeof(request_card), "%016llx", (unsigned long long)card_id);
    if (req->content_len <= 0 || req->content_len > 15000)
        return reject(req, "400 Bad Request", "Invalid body");
    char* body = thingsos_psram_alloc(req->content_len + 1);
    if (!body) return ESP_ERR_NO_MEM;
    size_t got = 0;
    while (got < (size_t)req->content_len) {
        int n = httpd_req_recv(req, body + got, req->content_len - (int)got);
        if (n <= 0) {
            free(body);
            return ESP_FAIL;
        }
        got += (size_t)n;
    }
    body[got] = 0;
    cJSON* json = cJSON_Parse(body);
    free(body);
    cJSON* names = cJSON_GetObjectItemCaseSensitive(json, "names");
    cJSON* rename = cJSON_GetObjectItemCaseSensitive(json, "rename");
    cJSON* context_directory = cJSON_GetObjectItemCaseSensitive(json, "directory");
    cJSON* context_card = cJSON_GetObjectItemCaseSensitive(json, "card");
    if (!cJSON_IsString(context_directory) || !cJSON_IsString(context_card) ||
        !thingsos_remote_directory(context_directory->valuestring) ||
        strlen(context_directory->valuestring)>=sizeof(request_directory) ||
        strcmp(context_card->valuestring, request_card)) {
        cJSON_Delete(json);
        return reject(
            req, "409 Conflict",
            "Directory or SD card changed; refresh the file list before managing");
    }
    strlcpy(request_directory,context_directory->valuestring,sizeof(request_directory));
    int count = cJSON_GetArraySize(names);
    bool ok = cJSON_IsArray(names) && count > 0 && count <= THINGSOS_MANAGE_BATCH &&
              (!rename ||
               (count == 1 && cJSON_IsString(rename) && thingsos_managed_name(rename->valuestring)));
    thingsos_manage_job_t* job = ok ? calloc(1, sizeof(*job)) : NULL;
    if (ok && !job) {
        cJSON_Delete(json);
        return ESP_ERR_NO_MEM;
    }
    if (ok) {
        job->count = (size_t)count;
        if (rename) strlcpy(job->new_name, rename->valuestring, sizeof(job->new_name));
        for (int i = 0; i < count; ++i) {
            cJSON* name = cJSON_GetArrayItem(names, i);
            if (!cJSON_IsString(name) || strlen(name->valuestring) >= 192 ||
                !name->valuestring[0] || name->valuestring[0] == '.' ||
                strpbrk(name->valuestring, "/\\:*?\"<>|")) {
                ok = false;
                break;
            }
            size_t prefix = strlen(request_directory), length = strlen(name->valuestring);
            if (prefix + length + 2 > sizeof(job->paths[i])) {
                ok = false;
                break;
            }
            memcpy(job->paths[i], request_directory, prefix);
            job->paths[i][prefix] = '/';
            memcpy(job->paths[i] + prefix + 1, name->valuestring, length + 1);
            if (!thingsos_managed_path(job->paths[i])) {
                ok = false;
                break;
            }
            for (int j = 0; j < i; ++j)
                if (!strcmp(job->paths[j], job->paths[i])) ok = false;
        }
    }
    cJSON_Delete(json);
    if (!ok) {
        free(job);
        return reject(req, "400 Bad Request", "Protected or invalid file name/type");
    }
    portENTER_CRITICAL(&state_guard);
    if (external_busy || busy || manage_job || management_card != card_id) {
        portEXIT_CRITICAL(&state_guard);
        free(job);
        return reject(req, "409 Conflict", "Directory or SD card changed; refresh files");
    }
    job->job = ++manage_sequence;
    job->card_id = card_id;
    manage_job = job;
    manage_claimed = false;
    manage_done = manage_failed = 0;
    unsigned id = job->job;
    portEXIT_CRITICAL(&state_guard);
    cJSON* out = cJSON_CreateObject();
    cJSON_AddNumberToObject(out, "job", id);
    httpd_resp_set_status(req, "202 Accepted");
    return json_response(req, out);
}

static bool recover_directory(const char* directory) {
    bool ok=true;
        DIR* dir = opendir(directory);
        if (!dir) return true;
        struct dirent* e;
        while ((e = readdir(dir))) {
            if (strncmp(e->d_name, ".replace-", 9)) continue;
            size_t length = strlen(e->d_name);
            if (length < 14 || strcmp(e->d_name + length - 4, ".bak") || length - 13 >= 192)
                continue;
            char name[192];
            memcpy(name, e->d_name + 9, length - 13);
            name[length - 13] = 0;
            if (!name[0] || name[0] == '.' || strpbrk(name, "/\\:*?\"<>|")) continue;
            const char* ext = strrchr(name, '.');
            if (!ext ||
                (strcasecmp(ext, ".txt") && strcasecmp(ext, ".md") && strcasecmp(ext, ".markdown") && strcasecmp(ext, ".epub") && strcasecmp(ext, ".ttf") &&
                 strcasecmp(ext, ".png") && strcasecmp(ext, ".jpg") && strcasecmp(ext, ".jpeg") && strcasecmp(ext, ".webp") && strcasecmp(ext,".mp3") && strcasecmp(ext,".wav")))
                continue;
            char source[768], target[768];
            snprintf(source, sizeof(source), "%s/%s", directory, e->d_name);
            snprintf(target, sizeof(target), "%s/%s", directory, name);
            struct stat info;
            if (stat(source, &info) || !S_ISREG(info.st_mode)) {
                ok = false;
                continue;
            }
            if (stat(target, &info) == 0) {
                if (S_ISREG(info.st_mode)) {
                    if (unlink(source)) ok = false;
                } else
                    ok = false;
            } else if (rename(source, target))
                ok = false;
        }
        closedir(dir);
    return ok;
}
bool thingsos_network_recover_files(void) {
    bool ok=true;
    const char* directories[]={library_path,"/sdcard/fonts","/sdcard/wallpapers","/sdcard/audio","/sdcard/music"};
    for(unsigned i=0;i<sizeof(directories)/sizeof(directories[0]);++i)if(!recover_directory(directories[i]))ok=false;
    return ok;
}
bool thingsos_network_pause_requested(void) {
    return pause_requested && !pause_complete;
}
void thingsos_network_pause_complete(bool ok) {
    pause_ok = ok;
    pause_complete = true;
    pause_requested = false;
}
static bool pause_reader(void) {
    if (rescue_mode) return true;
    pause_complete = pause_ok = false;
    pause_requested = true;
    for (unsigned i = 0; i < 2000 && !pause_complete; ++i) vTaskDelay(pdMS_TO_TICKS(10));
    pause_requested = false;
    return pause_complete && pause_ok;
}
#include "upload.inc"

static esp_err_t wifi(httpd_req_t* req) {
    if (!authorized(req)) return reject(req, "401 Unauthorized", "Token required");
    if (req->content_len <= 0 || req->content_len > 256)
        return reject(req, "400 Bad Request", "Invalid body");
    char body[257];
    size_t got = 0;
    while (got < (size_t)req->content_len) {
        int n = httpd_req_recv(req, body + got, req->content_len - (int)got);
        if (n <= 0) return ESP_FAIL;
        got += (size_t)n;
    }
    body[got] = 0;
    cJSON* json = cJSON_Parse(body);
    cJSON *ssid = cJSON_GetObjectItemCaseSensitive(json, "ssid"),
          *password = cJSON_GetObjectItemCaseSensitive(json, "password");
    esp_err_t result = cJSON_IsString(ssid) && cJSON_IsString(password)
                           ? thingsos_network_connect(ssid->valuestring, password->valuestring)
                           : ESP_ERR_INVALID_ARG;
    cJSON_Delete(json);
    memset(body, 0, sizeof(body));
    return result == ESP_OK ? httpd_resp_sendstr(req, "Connecting; AP remains available")
                            : reject(req, "400 Bad Request", "SSID/password invalid");
}
static void reboot_task(void* arg) {
    vTaskDelay(pdMS_TO_TICKS(1800));
    esp_restart();
    vTaskDelete(NULL);
}
#include "ota_adapter.inc"
#include "remote_update.inc"
esp_err_t thingsos_network_connect(const char* ssid, const char* password) {
    if (!ssid || !password || strlen(ssid) < 1 || strlen(ssid) > 32 ||
        (strlen(password) > 0 && strlen(password) < 8) || strlen(password) > 63 || busy)
        return ESP_ERR_INVALID_ARG;
    if (!wifi_control || xSemaphoreTake(wifi_control, pdMS_TO_TICKS(100)) != pdTRUE)
        return ESP_ERR_INVALID_STATE;
    esp_err_t e = wifi_suspended ? ESP_ERR_INVALID_STATE : wifi_radio_start();
    if (e != ESP_OK) {
        xSemaphoreGive(wifi_control);
        return e;
    }
    wifi_config_t c = { 0 };
    memcpy(c.sta.ssid, ssid, strlen(ssid));
    strlcpy((char*)c.sta.password, password, sizeof(c.sta.password));
    c.sta.threshold.authmode = password[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    reconnect_enabled = false;
    wifi_station_cancel();
    e = esp_wifi_disconnect();
    if (e == ESP_OK || e == ESP_ERR_WIFI_NOT_CONNECT)
        e = esp_wifi_set_config(WIFI_IF_STA, &c);
    memset(&c, 0, sizeof(c));
    if (e == ESP_OK) {
        strlcpy(saved_ssid, ssid, sizeof(saved_ssid));
        strlcpy(saved_password, password, sizeof(saved_password));
        user_disconnected = false;
        reconnect_enabled = true;
        wifi_station_request();
    } else ESP_LOGW("ThingsOS_network", "WiFi profile setup failed: %s", esp_err_to_name(e));
    xSemaphoreGive(wifi_control);
    return e;
}
static void rom_task(void* arg) {
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(800));
    thingsos_board_rom_download();
    vTaskDelete(NULL);
}
static esp_err_t rom_download(httpd_req_t* req) {
    if (!rescue_mode || !authorized(req))
        return reject(req, "403 Forbidden", "Rescue token required");
    if (thingsos_network_busy()) return reject(req, "409 Conflict", "Device operation in progress");
    char confirmation[16];
    if (httpd_req_get_hdr_value_str(
            req, "X-ThingsOS-Confirm", confirmation, sizeof(confirmation)) != ESP_OK ||
        strcmp(confirmation, "ROM-USB"))
        return reject(req, "400 Bad Request", "Explicit ROM confirmation required");
    esp_err_t result = httpd_resp_sendstr(req, "Entering ROM USB download mode");
    xTaskCreate(rom_task, "rom_download", 2048, NULL, 5, NULL);
    return result;
}
static bool diagnostic_chunk(void* context,const char* data,size_t bytes) {
    return httpd_resp_send_chunk(context,data,bytes)==ESP_OK;
}
/* Export file name carries the board model and firmware version (no time
   stamp), e.g. ThingsOS-diagnostics-Metalio-E-Ink4-Plus-0.2.1.json. */
static void diagnostic_disposition(char* out,size_t cap) {
    const esp_app_desc_t* app=esp_app_get_description();
    char version[sizeof(app->version)];size_t n=0;
    for(const char* s=app->version;*s && n+1<sizeof(version);++s) {
        char c=*s;
        version[n++]=isalnum((unsigned char)c) || c=='.' || c=='-' || c=='_' ? c : '_';
    }
    version[n]=0;
    snprintf(out,cap,"attachment; filename=\"ThingsOS-diagnostics-%s-%s.json\"",THINGSOS_RELEASE_BOARD,n?version:"unknown");
}
static esp_err_t diagnostic_download(httpd_req_t* req) {
    if(!authorized(req))return reject(req,"401 Unauthorized","Enter device token");
    if(busy || external_busy || manage_job)return reject(req,"409 Conflict","Device busy; retry later");
    httpd_resp_set_type(req,"application/json; charset=utf-8");
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    /* httpd keeps the header pointer until the response is sent. */
    char disposition[128];diagnostic_disposition(disposition,sizeof(disposition));
    httpd_resp_set_hdr(req,"Content-Disposition",disposition);
    if(!thingsos_diag_export(diagnostic_chunk,req))return ESP_FAIL;
    return httpd_resp_send_chunk(req,NULL,0);
}
static int http_service_start(void* ignored) {
    (void)ignored;
    httpd_config_t settings = HTTPD_DEFAULT_CONFIG();
    settings.stack_size = 12288;
    settings.max_uri_handlers = 13;
    settings.recv_wait_timeout = 10;
    if (!rescue_mode) thingsos_memory_trace("http-starting",0,0);
    esp_err_t e = httpd_start(&server, &settings);
    if (e != ESP_OK) {
        if (!rescue_mode) thingsos_memory_trace("http-start-failed",0,e);
        return e;
    }
    const httpd_uri_t routes[] = {
        { .uri = "/", .method = HTTP_GET, .handler = page },
        { .uri = "/sha256.js", .method = HTTP_GET, .handler = sha_script },
        { .uri = "/image.js", .method = HTTP_GET, .handler = image_script },
        { .uri = "/api/diagnostics", .method = HTTP_GET, .handler = diagnostic_download },
        { .uri = "/api/status", .method = HTTP_GET, .handler = status },
        { .uri = "/api/list", .method = HTTP_GET, .handler = list },
        { .uri = "/api/preview", .method = HTTP_GET, .handler = preview },
        { .uri = "/api/manage", .method = HTTP_POST, .handler = manage },
        { .uri = "/api/upload", .method = HTTP_POST, .handler = upload },
        { .uri = "/api/wifi", .method = HTTP_POST, .handler = wifi },
        { .uri = "/api/ota", .method = HTTP_POST, .handler = ota },
        { .uri = "/api/release", .method = HTTP_GET, .handler = release_status },
        { .uri = "/api/rom", .method = HTTP_POST, .handler = rom_download }
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        e = httpd_register_uri_handler(server, routes + i);
        if (e != ESP_OK) {httpd_stop(server);server=NULL;return e;}
    }
    ESP_LOGI("ThingsOS_network","Transfer HTTP service started");
    if (!rescue_mode) thingsos_memory_trace("http-started",0,0);
    return ESP_OK;
}
static int http_service_stop(void* ignored) {
    (void)ignored;
    esp_err_t result=httpd_stop(server);
    if(result==ESP_OK)server=NULL;
    return result;
}
esp_err_t thingsos_network_service_poll(void) {
    if(!network_ready)return ESP_OK;
    /* httpd_stop joins request handlers, which may be waiting for this mutex. */
    if(thingsos_io_lock&&xSemaphoreGetMutexHolder(thingsos_io_lock)==xTaskGetCurrentTaskHandle())return ESP_ERR_INVALID_STATE;
    bool before=http_service.running;
    thingsos_service_ops_t ops={NULL,http_service_start,http_service_stop};
    esp_err_t result=thingsos_service_poll(&http_service,service_enabled,rescue_mode,thingsos_network_busy()||manage_job,&ops);
    if(before&&!http_service.running) {
        vTaskDelay(pdMS_TO_TICKS(20)); /* Let idle tasks reclaim the deleted HTTP stack. */
        if (!rescue_mode) thingsos_memory_trace("http-stopped",0,0);
        unsigned internal=heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
        unsigned largest=heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
        unsigned dma=heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA);
        unsigned psram=heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        portENTER_CRITICAL(&state_guard);
        ++service_idle.sequence;service_idle.internal_free=internal;service_idle.largest=largest;
        service_idle.dma_free=dma;service_idle.psram_free=psram;
        portEXIT_CRITICAL(&state_guard);
        ESP_LOGI("ThingsOS_memory","HTTP stopped: internal_free=%u largest=%u DMA=%u PSRAM=%u",internal,largest,dma,psram);
    }
    return result;
}
esp_err_t thingsos_network_start(bool rescue) {
    if(network_ready)return rescue==rescue_mode?ESP_OK:ESP_ERR_INVALID_STATE;
    rescue_mode = rescue;
    if (!thingsos_io_lock) thingsos_io_lock = xSemaphoreCreateMutex();
    if (!wifi_control) wifi_control = xSemaphoreCreateMutex();
    if (!wifi_control) return ESP_ERR_NO_MEM;
    if (!thingsos_io_lock) return ESP_ERR_NO_MEM;
    esp_err_t e = esp_netif_init();
    if (e != ESP_OK) return e;
    e = esp_event_loop_create_default();
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) return e;
    esp_netif_create_default_wifi_ap();
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    init.nvs_enable = 0;
    e = esp_wifi_init(&init);
    if (e != ESP_OK) return e;
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    /* Older firmware left a station config in the driver's own NVS namespace.
       The driver restores it at init and starts connecting as soon as the STA
       runs, even though the app never asked: with the AP out of range that
       retries internally at ~45 Hz for hours and pins the Wi-Fi task on core 0.
       The app keeps its profiles in "thingsos-wifi" and applies them through
       thingsos_network_connect(), so the driver copy is redundant; erase it
       once here. esp_wifi_restore() only touches the driver's wifi NVS. */
    esp_err_t restored = esp_wifi_restore();
    if (restored != ESP_OK)
        ESP_LOGW("ThingsOS_network", "WiFi driver config reset: %s", esp_err_to_name(restored));
    for (unsigned i = 0; i < 16; i++)
        snprintf(token + i * 2, 3, "%02x", (unsigned)(esp_random() & 255));
    wifi_config_t config = { 0 };
    strlcpy((char*)config.ap.ssid, ap_ssid, sizeof(config.ap.ssid));
    strlcpy((char*)config.ap.password, ap_password, sizeof(config.ap.password));
    config.ap.ssid_len = strlen(ap_ssid);
    config.ap.channel = 1;
    config.ap.max_connection = 2;
    config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    /* AP parameters require an enabled AP interface, even before radio start.
       Configure both interfaces while stopped; normal boot starts STA only. */
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_APSTA), "ThingsOS", "WiFi config mode");
    /* Allow 802.11n alongside b/g for the cold-start TCP comparison. */
    ESP_RETURN_ON_ERROR(esp_wifi_set_protocol(WIFI_IF_STA,
                        WIFI_PROTOCOL_11B|WIFI_PROTOCOL_11G|WIFI_PROTOCOL_11N),
                        "ThingsOS", "STA compatible protocol");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &config), "ThingsOS", "AP config");
    /* The driver keeps its own NVS copy of the station config and connects to it
       as soon as the STA starts, independently of the app's profile list. When
       that AP is out of range it retries internally every ~22 ms for hours
       ("Haven't to connect to a suitable AP now!") and keeps the high-priority
       Wi-Fi task on core 0. Clear the driver copy; the app applies its own
       profile through thingsos_network_connect(), which is rate limited. */
    wifi_config_t cleared_station = { 0 };
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &cleared_station), "ThingsOS",
                        "STA config clear");
    wifi_config_t station_check = { 0 };
    if (esp_wifi_get_config(WIFI_IF_STA, &station_check) == ESP_OK)
        ESP_LOGI("ThingsOS_network", "STA config at start: ssid_len=%u",
                 (unsigned)strlen((const char*)station_check.sta.ssid));
    if(!rescue)ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), "ThingsOS", "STA boot mode");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL),
                        "ThingsOS", "WiFi event");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, NULL),
                        "ThingsOS", "WiFi IP event");
    /* Normal boot leaves the radio off until the app needs it; the rescue image
       needs its access point immediately. */
    if (rescue) ESP_RETURN_ON_ERROR(wifi_radio_start(), "ThingsOS", "WiFi start");
    ESP_RETURN_ON_ERROR(
        esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, network_time, NULL), "ThingsOS",
        "Time event");
    ESP_RETURN_ON_ERROR(
        esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, network_warmup_event, NULL),
        "ThingsOS", "Warm-up event");
    if (!rescue && nvs_flash_init() == ESP_OK) {
        nvs_handle_t h;
        if (nvs_open("thingsos-wifi", NVS_READONLY, &h) == ESP_OK) {
            struct {
                char ssid[33], password[64];
            } station = { 0 };
            size_t bytes = sizeof(station);
            if (nvs_get_blob(h, "station", &station, &bytes) == ESP_OK &&
                bytes == sizeof(station) && memchr(station.ssid, 0, 33) &&
                memchr(station.password, 0, 64) && station.ssid[0])
            {
                if(radio_allowed)thingsos_network_connect(station.ssid,station.password);
                else {strlcpy(saved_ssid,station.ssid,sizeof(saved_ssid));strlcpy(saved_password,station.password,sizeof(saved_password));
                    wifi_config_t saved={0};strlcpy((char*)saved.sta.ssid,station.ssid,sizeof(saved.sta.ssid));strlcpy((char*)saved.sta.password,station.password,sizeof(saved.sta.password));
                    esp_wifi_set_config(WIFI_IF_STA,&saved);memset(&saved,0,sizeof(saved));user_disconnected=true;}
            }
            memset(&station, 0, sizeof(station));
            nvs_close(h);
        }
    }
    network_ready=true;
    service_enabled=rescue;
    if(rescue) {
        thingsos_service_ops_t ops={NULL,http_service_start,http_service_stop};
        return thingsos_service_poll(&http_service,true,true,false,&ops);
    }
    /* TEMPORARY DIAGNOSTIC: phase timing probes after idle periods. */
    return ESP_OK;
}

esp_err_t thingsos_enter_rescue(void) {
    bool owned = thingsos_io_lock &&
                 xSemaphoreGetMutexHolder(thingsos_io_lock) == xTaskGetCurrentTaskHandle();
    if (thingsos_network_busy() || (!owned && !lock())) return ESP_ERR_INVALID_STATE;
    const esp_partition_t* p = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, "rescue");
    esp_app_desc_t description;
    esp_err_t e = p ? esp_ota_get_partition_description(p, &description) : ESP_ERR_NOT_FOUND;
    if (e == ESP_OK &&
        strncmp(description.project_name, "ThingsOS_rescue", sizeof(description.project_name)))
        e = ESP_ERR_INVALID_VERSION;
    if (e == ESP_OK) e = esp_ota_set_boot_partition(p);
    if(e==ESP_OK){thingsos_diag_reason(DIAG_REASON_MANUAL,0);thingsos_diag_poll(true);}
    if (!owned) unlock();
    if (e == ESP_OK) {xTaskCreate(reboot_task, "rescue_boot", 2048, NULL, 5, NULL);}
    return e;
}
bool thingsos_network_connected(void) {
    portENTER_CRITICAL(&state_guard);
    bool has_ip = station_join.has_ip;
    portEXIT_CRITICAL(&state_guard);
    if(!has_ip || user_disconnected || wifi_suspended || !radio_allowed)return false;
    return wifi_station_has_address();
}
esp_err_t thingsos_network_scan_start(void) {
    if(wifi_operation_busy() || !network_ready)return ESP_ERR_INVALID_STATE;
    if(wifi_radio_start()!=ESP_OK)return ESP_ERR_INVALID_STATE;
    portENTER_CRITICAL(&state_guard);
    if(scan_active){portEXIT_CRITICAL(&state_guard);return ESP_ERR_INVALID_STATE;}
    scan_active=true;scan_ready=false;scan_status=ESP_OK;scan_started=esp_timer_get_time();
    portEXIT_CRITICAL(&state_guard);
    wifi_scan_config_t config = { 0 };
    esp_err_t error=esp_wifi_scan_start(&config,false);
    if(error!=ESP_OK){portENTER_CRITICAL(&state_guard);scan_active=false;scan_ready=false;portEXIT_CRITICAL(&state_guard);}
    return error;
}
esp_err_t thingsos_network_scan_poll(thingsos_wifi_result_t* out,size_t capacity,size_t* count,bool* done) {
    if(!out || !count || !done || !capacity || capacity>12)return ESP_ERR_INVALID_ARG;
    *done=false;
    portENTER_CRITICAL(&state_guard);
    bool active=scan_active,ready=scan_ready;esp_err_t error=scan_status;
    portEXIT_CRITICAL(&state_guard);
    if(!active){*done=true;return ESP_ERR_INVALID_STATE;}
    if(!ready)return ESP_OK;
    *done=true;*count=0;
    if(error!=ESP_OK){esp_wifi_clear_ap_list();goto finished;}
    uint16_t number = (uint16_t)capacity;
    wifi_ap_record_t* records = calloc(capacity, sizeof(*records));
    if (!records) {
        esp_wifi_clear_ap_list();
        error=ESP_ERR_NO_MEM;goto finished;
    }
    error = esp_wifi_scan_get_ap_records(&number, records);
    if (error == ESP_OK)
        for (unsigned i = 0; i < number; i++) {
            if (!records[i].ssid[0]) continue;
            thingsos_wifi_result_t* r = out + (*count)++;
            memcpy(r->ssid, records[i].ssid, 32);
            r->ssid[32] = 0;
            r->rssi = records[i].rssi;
            r->open = records[i].authmode == WIFI_AUTH_OPEN;
        }
    free(records);
    if(error!=ESP_OK)esp_wifi_clear_ap_list();
finished:
    portENTER_CRITICAL(&state_guard);scan_active=false;scan_ready=false;portEXIT_CRITICAL(&state_guard);
    ESP_LOGI("ThingsOS_network","WiFi scan async: %u ms count=%u result=0x%x",(unsigned)((esp_timer_get_time()-scan_started)/1000),(unsigned)*count,(unsigned)error);
    return error;
}

void thingsos_network_library(const char* path) {
    if (path && (!strncmp(path, "/sdcard/", 8) || !strcmp(path, "/sdcard")) &&
        !strstr(path, "..")) {
        portENTER_CRITICAL(&state_guard);
        strlcpy(library_path, path, sizeof(library_path));
        portEXIT_CRITICAL(&state_guard);
    }
}
void thingsos_network_address(bool hotspot, char* out, size_t capacity) {
    if(!hotspot && !thingsos_network_connected()){strlcpy(out,"等待网络地址",capacity);return;}
    esp_netif_t* netif = esp_netif_get_handle_from_ifkey(hotspot ? "WIFI_AP_DEF" : "WIFI_STA_DEF");
    esp_netif_ip_info_t info;
    if (!netif || esp_netif_get_ip_info(netif, &info) != ESP_OK || !info.ip.addr)
        strlcpy(out, "等待网络地址", capacity);
    else
        snprintf(out, capacity, "http://" IPSTR, IP2STR(&info.ip));
}
esp_err_t thingsos_network_mode(bool hotspot) {
    if (wifi_operation_busy()) return ESP_ERR_INVALID_STATE;
    /* The access point is the only reason to wake the radio without a connect
       request (transfer page hot spot). */
    if (hotspot && wifi_radio_start() != ESP_OK) return ESP_FAIL;
    return esp_wifi_set_mode(hotspot ? WIFI_MODE_APSTA : WIFI_MODE_STA);
}
esp_err_t thingsos_network_disconnect(void) {
    if(!network_ready || wifi_operation_busy())return ESP_ERR_INVALID_STATE;
    bool previous_retry=reconnect_enabled,previous_manual=user_disconnected;
    reconnect_enabled=false;user_disconnected=true;
    esp_err_t result=esp_wifi_disconnect();
    if(result!=ESP_OK && result!=ESP_ERR_WIFI_NOT_CONNECT) {
        reconnect_enabled=previous_retry;user_disconnected=previous_manual;return result;
    }
    wifi_station_cancel();wifi_failure[0]=0;
    return ESP_OK;
}
esp_err_t thingsos_network_forget(void) {
    if (wifi_operation_busy()) return ESP_ERR_INVALID_STATE;
    reconnect_enabled = false;
    wifi_station_cancel();
    esp_wifi_disconnect();
    wifi_config_t config = { 0 };
    bool erased = credentials(true);
    memset(saved_ssid, 0, sizeof(saved_ssid));
    memset(saved_password, 0, sizeof(saved_password));
    wifi_failure[0] = 0;
    esp_err_t result = esp_wifi_set_config(WIFI_IF_STA, &config);
    return erased ? result : ESP_FAIL;
}
void thingsos_network_service(bool enabled) {
    /* The transfer page is one of the callers that needs the radio. */
    if (enabled && wifi_radio_start() != ESP_OK) service_enabled = false;
    else service_enabled = rescue_mode || enabled;
}
bool thingsos_network_radio_enabled(void){return radio_allowed;}
bool thingsos_network_cancel_upload(void){
    portENTER_CRITICAL(&state_guard);bool ok=transfer.active&&!transfer.ota;if(ok)upload_cancel_requested=true;portEXIT_CRITICAL(&state_guard);return ok;
}
esp_err_t thingsos_network_radio(bool enabled){
    if(enabled==radio_allowed&&(!enabled||radio_up||!network_ready))return ESP_OK;
    if(wifi_operation_busy())return ESP_ERR_INVALID_STATE;
    if(!network_ready){radio_allowed=enabled;return ESP_OK;}
    if(!enabled){
        esp_err_t error=radio_up?esp_wifi_stop():ESP_OK;
        if(error!=ESP_OK&&error!=ESP_ERR_WIFI_NOT_STARTED)return error;
        radio_up=false;radio_allowed=false;reconnect_enabled=false;user_disconnected=true;
        wifi_station_cancel();service_enabled=false;scan_active=false;scan_ready=false;
        if(sntp_started){esp_netif_sntp_deinit();sntp_started=false;}
        time_fallback_at=0;
        ESP_LOGI("ThingsOS_network","User radio OFF");return ESP_OK;
    }
    radio_allowed=true;user_disconnected=false;reconnect_enabled=saved_ssid[0]!=0;
    wifi_station_cancel();
    esp_err_t error=wifi_radio_start();
    if(error!=ESP_OK){radio_allowed=false;return error;}
    if(reconnect_enabled)wifi_station_request();
    ESP_LOGI("ThingsOS_network","User radio ON");return ESP_OK;
}
void thingsos_network_auto_time(bool enabled) {
    time_enabled = enabled;
    if (!enabled && sntp_started) {
        esp_netif_sntp_deinit();
        sntp_started = false;
    }
    if (!enabled) time_fallback_at = 0;
    if (enabled && thingsos_network_connected())
        network_time(NULL, IP_EVENT, IP_EVENT_STA_GOT_IP, NULL);
}
