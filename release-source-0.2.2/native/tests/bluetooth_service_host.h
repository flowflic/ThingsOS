// SPDX-License-Identifier: Apache-2.0
/* Deterministic UART/NVS/RTOS edges for the production Bluetooth service. */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
typedef int esp_err_t;typedef int portMUX_TYPE;typedef void* SemaphoreHandle_t;typedef void* TaskHandle_t;
typedef struct {unsigned size,count;unsigned char data[2][160];} host_bt_queue_t;
typedef host_bt_queue_t* QueueHandle_t;typedef int nvs_handle_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define NVS_READONLY 0
#define NVS_READWRITE 1
#define pdTRUE 1
#define pdPASS 1
#define portMAX_DELAY UINT32_MAX
#define portMUX_INITIALIZER_UNLOCKED 0
#define pdMS_TO_TICKS(x) (x)
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGD(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
static uint64_t host_bt_time;
static bool host_bt_ack=true,host_bt_nvs_fail,host_bt_task_fail,host_bt_audio_active,host_bt_audio_suspended,host_bt_wireless;
static uint8_t host_bt_store[1024];static size_t host_bt_store_bytes;
static char host_bt_uart[8192],host_bt_last_command[64];static size_t host_bt_uart_used;static unsigned host_bt_inquiries;
SemaphoreHandle_t thingsos_io_lock=(void*)1;
static int64_t esp_timer_get_time(void){return (int64_t)host_bt_time*1000;}
static void vTaskDelay(unsigned ticks){host_bt_time+=ticks;}
static SemaphoreHandle_t xSemaphoreCreateMutex(void){return (void*)1;}
static int xSemaphoreTake(SemaphoreHandle_t handle,unsigned ticks){(void)handle;(void)ticks;return pdTRUE;}
static void xSemaphoreGive(SemaphoreHandle_t handle){(void)handle;}
static QueueHandle_t xQueueCreate(unsigned count,size_t size){if(count!=2||size>160)return NULL;host_bt_queue_t* q=calloc(1,sizeof(*q));if(q)q->size=(unsigned)size;return q;}
static int xQueueSend(QueueHandle_t q,const void* data,unsigned ticks){(void)ticks;if(q->count==2)return 0;memcpy(q->data[q->count++],data,q->size);return pdPASS;}
static int xQueueReceive(QueueHandle_t q,void* out,unsigned ticks){host_bt_time+=ticks;if(!q->count)return 0;memcpy(out,q->data[0],q->size);--q->count;memmove(q->data,q->data+1,q->count*sizeof(q->data[0]));return pdTRUE;}
static unsigned uxQueueMessagesWaiting(QueueHandle_t q){return q->count;}
static void xQueueReset(QueueHandle_t q){q->count=0;}
static int xTaskCreate(void(*fn)(void*),const char* name,unsigned stack,void* arg,unsigned priority,TaskHandle_t* out){(void)fn;(void)name;(void)stack;(void)arg;(void)priority;if(host_bt_task_fail)return 0;*out=(void*)1;return pdPASS;}
static void vTaskDelete(void* handle){(void)handle;}
static uint32_t esp_rom_crc32_le(uint32_t seed,const uint8_t* bytes,size_t count){uint32_t c=~seed;for(size_t i=0;i<count;++i){c^=bytes[i];for(unsigned j=0;j<8;++j)c=(c>>1)^(0xedb88320u&-(int32_t)(c&1));}return ~c;}
static int nvs_open(const char* space,int mode,nvs_handle_t* out){(void)mode;if(strcmp(space,"thingsos-bt"))return ESP_FAIL;*out=1;return ESP_OK;}
static void nvs_close(nvs_handle_t handle){(void)handle;}
static int nvs_set_blob(nvs_handle_t handle,const char* key,const void* bytes,size_t n){(void)handle;if(host_bt_nvs_fail||strcmp(key,"devices")||n>sizeof(host_bt_store))return ESP_FAIL;memcpy(host_bt_store,bytes,n);host_bt_store_bytes=n;return ESP_OK;}
static int nvs_get_blob(nvs_handle_t handle,const char* key,void* bytes,size_t* n){(void)handle;if(strcmp(key,"devices")||!host_bt_store_bytes||*n<host_bt_store_bytes)return ESP_FAIL;memcpy(bytes,host_bt_store,host_bt_store_bytes);*n=host_bt_store_bytes;return ESP_OK;}
static int nvs_commit(nvs_handle_t handle){(void)handle;return host_bt_nvs_fail?ESP_FAIL:ESP_OK;}
bool thingsos_audio_active(void){return host_bt_audio_active;}
bool thingsos_audio_route_idle(void){return !host_bt_audio_active||host_bt_audio_suspended;}
void metalio_audio_wireless(bool connected){host_bt_wireless=connected;}
static unsigned host_bt_mode_commands;
static int metalio_bh_send(const char* text){snprintf(host_bt_last_command,sizeof(host_bt_last_command),"%s",text);
 if(!strncmp(text,"AT+MODE=",8))++host_bt_mode_commands;
 if(!strcmp(text,"AT+INQUIRING\r\n"))++host_bt_inquiries;
 if(host_bt_ack&&(!strcmp(text,"AT+MODE=1\r\n")||!strcmp(text,"AT+MODE=2\r\n"))){const char* response=text[8]=='1'?"SET MODE 1\r\n":"SET MODE 2\r\n";size_t n=strlen(response);memcpy(host_bt_uart+host_bt_uart_used,response,n);host_bt_uart_used+=n;}
 return ESP_OK;
}
static int metalio_bh_read(void* bytes,size_t count,unsigned timeout){host_bt_time+=timeout;if(count>host_bt_uart_used)count=host_bt_uart_used;memcpy(bytes,host_bt_uart,count);host_bt_uart_used-=count;memmove(host_bt_uart,host_bt_uart+count,host_bt_uart_used);return (int)count;}
