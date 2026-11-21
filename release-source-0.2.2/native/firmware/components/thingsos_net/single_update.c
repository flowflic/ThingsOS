// SPDX-License-Identifier: Apache-2.0
#include "single_update.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef THINGSOS_SINGLE_TEST
#include "single_update_stubs.h"
#else
#include "esp_app_format.h"
#include "esp_app_desc.h"
#include "esp_ota_ops.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "psa/crypto.h"
#include "thingsos/diagnostics.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif
typedef struct {uint32_t magic,bytes;uint8_t hash[32];char version[32];} intent_t;
#define INTENT_MAGIC 0x31505553u
static esp_err_t open_intent(nvs_handle_t* h) {
    esp_err_t e=nvs_flash_init();
    return e==ESP_OK?nvs_open("thingsos-update",NVS_READWRITE,h):e;
}
void thingsos_single_clear_intent(void) {
    nvs_handle_t h;
    if(open_intent(&h)==ESP_OK){nvs_erase_key(h,"intent");nvs_commit(h);nvs_close(h);}
}
static const esp_partition_t* main_partition(void) {
    const esp_partition_t* p=esp_partition_find_first(ESP_PARTITION_TYPE_APP,ESP_PARTITION_SUBTYPE_APP_OTA_0,"ota_0");
    return p&&p->address==0x220000&&p->size==THINGSOS_SINGLE_CAPACITY?p:NULL;
}
static esp_err_t check(FILE* f,const intent_t* intent,uint8_t* block) {
    struct stat st;
    if(fstat(fileno(f),&st)||st.st_size!=intent->bytes||intent->bytes<288||intent->bytes>THINGSOS_SINGLE_CAPACITY)return ESP_ERR_INVALID_SIZE;
    psa_hash_operation_t sha=PSA_HASH_OPERATION_INIT;
    bool ok=psa_crypto_init()==PSA_SUCCESS&&psa_hash_setup(&sha,PSA_ALG_SHA_256)==PSA_SUCCESS;
    size_t at=0;
    while(ok&&at<intent->bytes) {
        size_t n=intent->bytes-at;if(n>8192)n=8192;
        ok=fread(block,1,n,f)==n;
        if(ok&&!at) {
            esp_image_header_t image;esp_app_desc_t app;
            memcpy(&image,block,sizeof(image));memcpy(&app,block+sizeof(image)+sizeof(esp_image_segment_header_t),sizeof(app));
            ok=image.magic==ESP_IMAGE_HEADER_MAGIC&&image.chip_id==CONFIG_IDF_FIRMWARE_CHIP_ID&&app.magic_word==ESP_APP_DESC_MAGIC_WORD&&
                memchr(app.project_name,0,sizeof(app.project_name))&&!strcmp(app.project_name,"ThingsOS_native")&&
                memchr(app.version,0,sizeof(app.version))&&(!intent->version[0]||!strcmp(app.version,intent->version));
        }
        if(ok)ok=psa_hash_update(&sha,block,n)==PSA_SUCCESS;
        at+=n;vTaskDelay(pdMS_TO_TICKS(1));
    }
    uint8_t digest[32];size_t size=0;
    if(ok)ok=psa_hash_finish(&sha,digest,32,&size)==PSA_SUCCESS&&size==32&&!memcmp(digest,intent->hash,32);
    psa_hash_abort(&sha);rewind(f);
    return ok?ESP_OK:ESP_ERR_INVALID_CRC;
}
esp_err_t thingsos_single_stage(size_t bytes,const uint8_t hash[32],const char* version) {
    const esp_partition_t* running=esp_ota_get_running_partition();
    const esp_partition_t* rescue=esp_partition_find_first(ESP_PARTITION_TYPE_APP,ESP_PARTITION_SUBTYPE_APP_FACTORY,"rescue");
    esp_app_desc_t description;
    if(!main_partition()||!running||running->subtype!=ESP_PARTITION_SUBTYPE_APP_OTA_0||!rescue||
       esp_ota_get_partition_description(rescue,&description)!=ESP_OK||strcmp(description.project_name,"ThingsOS_rescue"))return ESP_ERR_INVALID_STATE;
    if(bytes>THINGSOS_SINGLE_CAPACITY||bytes<288||!hash||(version&&strlen(version)>=32))return ESP_ERR_INVALID_SIZE;
    intent_t intent={.magic=INTENT_MAGIC,.bytes=(uint32_t)bytes};memcpy(intent.hash,hash,32);
    if(version)strcpy(intent.version,version);
    uint8_t* block=heap_caps_malloc(8192,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    FILE* f=block?fopen(THINGSOS_UPDATE_FILE,"rb"):NULL;
    esp_err_t e=f?check(f,&intent,block):ESP_ERR_NOT_FOUND;
    if(f)fclose(f);
    free(block);
    if(e!=ESP_OK)return e;
    nvs_handle_t h;e=open_intent(&h);if(e!=ESP_OK)return e;
    e=nvs_set_blob(h,"intent",&intent,sizeof(intent));if(e==ESP_OK)e=nvs_commit(h);
    if(e==ESP_OK)e=esp_ota_set_boot_partition(rescue);
    if(e!=ESP_OK){nvs_erase_key(h,"intent");nvs_commit(h);}
    nvs_close(h);
#ifndef THINGSOS_SINGLE_TEST
    if(e==ESP_OK){thingsos_diag_reason(DIAG_REASON_UPDATE,0);thingsos_diag_poll(true);}
#endif
    ESP_LOGI("ThingsOS_single","SD package staged: bytes=%u result=%s; rescue selected, main unchanged",(unsigned)bytes,esp_err_to_name(e));
    return e;
}
esp_err_t thingsos_single_install(void) {
    nvs_handle_t h;esp_err_t e=open_intent(&h);if(e!=ESP_OK)return e;
    intent_t intent={0};size_t size=sizeof(intent);e=nvs_get_blob(h,"intent",&intent,&size);nvs_close(h);
    if(e!=ESP_OK)return e;
#ifndef THINGSOS_SINGLE_TEST
    thingsos_diag_event(DIAG_INSTALL,1,0,0,0,0);
#endif
    const esp_partition_t *running=esp_ota_get_running_partition(),*target=main_partition();
    if(size!=sizeof(intent)||intent.magic!=INTENT_MAGIC||!memchr(intent.version,0,32)||!target||!running||
       running->subtype!=ESP_PARTITION_SUBTYPE_APP_FACTORY||running->address==target->address)return ESP_ERR_INVALID_STATE;
    uint8_t* block=heap_caps_malloc(8192,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    FILE* f=block?fopen(THINGSOS_UPDATE_FILE,"rb"):NULL;
    e=f?check(f,&intent,block):ESP_ERR_NOT_FOUND;
    esp_ota_handle_t handle=0;
    /* Rescue remains the selected boot partition throughout erase/write and
       verification, including after power loss. Never touch NVS/font/rescue. */
    if(e==ESP_OK)e=esp_ota_set_boot_partition(running);
    if(e==ESP_OK)e=esp_ota_begin(target,intent.bytes,&handle);
    size_t at=0;unsigned last=101;
    while(e==ESP_OK&&at<intent.bytes) {
        size_t n=intent.bytes-at;if(n>8192)n=8192;
        e=fread(block,1,n,f)==n?esp_ota_write(handle,block,n):ESP_FAIL;at+=n;
        unsigned percent=(unsigned)(at*100/intent.bytes);
        if(percent/10!=last/10){ESP_LOGI("ThingsOS_single","Installing main: %u%%",percent);last=percent;}
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    if(f)fclose(f);
    free(block);
    if(e==ESP_OK){e=esp_ota_end(handle);handle=0;}
    if(handle)esp_ota_abort(handle);
    /* esp_ota_end validates the full image, but also compare readback digest
       to the staged download before making the sole app bootable. */
    if(e==ESP_OK) {
        psa_hash_operation_t sha=PSA_HASH_OPERATION_INIT;uint8_t digest[32];size_t n=0;
        uint8_t* b=heap_caps_malloc(8192,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        bool ok=b&&psa_hash_setup(&sha,PSA_ALG_SHA_256)==PSA_SUCCESS;
        for(size_t pos=0;ok&&pos<intent.bytes;) {
            size_t take=intent.bytes-pos;if(take>8192)take=8192;
            ok=esp_partition_read(target,pos,b,take)==ESP_OK&&psa_hash_update(&sha,b,take)==PSA_SUCCESS;pos+=take;
            vTaskDelay(pdMS_TO_TICKS(1));
        }
        if(ok)ok=psa_hash_finish(&sha,digest,32,&n)==PSA_SUCCESS&&n==32&&!memcmp(digest,intent.hash,32);
        psa_hash_abort(&sha);free(b);if(!ok)e=ESP_ERR_INVALID_CRC;
    }
    if(e==ESP_OK)e=esp_ota_set_boot_partition(target);
    /* A stale intent after a cut between selection and clearing is harmless:
       main never installs it, and a later rescue visit re-verifies it. */
    if(e==ESP_OK&&open_intent(&h)==ESP_OK){nvs_erase_key(h,"intent");nvs_commit(h);nvs_close(h);}
#ifndef THINGSOS_SINGLE_TEST
    thingsos_diag_event(DIAG_INSTALL,2,(unsigned)at,intent.bytes,e,0);
    if(e!=ESP_OK)thingsos_diag_reason(DIAG_REASON_INSTALL_FAILED,e);
    thingsos_diag_poll(true);
#endif
    ESP_LOGI("ThingsOS_single","Install result=%s bytes=%u",esp_err_to_name(e),(unsigned)at);
    return e;
}
