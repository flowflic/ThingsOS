// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <direct.h>
#define mkdir(path,mode) _mkdir(path)
#define RTC_NOINIT_ATTR
#define EXT_RAM_BSS_ATTR
#define portMAX_DELAY 0xffffffffu
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 2
#define ESP_ERR_NOT_FINISHED 3
#define ESP_ERR_NVS_NOT_FOUND 4
#define ESP_RST_POWERON 1
#define ESP_PARTITION_SUBTYPE_APP_FACTORY 0
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
#define pdMS_TO_TICKS(n) (n)
#define pdTRUE 1
#define NVS_READWRITE 1
#define NVS_READONLY 0
typedef int portMUX_TYPE,esp_err_t,nvs_handle_t;
typedef void* SemaphoreHandle_t;
typedef struct {char version[32];unsigned char app_elf_sha256[32];} esp_app_desc_t;
typedef struct {unsigned subtype;char label[17];} esp_partition_t;
typedef struct {bool mounted;} thingsos_sd_info_t;
typedef struct {unsigned char custom[16];} rtc_retain_mem_t;
static const char* host_diag_dir;
#define DIAG_STORAGE_DIR host_diag_dir
static unsigned host_reset=ESP_RST_POWERON,host_writes;
static int64_t host_time=1000000;
static int host_media=ESP_ERR_NOT_FINISHED;
static bool host_mounted,host_nvs_fail;
static esp_app_desc_t host_app={"0.1.8",{1,2,3}};
static esp_partition_t host_running={16,"ota_0"},host_selected={16,"ota_0"};
static rtc_retain_mem_t host_rtc;
static unsigned char host_nvs[2048];static size_t host_nvs_bytes;
static int esp_reset_reason(void){return host_reset;}
static int64_t esp_timer_get_time(void){return host_time;}
static void* heap_caps_malloc(size_t n,unsigned caps){(void)caps;return malloc(n);}
static const esp_app_desc_t* esp_app_get_description(void){return &host_app;}
static const esp_partition_t* esp_ota_get_running_partition(void){return &host_running;}
static const esp_partition_t* esp_ota_get_boot_partition(void){return &host_selected;}
static rtc_retain_mem_t* bootloader_common_get_rtc_retain_mem(void){return &host_rtc;}
static int thingsos_board_sd_info(thingsos_sd_info_t* out){out->mounted=host_mounted;return host_media;}
static int nvs_flash_init(void){return 0;}
static int nvs_open(const char* name,int mode,nvs_handle_t* h){(void)name;(void)mode;*h=1;return host_nvs_fail?ESP_FAIL:0;}
static int nvs_get_blob(nvs_handle_t h,const char* key,void* data,size_t* n){(void)h;(void)key;if(!host_nvs_bytes || *n<host_nvs_bytes)return ESP_ERR_NVS_NOT_FOUND;memcpy(data,host_nvs,host_nvs_bytes);*n=host_nvs_bytes;return 0;}
static int nvs_set_blob(nvs_handle_t h,const char* key,const void* data,size_t n){(void)h;(void)key;if(n>sizeof(host_nvs))return ESP_FAIL;memcpy(host_nvs,data,n);host_nvs_bytes=n;++host_writes;return 0;}
static int nvs_commit(nvs_handle_t h){(void)h;return 0;}
static void nvs_close(nvs_handle_t h){(void)h;}
static SemaphoreHandle_t xSemaphoreCreateMutex(void){return (void*)1;}
static int xSemaphoreTake(SemaphoreHandle_t h,unsigned ticks){(void)h;(void)ticks;return 1;}
static void xSemaphoreGive(SemaphoreHandle_t h){(void)h;}
