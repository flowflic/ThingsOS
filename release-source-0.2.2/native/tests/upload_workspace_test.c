/* SPDX-License-Identifier: Apache-2.0 */
/* Exercise the actual request handler with socket/SD fault injection. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"upload workspace:%d: %s\n",__LINE__,#x);exit(1);}}while(0)
typedef int esp_err_t;
typedef int wifi_ps_type_t;
typedef struct {int content_len;unsigned cursor;} httpd_req_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define WIFI_PS_NONE 0
#define WIFI_PS_MIN_MODEM 1
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define ESP_LOGI(...) ((void)0)
#define pdMS_TO_TICKS(x) (x)
#ifndef S_ISREG
#define S_ISREG(x) (true)
#endif
static bool busy,service_enabled=true,manage_job,upload_cancel_requested;
static bool held,oom,open_fail,write_fail,sync_fail,prepared=true,card_present=true,last_failed;
static unsigned allocations,frees,live,opens,closes,receives,writes,commits,deletes,fail_receive_at,cancel_at,lose_card_at;
static size_t written_bytes;
static unsigned yields,recv_limit=1440;
static FILE* opened;
static unsigned ps=WIFI_PS_MIN_MODEM;
static uint64_t tick;
static int reply_status;
static unsigned char payload(unsigned at){return (unsigned char)(at*37u+11u);}
static void* heap_caps_malloc(size_t bytes,unsigned caps){
    CHECK(bytes==16384 && caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));++allocations;
    if(oom)return NULL;void* p=malloc(bytes);CHECK(p);++live;return p;
}
static void heap_caps_free(void* p){CHECK(p && live==1);--live;++frees;free(p);}
static bool authorized(httpd_req_t* r){(void)r;return true;}
static bool filename(httpd_req_t* r,char* out,int* kind){(void)r;strcpy(out,"song.mp3");*kind=4;return true;}
static bool request_directory(httpd_req_t* r,char* out){(void)r;strcpy(out,"/sdcard/music");return true;}
static bool sd_ready(void){return card_present;}
static bool lock(void){CHECK(!held);held=true;return true;}
static void unlock(void){CHECK(held);held=false;}
static bool claim_transfer(void){busy=true;return true;}
static int httpd_req_get_hdr_value_str(httpd_req_t* r,const char* a,char* b,size_t n){(void)r;(void)a;(void)b;(void)n;return -1;}
static int test_stat(const char* p,struct stat* s){(void)p;(void)s;return -1;}
static int test_mkdir(const char* p,int m){(void)p;(void)m;return 0;}
static bool thingsos_collision_name(const char* n,unsigned c,char* out){(void)n;(void)c;(void)out;return false;}
static int esp_vfs_fat_info(const char* p,uint64_t* total,uint64_t* available){(void)p;*total=*available=1024u*1024u*1024u;return ESP_OK;}
static bool pause_reader(void){return prepared;}
static FILE* test_fopen(const char* p,const char* mode){
    (void)p;CHECK(held && !strcmp(mode,"wb") && live==1);++opens;
    if(open_fail)return NULL;opened=tmpfile();CHECK(opened);return opened;
}
static size_t test_fwrite(const void* p,size_t size,size_t count,FILE* f){
    CHECK(held && f==opened && live==1);++writes;if(write_fail)return 0;
    size_t n=fwrite(p,size,count,f);written_bytes+=n*size;
    if(lose_card_at && writes==lose_card_at)card_present=false;return n;
}
static int test_fclose(FILE* f){
    CHECK(held && f==opened && !live);++closes;CHECK(!fflush(f) && !fseek(f,0,SEEK_SET));
    for(unsigned i=0;i<written_bytes;++i)CHECK(fgetc(f)==payload(i));
    CHECK(fgetc(f)==EOF);opened=NULL;return fclose(f);
}
static int test_fsync(int fd){(void)fd;CHECK(!live);return sync_fail?-1:0;}
static int test_rename(const char* a,const char* b){(void)a;(void)b;CHECK(held && !live && !opened);++commits;return 0;}
static int test_unlink(const char* p){(void)p;CHECK(held && !live && !opened);++deletes;return 0;}
static void transfer_set(const char* n,const char* m,unsigned percent,bool active,bool failed,bool ota){
    (void)n;(void)m;(void)percent;(void)active;(void)ota;last_failed=failed;
}
static int esp_wifi_get_ps(wifi_ps_type_t* p){*p=(int)ps;return ESP_OK;}
static int esp_wifi_set_ps(wifi_ps_type_t p){ps=(unsigned)p;return ESP_OK;}
static int64_t esp_timer_get_time(void){tick+=100;return (int64_t)tick;}
static int httpd_req_recv(httpd_req_t* r,char* out,size_t cap){
    CHECK(!held && live==1 && cap<=16384);++receives;
    if(fail_receive_at && receives==fail_receive_at)return -1;
    size_t n=(size_t)r->content_len-r->cursor;if(n>cap)n=cap;if(n>recv_limit)n=recv_limit;
    for(unsigned i=0;i<n;++i)out[i]=(char)payload(r->cursor+i);r->cursor+=(unsigned)n;
    if(cancel_at && receives==cancel_at)upload_cancel_requested=true;return (int)n;
}
static void vTaskDelay(unsigned ticks){CHECK(ticks==1);++yields;}
static unsigned long esp_random(void){return 123;}
static int reject(httpd_req_t* r,const char* status,const char* message){(void)r;(void)message;reply_status=atoi(status);return ESP_FAIL;}
static int httpd_resp_sendstr(httpd_req_t* r,const char* message){(void)r;(void)message;reply_status=200;return ESP_OK;}
#define stat(p,s) test_stat(p,s)
#define mkdir(p,m) test_mkdir(p,m)
#define fopen(p,m) test_fopen(p,m)
#define fwrite(p,s,n,f) test_fwrite(p,s,n,f)
#define fclose(f) test_fclose(f)
#define fsync(fd) test_fsync(fd)
#define fileno(f) _fileno(f)
#define rename(a,b) test_rename(a,b)
#define unlink(p) test_unlink(p)
#include "../firmware/components/thingsos_net/upload.inc"
static void reset(void){
    CHECK(!held && !live && !opened);busy=false;upload_cancel_requested=false;
    oom=open_fail=write_fail=sync_fail=last_failed=false;prepared=card_present=true;
    allocations=frees=opens=closes=receives=writes=commits=deletes=fail_receive_at=cancel_at=lose_card_at=0;
    written_bytes=0;ps=WIFI_PS_MIN_MODEM;reply_status=0;yields=0;recv_limit=1440;
}
int main(void){
    for(unsigned i=0;i<12;++i){reset();httpd_req_t req={.content_len=(int)(40001+i)};
        CHECK(upload(&req)==ESP_OK && !busy && !held && !live && !last_failed);
        CHECK(allocations==1 && frees==1 && opens==1 && closes==1 && commits==1 && !deletes);
        CHECK(written_bytes==(unsigned)req.content_len && ps==WIFI_PS_MIN_MODEM);
        CHECK(writes==3 && yields==3 && receives>writes);}
    const unsigned sizes[]={1,1439,1440,16383,16384,16385,32768};
    for(unsigned i=0;i<sizeof(sizes)/sizeof(sizes[0]);++i)for(unsigned tiny=0;tiny<2;++tiny){
        reset();if(tiny)recv_limit=137;httpd_req_t req={.content_len=(int)sizes[i]};
        CHECK(upload(&req)==ESP_OK&&written_bytes==sizes[i]&&!live&&!held);
        CHECK(writes==(sizes[i]+16383)/16384&&yields==writes);
    }
    reset();oom=true;httpd_req_t req={.content_len=40001};
    CHECK(upload(&req)==ESP_FAIL && reply_status==503 && last_failed && !busy);
    CHECK(allocations==1 && !frees && !opens && !receives && !live && !held);
    for(unsigned fault=0;fault<5;++fault){reset();req=(httpd_req_t){.content_len=40001};
        if(fault==0)open_fail=true;if(fault==1)fail_receive_at=2;if(fault==2)cancel_at=2;
        if(fault==3)write_fail=true;if(fault==4)sync_fail=true;
        CHECK(upload(&req)==ESP_FAIL && last_failed && !busy && !held && !live && !opened);
        CHECK(allocations==1 && frees==1 && !commits && deletes==1 && ps==WIFI_PS_MIN_MODEM);
        CHECK(closes==(open_fail?0u:1u));}
    reset();cancel_at=2;req=(httpd_req_t){.content_len=40001};
    CHECK(upload(&req)==ESP_FAIL&&!writes&&!written_bytes&&deletes==1);
    reset();lose_card_at=2;req=(httpd_req_t){.content_len=40001};
    CHECK(upload(&req)==ESP_FAIL && frees==1 && closes==1 && deletes==1 && !commits && !live && !busy);
    reset();prepared=false;req=(httpd_req_t){.content_len=40001};
    CHECK(upload(&req)==ESP_FAIL && !allocations && !receives && !held && !busy);
    puts("PASS: actual upload handler uses strict PSRAM; exact streamed bytes; repeated uploads; OOM before file/body; cleanup on open/socket/write/sync/cancel/card faults");
    return 0;
}
