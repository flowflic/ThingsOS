/* SPDX-License-Identifier: Apache-2.0 */
/* Deterministic SDK/RTOS boundaries; the central and GATT callbacks under test
   are the production implementation. No radio success is inferred from this. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
typedef int esp_err_t,portMUX_TYPE,nvs_handle_t;typedef void* SemaphoreHandle_t;typedef void* TaskHandle_t;
typedef struct {unsigned size,capacity,count;uint8_t* data;} host_ble_queue_t;typedef host_ble_queue_t* QueueHandle_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define NVS_READWRITE 1
#define NVS_READONLY 0
#define pdTRUE 1
#define pdPASS 1
#define pdMS_TO_TICKS(x) (x)
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define MALLOC_CAP_INTERNAL 4
#define MALLOC_CAP_DMA 8
#define CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE 4096
#define configMAX_PRIORITIES 25
#define BLE_HS_CONN_HANDLE_NONE 0xffff
#define BLE_HS_ESTORE_FAIL 28
#define BLE_HS_ENOMEM 6
#define BLE_HS_EAPP 9
#define BLE_HS_EDONE 14
#define BLE_HS_EALREADY 2
#define BLE_HS_EBUSY 15
#define BLE_ERR_REM_USER_CONN_TERM 0x13
#define BLE_HS_IO_DISPLAY_YESNO 1
#define BLE_SM_PAIR_KEY_DIST_ENC 1
#define BLE_SM_PAIR_KEY_DIST_ID 2
#define BLE_SM_IOACT_NONE 0
#define BLE_SM_IOACT_DISP 3
#define BLE_SM_IOACT_INPUT 2
#define BLE_SM_IOACT_NUMCMP 4
#define BLE_GAP_REPEAT_PAIRING_IGNORE 2
enum {BLE_GAP_EVENT_DISC=1,BLE_GAP_EVENT_DISC_COMPLETE,BLE_GAP_EVENT_CONNECT,BLE_GAP_EVENT_DISCONNECT,BLE_GAP_EVENT_ENC_CHANGE,BLE_GAP_EVENT_PASSKEY_ACTION,BLE_GAP_EVENT_REPEAT_PAIRING,BLE_GAP_EVENT_NOTIFY_RX};
#define BLE_HCI_ADV_RPT_EVTYPE_ADV_IND 0
#define BLE_HCI_ADV_RPT_EVTYPE_DIR_IND 1
typedef struct {uint8_t type,val[6];} ble_addr_t;
struct os_mbuf {uint16_t len;uint8_t data[260];};
#define OS_MBUF_PKTLEN(m) ((m)->len)
struct ble_gap_conn_desc {ble_addr_t peer_id_addr,peer_ota_addr;struct {bool encrypted,bonded,authenticated;} sec_state;};
struct ble_gap_disc_desc {ble_addr_t addr;int8_t rssi;uint8_t event_type;uint8_t* data;uint8_t length_data;};
struct ble_gap_event {int type;union {
 struct ble_gap_disc_desc disc;struct {int reason;} disc_complete;
 struct {int status;uint16_t conn_handle;} connect;
 struct {struct {uint16_t conn_handle;} conn;int reason;} disconnect;
 struct {int status;uint16_t conn_handle;} enc_change;
 struct {uint16_t conn_handle;struct {uint8_t action;uint32_t numcmp;} params;} passkey;
 struct {uint16_t conn_handle,attr_handle;struct os_mbuf* om;bool indication;} notify_rx;
};};
struct ble_gap_disc_params {uint16_t itvl,window;uint8_t passive,filter_duplicates,filter_policy,limited;};
typedef struct {uint8_t type;} ble_uuid_t;typedef struct {ble_uuid_t u;uint16_t value;} ble_uuid16_t;
typedef union {ble_uuid_t u;ble_uuid16_t u16;} ble_uuid_any_t;
#define BLE_UUID16_INIT(x) {{16},x}
static uint16_t ble_uuid_u16(const ble_uuid_t* u){return u->type==16?((const ble_uuid16_t*)u)->value:0;}
struct ble_gatt_error {int status;uint16_t att_handle;};
struct ble_gatt_attr {uint16_t handle,offset;struct os_mbuf* om;};
struct ble_gatt_svc {uint16_t start_handle,end_handle;ble_uuid_any_t uuid;};
struct ble_gatt_chr {uint16_t def_handle,val_handle;uint8_t properties;ble_uuid_any_t uuid;};
struct ble_gatt_dsc {uint16_t handle;ble_uuid_any_t uuid;};
typedef int ble_gatt_disc_svc_fn(uint16_t,const struct ble_gatt_error*,const struct ble_gatt_svc*,void*);
typedef int ble_gatt_chr_fn(uint16_t,const struct ble_gatt_error*,const struct ble_gatt_chr*,void*);
typedef int ble_gatt_dsc_fn(uint16_t,const struct ble_gatt_error*,uint16_t,const struct ble_gatt_dsc*,void*);
typedef int ble_gatt_attr_fn(uint16_t,const struct ble_gatt_error*,struct ble_gatt_attr*,void*);
struct ble_sm_io {uint8_t action;union {uint32_t passkey;uint8_t numcmp_accept,oob[16];};};
union ble_store_key {int dummy;};union ble_store_value {int dummy;};struct ble_store_status_event {int dummy;};
typedef int ble_store_read_fn(int,const union ble_store_key*,union ble_store_value*);
typedef int ble_store_write_fn(int,const union ble_store_value*);
typedef int ble_store_delete_fn(int,const union ble_store_key*);
struct {void(*sync_cb)(void);void(*reset_cb)(int);uint8_t sm_io_cap,sm_bonding,sm_sc,sm_mitm,sm_our_key_dist,sm_their_key_dist;
 ble_store_read_fn* store_read_cb;ble_store_write_fn* store_write_cb;ble_store_delete_fn* store_delete_cb;int(*store_status_cb)(struct ble_store_status_event*,void*);} ble_hs_cfg;
struct ble_npl_event {int dummy;};struct ble_npl_callout {void(*fn)(struct ble_npl_event*);};
static uint64_t host_ble_time=1000;static bool host_ble_io=true,host_ble_io_held,host_ble_nvs_fail,host_ble_task_fail;
static unsigned host_ble_connects,host_ble_terminates,host_ble_cancels,host_ble_injections,host_ble_writes,host_ble_deletes;
static unsigned host_ble_timer_resets;
static int host_ble_radio_rc,host_ble_security_rc,host_ble_gatt_rc;static uint16_t host_ble_conn=7;
static struct ble_gap_conn_desc host_ble_peer;static struct ble_sm_io host_ble_sm_io;
static uint16_t host_ble_last_terminated,host_ble_attribute,host_ble_end;static uint8_t host_ble_write[4];static size_t host_ble_write_len;
static void* host_ble_scan_arg;static void* host_ble_connect_arg;static void* host_ble_gatt_arg;
static ble_gatt_disc_svc_fn* host_ble_service_cb;static ble_gatt_chr_fn* host_ble_chr_cb;static ble_gatt_dsc_fn* host_ble_dsc_cb;static ble_gatt_attr_fn* host_ble_attr_cb;
static uint8_t host_ble_blob[2048];static size_t host_ble_blob_size;
static void(*host_ble_initializer)(void*);
SemaphoreHandle_t thingsos_io_lock=(void*)1;
static int64_t esp_timer_get_time(void){return (int64_t)host_ble_time*1000;}
static uint32_t esp_random(void){return 537201;}
static void* heap_caps_calloc(size_t n,size_t s,unsigned caps){(void)caps;return calloc(n,s);}
static size_t host_ble_dma_free=96u*1024u,host_ble_dma_largest=48u*1024u;
static unsigned host_ble_port_inits,host_ble_port_deinits;static bool host_ble_host_task_fail;
static size_t heap_caps_get_free_size(unsigned caps){return (caps&MALLOC_CAP_DMA)?host_ble_dma_free:host_ble_dma_free+30u*1024u;}
static size_t heap_caps_get_largest_free_block(unsigned caps){(void)caps;return host_ble_dma_largest;}
static TaskHandle_t xTaskGetCurrentTaskHandle(void){return (void*)2;}
static TaskHandle_t xSemaphoreGetMutexHolder(SemaphoreHandle_t s){(void)s;return host_ble_io_held?(void*)2:NULL;}
static int xSemaphoreTake(SemaphoreHandle_t s,unsigned ticks){(void)s;(void)ticks;if(!host_ble_io)return 0;host_ble_io_held=true;return pdTRUE;}
static void xSemaphoreGive(SemaphoreHandle_t s){(void)s;host_ble_io_held=false;}
static QueueHandle_t xQueueCreate(unsigned capacity,size_t size){QueueHandle_t q=calloc(1,sizeof(*q));if(q){q->capacity=capacity;q->size=(unsigned)size;q->data=calloc(capacity,size);}return q;}
static int xQueueSend(QueueHandle_t q,const void* value,unsigned wait){(void)wait;if(q->count==q->capacity)return 0;memcpy(q->data+q->count++*q->size,value,q->size);return pdPASS;}
static int xQueueReceive(QueueHandle_t q,void* value,unsigned wait){(void)wait;if(!q->count)return 0;memcpy(value,q->data,q->size);--q->count;memmove(q->data,q->data+q->size,q->count*q->size);return pdTRUE;}
static void xQueueReset(QueueHandle_t q){if(q)q->count=0;}
static int xTaskCreate(void(*fn)(void*),const char* name,unsigned stack,void* arg,unsigned pri,TaskHandle_t* out){(void)stack;(void)arg;(void)pri;if(host_ble_task_fail)return 0;if(!strcmp(name,"ble_init"))host_ble_initializer=fn;else if(host_ble_host_task_fail)return 0;if(out)*out=(void*)2;return pdPASS;}
static void vTaskDelete(void* task){(void)task;}
static int nvs_open(const char* ns,int mode,nvs_handle_t* h){(void)mode;if(strcmp(ns,"thingsos-ble")||!host_ble_io_held)return ESP_FAIL;*h=1;return ESP_OK;}
static int nvs_get_blob(nvs_handle_t h,const char* key,void* out,size_t* size){(void)h;if(strcmp(key,"devices")||!host_ble_blob_size||*size<host_ble_blob_size)return ESP_FAIL;memcpy(out,host_ble_blob,host_ble_blob_size);*size=host_ble_blob_size;return ESP_OK;}
static int nvs_set_blob(nvs_handle_t h,const char* key,const void* data,size_t size){(void)h;if(host_ble_nvs_fail||strcmp(key,"devices")||size>sizeof(host_ble_blob))return ESP_FAIL;memcpy(host_ble_blob,data,size);host_ble_blob_size=size;return ESP_OK;}
static int nvs_commit(nvs_handle_t h){(void)h;return host_ble_nvs_fail?ESP_FAIL:ESP_OK;}
static void nvs_close(nvs_handle_t h){(void)h;}
static uint32_t esp_rom_crc32_le(uint32_t seed,const uint8_t* b,size_t n){uint32_t c=~seed;for(size_t i=0;i<n;++i){c^=b[i];for(unsigned j=0;j<8;++j)c=(c>>1)^(0xedb88320u&-(int32_t)(c&1));}return ~c;}
static int host_store_read(int type,const union ble_store_key* key,union ble_store_value* value){(void)type;(void)key;(void)value;return host_ble_io_held?0:BLE_HS_ESTORE_FAIL;}
static int host_store_write(int type,const union ble_store_value* value){(void)type;(void)value;++host_ble_writes;return host_ble_io_held?0:BLE_HS_ESTORE_FAIL;}
static int host_store_delete(int type,const union ble_store_key* key){(void)type;(void)key;++host_ble_deletes;return host_ble_io_held?0:BLE_HS_ESTORE_FAIL;}
static void ble_store_config_init(void){ble_hs_cfg.store_read_cb=host_store_read;ble_hs_cfg.store_write_cb=host_store_write;ble_hs_cfg.store_delete_cb=host_store_delete;}
static int ble_store_util_delete_peer(const ble_addr_t* addr){(void)addr;union ble_store_key key={0};return ble_hs_cfg.store_delete_cb(0,&key);}
static int nimble_port_init(void){++host_ble_port_inits;return 0;}
static int nimble_port_deinit(void){++host_ble_port_deinits;return 0;}
#define ESP_BT_MODE_BLE 1
#ifndef BLE_HS_EUNKNOWN
#define BLE_HS_EUNKNOWN 17
#endif
static unsigned host_ble_enables,host_ble_disables,host_ble_host_inits,host_ble_host_deinits;
static int esp_bt_controller_enable(int mode){(void)mode;++host_ble_enables;return 0;}
static int esp_bt_controller_disable(void){++host_ble_disables;return 0;}
static int esp_nimble_init(void){++host_ble_host_inits;return 0;}
static int esp_nimble_deinit(void){++host_ble_host_deinits;return 0;}
static unsigned host_ble_port_stops;
static int nimble_port_stop(void){++host_ble_port_stops;return 0;}
static void vTaskDelay(unsigned ticks){(void)ticks;}
static void nimble_port_run(void){}
static void nimble_port_freertos_deinit(void){}
static void nimble_port_freertos_init(void(*fn)(void*)){(void)fn;}
static void* nimble_port_get_dflt_eventq(void){return (void*)1;}
static void ble_npl_callout_init(struct ble_npl_callout* t,void* q,void(*fn)(struct ble_npl_event*),void* arg){(void)q;(void)arg;t->fn=fn;}
static void ble_npl_callout_deinit(struct ble_npl_callout* t){t->fn=NULL;}
static int ble_npl_callout_reset(struct ble_npl_callout* t,unsigned ticks){(void)t;(void)ticks;++host_ble_timer_resets;return 0;}
static unsigned ble_npl_time_ms_to_ticks32(unsigned ms){return ms;}
static int ble_hs_util_ensure_addr(int prefer){(void)prefer;return 0;}
static int ble_hs_id_infer_auto(int privacy,uint8_t* type){(void)privacy;*type=0;return 0;}
static int ble_gap_disc(uint8_t own,int32_t duration,const struct ble_gap_disc_params* p,int(*cb)(struct ble_gap_event*,void*),void* arg){(void)own;(void)duration;(void)p;(void)cb;host_ble_scan_arg=arg;return host_ble_radio_rc;}
static int ble_gap_disc_cancel(void){return 0;}
static int ble_gap_conn_cancel(void){++host_ble_cancels;return 0;}
static int ble_gap_terminate(uint16_t conn,uint8_t why){(void)why;++host_ble_terminates;host_ble_last_terminated=conn;return 0;}
static int ble_gap_connect(uint8_t own,const ble_addr_t* peer,int32_t duration,const void* params,int(*cb)(struct ble_gap_event*,void*),void* arg){(void)own;(void)duration;(void)params;(void)cb;++host_ble_connects;host_ble_connect_arg=arg;host_ble_peer.peer_id_addr=*peer;host_ble_peer.peer_ota_addr=*peer;return host_ble_radio_rc;}
static int ble_gap_security_initiate(uint16_t conn){(void)conn;return host_ble_security_rc;}
static int ble_gap_conn_find(uint16_t conn,struct ble_gap_conn_desc* desc){if(conn!=host_ble_conn)return 1;*desc=host_ble_peer;return 0;}
static int ble_sm_inject_io(uint16_t conn,struct ble_sm_io* io){(void)conn;host_ble_sm_io=*io;++host_ble_injections;return 0;}
static int ble_hs_mbuf_to_flat(const struct os_mbuf* om,void* data,uint16_t size,uint16_t* copied){if(size<om->len)return 1;memcpy(data,om->data,om->len);if(copied)*copied=om->len;return 0;}
static int ble_gattc_disc_svc_by_uuid(uint16_t c,const ble_uuid_t* u,ble_gatt_disc_svc_fn* cb,void* arg){(void)c;(void)u;host_ble_service_cb=cb;host_ble_gatt_arg=arg;return host_ble_gatt_rc;}
static int ble_gattc_disc_all_chrs(uint16_t c,uint16_t start,uint16_t end,ble_gatt_chr_fn* cb,void* arg){(void)c;host_ble_attribute=start;host_ble_end=end;host_ble_chr_cb=cb;host_ble_gatt_arg=arg;return host_ble_gatt_rc;}
static int ble_gattc_read_long(uint16_t c,uint16_t handle,uint16_t offset,ble_gatt_attr_fn* cb,void* arg){(void)c;(void)offset;host_ble_attribute=handle;host_ble_attr_cb=cb;host_ble_gatt_arg=arg;return host_ble_gatt_rc;}
static int ble_gattc_read(uint16_t c,uint16_t handle,ble_gatt_attr_fn* cb,void* arg){return ble_gattc_read_long(c,handle,0,cb,arg);}
static int ble_gattc_disc_all_dscs(uint16_t c,uint16_t start,uint16_t end,ble_gatt_dsc_fn* cb,void* arg){(void)c;host_ble_attribute=start;host_ble_end=end;host_ble_dsc_cb=cb;host_ble_gatt_arg=arg;return host_ble_gatt_rc;}
static int ble_gattc_write_no_rsp_flat(uint16_t c,uint16_t handle,const void* data,uint16_t len){(void)c;host_ble_attribute=handle;host_ble_write_len=len;if(len<=sizeof(host_ble_write))memcpy(host_ble_write,data,len);return host_ble_gatt_rc;}
static int ble_gattc_write_flat(uint16_t c,uint16_t handle,const void* data,uint16_t len,ble_gatt_attr_fn* cb,void* arg){host_ble_attr_cb=cb;host_ble_gatt_arg=arg;return ble_gattc_write_no_rsp_flat(c,handle,data,len);}
