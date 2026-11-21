/* SPDX-License-Identifier: Apache-2.0 */
#include "thingsos/weidu.h"
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"HTTP test line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static size_t strlcpy(char* dst,const char* src,size_t cap){size_t n=strlen(src);if(cap){size_t take=n<cap-1?n:cap-1;memcpy(dst,src,take);dst[take]=0;}return n;}
static struct {char reader_pc[128],reader_token[128];} workspace,*work=&workspace;
static bool reading_context_active;
#include "../firmware/components/thingsos_weidu/reader_http_body.inc"
typedef int esp_err_t;
typedef int esp_http_client_handle_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_HTTP_EAGAIN 11
#define ESP_ERR_TIMEOUT 12
#define ESP_LOGI(...) ((void)0)
#define portMAX_DELAY 0xffffffffu
static bool locked,abort_requested,cancel_wait;
static unsigned calls,gives,takes,ui_frames,wait_ms;
static uint64_t clock_ms;
static int terminal;
static unsigned ready_call;
static transfer_t* active_transfer;
static int thingsos_io_lock;
static uint64_t now_ms(void* unused){(void)unused;return clock_ms;}
static void xSemaphoreGive(int lock){(void)lock;CHECK(locked);locked=false;++gives;}
static void xSemaphoreTake(int lock,unsigned timeout){(void)lock;(void)timeout;CHECK(!locked);locked=true;++takes;}
static void vTaskDelay(unsigned ticks){CHECK(ticks==1 && !locked);++ui_frames;clock_ms+=wait_ms;if(cancel_wait)abort_requested=true;}
static esp_err_t esp_http_client_perform(int connection){CHECK(connection==7 && locked);++calls;clock_ms+=2;if(ready_call==calls)active_transfer->reader_ready=true;return calls<4?ESP_ERR_HTTP_EAGAIN:terminal;}
static void reader_transport_step(void* transport){(void)transport;}
#include "../firmware/components/thingsos_weidu/reader_http_pump.inc"
/* Exercise the actual DNS preflight: even a 20-second failure must release
   the display mutex, return the original error, and send no HTTP request. */
#define AF_INET 2
#define SOCK_STREAM 1
struct addrinfo { int ai_family, ai_socktype; };
static bool dns_failure, dns_cancel;
static unsigned dns_frees;
static int getaddrinfo(const char* host,const char* port,const struct addrinfo* hints,struct addrinfo** out) {
    static struct addrinfo address;
    CHECK(!locked && !strcmp(host,"weread.qq.com") && !port && hints->ai_family==AF_INET && hints->ai_socktype==SOCK_STREAM);
    clock_ms+=20000; ++ui_frames;
    if(dns_cancel)abort_requested=true;
    *out=dns_failure?NULL:&address;
    return dns_failure?-1:0;
}
static void freeaddrinfo(struct addrinfo* address){CHECK(!locked && address);++dns_frees;}
#include "../firmware/components/thingsos_weidu/reader_dns.inc"
static void reset(void){locked=true;abort_requested=cancel_wait=false;calls=gives=takes=ui_frames=ready_call=0;active_transfer=NULL;clock_ms=0;wait_ms=1;terminal=ESP_OK;}
int main(void){
    reset();CHECK(reader_dns_ready("weread.qq.com")==WD_OK && locked && gives==1 && takes==1 && ui_frames==1 && dns_frees==1 && !calls);
    reset();dns_failure=true;CHECK(reader_dns_ready("weread.qq.com")==WD_DNS && locked && gives==takes && ui_frames==1 && !calls);
    reset();dns_cancel=true;CHECK(reader_dns_ready("weread.qq.com")==WD_CANCEL && locked && gives==takes && !calls);
    dns_failure=dns_cancel=false;
    transfer_t t={0};reset();
    CHECK(reader_http_perform(7,&t)==ESP_OK && locked && calls==4 && gives==3 && takes==3 && ui_frames==3);
    reset();terminal=-9;CHECK(reader_http_perform(7,&t)==-9 && locked && calls==4); /* Terminal POST errors are not replayed. */
    reset();abort_requested=true;t.error=WD_OK;CHECK(reader_http_perform(7,&t)==ESP_FAIL && t.error==WD_CANCEL && locked && !calls);
    reset();cancel_wait=true;t.error=WD_OK;CHECK(reader_http_perform(7,&t)!=ESP_OK && t.error==WD_CANCEL && locked && calls==1 && gives==takes);
    reset();wait_ms=60001;t.error=WD_OK;CHECK(reader_http_perform(7,&t)==ESP_ERR_TIMEOUT && locked && calls==1 && gives==takes);
    reset();t.error=WD_LIMIT;CHECK(reader_http_perform(7,&t)==ESP_FAIL && locked && calls==1);
    char memory[9];memset(memory,'!',sizeof(memory));t=(transfer_t){.memory=memory,.limit=8};
    reader_http_data(&t,(const unsigned char*)"abcd",4,200);reader_http_data(&t,(const unsigned char*)"efgh",4,200);
    CHECK(t.bytes==8 && !memcmp(memory,"abcdefgh!",9));reader_http_data(&t,(const unsigned char*)"x",1,200);
    CHECK(t.error==WD_LIMIT && t.bytes==8 && memory[8]=='!');
    t=(transfer_t){.memory=memory,.limit=8};reader_http_data(&t,(const unsigned char*)"secret",6,403);CHECK(t.bytes==6 && !memcmp(memory,"abcdefgh!",9));
    t=(transfer_t){.file=tmpfile(),.limit=8};CHECK(t.file);reader_http_data(&t,(const unsigned char*)"cache",5,200);rewind(t.file);CHECK(fread(memory,1,5,t.file)==5 && !memcmp(memory,"cache",5));fclose(t.file);
    thingsos_weidu_ps_t ps={0},pc={0},token={0};reading_context_active=true;
    t=(transfer_t){.memory=memory,.limit=512,.extract_reader=true,.ps=&ps,.pc=&pc,.token=&token};
    const char* json="{\"pclts\":\"clock\",\"token\":\"session\",\"psvts\":\"opaque\"}";
    for(size_t i=0;i<strlen(json);++i)reader_http_data(&t,(const unsigned char*)json+i,1,200);
    CHECK(t.error==WD_OK && t.reader_ready && !strcmp(memory,"opaque") && !strcmp(work->reader_pc,"clock") && !strcmp(work->reader_token,"session"));
    size_t prefix=t.bytes;reader_http_data(&t,(const unsigned char*)"unneeded long HTML tail",22,200);
    CHECK(t.bytes==prefix && t.error==WD_OK);
    /* The chapter path needs only psvts; full reading sessions must wait for
       all three fields even if psvts occurs before the others. */
    memset(&ps,0,sizeof(ps));memset(&pc,0,sizeof(pc));memset(&token,0,sizeof(token));
    t=(transfer_t){.memory=memory,.limit=512,.extract_reader=true,.ps=&ps,.pc=&pc,.token=&token};
    const char* partial="{\"psvts\":\"opaque\"}";
    reader_http_data(&t,(const unsigned char*)partial,strlen(partial),200);CHECK(!t.reader_ready);
    reading_context_active=false;memset(&ps,0,sizeof(ps));t.bytes=0;
    reader_http_data(&t,(const unsigned char*)partial,strlen(partial),403);CHECK(!t.reader_ready);
    t.bytes=0;reader_http_data(&t,(const unsigned char*)partial,strlen(partial),200);CHECK(t.reader_ready);
    reset();t=(transfer_t){0};active_transfer=&t;ready_call=2;
    CHECK(reader_http_perform(7,&t)==ESP_OK && calls==2 && gives==takes && t.reader_ready);
    return 0;
}
