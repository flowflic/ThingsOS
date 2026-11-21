/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef int esp_err_t;
typedef int portMUX_TYPE;
typedef void* QueueHandle_t;
typedef void* SemaphoreHandle_t;
typedef void (*TaskFunction_t)(void*);

#define ESP_OK 0
#define ESP_FAIL -1
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(m) ((void)(m))
#define portEXIT_CRITICAL(m) ((void)(m))
#define pdPASS 1
#define pdTRUE 1
#define pdFALSE 0
#define portMAX_DELAY 0xffffffffu
#define pdMS_TO_TICKS(ms) (ms)
#define configMAX_PRIORITIES 25
#define CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE 4096

#define MALLOC_CAP_INTERNAL 1u
#define MALLOC_CAP_DMA 2u
#define MALLOC_CAP_8BIT 4u
#define MALLOC_CAP_SPIRAM 8u

#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)

#define NVS_READWRITE 1
#define NVS_READONLY 0
typedef unsigned nvs_handle_t;

#define BLE_HS_CONN_HANDLE_NONE 0xffffu
#define BLE_HS_ESTORE_FAIL 1
#define BLE_HS_ENOMEM 2
#define BLE_HS_EALREADY 3
#define BLE_HS_EBUSY 4
#define BLE_HS_EDONE 5
#define BLE_HS_EAPP 6
#define BLE_ERR_REM_USER_CONN_TERM 0x13

#define BLE_HCI_ADV_RPT_EVTYPE_ADV_IND 0
#define BLE_HCI_ADV_RPT_EVTYPE_DIR_IND 1

#define BLE_GAP_EVENT_DISC 1
#define BLE_GAP_EVENT_DISC_COMPLETE 2
#define BLE_GAP_EVENT_CONNECT 3
#define BLE_GAP_EVENT_DISCONNECT 4
#define BLE_GAP_EVENT_ENC_CHANGE 5
#define BLE_GAP_EVENT_PASSKEY_ACTION 6
#define BLE_GAP_EVENT_REPEAT_PAIRING 7
#define BLE_GAP_EVENT_NOTIFY_RX 8
#define BLE_GAP_REPEAT_PAIRING_IGNORE 1

#define BLE_SM_IOACT_NONE 0
#define BLE_SM_IOACT_DISP 1
#define BLE_SM_IOACT_NUMCMP 2
#define BLE_SM_IOACT_INPUT 3
#define BLE_HS_IO_DISPLAY_YESNO 1
#define BLE_SM_PAIR_KEY_DIST_ENC 1
#define BLE_SM_PAIR_KEY_DIST_ID 2

struct ble_npl_event { void* dummy; };
typedef void (*ble_npl_event_fn)(struct ble_npl_event*);
struct ble_npl_callout {
    ble_npl_event_fn fn;
    void* arg;
    bool active;
    bool armed;
    uint64_t fire_at_ms;
    struct ble_npl_event ev;
};

typedef struct { uint8_t type; uint8_t val[6]; } ble_addr_t;
typedef struct { uint8_t u[2]; } ble_uuid_t;
typedef struct { ble_uuid_t u; uint16_t val; } ble_uuid16_t;
#define BLE_UUID16_INIT(v) { .u = { .u = { (uint8_t)(v), (uint8_t)((v) >> 8) } }, .val = (v) }

struct os_mbuf { uint8_t data[257]; size_t len; };
#define OS_MBUF_PKTLEN(om) ((om) ? (om)->len : 0u)

struct ble_gap_disc_desc {
    uint8_t event_type;
    ble_addr_t addr;
    int8_t rssi;
    const uint8_t* data;
    uint8_t length_data;
};
struct ble_gap_conn_desc {
    ble_addr_t peer_id_addr;
    struct { unsigned encrypted : 1; unsigned bonded : 1; } sec_state;
};
struct ble_gap_event {
    int type;
    union {
        struct ble_gap_disc_desc disc;
        struct { int status; uint16_t conn_handle; } connect;
        struct { struct { uint16_t conn_handle; } conn; } disconnect;
        struct { int status; uint16_t conn_handle; } enc_change;
        struct { uint16_t conn_handle; struct { unsigned action; uint32_t numcmp; } params; } passkey;
        struct { uint16_t conn_handle; uint16_t attr_handle; struct os_mbuf* om; } notify_rx;
    };
};
struct ble_gap_disc_params { uint16_t itvl, window; };
struct ble_sm_io { unsigned action; uint32_t passkey; bool numcmp_accept; };
struct ble_gatt_error { int status; };
struct ble_gatt_attr { uint16_t handle; uint16_t offset; struct os_mbuf* om; };
struct ble_gatt_dsc { uint16_t handle; ble_uuid_t uuid; };
struct ble_gatt_chr { uint16_t def_handle, val_handle; ble_uuid_t uuid; uint8_t properties; };
struct ble_gatt_svc { uint16_t start_handle, end_handle; };
union ble_store_key { int dummy; };
union ble_store_value { int dummy; };
struct ble_store_status_event { int dummy; };

typedef int (*ble_gap_event_fn)(struct ble_gap_event* event, void* arg);
typedef int (*ble_gatt_disc_svc_fn)(uint16_t, const struct ble_gatt_error*, const struct ble_gatt_svc*, void*);
typedef int (*ble_gatt_chr_fn)(uint16_t, const struct ble_gatt_error*, const struct ble_gatt_chr*, void*);
typedef int (*ble_gatt_attr_fn)(uint16_t, const struct ble_gatt_error*, struct ble_gatt_attr*, void*);
typedef int (*ble_gatt_dsc_fn)(uint16_t, const struct ble_gatt_error*, uint16_t, const struct ble_gatt_dsc*, void*);
typedef int ble_store_read_fn(int type, const union ble_store_key* key, union ble_store_value* value);
typedef int ble_store_write_fn(int type, const union ble_store_value* value);
typedef int ble_store_delete_fn(int type, const union ble_store_key* key);
typedef int ble_store_status_fn(struct ble_store_status_event* event, void* arg);

static struct {
    void (*sync_cb)(void);
    void (*reset_cb)(int);
    int sm_io_cap, sm_bonding, sm_sc, sm_mitm;
    int sm_our_key_dist, sm_their_key_dist;
    ble_store_read_fn* store_read_cb;
    ble_store_write_fn* store_write_cb;
    ble_store_delete_fn* store_delete_cb;
    ble_store_status_fn* store_status_cb;
} ble_hs_cfg;

typedef struct {
    size_t item_size, capacity, count, head;
    uint8_t* items;
} host_queue_t;

static SemaphoreHandle_t thingsos_io_lock = (SemaphoreHandle_t)1;
static void* host_current_task = (void*)1;
static uint64_t host_clock_us;
static bool host_controller_active;
static unsigned host_init_calls, host_deinit_calls, host_stop_calls;
static unsigned host_scan_calls, host_scan_cancel_calls, host_connect_calls, host_conn_cancel_calls, host_terminate_calls;
static bool host_fail_connect_once;
static int host_connect_result;
static int host_nimble_init_rc;
static TaskFunction_t host_pending_init_fn, host_pending_host_fn, host_pending_stop_fn;
static void* host_pending_init_arg, *host_pending_host_arg, *host_pending_stop_arg;
static struct ble_npl_callout* host_active_timer;
static ble_gap_event_fn host_gap_cb;
static void* host_gap_arg;
static void* host_scan_arg;
static void (*host_deinit_hook)(void);

static uint8_t host_nvs_blob[2048];
static size_t host_nvs_blob_size;
static bool host_nvs_valid;

static inline void* heap_caps_calloc(size_t n, size_t b, unsigned caps) { (void)caps; return calloc(n, b); }
static inline size_t heap_caps_get_free_size(unsigned caps) { return (caps & MALLOC_CAP_SPIRAM) ? 4u * 1024u * 1024u : 64u * 1024u; }
static inline size_t heap_caps_get_largest_free_block(unsigned caps) { (void)caps; return 32u * 1024u; }
static inline uint64_t esp_timer_get_time(void) { return host_clock_us; }
static inline uint32_t esp_random(void) { return 123456u; }
static inline uint32_t esp_rom_crc32_le(uint32_t crc, const uint8_t* buf, size_t len) {
    uint32_t h = crc ^ 0x811c9dc5u;
    for (size_t i = 0; i < len; ++i) h = (h ^ buf[i]) * 16777619u;
    return h;
}
static inline unsigned uxTaskGetStackHighWaterMark(void* t) { (void)t; return 2048u; }
static inline SemaphoreHandle_t xSemaphoreCreateBinary(void) { return (SemaphoreHandle_t)2; }
static inline void* xSemaphoreGetMutexHolder(SemaphoreHandle_t s) { (void)s; return NULL; }
static inline int xSemaphoreTake(SemaphoreHandle_t s, unsigned ticks) { (void)s; (void)ticks; return pdTRUE; }
static inline int xSemaphoreGive(SemaphoreHandle_t s) { (void)s; return pdTRUE; }
static inline void* xTaskGetCurrentTaskHandle(void) { return host_current_task; }
static inline QueueHandle_t xQueueCreate(size_t cap, size_t item) {
    host_queue_t* q = calloc(1, sizeof(*q));
    if (!q) return NULL;
    q->capacity = cap; q->item_size = item;
    q->items = calloc(cap, item);
    return q;
}
static inline void xQueueReset(QueueHandle_t h) {
    host_queue_t* q = h;
    if (q) { q->count = q->head = 0; }
}
static inline int xQueueSend(QueueHandle_t h, const void* item, unsigned ticks) {
    (void)ticks;
    host_queue_t* q = h;
    if (!q || q->count >= q->capacity) return pdFALSE;
    size_t tail = (q->head + q->count) % q->capacity;
    memcpy(q->items + tail * q->item_size, item, q->item_size);
    ++q->count;
    return pdTRUE;
}
static inline int xQueueReceive(QueueHandle_t h, void* out, unsigned ticks) {
    (void)ticks;
    host_queue_t* q = h;
    if (!q || !q->count) return pdFALSE;
    memcpy(out, q->items + q->head * q->item_size, q->item_size);
    q->head = (q->head + 1) % q->capacity;
    --q->count;
    return pdTRUE;
}
static inline unsigned uxQueueMessagesWaiting(QueueHandle_t h) {
    host_queue_t* q = h;
    return q ? (unsigned)q->count : 0u;
}
static inline int xTaskCreate(TaskFunction_t fn, const char* name, unsigned stack, void* arg, int prio, void* handle) {
    (void)stack; (void)prio; (void)handle;
    if (!strcmp(name, "ble_init")) { host_pending_init_fn = fn; host_pending_init_arg = arg; return pdPASS; }
    if (!strcmp(name, "nimble_host")) { host_pending_host_fn = fn; host_pending_host_arg = arg; return pdPASS; }
    if (!strcmp(name, "ble_stop")) { host_pending_stop_fn = fn; host_pending_stop_arg = arg; return pdPASS; }
    return pdFALSE;
}
static inline void vTaskDelay(unsigned ticks) { host_clock_us += (uint64_t)ticks * 1000u; }
static inline void vTaskDelete(void* t) { (void)t; }

static inline esp_err_t nvs_open(const char* ns, int mode, nvs_handle_t* h) { (void)ns; (void)mode; *h = 1; return ESP_OK; }
static inline esp_err_t nvs_get_blob(nvs_handle_t h, const char* key, void* out, size_t* size) {
    (void)h; (void)key;
    if (!host_nvs_valid || *size < host_nvs_blob_size) return -1;
    memcpy(out, host_nvs_blob, host_nvs_blob_size);
    *size = host_nvs_blob_size;
    return ESP_OK;
}
static inline esp_err_t nvs_set_blob(nvs_handle_t h, const char* key, const void* in, size_t size) {
    (void)h; (void)key;
    if (size > sizeof(host_nvs_blob)) return -1;
    memcpy(host_nvs_blob, in, size);
    host_nvs_blob_size = size;
    host_nvs_valid = true;
    return ESP_OK;
}
static inline esp_err_t nvs_commit(nvs_handle_t h) { (void)h; return ESP_OK; }
static inline void nvs_close(nvs_handle_t h) { (void)h; }

static inline int nimble_port_init(void) {
    if (host_nimble_init_rc) return host_nimble_init_rc;
    host_controller_active = true;
    ++host_init_calls;
    return 0;
}
static inline int nimble_port_deinit(void) {
    if (host_deinit_hook) host_deinit_hook();
    host_controller_active = false;
    ++host_deinit_calls;
    return 0;
}
static inline void nimble_port_run(void) {}
static inline int nimble_port_stop(void) {
    ++host_stop_calls;
    if (host_pending_host_fn) {
        TaskFunction_t fn = host_pending_host_fn;
        void* arg = host_pending_host_arg;
        host_pending_host_fn = NULL;
        host_pending_host_arg = NULL;
        fn(arg);
    }
    return 0;
}
static inline void* nimble_port_get_dflt_eventq(void) { return (void*)1; }
void ble_store_config_init(void);

static inline void ble_npl_callout_init(struct ble_npl_callout* c, void* eq, ble_npl_event_fn fn, void* arg) {
    (void)eq;
    memset(c, 0, sizeof(*c));
    c->fn = fn; c->arg = arg; c->active = true;
    host_active_timer = c;
}
static inline void ble_npl_callout_deinit(struct ble_npl_callout* c) {
    c->active = false; c->armed = false;
    if (host_active_timer == c) host_active_timer = NULL;
}
static inline void ble_npl_callout_reset(struct ble_npl_callout* c, uint32_t ticks) {
    if (!c->active) return;
    c->armed = true;
    c->fire_at_ms = host_clock_us / 1000u + ticks;
}
static inline uint32_t ble_npl_time_ms_to_ticks32(uint32_t ms) { return ms; }

static inline int ble_hs_util_ensure_addr(int rnd) { (void)rnd; return 0; }
static inline int ble_hs_id_infer_auto(int priv, uint8_t* out) { (void)priv; *out = 0; return 0; }
static inline int ble_gap_disc(uint8_t own, int32_t dur, const struct ble_gap_disc_params* p, ble_gap_event_fn cb, void* arg) {
    (void)own; (void)dur; (void)p;
    ++host_scan_calls;
    host_gap_cb = cb; host_scan_arg = arg;
    return 0;
}
static inline int ble_gap_disc_cancel(void) { ++host_scan_cancel_calls; return 0; }
static inline int ble_gap_connect(uint8_t own, const ble_addr_t* peer, int32_t dur, void* p, ble_gap_event_fn cb, void* arg) {
    (void)own; (void)peer; (void)dur; (void)p;
    ++host_connect_calls;
    host_gap_cb = cb; host_gap_arg = arg;
    if (host_fail_connect_once) { host_fail_connect_once = false; return -1; }
    return host_connect_result;
}
static inline int ble_gap_conn_cancel(void) { ++host_conn_cancel_calls; return 0; }
static inline int ble_gap_terminate(uint16_t conn, uint8_t reason) {
    (void)conn; (void)reason;
    ++host_terminate_calls;
    return 0;
}
static inline int ble_gap_security_initiate(uint16_t conn) { (void)conn; return 0; }
static inline int ble_gap_conn_find(uint16_t conn, struct ble_gap_conn_desc* desc) {
    (void)conn;
    memset(desc, 0, sizeof(*desc));
    desc->sec_state.encrypted = 1;
    desc->sec_state.bonded = 1;
    desc->peer_id_addr.val[0] = 0x11;
    return 0;
}
static inline int ble_sm_inject_io(uint16_t conn, struct ble_sm_io* io) { (void)conn; (void)io; return 0; }
static inline int ble_store_util_delete_peer(const ble_addr_t* addr) { (void)addr; return 0; }
static inline int ble_hs_mbuf_to_flat(const struct os_mbuf* om, void* dst, uint16_t len, uint16_t* out) {
    if (!om || len > om->len) return -1;
    memcpy(dst, om->data, len);
    if (out) *out = len;
    return 0;
}
static inline uint16_t ble_uuid_u16(const ble_uuid_t* u) { return (uint16_t)(u->u[0] | ((uint16_t)u->u[1] << 8)); }
static inline int ble_gattc_disc_svc_by_uuid(uint16_t c, const ble_uuid_t* u, ble_gatt_disc_svc_fn cb, void* arg) {
    (void)u;
    struct ble_gatt_error err = { .status = BLE_HS_EDONE };
    cb(c, &err, NULL, arg);
    return 0;
}
static inline int ble_gattc_disc_all_chrs(uint16_t c, uint16_t s, uint16_t e, ble_gatt_chr_fn cb, void* arg) { (void)c; (void)s; (void)e; (void)cb; (void)arg; return 0; }
static inline int ble_gattc_read_long(uint16_t c, uint16_t h, uint16_t o, ble_gatt_attr_fn cb, void* arg) { (void)c; (void)h; (void)o; (void)cb; (void)arg; return 0; }
static inline int ble_gattc_write_no_rsp_flat(uint16_t c, uint16_t h, const void* d, uint16_t l) { (void)c; (void)h; (void)d; (void)l; return 0; }
static inline int ble_gattc_write_flat(uint16_t c, uint16_t h, const void* d, uint16_t l, ble_gatt_attr_fn cb, void* arg) { (void)c; (void)h; (void)d; (void)l; (void)cb; (void)arg; return 0; }
static inline int ble_gattc_disc_all_dscs(uint16_t c, uint16_t s, uint16_t e, ble_gatt_dsc_fn cb, void* arg) { (void)c; (void)s; (void)e; (void)cb; (void)arg; return 0; }
static inline int ble_gattc_read(uint16_t c, uint16_t h, ble_gatt_attr_fn cb, void* arg) { (void)c; (void)h; (void)cb; (void)arg; return 0; }
