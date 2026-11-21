// SPDX-License-Identifier: Apache-2.0
/* Independent streaming IMA client. Credentials are provisioned over ephemeral
   a bounded local HTTP pairing session and retained in device NVS (secure.c). */
#include "thingsos/ima.h"
#include "thingsos/ima_gate.h"
#include "thingsos/audio.h"
#include "thingsos/audio_format.h"
#include "secure.h"
#include "network.h"
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_http_server.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "esp_log.h"
#include "nvs.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#define RESPONSE_MAX 65536
typedef struct {
    thingsos_ima_status_t status;
    ima_config_t config,candidate;
    thingsos_ima_library_t libraries[8];char cursor[512],target[160],target_name[192];
    uint64_t pair_until;unsigned attempts,command;volatile bool cancel;bool want_pair;
    FILE* file;
    thingsos_ima_job_t journal;bool have_journal;
    uint64_t receipts[32];unsigned receipt_head;
    httpd_handle_t pairing_http;
} ima_service_t;
static ima_service_t* s;
static SemaphoreHandle_t lifecycle;
static TaskHandle_t task;
static bool external_claimed;
static portMUX_TYPE guard=portMUX_INITIALIZER_UNLOCKED;
enum {LIST=1,CONFIGURE,UPLOAD,CHECK};
static uint64_t now_ms(void){return esp_timer_get_time()/1000;}
static void io_take(void){if(thingsos_io_lock)xSemaphoreTake(thingsos_io_lock,portMAX_DELAY);}
static void io_give(void){if(thingsos_io_lock)xSemaphoreGive(thingsos_io_lock);}
static void message(const char* m){portENTER_CRITICAL(&guard);strlcpy(s->status.message,m,sizeof(s->status.message));++s->status.serial;portEXIT_CRITICAL(&guard);}
static const char* string(cJSON* o,const char* k){cJSON* v=cJSON_GetObjectItemCaseSensitive(o,k);return cJSON_IsString(v)?v->valuestring:NULL;}
static bool text_ok(const char* t,size_t n){if(!t||!*t||strlen(t)>=n)return false;for(;*t;++t)if((unsigned char)*t<32||*t==127)return false;return true;}
void thingsos_ima_init(void){
    if(s)return;s=heap_caps_calloc(1,sizeof(*s),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(!s)return;lifecycle=xSemaphoreCreateMutex();if(!lifecycle){free(s);s=NULL;return;}
    s->status.configured=ima_config_load(&s->config);s->status.serial=1;
    if(s->status.configured){strlcpy(s->target,s->config.kb,sizeof(s->target));strlcpy(s->target_name,s->config.name,sizeof(s->target_name));strlcpy(s->status.kb_name,s->config.name,sizeof(s->status.kb_name));}
    nvs_handle_t h;if(nvs_open("thingsos-ima",NVS_READONLY,&h)==ESP_OK){size_t n=sizeof(s->journal);
        if(nvs_get_blob(h,"pending",&s->journal,&n)==ESP_OK&&n==sizeof(s->journal)){
            s->journal.path[511]=0;s->journal.name[255]=0;s->journal.kb[159]=0;s->journal.media[159]=0;s->journal.kb_name[191]=0;s->journal.message[191]=0;
            if(thingsos_ima_recording_path(s->journal.path)&&s->journal.media[0]){s->have_journal=true;if(s->journal.phase!=IMA_UPLOADED)s->journal.phase=IMA_UNCERTAIN;s->status.job=s->journal;}}
        n=sizeof(s->receipts);if(nvs_get_blob(h,"receipts",s->receipts,&n)!=ESP_OK||n!=sizeof(s->receipts))memset(s->receipts,0,sizeof(s->receipts));
        uint32_t head=0;nvs_get_u32(h,"receipt-head",&head);s->receipt_head=head%32;nvs_close(h);}
}
void thingsos_ima_status(thingsos_ima_status_t* out){if(!out)return;memset(out,0,sizeof(*out));if(!s)return;portENTER_CRITICAL(&guard);*out=s->status;uint64_t until=s->pair_until;portEXIT_CRITICAL(&guard);uint64_t current=now_ms();out->pairing=s->pairing_http&&until>current;out->pair_seconds=out->pairing?(unsigned)((until-current+999)/1000):0;if(!out->pairing)out->code[0]=0;}
bool thingsos_ima_active(void){if(!s)return false;portENTER_CRITICAL(&guard);bool b=s->status.busy;portEXIT_CRITICAL(&guard);return b;}
bool thingsos_ima_pairing(void){return s&&s->pairing_http&&s->pair_until>now_ms();}
bool thingsos_ima_pair(bool enable){
    thingsos_ima_init();if(!s)return false;
    if(enable){
        /* Opening the phone page only serves a static HTML file from flash, so
           an audio session (which claims the network only for itself) must not
           block it. A real transfer still owns SD/IO for its whole run. */
        if(!thingsos_network_connected()){message("请先连接 WiFi，再打开手机配对");return false;}
        if(!thingsos_ima_pair_allowed(true,thingsos_ima_active(),thingsos_network_wifi_busy())){
            message("传输或上传进行中，请完成后再打开手机配对");return false;
        }
    }
    s->want_pair=enable;s->pair_until=0;if(enable){portENTER_CRITICAL(&guard);s->status.config_phase=THINGSOS_IMA_CONFIG_IDLE;s->status.message[0]=0;++s->status.serial;portEXIT_CRITICAL(&guard);}return true;
}
bool thingsos_ima_library(unsigned i,thingsos_ima_library_t* out){if(!s||!out)return false;portENTER_CRITICAL(&guard);bool ok=i<s->status.count;if(ok)*out=s->libraries[i];portEXIT_CRITICAL(&guard);return ok;}
#include "ima_rpc.inc"
static bool cancelled(void* ignored){(void)ignored;return s->cancel;}
static uint64_t receipt_signature(const char* path,uint64_t bytes,uint64_t modified){uint64_t h=14695981039346656037ull;for(;*path;++path)h=(h^(unsigned char)*path)*1099511628211ull;return (h^bytes)*1099511628211ull^modified;}
static bool changed(void* ignored,const thingsos_ima_job_t* j){
    (void)ignored;bool ok=true;
    if(j->phase==IMA_ADDING||j->phase==IMA_UPLOADED||j->phase==IMA_UNCERTAIN){io_take();nvs_handle_t h;ok=nvs_open("thingsos-ima",NVS_READWRITE,&h)==ESP_OK;if(ok){
            ok=nvs_set_blob(h,"pending",j,sizeof(*j))==ESP_OK;
            if(ok&&j->phase==IMA_UPLOADED){s->receipts[s->receipt_head++%32]=receipt_signature(j->path,j->bytes,j->modified);ok=nvs_set_blob(h,"receipts",s->receipts,sizeof(s->receipts))==ESP_OK&&nvs_set_u32(h,"receipt-head",s->receipt_head%32)==ESP_OK;}
            ok=ok&&nvs_commit(h)==ESP_OK;nvs_close(h);}io_give();if(ok){s->journal=*j;s->have_journal=true;}}
    portENTER_CRITICAL(&guard);s->status.job=*j;++s->status.serial;portEXIT_CRITICAL(&guard);return ok;
}
static bool put(void* ignored,const char* host,const char* raw,const char* path,const char* auth,const char* token,uint64_t bytes){
    (void)ignored;(void)raw;size_t cap=strlen(host)+strlen(path)+16;char* url=malloc(cap);if(!url)return false;snprintf(url,cap,"https://%s%s",host,path);
    esp_http_client_config_t c={.url=url,.method=HTTP_METHOD_PUT,.crt_bundle_attach=esp_crt_bundle_attach,.timeout_ms=5000,.disable_auto_redirect=true,.buffer_size=2048,.buffer_size_tx=8192};esp_http_client_handle_t http=esp_http_client_init(&c);free(url);if(!http)return false;
    esp_http_client_set_header(http,"Content-Type","audio/wav");esp_http_client_set_header(http,"Authorization",auth);esp_http_client_set_header(http,"x-cos-security-token",token);
    uint8_t* block=heap_caps_malloc(8192,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);bool ok=block&&esp_http_client_open(http,(int)bytes)==ESP_OK;uint64_t sent=0;
    while(ok&&sent<bytes&&!s->cancel){size_t want=bytes-sent<8192?(size_t)(bytes-sent):8192;io_take();size_t got=fread(block,1,want,s->file);io_give();if(got!=want){ok=false;break;}
        size_t at=0;while(at<got&&!s->cancel){int n=esp_http_client_write(http,(char*)block+at,(int)(got-at));if(n<=0){ok=false;break;}at+=(unsigned)n;}sent+=at;portENTER_CRITICAL(&guard);s->status.job.percent=(unsigned)(sent*95/bytes);portEXIT_CRITICAL(&guard);vTaskDelay(1);}
    if(ok&&!s->cancel&&sent==bytes){ok=esp_http_client_fetch_headers(http)>=0;int status=esp_http_client_get_status_code(http);ok=ok&&status>=200&&status<300;}else ok=false;
    free(block);esp_http_client_close(http);esp_http_client_cleanup(http);return ok;
}
static thingsos_ima_ops_t ops(void){return (thingsos_ima_ops_t){.rpc=rpc,.put=put,.sha1=ima_sha1,.hmac=ima_hmac1,.cancelled=cancelled,.changed=changed};}
static bool libraries(bool first){
    cJSON* b=cJSON_CreateObject();cJSON_AddNumberToObject(b,"limit",8);cJSON_AddStringToObject(b,"cursor",first?"":s->cursor);char* json=cJSON_PrintUnformatted(b);cJSON_Delete(b);if(!json)return false;char* text=rpc(NULL,"get_addable_knowledge_base_list",json);free(json);cJSON* r=text?cJSON_Parse(text):NULL;if(text&&!r)message("ima 响应格式无效");if(text){ima_wipe(text,strlen(text));free(text);}cJSON* code=cJSON_GetObjectItemCaseSensitive(r,"code"),*d=cJSON_GetObjectItemCaseSensitive(r,"data"),*a=cJSON_GetObjectItemCaseSensitive(d,"addable_knowledge_base_list");
    bool ok=cJSON_IsNumber(code)&&code->valuedouble==0&&cJSON_IsArray(a)&&cJSON_GetArraySize(a)<=8;const char* next=string(d,"next_cursor");bool more=!cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(d,"is_end"));
    if(ok&&more)ok=text_ok(next,sizeof(s->cursor))&&(first||strcmp(next,s->cursor));
    if(!ok){if(r)message(string(r,"msg")?string(r,"msg"):"知识库响应无效，请检查 ima 凭证与权限");cJSON_Delete(r);return false;}
    thingsos_ima_library_t* staged=heap_caps_calloc(8,sizeof(*staged),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(!staged){cJSON_Delete(r);return false;}
    unsigned count=0;cJSON* item;cJSON_ArrayForEach(item,a){const char* id=string(item,"id"),*name=string(item,"name");if(!text_ok(id,160)||!text_ok(name,192)){ok=false;break;}strlcpy(staged[count].id,id,160);strlcpy(staged[count++].name,name,192);}
    if(ok){portENTER_CRITICAL(&guard);memcpy(s->libraries,staged,8*sizeof(*staged));s->status.count=count;s->status.more=more;portEXIT_CRITICAL(&guard);strlcpy(s->cursor,more?next:"",sizeof(s->cursor));}else message("知识库响应无效");free(staged);cJSON_Delete(r);return ok;
}
static void worker(void* ignored){
    (void)ignored;unsigned command=s->command;bool ok=false;
    if(command==CONFIGURE){
        ima_config_t* old=malloc(sizeof(*old));
        if(old){*old=s->config;s->config=s->candidate;ok=libraries(true);
            if(ok){ok=ima_config_save(&s->config);if(!ok)message("连接保存失败，请检查设备存储");}
            if(!ok){s->config=*old;s->cursor[0]=0;portENTER_CRITICAL(&guard);s->status.count=0;s->status.more=false;portEXIT_CRITICAL(&guard);}
            else{strlcpy(s->target,s->config.kb,sizeof(s->target));strlcpy(s->target_name,s->config.name,sizeof(s->target_name));
                s->pair_until=now_ms()+15000;message("ima 已连接，请选择默认知识库");}
            ima_wipe(old,sizeof(*old));free(old);
        }else message("配置内存不足");
        ima_wipe(&s->candidate,sizeof(s->candidate));
        portENTER_CRITICAL(&guard);s->status.configured=s->config.key[0]!=0;
        s->status.config_phase=ok?THINGSOS_IMA_CONFIG_READY:THINGSOS_IMA_CONFIG_FAILED;
        if(ok)++s->status.connection_serial;strlcpy(s->status.kb_name,s->config.name,sizeof(s->status.kb_name));portEXIT_CRITICAL(&guard);
    }else if(command==LIST){ok=libraries(false);if(ok)message(s->status.count?"请选择知识库":"没有可添加内容的知识库");}
    else {
        thingsos_ima_job_t* j=malloc(sizeof(*j));if(j){*j=s->status.job;thingsos_ima_ops_t o=ops();
            if(command==CHECK)thingsos_ima_reconcile(j,&o);
            else{io_take();struct stat info;thingsos_wav_t wav={0};bool valid=thingsos_ima_recording_path(j->path)&&!stat(j->path,&info)&&S_ISREG(info.st_mode);if(valid)s->file=fopen(j->path,"rb");valid=valid&&s->file&&thingsos_wav_read(s->file,&wav);
                if(valid){j->bytes=(uint64_t)info.st_size;j->modified=(uint64_t)info.st_mtime;valid=thingsos_ima_preflight(j->bytes,(uint64_t)wav.bytes*1000/(wav.rate*wav.channels*2));rewind(s->file);}io_give();
                if(valid)thingsos_ima_execute(j,&o);else{j->phase=IMA_FAILED;strlcpy(j->message,"录音无效、超过 200 MB / 2 小时或 SD 卡不可读",sizeof(j->message));changed(NULL,j);}
                if(s->file){io_take();fclose(s->file);s->file=NULL;io_give();}}
            free(j);
        }else message("上传内存不足");
    }
    ESP_LOGI("ThingsOS_ima","Job complete command=%u phase=%u stack_remaining=%u",command,(unsigned)s->status.job.phase,(unsigned)uxTaskGetStackHighWaterMark(NULL));
    if(external_claimed)thingsos_network_external_end();external_claimed=false;xSemaphoreTake(lifecycle,portMAX_DELAY);portENTER_CRITICAL(&guard);s->status.busy=false;++s->status.serial;portEXIT_CRITICAL(&guard);task=NULL;xSemaphoreGive(lifecycle);vTaskDelete(NULL);
}
static bool start(unsigned command){
    if(s->status.busy||!thingsos_network_connected())return false;
    /* Audio holds the network claim for its whole open file, and only to exclude
       transfers. Credential verification and library listing are short network
       requests, so they share that claim instead of asking the user to stop the
       music; uploads and result checks keep the exclusive claim. */
    bool claimed=thingsos_network_external_begin();
    if(!claimed&&!((command==CONFIGURE||command==LIST)&&thingsos_audio_active()))return false;
    external_claimed=claimed;
    s->cancel=false;s->command=command;portENTER_CRITICAL(&guard);s->status.busy=true;++s->status.serial;portEXIT_CRITICAL(&guard);
    if(xTaskCreate(worker,"ima_upload",16384,NULL,3,&task)!=pdPASS){if(claimed)thingsos_network_external_end();s->status.busy=false;message("ima 任务内存不足");return false;}return true;
}
bool thingsos_ima_refresh(bool next){thingsos_ima_init();if(!s)return false;xSemaphoreTake(lifecycle,portMAX_DELAY);bool ok=s->status.configured&&!s->status.busy;if(ok){if(!next)s->cursor[0]=0;ok=start(LIST);}xSemaphoreGive(lifecycle);return ok;}
bool thingsos_ima_choose(unsigned index,bool persist){
    if(!s)return false;xSemaphoreTake(lifecycle,portMAX_DELAY);bool ok=!s->status.busy&&index<s->status.count;
    if(ok){ima_config_t* c=malloc(sizeof(*c));if(!c)ok=false;else{*c=s->config;strlcpy(c->kb,s->libraries[index].id,sizeof(c->kb));strlcpy(c->name,s->libraries[index].name,sizeof(c->name));if(persist){ok=ima_config_save(c);if(ok)s->config=*c;}if(ok){strlcpy(s->target,c->kb,sizeof(s->target));strlcpy(s->target_name,c->name,sizeof(s->target_name));portENTER_CRITICAL(&guard);strlcpy(s->status.kb_name,c->name,sizeof(s->status.kb_name));++s->status.serial;portEXIT_CRITICAL(&guard);}ima_wipe(c,sizeof(*c));free(c);}}
    xSemaphoreGive(lifecycle);return ok;
}
bool thingsos_ima_upload(const char* path,bool keep){
    thingsos_ima_init();if(!s||!thingsos_ima_recording_path(path))return false;xSemaphoreTake(lifecycle,portMAX_DELAY);bool ok=s->status.configured&&!s->status.busy&&s->target[0];
    if(ok&&s->have_journal&&s->journal.phase!=IMA_UPLOADED){s->status.job=s->journal;message("请先检查上次上传结果，避免重复添加");ok=false;}
    if(ok){thingsos_ima_job_t* j=&s->status.job;memset(j,0,sizeof(*j));strlcpy(j->path,path,sizeof(j->path));strlcpy(j->kb,s->target,sizeof(j->kb));strlcpy(j->kb_name,s->target_name,sizeof(j->kb_name));const char* name=strrchr(path,'/')+1;
        if(keep){time_t t=time(NULL);struct tm tm;gmtime_r(&t,&tm);char stamp[24];strftime(stamp,sizeof(stamp),"%Y%m%d%H%M%S",&tm);snprintf(j->name,sizeof(j->name),"%.*s_%s_%04x.wav",(int)strlen(name)-4,name,stamp,(unsigned)(esp_random()&65535));}else strlcpy(j->name,name,sizeof(j->name));j->phase=IMA_CHECKING;ok=start(UPLOAD);}
    xSemaphoreGive(lifecycle);return ok;
}
bool thingsos_ima_check(void){if(!s)return false;xSemaphoreTake(lifecycle,portMAX_DELAY);bool ok=s->status.configured&&!s->status.busy&&s->status.job.media[0]&&start(CHECK);xSemaphoreGive(lifecycle);return ok;}
void thingsos_ima_cancel(void){if(s)s->cancel=true;}
bool thingsos_ima_quiesce_locked(void){thingsos_ima_cancel();thingsos_ima_pair(false);if(!thingsos_ima_active())return true;io_give();uint64_t until=now_ms()+20000;while(thingsos_ima_active()&&now_ms()<until)vTaskDelay(pdMS_TO_TICKS(20));io_take();return !thingsos_ima_active();}
bool thingsos_ima_disconnect(void){
    if(!s)return false;
    xSemaphoreTake(lifecycle,portMAX_DELAY);
    if(s->status.busy){xSemaphoreGive(lifecycle);return false;}
    s->want_pair=false;s->pair_until=0;
    bool ok=ima_config_clear();
    if(ok){ima_wipe(&s->config,sizeof(s->config));s->target[0]=0;portENTER_CRITICAL(&guard);s->have_journal=false;memset(&s->journal,0,sizeof(s->journal));memset(s->receipts,0,sizeof(s->receipts));memset(&s->status.job,0,sizeof(s->status.job));s->status.configured=false;s->status.config_phase=THINGSOS_IMA_CONFIG_IDLE;s->status.count=0;s->status.kb_name[0]=0;++s->status.serial;portEXIT_CRITICAL(&guard);}
    xSemaphoreGive(lifecycle);return ok;
}
bool thingsos_ima_file_status(const char* path,char* out,size_t cap){
    if(!s||!path||!out||!cap)return false;struct stat st;if(stat(path,&st))return false;uint64_t signature=receipt_signature(path,(uint64_t)st.st_size,(uint64_t)st.st_mtime);
    portENTER_CRITICAL(&guard);bool found=false;for(unsigned i=0;i<32;++i)if(s->receipts[i]==signature){found=true;strlcpy(out,"已上传",cap);break;}
    if(!found&&!strcmp(path,s->status.job.path)){found=true;strlcpy(out,s->status.job.phase==IMA_UNCERTAIN?"待确认":s->status.busy?"上传中":"未上传",cap);}
    portEXIT_CRITICAL(&guard);return found;
}
extern const unsigned char page_start[] asm("_binary_ima_html_start");
extern const unsigned char page_end[] asm("_binary_ima_html_end");
static bool pair_code_matches(const char* code){
    if(!code||strlen(code)!=8)return false;unsigned diff=0;
    for(unsigned i=0;i<8;++i)diff|=(unsigned char)code[i]^(unsigned char)s->status.code[i];return diff==0;
}
static esp_err_t phone_page(httpd_req_t* req){
    if(!thingsos_ima_pairing()){httpd_resp_set_status(req,"403 Forbidden");return httpd_resp_sendstr(req,"请在设备重新打开手机配置。");}
    httpd_resp_set_type(req,"text/html; charset=utf-8");httpd_resp_set_hdr(req,"Cache-Control","no-store");
    httpd_resp_set_hdr(req,"Content-Security-Policy","default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; connect-src 'self'; form-action 'self'; frame-ancestors 'none'");
    return httpd_resp_send(req,(const char*)page_start,page_end-page_start-1);
}
static esp_err_t configure_status(httpd_req_t* req){
    char code[16];
    if(!thingsos_ima_pairing()||httpd_req_get_hdr_value_str(req,"X-ThingsOS-Pair-Code",code,sizeof(code))!=ESP_OK||!pair_code_matches(code)){
        httpd_resp_set_status(req,"403 Forbidden");return httpd_resp_sendstr(req,"配对已结束，请查看设备或重新打开配对。");}
    char detail[192];thingsos_ima_config_phase_t phase;bool busy;
    portENTER_CRITICAL(&guard);phase=s->status.config_phase;busy=s->status.busy;strlcpy(detail,s->status.message,sizeof(detail));portEXIT_CRITICAL(&guard);
    cJSON* result=cJSON_CreateObject();if(!result)return ESP_ERR_NO_MEM;
    cJSON_AddStringToObject(result,"phase",phase==THINGSOS_IMA_CONFIG_READY?"ready":phase==THINGSOS_IMA_CONFIG_FAILED?"failed":busy?"verifying":"idle");
    cJSON_AddStringToObject(result,"message",detail);char* body=cJSON_PrintUnformatted(result);cJSON_Delete(result);if(!body)return ESP_ERR_NO_MEM;
    httpd_resp_set_type(req,"application/json; charset=utf-8");httpd_resp_set_hdr(req,"Cache-Control","no-store");
    esp_err_t error=httpd_resp_sendstr(req,body);free(body);return error;
}
static esp_err_t configure(httpd_req_t* req){
    if(!thingsos_ima_pairing()||req->content_len<=0||req->content_len>4096){httpd_resp_set_status(req,"403 Forbidden");return httpd_resp_sendstr(req,"配对已关闭");}
    char type[64];if(httpd_req_get_hdr_value_str(req,"Content-Type",type,sizeof(type))!=ESP_OK||strncmp(type,"application/json",16)){httpd_resp_set_status(req,"415 Unsupported Media Type");return httpd_resp_sendstr(req,"JSON required");}
    char* body=calloc(1,(size_t)req->content_len+1);if(!body)return ESP_ERR_NO_MEM;size_t got=0;
    while(got<(size_t)req->content_len){int n=httpd_req_recv(req,body+got,req->content_len-got);if(n<=0)break;got+=(unsigned)n;}
    cJSON* r=got==(size_t)req->content_len?cJSON_Parse(body):NULL;ima_wipe(body,got);free(body);
    const char *code=string(r,"code"),*client=string(r,"clientId"),*key=string(r,"apiKey");bool accepted=false;
    xSemaphoreTake(lifecycle,portMAX_DELAY);
    if(!s->status.busy&&thingsos_ima_pairing()&&s->attempts<8){++s->attempts;
        if(pair_code_matches(code)&&text_ok(client,256)&&text_ok(key,1024)){
            memset(&s->candidate,0,sizeof(s->candidate));strlcpy(s->candidate.client,client,256);strlcpy(s->candidate.key,key,1024);
            portENTER_CRITICAL(&guard);s->status.config_phase=THINGSOS_IMA_CONFIG_VERIFYING;portEXIT_CRITICAL(&guard);
            message("正在验证 ima 凭证并获取知识库…");accepted=start(CONFIGURE);
            if(!accepted){ima_wipe(&s->candidate,sizeof(s->candidate));portENTER_CRITICAL(&guard);s->status.config_phase=THINGSOS_IMA_CONFIG_FAILED;portEXIT_CRITICAL(&guard);message("验证任务未启动，请检查 WiFi 或结束其他任务");}
        }
        if(s->attempts>=8&&!accepted)s->pair_until=0;
    }
    xSemaphoreGive(lifecycle);cJSON* value;cJSON_ArrayForEach(value,r){if(cJSON_IsString(value))ima_wipe(value->valuestring,strlen(value->valuestring));}cJSON_Delete(r);
    httpd_resp_set_hdr(req,"Cache-Control","no-store");httpd_resp_set_status(req,accepted?"202 Accepted":"403 Forbidden");
    return httpd_resp_sendstr(req,accepted?"正在验证 ima 凭证…":"未接受配置：请检查配对码、凭证与设备任务状态。");
}
void thingsos_ima_pair_poll(void){
    if(!s)return;
    if(s->pairing_http&&(!s->want_pair||s->pair_until<=now_ms()||!thingsos_network_connected())){
        bool restart=s->want_pair&&s->pair_until==0;
        httpd_stop(s->pairing_http);s->pairing_http=NULL;
        s->want_pair=restart;
        if(s->status.config_phase==THINGSOS_IMA_CONFIG_IDLE)message("手机配对已关闭，请重新打开以获取新地址");
    }
    if(!s->want_pair||s->pairing_http||thingsos_ima_active())return;s->want_pair=false;
    if(!thingsos_network_connected()){message("请先连接 WiFi");return;}
    httpd_config_t c=HTTPD_DEFAULT_CONFIG();c.server_port=8081;c.ctrl_port=32769;c.stack_size=8192;
    c.max_uri_handlers=3;c.max_open_sockets=2;c.lru_purge_enable=true;c.recv_wait_timeout=5;c.max_req_hdr_len=2048;
    esp_err_t started=httpd_start(&s->pairing_http,&c);
    if(started!=ESP_OK){s->pairing_http=NULL;message("手机配对服务启动失败，请返回后重试");ESP_LOGE("ThingsOS_ima","HTTP pairing start failed: %s",esp_err_to_name(started));return;}
    const httpd_uri_t routes[]={{.uri="/",.method=HTTP_GET,.handler=phone_page},{.uri="/configure",.method=HTTP_POST,.handler=configure},{.uri="/configure/status",.method=HTTP_GET,.handler=configure_status}};
    bool ok=true;for(unsigned i=0;i<3;++i)if(httpd_register_uri_handler(s->pairing_http,routes+i)!=ESP_OK)ok=false;
    if(!ok){httpd_stop(s->pairing_http);s->pairing_http=NULL;message("手机配置入口创建失败");return;}
    portENTER_CRITICAL(&guard);s->pair_until=now_ms()+300000;s->attempts=0;s->want_pair=true;
    snprintf(s->status.code,sizeof(s->status.code),"%08u",(unsigned)(esp_random()%100000000));++s->status.serial;portEXIT_CRITICAL(&guard);
    ESP_LOGI("ThingsOS_ima","Pairing HTTP listening on port 8081");
}
