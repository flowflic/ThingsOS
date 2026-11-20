// SPDX-License-Identifier: Apache-2.0
/* Host adapter for credential persistence. Windows CNG independently supplies
   PSA digest/MAC primitives; the production record format and storage execute. */
#include <windows.h>
#include <bcrypt.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
typedef int psa_status_t;typedef unsigned psa_algorithm_t;typedef void* mbedtls_svc_key_id_t;
typedef struct {unsigned algorithm;} psa_key_attributes_t;
#define PSA_SUCCESS 0
#define PSA_KEY_ATTRIBUTES_INIT {0}
#define PSA_ALG_SHA_1 1
#define PSA_ALG_SHA_256 2
#define PSA_ALG_GCM 3
#define PSA_ALG_HMAC(x) (10+(x))
#define PSA_KEY_TYPE_HMAC 1
#define PSA_KEY_TYPE_AES 2
#define PSA_KEY_USAGE_SIGN_MESSAGE 1
#define PSA_KEY_USAGE_ENCRYPT 2
#define PSA_KEY_USAGE_DECRYPT 4
static void psa_set_key_type(psa_key_attributes_t* a,unsigned t){(void)a;(void)t;}
static void psa_set_key_usage_flags(psa_key_attributes_t* a,unsigned t){(void)a;(void)t;}
static void psa_set_key_bits(psa_key_attributes_t* a,unsigned t){(void)a;(void)t;}
static void psa_set_key_algorithm(psa_key_attributes_t* a,unsigned t){a->algorithm=t;}
static void psa_reset_key_attributes(psa_key_attributes_t* a){a->algorithm=0;}
typedef struct {uint8_t bytes[256];size_t size;unsigned algorithm;} host_key_t;
static int psa_import_key(psa_key_attributes_t* a,const void* key,size_t n,mbedtls_svc_key_id_t* id){if(n>256)return -1;host_key_t* k=calloc(1,sizeof(*k));if(!k)return -1;memcpy(k->bytes,key,n);k->size=n;k->algorithm=a->algorithm;*id=k;return 0;}
static void psa_destroy_key(mbedtls_svc_key_id_t id){host_key_t* k=id;memset(k,0,sizeof(*k));free(k);}
static int host_digest(bool hmac,const void* key,size_t kn,unsigned alg,const void* p,size_t n,uint8_t* out,size_t size){
    BCRYPT_ALG_HANDLE a=NULL;BCRYPT_HASH_HANDLE h=NULL;bool ok=BCryptOpenAlgorithmProvider(&a,alg==PSA_ALG_SHA_1?BCRYPT_SHA1_ALGORITHM:BCRYPT_SHA256_ALGORITHM,NULL,hmac?BCRYPT_ALG_HANDLE_HMAC_FLAG:0)>=0;
    if(ok)ok=BCryptCreateHash(a,&h,NULL,0,(PUCHAR)key,(ULONG)kn,0)>=0;
    if(ok)ok=BCryptHashData(h,(PUCHAR)p,(ULONG)n,0)>=0&&BCryptFinishHash(h,out,(ULONG)size,0)>=0;
    if(h)BCryptDestroyHash(h);if(a)BCryptCloseAlgorithmProvider(a,0);return ok?0:-1;
}
static int psa_hash_compute(unsigned alg,const void* p,size_t n,void* out,size_t cap,size_t* wrote){*wrote=alg==PSA_ALG_SHA_1?20:32;if(cap<*wrote)return -1;return host_digest(false,NULL,0,alg,p,n,out,*wrote);}
static int psa_mac_compute(mbedtls_svc_key_id_t id,unsigned alg,const void* p,size_t n,void* out,size_t cap,size_t* wrote){host_key_t* k=id;*wrote=alg==PSA_ALG_HMAC(PSA_ALG_SHA_1)?20:32;if(cap<*wrote)return -1;return host_digest(true,k->bytes,k->size,alg-10,p,n,out,*wrote);}
static int host_aead(mbedtls_svc_key_id_t id,bool encrypt,const uint8_t* nonce,size_t nn,const uint8_t* aad,size_t an,const uint8_t* input,size_t n,uint8_t* out,size_t cap,size_t* wrote){
    host_key_t* k=id;BCRYPT_ALG_HANDLE a=NULL;BCRYPT_KEY_HANDLE key=NULL;bool ok=BCryptOpenAlgorithmProvider(&a,BCRYPT_AES_ALGORITHM,NULL,0)>=0;
    if(ok)ok=BCryptSetProperty(a,BCRYPT_CHAINING_MODE,(PUCHAR)BCRYPT_CHAIN_MODE_GCM,sizeof(BCRYPT_CHAIN_MODE_GCM),0)>=0;
    if(ok)ok=BCryptGenerateSymmetricKey(a,&key,NULL,0,k->bytes,(ULONG)k->size,0)>=0;
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;BCRYPT_INIT_AUTH_MODE_INFO(info);info.pbNonce=(PUCHAR)nonce;info.cbNonce=(ULONG)nn;info.pbAuthData=(PUCHAR)aad;info.cbAuthData=(ULONG)an;info.cbTag=16;
    uint8_t tag[16];if(!encrypt){if(n<16)ok=false;else{n-=16;memcpy(tag,input+n,16);}}info.pbTag=tag;ULONG size=0;
    if(ok){if(encrypt)ok=cap>=n+16&&BCryptEncrypt(key,(PUCHAR)input,(ULONG)n,&info,NULL,0,out,(ULONG)n,&size,0)>=0;else ok=cap>=n&&BCryptDecrypt(key,(PUCHAR)input,(ULONG)n,&info,NULL,0,out,(ULONG)cap,&size,0)>=0;}
    if(ok&&encrypt)memcpy(out+size,tag,16);*wrote=size+(encrypt?16:0);if(key)BCryptDestroyKey(key);if(a)BCryptCloseAlgorithmProvider(a,0);return ok?0:-1;
}
static int psa_aead_encrypt(mbedtls_svc_key_id_t id,unsigned alg,const uint8_t* nonce,size_t nn,const uint8_t* aad,size_t an,const uint8_t* input,size_t n,uint8_t* out,size_t cap,size_t* wrote){(void)alg;return host_aead(id,true,nonce,nn,aad,an,input,n,out,cap,wrote);}
static int psa_aead_decrypt(mbedtls_svc_key_id_t id,unsigned alg,const uint8_t* nonce,size_t nn,const uint8_t* aad,size_t an,const uint8_t* input,size_t n,uint8_t* out,size_t cap,size_t* wrote){(void)alg;return host_aead(id,false,nonce,nn,aad,an,input,n,out,cap,wrote);}
typedef int esp_err_t;typedef int nvs_handle_t;
#define ESP_OK 0
#define ESP_ERR_NVS_NOT_FOUND -1
#define NVS_READONLY 1
#define NVS_READWRITE 2
#define portMAX_DELAY 0
static void* thingsos_io_lock;
static void* xTaskGetCurrentTaskHandle(void){return NULL;}
static void* xSemaphoreGetMutexHolder(void* p){(void)p;return NULL;}
static void xSemaphoreTake(void* p,int t){(void)p;(void)t;}
static void xSemaphoreGive(void* p){(void)p;}
static void vTaskDelay(unsigned ticks){(void)ticks;}
static uint8_t nvs_blob[4096];static size_t nvs_bytes;static bool host_nvs_fail_write;
static int nvs_open(const char* name,int mode,int* out){(void)name;(void)mode;*out=1;return 0;}
static void nvs_close(int h){(void)h;}
static int nvs_get_blob(int h,const char* key,void* out,size_t* n){(void)h;(void)key;if(!nvs_bytes||*n<nvs_bytes)return -1;memcpy(out,nvs_blob,nvs_bytes);*n=nvs_bytes;return 0;}
static int nvs_set_blob(int h,const char* key,const void* in,size_t n){(void)h;(void)key;if(host_nvs_fail_write||n>sizeof(nvs_blob))return -1;memcpy(nvs_blob,in,n);nvs_bytes=n;return 0;}
static int nvs_commit(int h){(void)h;return 0;}
static int nvs_erase_key(int h,const char* key){(void)h;if(!strcmp(key,"sealed"))return ESP_ERR_NVS_NOT_FOUND;nvs_bytes=0;memset(nvs_blob,0,sizeof(nvs_blob));return 0;}
static int nvs_erase_all(int h){return nvs_erase_key(h,"connection");}
static void esp_fill_random(void* p,size_t n){if(BCryptGenRandom(NULL,p,(ULONG)n,BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)abort();}
