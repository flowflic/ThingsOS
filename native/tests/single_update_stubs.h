// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE 1
#define ESP_ERR_INVALID_SIZE 2
#define ESP_ERR_NOT_FOUND 3
#define ESP_ERR_INVALID_CRC 4
#define ESP_IMAGE_HEADER_MAGIC 0xe9
#define CONFIG_IDF_FIRMWARE_CHIP_ID 0x20 /* ESP32-S31 */
#define ESP_APP_DESC_MAGIC_WORD 0xabcd5432
#define ESP_PARTITION_TYPE_APP 0
#define ESP_PARTITION_SUBTYPE_APP_FACTORY 0
#define ESP_PARTITION_SUBTYPE_APP_OTA_0 16
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define NVS_READWRITE 1
#define ESP_LOGI(...) ((void)0)
#define pdMS_TO_TICKS(x) (x)
#define vTaskDelay(x) ((void)(x))
#define heap_caps_malloc(n,c) malloc(n)
#define PSA_SUCCESS 0
#define PSA_ALG_SHA_256 1
typedef struct {uint8_t magic,pad[11];uint16_t chip_id;uint8_t rest[10];} esp_image_header_t;
typedef struct {uint32_t a,b;} esp_image_segment_header_t;
typedef struct {uint32_t magic_word,secure;uint8_t reserve[8];char version[32],project_name[32];uint8_t tail[176];} esp_app_desc_t;
typedef struct {int subtype;uint32_t address,size;} esp_partition_t;
typedef unsigned esp_ota_handle_t;
typedef int nvs_handle_t;
typedef struct {uint8_t hash[32];size_t at;} psa_hash_operation_t;
#define PSA_HASH_OPERATION_INIT {0}
static uint8_t persisted[128],flash_data[32768];
static size_t persisted_size,written;
static unsigned erases,writes;
static int fail_write,fail_end,corrupt_read,fail_commit;
static esp_partition_t app_partition={16,0x220000,0xae0000},rescue_partition={0,0x20000,0x200000};
static const esp_partition_t *running_partition=&app_partition,*selected_partition=&app_partition;
static const esp_partition_t* esp_ota_get_running_partition(void){return running_partition;}
static const esp_partition_t* esp_partition_find_first(int type,int sub,const char* name){(void)type;(void)name;return sub==16?&app_partition:&rescue_partition;}
static int esp_ota_get_partition_description(const esp_partition_t* p,esp_app_desc_t* out){(void)p;memset(out,0,sizeof(*out));strcpy(out->project_name,"ThingsOS_rescue");return 0;}
static int esp_ota_set_boot_partition(const esp_partition_t* p){selected_partition=p;return 0;}
static int nvs_flash_init(void){return 0;}
static int nvs_open(const char* ns,int mode,nvs_handle_t* h){(void)ns;(void)mode;*h=1;return 0;}
static int nvs_set_blob(nvs_handle_t h,const char* key,const void* p,size_t n){(void)h;(void)key;assert(n<=sizeof(persisted));memcpy(persisted,p,n);persisted_size=n;return 0;}
static int nvs_get_blob(nvs_handle_t h,const char* key,void* p,size_t* n){(void)h;(void)key;if(!persisted_size)return ESP_ERR_NOT_FOUND;assert(*n>=persisted_size);memcpy(p,persisted,persisted_size);*n=persisted_size;return 0;}
static int nvs_erase_key(nvs_handle_t h,const char* key){(void)h;(void)key;persisted_size=0;return 0;}
static int nvs_commit(nvs_handle_t h){(void)h;return fail_commit?ESP_FAIL:0;}
static void nvs_close(nvs_handle_t h){(void)h;}
static int psa_crypto_init(void){return 0;}
/* Deterministic digest double tests byte flow and failure ordering. Production
   cryptography is PSA SHA-256; existing SHA tests cover real known vectors. */
static int psa_hash_setup(psa_hash_operation_t* s,int alg){(void)alg;memset(s,0,sizeof(*s));return 0;}
static int psa_hash_update(psa_hash_operation_t* s,const uint8_t* p,size_t n){for(size_t i=0;i<n;++i){size_t j=s->at++%32;s->hash[j]=(uint8_t)(s->hash[j]*33+p[i]+j);}return 0;}
static int psa_hash_finish(psa_hash_operation_t* s,uint8_t* p,size_t cap,size_t* n){assert(cap>=32);memcpy(p,s->hash,32);*n=32;return 0;}
static int psa_hash_abort(psa_hash_operation_t* s){(void)s;return 0;}
static int esp_ota_begin(const esp_partition_t* p,size_t n,esp_ota_handle_t* h){assert(running_partition==&rescue_partition&&selected_partition==&rescue_partition&&p==&app_partition);assert(n<=sizeof(flash_data));++erases;written=0;memset(flash_data,255,sizeof(flash_data));*h=1;return 0;}
static int esp_ota_write(esp_ota_handle_t h,const void* p,size_t n){assert(h);++writes;if(fail_write)return ESP_FAIL;memcpy(flash_data+written,p,n);written+=n;return 0;}
static int esp_ota_end(esp_ota_handle_t h){assert(h);return fail_end?ESP_FAIL:0;}
static int esp_ota_abort(esp_ota_handle_t h){assert(h);return 0;}
static int esp_partition_read(const esp_partition_t* p,size_t at,void* b,size_t n){assert(p==&app_partition);memcpy(b,flash_data+at,n);if(corrupt_read)((uint8_t*)b)[0]^=1;return 0;}
