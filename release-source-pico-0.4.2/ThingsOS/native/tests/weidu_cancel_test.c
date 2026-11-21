/* SPDX-License-Identifier: Apache-2.0 */
#include "thingsos/weidu.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"Cache cancel line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_HTTP_EAGAIN 11
#define ESP_ERR_TIMEOUT 12
#define ESP_LOGI(...) ((void)0)
#define portMAX_DELAY 0xffffffffu
#define pdMS_TO_TICKS(ms) (ms)
#define pdTRUE 1
typedef int esp_err_t;
typedef int esp_http_client_handle_t;
static atomic_bool abort_requested;
static bool locked,worker_stop,cancel_wait,worker_wait,endless;
static unsigned calls,joins,scan_waits;
static bool scan_busy,cancel_on_lock;
static uint64_t clock_ms;
static unsigned wait_ms=1;
static int terminal,thingsos_io_lock=1;
static unsigned ready_call;
static void* ready_transfer;
static uint64_t now_ms(void* context){(void)context;return clock_ms;}
static size_t strlcpy(char* out,const char* in,size_t cap){size_t n=strlen(in);if(cap){size_t k=n<cap-1?n:cap-1;memcpy(out,in,k);out[k]=0;}return n;}
static struct {char reader_pc[128],reader_token[128];} workspace,*work=&workspace;
static bool reading_context_active;
#include "../firmware/components/thingsos_weidu/reader_http_body.inc"
static void dns_tick(void);
static void vTaskDelay(unsigned ticks){CHECK(ticks==1 && !locked);clock_ms+=wait_ms;if(cancel_wait)atomic_store(&abort_requested,true);if(worker_wait)worker_stop=true;dns_tick();}
static void reader_transport_step(void* transport){(void)transport;}
static esp_err_t esp_http_client_perform(int connection){CHECK(connection==7 && locked && !scan_busy);++calls;clock_ms+=2;if(calls==ready_call)((transfer_t*)ready_transfer)->reader_ready=true;return endless || calls<4?ESP_ERR_HTTP_EAGAIN:terminal;}
static void xSemaphoreGive(int lock){CHECK(lock==1 && locked);locked=false;}
static int xSemaphoreTake(int lock,unsigned timeout){
    CHECK(!locked);
    if(lock==2){CHECK(timeout==portMAX_DELAY);++joins;return pdTRUE;}
    CHECK(lock==1 && (timeout==portMAX_DELAY || timeout==10));
    if(scan_busy){CHECK(timeout==10);++scan_waits;clock_ms+=timeout;scan_busy=false;return 0;}
    locked=true;
    if(cancel_on_lock)atomic_store(&abort_requested,true);
    return pdTRUE;
}
#include "../firmware/components/thingsos_weidu/cache_http_pump.inc"
static bool stop_worker(void* context){CHECK(context==&worker_stop);return worker_stop;}
typedef struct {unsigned address;} ip_addr_t;
typedef int err_t;
#define ERR_OK 0
#define ERR_INPROGRESS 1
#define LWIP_DNS_ADDRTYPE_IPV4 0
#define ip_addr_isany(p) (!(p)->address)
static void (*pending_dns)(const char*,const ip_addr_t*,void*);
static void* pending_context;
static unsigned dns_mode,dns_live;
static uint64_t dns_at;
static void dns_deliver(void){CHECK(pending_dns);ip_addr_t ip={1};void(*fn)(const char*,const ip_addr_t*,void*)=pending_dns;void* ctx=pending_context;pending_dns=NULL;pending_context=NULL;fn("weread.qq.com",&ip,ctx);}
static void dns_tick(void){if(pending_dns && clock_ms>=dns_at)dns_deliver();}
static err_t dns_gethostbyname_addrtype(const char* host,ip_addr_t* address,void(*cb)(const char*,const ip_addr_t*,void*),void* context,int type){
    CHECK(!locked && !strcmp(host,"weread.qq.com") && type==0);
    if(dns_mode==0){address->address=1;return ERR_OK;}
    if(dns_mode==2)return -1;
    CHECK(!pending_dns);pending_dns=cb;pending_context=context;return ERR_INPROGRESS;
}
static err_t tcpip_callback(void(*fn)(void*),void* context){if(dns_mode==3)return -1;fn(context);return ERR_OK;}
static void* dns_alloc(size_t n,size_t size){void* p=calloc(n,size);if(p)++dns_live;return p;}
static void dns_free(void* p){CHECK(p && dns_live);--dns_live;free(p);}
#define calloc dns_alloc
#define free dns_free
#include "../firmware/components/thingsos_weidu/cache_dns.inc"
#undef calloc
#undef free
typedef struct {atomic_bool stop;int done;} prefetch_t;
static int xTaskGetCurrentTaskHandle(void){return 1;}
static int xSemaphoreGetMutexHolder(int lock){CHECK(lock==1);return locked?1:0;}
#include "../firmware/components/thingsos_weidu/prefetch_join.inc"
static void reset(void){CHECK(!pending_dns && !dns_live);atomic_store(&abort_requested,false);locked=worker_stop=cancel_wait=worker_wait=endless=scan_busy=cancel_on_lock=false;calls=joins=ready_call=scan_waits=0;ready_transfer=NULL;clock_ms=0;wait_ms=1;terminal=ESP_OK;dns_mode=0;dns_at=30;}
int main(void){
    transfer_t t={0};reset();CHECK(cache_http_perform(7,&t)==ESP_OK && calls==4 && !locked);
    reset();scan_busy=true;CHECK(cache_http_perform(7,&t)==ESP_OK && scan_waits==1 && calls==4 && !locked);
    reset();cancel_on_lock=true;CHECK(cache_http_perform(7,&t)==ESP_FAIL && t.error==WD_CANCEL && !calls && !locked);
    t=(transfer_t){0};
    reset();terminal=-9;CHECK(cache_http_perform(7,&t)==-9 && calls==4);
    reset();endless=cancel_wait=true;t=(transfer_t){0};
    CHECK(cache_http_perform(7,&t)!=ESP_OK && t.error==WD_CANCEL && calls==1 && clock_ms<=3);
    reset();endless=worker_wait=true;t=(transfer_t){.cancelled=stop_worker,.cancel_context=&worker_stop};
    CHECK(cache_http_perform(7,&t)!=ESP_OK && t.error==WD_CANCEL && calls==1 && !atomic_load(&abort_requested));
    reset();endless=true;t=(transfer_t){.cancelled=stop_worker,.cancel_context=&worker_stop};worker_stop=true;
    CHECK(cache_http_perform(7,&t)!=ESP_OK && t.error==WD_CANCEL && !calls);
    reset();endless=true;wait_ms=300001;t=(transfer_t){0};CHECK(cache_http_perform(7,&t)==ESP_ERR_TIMEOUT && calls==1);
    reset();endless=true;wait_ms=1000;t=(transfer_t){.phase=2,.response_timeout_ms=25000};
    CHECK(cache_http_perform(7,&t)==ESP_ERR_TIMEOUT && t.error==WD_OK && clock_ms<26000 && !locked);
    reset();wait_ms=10000;t=(transfer_t){.phase=5,.response_timeout_ms=25000};
    CHECK(cache_http_perform(7,&t)==ESP_OK && calls==4 && clock_ms>25000 && !locked);
    reset();endless=true;t=(transfer_t){0};ready_transfer=&t;ready_call=2;
    CHECK(cache_http_perform(7,&t)==ESP_OK && calls==2 && t.reader_ready);
    reset();CHECK(cache_dns_ready("weread.qq.com",NULL,NULL)==WD_OK && !dns_live);
    reset();dns_mode=1;CHECK(cache_dns_ready("weread.qq.com",NULL,NULL)==WD_OK && clock_ms==30 && !dns_live);
    reset();dns_mode=2;CHECK(cache_dns_ready("weread.qq.com",NULL,NULL)==WD_DNS && !dns_live);
    reset();dns_mode=3;CHECK(cache_dns_ready("weread.qq.com",NULL,NULL)==WD_DNS && !dns_live);
    reset();dns_mode=1;dns_at=10000;cancel_wait=true;
    CHECK(cache_dns_ready("weread.qq.com",NULL,NULL)==WD_CANCEL && clock_ms==1 && dns_live==1);dns_deliver();CHECK(!dns_live);
    reset();dns_mode=1;dns_at=10000;
    CHECK(cache_dns_ready("weread.qq.com",NULL,NULL)==WD_DNS && clock_ms==5000 && dns_live==1);dns_deliver();CHECK(!dns_live);
    reset();prefetch_t p;atomic_init(&p.stop,false);p.done=2;locked=true;prefetch_join(&p);
    CHECK(atomic_load(&p.stop) && joins==2 && locked);locked=false;joins=0;prefetch_join(&p);CHECK(joins==2 && !locked);
    puts("PASS: short-slice cache cancellation, DNS late-callback ownership, and mutex-free prefetch join");return 0;
}
