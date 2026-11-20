// SPDX-License-Identifier: Apache-2.0
/* Execute the production HTTP exchange against deterministic IDF transport
   edges. There is deliberately no perform()/Basic/Digest implementation. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "../firmware/components/thingsos_ima/secure.h"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"IMA RPC line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM -2
#define ESP_ERR_INVALID_SIZE -3
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define HTTP_METHOD_POST 1
#define RESPONSE_MAX 65536
typedef int esp_err_t;
typedef struct {const char* url;int method;int (*crt_bundle_attach)(void*);int timeout_ms;bool disable_auto_redirect;int max_authorization_retries,buffer_size,buffer_size_tx;} esp_http_client_config_t;
typedef struct {bool cancel;ima_config_t config;} service_t;
static service_t service,*s=&service;
typedef struct {const char* body;size_t bytes,at;int status;bool complete,fail_open,fail_write,fail_read;} fake_http_t;
typedef fake_http_t* esp_http_client_handle_t;
static fake_http_t http;
static char saved_url[192],saved_client[256],saved_key[1024],sent[1024],last_message[192];
static size_t sent_bytes;static unsigned closes,cleanups;
static void* heap_caps_calloc(size_t n,size_t bytes,unsigned caps){(void)caps;return calloc(n,bytes);}
static int esp_crt_bundle_attach(void* p){(void)p;return 0;}
static const char* esp_err_to_name(int error){return error==ESP_ERR_INVALID_SIZE?"ESP_ERR_INVALID_SIZE":"ESP_FAIL";}
static const char* string(cJSON* r,const char* key){cJSON* v=cJSON_GetObjectItemCaseSensitive(r,key);return cJSON_IsString(v)?v->valuestring:NULL;}
static void message(const char* text){snprintf(last_message,sizeof(last_message),"%s",text);}
void ima_wipe(void* p,size_t n){memset(p,0,n);}
static esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t* config){
    CHECK(config->method==HTTP_METHOD_POST&&config->disable_auto_redirect&&config->max_authorization_retries==-1);
    CHECK(config->crt_bundle_attach==esp_crt_bundle_attach);snprintf(saved_url,sizeof(saved_url),"%s",config->url);return &http;
}
static int esp_http_client_set_header(fake_http_t* c,const char* name,const char* value){
    (void)c;if(!strcmp(name,"ima-openapi-clientid"))snprintf(saved_client,sizeof(saved_client),"%s",value);
    else if(!strcmp(name,"ima-openapi-apikey"))snprintf(saved_key,sizeof(saved_key),"%s",value);
    else CHECK(!strcmp(name,"Content-Type"));return ESP_OK;
}
static int esp_http_client_open(fake_http_t* c,int length){CHECK(length<sizeof(sent));return c->fail_open?ESP_FAIL:ESP_OK;}
static int esp_http_client_write(fake_http_t* c,const char* body,int bytes){if(c->fail_write)return 0;int n=bytes>3?3:bytes;memcpy(sent+sent_bytes,body,n);sent_bytes+=n;sent[sent_bytes]=0;return n;}
static int64_t esp_http_client_fetch_headers(fake_http_t* c){return (int64_t)c->bytes;}
static int esp_http_client_get_status_code(fake_http_t* c){return c->status;}
static int esp_http_client_read(fake_http_t* c,char* output,int bytes){
    if(c->fail_read)return -1;size_t n=c->bytes-c->at;if(n>(size_t)bytes)n=bytes;if(n>7)n=7;
    memcpy(output,c->body+c->at,n);c->at+=n;return (int)n;
}
static bool esp_http_client_is_complete_data_received(fake_http_t* c){return c->complete&&c->at==c->bytes;}
static void esp_http_client_close(fake_http_t* c){(void)c;++closes;}
static void esp_http_client_cleanup(fake_http_t* c){(void)c;++cleanups;}
#include "../firmware/components/thingsos_ima/ima_rpc.inc"
static void setup(int status,const char* body){
    memset(&http,0,sizeof(http));http.status=status;http.body=body;http.bytes=strlen(body);http.complete=true;
    closes=cleanups=0;sent_bytes=0;sent[0]=last_message[0]=0;s->cancel=false;
}
static void check_error(int status,int expected,const char* body){
    setup(status,body);char* text=rpc(NULL,"get_addable_knowledge_base_list","{\"limit\":8}");CHECK(text);
    cJSON* result=cJSON_Parse(text);CHECK(result);CHECK(cJSON_GetObjectItem(result,"code")->valueint==expected);
    CHECK(strstr(string(result,"msg"),"HTTP"));CHECK(!strstr(text,"ESP_ERR_NOT_SUPPORTED"));
    CHECK(!strstr(text,"test-api-key")&&!strstr(last_message,"test-api-key"));cJSON_Delete(result);free(text);CHECK(closes==1&&cleanups==1);
}
int main(void){
    strcpy(s->config.client,"test-client");strcpy(s->config.key,"test-api-key");
    setup(200,"{\"code\":0,\"data\":{}}");char* text=rpc(NULL,"get_addable_knowledge_base_list","{\"limit\":8}");
    CHECK(text&&!strcmp(text,http.body));free(text);CHECK(!strcmp(sent,"{\"limit\":8}"));
    CHECK(!strcmp(saved_url,"https://ima.qq.com/openapi/wiki/v1/get_addable_knowledge_base_list"));
    CHECK(!strcmp(saved_client,"test-client")&&!strcmp(saved_key,"test-api-key"));CHECK(closes==1&&cleanups==1);
    /* Live unauthenticated IMA response: no WWW-Authenticate header. */
    check_error(401,200002,"{\"code\":200002,\"msg\":\"skill auth failed\",\"data\":{}}");
    check_error(403,403,"<html>denied</html>");check_error(429,429,"{}");
    setup(200,"{}");http.complete=false;CHECK(!rpc(NULL,"method","{}"));
    setup(200,"{}");http.fail_read=true;CHECK(!rpc(NULL,"method","{}"));CHECK(closes==1&&cleanups==1);
    setup(200,"{}");http.fail_write=true;CHECK(!rpc(NULL,"method","{}"));CHECK(http.at==0);
    setup(200,"{}");http.fail_open=true;CHECK(!rpc(NULL,"method","{}"));
    setup(302,"{}");CHECK(!rpc(NULL,"method","{}"));CHECK(strstr(last_message,"302"));
    setup(500,"{\"code\":1,\"msg\":\"server error\"}");CHECK(!rpc(NULL,"add_knowledge","{}")); /* Keep uncertain adds uncertain. */
    char* huge=malloc(RESPONSE_MAX+2);CHECK(huge);memset(huge,'x',RESPONSE_MAX+1);huge[RESPONSE_MAX+1]=0;
    setup(200,huge);CHECK(!rpc(NULL,"method","{}"));CHECK(strstr(last_message,"INVALID_SIZE"));free(huge);
    setup(200,"{}");s->cancel=true;CHECK(!rpc(NULL,"method","{}"));CHECK(closes==0&&cleanups==0);
    puts("Production IMA HTTP: header auth, partial IO, 401/403/429 responses, redirect refusal, uncertain 5xx and limits passed");return 0;
}
