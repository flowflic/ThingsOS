// SPDX-License-Identifier: Apache-2.0
/* A single worker owns the network transaction. UI state is copied under the
   guard; every flash operation shares the canvas/SD ownership lock. */
#include "thingsos/download.h"
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
static EXT_RAM_BSS_ATTR thingsos_release_state_t release_state; /* status text, not flashed data */
static bool release_metadata_owned;
static size_t release_received;
static int release_error;
static unsigned release_retries;
static char release_stage[32];
static int release_http_error,release_http_status,release_http_errno,release_tls_error;
bool thingsos_network_release_checking(void) {
    portENTER_CRITICAL(&state_guard);bool checking=release_metadata_owned;portEXIT_CRITICAL(&state_guard);
    return checking;
}
void thingsos_network_release_state(thingsos_release_state_t* out) {
    portENTER_CRITICAL(&state_guard);*out=release_state;portEXIT_CRITICAL(&state_guard);
}
static void release_message(const char* message,bool failed,bool finished) {
    portENTER_CRITICAL(&state_guard);
    strlcpy(release_state.message,message,sizeof(release_state.message));
    release_state.failed=failed;if(finished)release_state.active=false;
    ++release_state.serial;
    bool installation=!release_metadata_owned;
    portEXIT_CRITICAL(&state_guard);
    if(installation)transfer_set("在线固件升级",message,0,!finished,failed,true);
}
typedef struct {char range[96];bool seen,invalid;} release_headers_t;
static esp_err_t release_http_event(esp_http_client_event_t* event) {
    release_headers_t* headers=event->user_data;
    if(headers&&event->event_id==HTTP_EVENT_ON_HEADER&&!strcasecmp(event->header_key,"Content-Range")) {
        if(headers->seen||strlen(event->header_value)>=sizeof(headers->range))headers->invalid=true;
        else strlcpy(headers->range,event->header_value,sizeof(headers->range));
        headers->seen=true;
    }
    return ESP_OK;
}
static esp_http_client_handle_t release_connection_at(const char* url,size_t offset,size_t total,unsigned timeout_ms) {
    release_headers_t headers={0};
    esp_http_client_config_t config={.url=url,.timeout_ms=(int)timeout_ms,.disable_auto_redirect=true,
        .crt_bundle_attach=esp_crt_bundle_attach,.buffer_size=4096,.buffer_size_tx=1024,
        .event_handler=release_http_event,.user_data=&headers};
    esp_http_client_handle_t http=esp_http_client_init(&config);
    if(!http)return NULL;
    esp_http_client_set_header(http,"Accept-Encoding","identity");
    if(offset) {
        char range[48];snprintf(range,sizeof(range),"bytes=%u-",(unsigned)offset);
        esp_http_client_set_header(http,"Range",range);
    }
    esp_err_t connected=esp_http_client_open(http,0);
    bool ok=connected==ESP_OK && esp_http_client_fetch_headers(http)>=0;
    int status=esp_http_client_get_status_code(http);
    int64_t length=esp_http_client_get_content_length(http);
    if(offset)ok=ok&&status==206&&!headers.invalid&&headers.seen&&thingsos_download_range(headers.range,offset,total,length);
    else ok=ok&&status==200&&(!total||length==(int64_t)total);
    /* The header collector lives on this stack, not for the body lifetime. */
    esp_http_client_set_user_data(http,NULL);
    if(!ok) {
        int tls=0,flags=0;esp_http_client_get_and_clear_last_tls_error(http,&tls,&flags);
        portENTER_CRITICAL(&state_guard);
        release_http_error=connected;release_http_status=status;
        release_http_errno=esp_http_client_get_errno(http);release_tls_error=tls;
        portEXIT_CRITICAL(&state_guard);
        ESP_LOGE("ThingsOS_ota","connect/range failed: error=%d status=%d errno=%d TLS=%d offset=%u",connected,status,release_http_errno,tls,(unsigned)offset);
        esp_http_client_close(http);esp_http_client_cleanup(http);return NULL;
    }
    return http;
}
static void release_close(esp_http_client_handle_t http) {
    if(http){esp_http_client_close(http);esp_http_client_cleanup(http);}
}
/* The first outbound TCP connection after association can be refused for up to
   ~90 s while UDP works (measured on this device); the first success is
   followed by normal latency. One short connect in the background keeps that
   delay off the "check for updates" tap.
   / 刚连上热点时，首个对外 TCP 连接可能被拒长达 ~90 秒（同窗口 UDP 正常），
   第一次成功后恢复正常。后台先连一次发布主机，把这段等待从用户点击
   “检查新固件”里移走。 */
static bool warm_connect(unsigned timeout_ms) {
    struct addrinfo hints={.ai_family=AF_INET,.ai_socktype=SOCK_STREAM},*ai=NULL;
    if(getaddrinfo(THINGSOS_RELEASE_HOST,"443",&hints,&ai)!=0||!ai)return false;
    bool ok=false;
    int fd=socket(ai->ai_family,ai->ai_socktype,0);
    if(fd>=0) {
        fcntl(fd,F_SETFL,fcntl(fd,F_GETFL,0)|O_NONBLOCK);
        int r=connect(fd,ai->ai_addr,ai->ai_addrlen),error=errno;
        if(r==0)ok=true;
        else if(error==EINPROGRESS) {
            fd_set write;FD_ZERO(&write);FD_SET(fd,&write);
            struct timeval tv={.tv_sec=timeout_ms/1000,.tv_usec=(timeout_ms%1000)*1000};
            if(select(fd+1,NULL,&write,NULL,&tv)>0) {
                int so=0;socklen_t len=sizeof(so);
                ok=getsockopt(fd,SOL_SOCKET,SO_ERROR,&so,&len)==0&&!so;
            }
        }
        close(fd);
    }
    freeaddrinfo(ai);
    return ok;
}
static bool warmup_running;
static void warmup_task(void* argument) {
    (void)argument;
    int64_t started=esp_timer_get_time();
    for(unsigned attempt=0;attempt<150;++attempt) {
        if(!thingsos_network_connected()){vTaskDelay(pdMS_TO_TICKS(1000));continue;}
        if(warm_connect(2000)) {
            ESP_LOGI("ThingsOS_net","release host reachable after %u ms",
                     (unsigned)((esp_timer_get_time()-started)/1000));
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    portENTER_CRITICAL(&state_guard);warmup_running=false;portEXIT_CRITICAL(&state_guard);
    vTaskDelete(NULL);
}
void thingsos_network_warmup_start(void) {
    portENTER_CRITICAL(&state_guard);
    bool start=!warmup_running;warmup_running=true;
    portEXIT_CRITICAL(&state_guard);
    if(start && xTaskCreate(warmup_task,"net_warmup",6144,NULL,3,NULL)!=pdPASS) {
        portENTER_CRITICAL(&state_guard);warmup_running=false;portEXIT_CRITICAL(&state_guard);
        ESP_LOGW("ThingsOS_net","warm-up task not created");
    }
}
typedef struct {
    ota_context_t ota; /* First: shared image/hash/flash operations use this. */
    thingsos_download_t stream;
    const char* url;
    int64_t started;
} release_download_t;
static void release_download_close(void* p) {
    release_download_t* c=p;release_close(c->ota.download);c->ota.download=NULL;
}
static int release_download_raw(void* p,uint8_t* data,size_t n) {
    release_download_t* c=p;
    if(esp_timer_get_time()-c->started>600000000)return -1;
    int got=ota_read(&c->ota,data,n);
    if(got<=0) {
        int tls=0,flags=0;esp_http_client_get_and_clear_last_tls_error(c->ota.download,&tls,&flags);
        portENTER_CRITICAL(&state_guard);
        release_http_errno=esp_http_client_get_errno(c->ota.download);release_tls_error=tls;
        portEXIT_CRITICAL(&state_guard);
    }
    return got;
}
static bool release_download_open(void* p,size_t offset) {
    release_download_t* c=p;
    if(esp_timer_get_time()-c->started>600000000)return false;
    transfer_set(NULL,"网络中断，正在断点续传",(unsigned)(offset*100/c->stream.bytes),true,false,true);
    c->ota.download=release_connection_at(c->url,offset,c->stream.bytes,20000);
    return c->ota.download!=NULL;
}
static int release_download_read(void* p,uint8_t* data,size_t n) {
    release_download_t* c=p;
    int got=thingsos_download_read(&c->stream,data,n);
    if(got>0)c->ota.error=0;
    portENTER_CRITICAL(&state_guard);
    release_received=c->stream.received;release_retries=c->stream.retries;
    portEXIT_CRITICAL(&state_guard);
    return got;
}
#define RELEASE_SD_PART "/sdcard/.thingsos/update.part"
#define RELEASE_SD_FILE "/sdcard/.thingsos/update.bin"
static bool release_file_check(const char* path,const thingsos_release_t* candidate,uint8_t* block,size_t capacity) {
    if(!lock())return false;
    struct stat info;FILE* file=NULL;
    if(sd_ready()&&stat(path,&info)==0&&info.st_size==(off_t)candidate->bytes)file=fopen(path,"rb");
    unlock();if(!file)return false;
    ota_context_t hash={0};bool ok=ota_hash_begin(&hash);size_t used=0;
    while(ok&&used<candidate->bytes) {
        size_t n=candidate->bytes-used;if(n>capacity)n=capacity;
        if(!lock()){ok=false;break;}
        size_t got=fread(block,1,n,file);unlock();
        ok=got==n&&ota_hash_update(&hash,block,got);used+=got;
    }
    if(lock()){ok=(fclose(file)==0)&&ok;unlock();}else ok=false;
    uint8_t digest[32];
    if(ok)ok=ota_hash_finish(&hash,digest);
    ota_hash_abort(&hash);
    unsigned difference=0;
    if(ok)for(unsigned i=0;i<32;++i)difference|=digest[i]^candidate->sha256[i];
    return ok&&!difference;
}
static bool release_sd_download(release_download_t* context,const thingsos_release_t* candidate,uint8_t* block,size_t capacity) {
    if(!lock())return ota_result(&context->ota,"sd-open-lock",ESP_ERR_TIMEOUT);
    uint64_t total=0,available=0;
    bool ready=sd_ready()&&esp_vfs_fat_info("/sdcard",&total,&available)==ESP_OK&&available>=candidate->bytes+65536;
    if(ready)mkdir("/sdcard/.thingsos",0775);
    FILE* file=ready?fopen(RELEASE_SD_PART,"wb"):NULL;
    unlock();
    if(!file)return ota_result(&context->ota,"sd-space/open",ESP_ERR_INVALID_STATE);
    context->ota.download=release_connection_at(context->url,0,candidate->bytes,20000);
    bool ok=context->ota.download!=NULL;
    context->ota.stage="download";
    if(!ok)context->ota.error=ESP_FAIL;
    while(ok&&context->stream.received<candidate->bytes) {
        int n=release_download_read(context,block,capacity);
        if(n<=0){ok=false;break;}
        if(!lock()){ok=ota_result(&context->ota,"sd-write-lock",ESP_ERR_TIMEOUT);break;}
        ok=ota_result(&context->ota,"sd-write",sd_ready()&&fwrite(block,1,(size_t)n,file)==(size_t)n?ESP_OK:ESP_FAIL);
        unlock();
    }
    /* No HTTPS or WiFi download buffers remain during SD verification/flash. */
    release_download_close(context);
    if(lock()) {
        if(ok)ok=ota_result(&context->ota,"sd-sync",fflush(file)==0&&fsync(fileno(file))==0?ESP_OK:ESP_FAIL);
        int closed=fclose(file);
        if(ok)ok=ota_result(&context->ota,"sd-close",closed==0?ESP_OK:ESP_FAIL);
        unlock();
    }else ok=ota_result(&context->ota,"sd-close-lock",ESP_ERR_TIMEOUT);
    if(ok) {
        transfer_set(NULL,"SD 下载完成，校验完整固件",100,true,false,true);
        ok=ota_result(&context->ota,"sd-sha256",release_file_check(RELEASE_SD_PART,candidate,block,capacity)?ESP_OK:ESP_FAIL);
    }
    if(lock()) {
        if(ok) {
            remove(RELEASE_SD_FILE);
            ok=ota_result(&context->ota,"sd-rename",rename(RELEASE_SD_PART,RELEASE_SD_FILE)==0?ESP_OK:ESP_FAIL);
        }
        if(!ok)remove(RELEASE_SD_PART);
        unlock();
    }else ok=ota_result(&context->ota,"sd-rename-lock",ESP_ERR_TIMEOUT);
    return ok;
}
static bool release_check(void) {
    /* 空闲后首个连接偶发在 TLS 之前被中断（errno 113），实测要等 16–19 秒才被
       底层中止。每次超时压到 5 秒、最多试 3 次、失败立即重试，不再静默等 10 秒；
       开机后的后台预热通常已经让第一次连接变快。
       / The first connection after an idle period is aborted before TLS (16–19 s
       observed). Each attempt is capped at 5 s and retried right away instead of
       waiting 10 s; the boot-time warm-up usually makes the first one fast. */
    int64_t started=esp_timer_get_time();
    esp_http_client_handle_t http=NULL;
    release_message("正在连接网络…",false,false);
    for(unsigned attempt=0;attempt<3&&!http;++attempt) {
        if(attempt){ESP_LOGW("ThingsOS_ota","release check retrying after a failed connection");
            vTaskDelay(pdMS_TO_TICKS(300));}
        http=release_connection_at(THINGSOS_RELEASE_ORIGIN "metalio/release.json",0,0,5000);
    }
    if(!http){
        ESP_LOGW("ThingsOS_ota","release check failed after %u ms",
                 (unsigned)((esp_timer_get_time()-started)/1000));
        release_message("检查失败，请检查 WiFi、网络和系统时间",true,true);return false;
    }
    release_message("正在获取更新信息…",false,false);
    char* json=malloc(16385);size_t used=0;bool ok=json!=NULL;
    while(ok && used<16384) {
        int n=esp_http_client_read(http,json+used,(int)(16384-used));
        if(n<0){ok=false;break;}if(!n)break;used+=(size_t)n;
    }
    ok=ok && used<16384 && esp_http_client_is_complete_data_received(http);
    release_close(http);
    ESP_LOGI("ThingsOS_ota","release check done in %u ms",
             (unsigned)((esp_timer_get_time()-started)/1000));
    thingsos_release_t* candidate=calloc(1,sizeof(*candidate));
    if(json)json[used]=0;
    ok=ok && candidate && thingsos_release_parse(json,used+1,candidate);
    free(json);
    if(ok) {
        bool available=thingsos_release_newer(candidate->version,esp_app_get_description()->version);
        portENTER_CRITICAL(&state_guard);
        release_state.release=*candidate;release_state.checked=true;release_state.available=available;
        portEXIT_CRITICAL(&state_guard);
        release_message(available?"发现新固件，请查看更新说明":"当前已是最新版本",false,true);
    } else release_message("更新信息无效或与本机固件不兼容",true,true);
    free(candidate);return ok;
}
static bool release_install(void) {
    thingsos_release_t* candidate=malloc(sizeof(*candidate));
    if(!candidate){release_message("内存不足，请稍后重试",true,true);return false;}
    portENTER_CRITICAL(&state_guard);*candidate=release_state.release;portEXIT_CRITICAL(&state_guard);
    const esp_partition_t *target=esp_ota_get_next_update_partition(NULL),*running=esp_ota_get_running_partition();
    bool ok=target && running && candidate->bytes<=target->size &&
        thingsos_release_newer(candidate->version,esp_app_get_description()->version);
    if(!ok){free(candidate);release_message("没有可安装的新固件",true,true);return false;}
    char url[256];snprintf(url,sizeof(url),THINGSOS_RELEASE_ORIGIN "%s",candidate->path);
    if(!pause_reader()) {
        free(candidate);release_message("阅读记录未保存，升级未开始",true,true);return false;
    }
    if(!sd_ready()) {
        free(candidate);release_message("在线升级需要可写 SD 卡",true,true);return false;
    }
    uint8_t* block=heap_caps_malloc(8192,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!block){free(candidate);release_message("下载缓冲区不足，升级未开始",true,true);return false;}
    release_download_t context={.ota={.target=target,.download_bytes=candidate->bytes,.version=candidate->version},
        .url=url,.started=esp_timer_get_time()};
    context.stream=(thingsos_download_t){.context=&context,.read=release_download_raw,.open=release_download_open,
        .close=release_download_close,.bytes=candidate->bytes};
    transfer_set(candidate->version,"正在下载固件到 SD 卡",0,true,false,true);
    ok=release_file_check(RELEASE_SD_FILE,candidate,block,8192);
    if(ok)context.stream.received=candidate->bytes;
    if(!ok)ok=release_sd_download(&context,candidate,block,8192);
    free(block);
    /* Cache is usable only after its complete SHA-256 matches this release.
       A failed/partial download never calls esp_ota_begin. */
    if(ok) {
        transfer_set(candidate->version,"SD 校验通过，准备进入升级程序",100,true,false,true);
        if(lock()) {
            ok=ota_result(&context.ota,"stage-rescue",thingsos_single_stage(candidate->bytes,candidate->sha256,candidate->version));
            unlock();
        }else ok=ota_result(&context.ota,"stage-lock",ESP_ERR_TIMEOUT);
    }
    portENTER_CRITICAL(&state_guard);
    release_received=context.stream.received;release_error=context.ota.error;
    strlcpy(release_stage,context.ota.stage?context.ota.stage:"transaction",sizeof(release_stage));
    portEXIT_CRITICAL(&state_guard);
    release_download_close(&context);free(candidate);
    char message[128];
    if(ok)strlcpy(message,"校验通过，正在重启",sizeof(message));
    else {
        snprintf(message,sizeof(message),"升级失败 %s (%d)，原固件保留",context.ota.stage?context.ota.stage:"transaction",context.ota.error);
        ESP_LOGE("ThingsOS_ota","%s; received=%u",message,(unsigned)context.ota.received);
    }
    release_message(message,!ok,true);
    return ok;
}
static void release_worker(void* argument) {
    bool install=(uintptr_t)argument!=0;
    bool reboot=install?release_install():(release_check(),false);
    if(reboot){vTaskDelay(pdMS_TO_TICKS(300));esp_restart();}
    portENTER_CRITICAL(&state_guard);busy=false;release_metadata_owned=false;portEXIT_CRITICAL(&state_guard);
    vTaskDelete(NULL);
}
esp_err_t thingsos_network_release_start(bool install) {
    if(!thingsos_network_connected())return ESP_ERR_INVALID_STATE;
    if(install && !ota_running_valid())return ESP_ERR_INVALID_STATE;
    portENTER_CRITICAL(&state_guard);
    bool allowed=!busy && !external_busy && !manage_job && !rescue_mode &&
        (!install || (release_state.checked && release_state.available));
    if(allowed){busy=true;release_metadata_owned=!install;release_state.active=true;release_state.failed=false;if(!install){release_state.checked=false;release_state.available=false;}}
    if(allowed){release_received=0;release_error=0;release_retries=0;release_stage[0]=0;
        release_http_error=release_http_status=release_http_errno=release_tls_error=0;}
    portEXIT_CRITICAL(&state_guard);
    if(!allowed)return ESP_ERR_INVALID_STATE;
    release_message(install?"准备下载固件":"正在检查新固件",false,false);
    /* Download block lives in PSRAM. TLS is closed before the 4 KiB flash
       transaction frame is entered, so the two stack peaks do not overlap. */
    if(xTaskCreate(release_worker,"release_update",12288,(void*)(uintptr_t)install,5,NULL)!=pdPASS) {
        release_message("无法启动升级任务，请稍后重试",true,true);
        portENTER_CRITICAL(&state_guard);busy=false;release_metadata_owned=false;portEXIT_CRITICAL(&state_guard);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
static esp_err_t release_status(httpd_req_t* req) {
    if(!authorized(req))return reject(req,"401 Unauthorized","Token required");
    thingsos_release_state_t current;thingsos_network_release_state(&current);
    cJSON* object=cJSON_CreateObject();if(!object)return ESP_ERR_NO_MEM;
    cJSON_AddStringToObject(object,"source",THINGSOS_RELEASE_ORIGIN);
    cJSON_AddStringToObject(object,"installed",esp_app_get_description()->version);
    cJSON_AddStringToObject(object,"version",current.release.version);
    cJSON_AddStringToObject(object,"notes",current.release.notes);
    cJSON_AddStringToObject(object,"message",current.message);
    cJSON_AddBoolToObject(object,"available",current.available);
    cJSON_AddBoolToObject(object,"active",current.active);
    cJSON_AddBoolToObject(object,"checked",current.checked);
    cJSON_AddBoolToObject(object,"failed",current.failed);
    portENTER_CRITICAL(&state_guard);
    size_t received=release_received;int error=release_error;unsigned retries=release_retries;char stage[32];
    int http_error=release_http_error,http_status=release_http_status,http_errno=release_http_errno,tls_error=release_tls_error;
    strlcpy(stage,release_stage,sizeof(stage));
    portEXIT_CRITICAL(&state_guard);
    cJSON_AddNumberToObject(object,"received",received);
    cJSON_AddNumberToObject(object,"error",error);
    cJSON_AddNumberToObject(object,"retries",retries);
    cJSON_AddStringToObject(object,"stage",stage);
    cJSON_AddStringToObject(object,"cache","/.thingsos/update.bin");
    cJSON_AddNumberToObject(object,"http_error",http_error);
    cJSON_AddNumberToObject(object,"http_status",http_status);
    cJSON_AddNumberToObject(object,"socket_errno",http_errno);
    cJSON_AddNumberToObject(object,"tls_error",tls_error);
    return json_response(req,object);
}
