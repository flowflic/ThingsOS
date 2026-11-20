// SPDX-License-Identifier: Apache-2.0
/* User-selected persistent local credentials. No device unlock password.
   HTTPS is still used for the device's requests to ima cloud. */
#include "secure.h"
#ifdef THINGSOS_IMA_VAULT_HOST
#include "ima_vault_host.h"
#else
#include "network.h"
#include "nvs.h"
#include "psa/crypto.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
void ima_wipe(void* p,size_t n){volatile uint8_t* b=p;while(n--)*b++=0;}
static bool hash(psa_algorithm_t a,const void* p,size_t n,uint8_t* out,size_t size){size_t wrote=0;return psa_hash_compute(a,p,n,out,size,&wrote)==PSA_SUCCESS&&wrote==size;}
bool ima_sha1(const void* p,size_t n,unsigned char out[20]){return hash(PSA_ALG_SHA_1,p,n,out,20);}
static bool import_hmac(const void* k,size_t n,psa_algorithm_t alg,mbedtls_svc_key_id_t* id){
    psa_key_attributes_t a=PSA_KEY_ATTRIBUTES_INIT;psa_set_key_type(&a,PSA_KEY_TYPE_HMAC);psa_set_key_usage_flags(&a,PSA_KEY_USAGE_SIGN_MESSAGE);psa_set_key_algorithm(&a,alg);
    psa_status_t e=psa_import_key(&a,k,n,id);psa_reset_key_attributes(&a);return e==PSA_SUCCESS;
}
bool ima_hmac1(const void* k,size_t kn,const void* p,size_t n,unsigned char out[20]){mbedtls_svc_key_id_t id;size_t wrote=0;if(!import_hmac(k,kn,PSA_ALG_HMAC(PSA_ALG_SHA_1),&id))return false;psa_status_t e=psa_mac_compute(id,PSA_ALG_HMAC(PSA_ALG_SHA_1),p,n,out,20,&wrote);psa_destroy_key(id);return e==PSA_SUCCESS&&wrote==20;}

/* Version and digest detect corrupt records; this is not encryption. */
typedef struct {uint32_t magic;ima_config_t config;uint8_t digest[32];} connection_t;
static bool config_valid(const ima_config_t* c){
    return c&&c->client[0]&&c->key[0]&&memchr(c->client,0,sizeof(c->client))&&
        memchr(c->key,0,sizeof(c->key))&&memchr(c->kb,0,sizeof(c->kb))&&memchr(c->name,0,sizeof(c->name));
}
bool ima_config_load(ima_config_t* out){
    if(!out)return false;memset(out,0,sizeof(*out));
    connection_t* record=calloc(1,sizeof(*record));if(!record)return false;
    nvs_handle_t h;bool ok=nvs_open("thingsos-ima",NVS_READONLY,&h)==ESP_OK;
    if(ok){size_t n=sizeof(*record);ok=nvs_get_blob(h,"connection",record,&n)==ESP_OK&&n==sizeof(*record);nvs_close(h);}
    uint8_t digest[32];ok=ok&&record->magic==0x494d4133&&config_valid(&record->config)&&
        hash(PSA_ALG_SHA_256,&record->config,sizeof(record->config),digest,sizeof(digest))&&!memcmp(digest,record->digest,32);
    if(ok)*out=record->config;ima_wipe(digest,sizeof(digest));ima_wipe(record,sizeof(*record));free(record);return ok;
}
bool ima_config_save(const ima_config_t* config){
    if(!config_valid(config))return false;
    connection_t* record=calloc(1,sizeof(*record));if(!record)return false;
    record->magic=0x494d4133;record->config=*config;
    bool ok=hash(PSA_ALG_SHA_256,&record->config,sizeof(record->config),record->digest,32);
    bool locked=thingsos_io_lock&&xSemaphoreGetMutexHolder(thingsos_io_lock)!=xTaskGetCurrentTaskHandle();
    if(locked)xSemaphoreTake(thingsos_io_lock,portMAX_DELAY);
    nvs_handle_t h;if(ok){ok=nvs_open("thingsos-ima",NVS_READWRITE,&h)==ESP_OK;if(ok){
        ok=nvs_set_blob(h,"connection",record,sizeof(*record))==ESP_OK&&nvs_commit(h)==ESP_OK;
        if(ok){nvs_erase_key(h,"sealed");nvs_commit(h);} /* Obsolete encrypted format. */
        nvs_close(h);}}
    if(locked)xSemaphoreGive(thingsos_io_lock);ima_wipe(record,sizeof(*record));free(record);return ok;
}
bool ima_config_clear(void){nvs_handle_t h;if(nvs_open("thingsos-ima",NVS_READWRITE,&h)!=ESP_OK)return false;bool ok=nvs_erase_all(h)==ESP_OK&&nvs_commit(h)==ESP_OK;nvs_close(h);return ok;}
