/* SPDX-License-Identifier: Apache-2.0
 * Exercise the production adapter against a delayed connection and an endless
 * readable stream. The fake SDK honors the timeout actually passed to it. */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"transport line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
typedef struct transport* esp_transport_handle_t;
typedef int (*connect_fn)(esp_transport_handle_t,const char*,int,int);
typedef int (*read_fn)(esp_transport_handle_t,char*,int,int);
typedef int (*write_fn)(esp_transport_handle_t,const char*,int,int);
typedef int (*action_fn)(esp_transport_handle_t);
typedef int (*poll_fn)(esp_transport_handle_t,int);
struct transport {void* context;connect_fn connect;read_fn read;write_fn write;action_fn close,destroy;};
static uint64_t clock_ms;
static unsigned creates,destroys,connects,reads,writes,certs,bundles;
static bool no_data,write_blocked,fail_connect;
static int write_result;
#define ESP_TLS_ERR_SSL_WANT_READ -0x6900
#define ESP_TLS_ERR_SSL_WANT_WRITE -0x6880
static uint64_t now_ms(void* p){(void)p;return clock_ms;}
static const char weread_root_pem[]="trusted-root";
#define ESP_TLS_AF_INET 4
static int esp_crt_bundle_attach(void* p){(void)p;return 0;}
static esp_transport_handle_t esp_transport_init(void){++creates;return calloc(1,sizeof(struct transport));}
static esp_transport_handle_t esp_transport_ssl_init(void){return esp_transport_init();}
static void* esp_transport_get_context_data(esp_transport_handle_t t){return t->context;}
static void esp_transport_set_context_data(esp_transport_handle_t t,void* p){t->context=p;}
static void esp_transport_set_default_port(esp_transport_handle_t t,int p){(void)t;CHECK(p==443);}
static void esp_transport_ssl_crt_bundle_attach(esp_transport_handle_t t,int(*f)(void*)){(void)t;CHECK(f==esp_crt_bundle_attach);++bundles;}
static void esp_transport_ssl_set_cert_data(esp_transport_handle_t t,const char* s,int n){(void)t;CHECK(s==weread_root_pem && n==strlen(s));++certs;}
static void esp_transport_ssl_set_addr_family(esp_transport_handle_t t,int f){(void)t;CHECK(f==4);}
static void esp_transport_set_func(esp_transport_handle_t t,connect_fn c,read_fn r,write_fn w,action_fn close,poll_fn pr,poll_fn pw,action_fn d){(void)pr;(void)pw;t->connect=c;t->read=r;t->write=w;t->close=close;t->destroy=d;}
static void esp_transport_set_async_connect_func(esp_transport_handle_t t,connect_fn c){t->connect=c;}
static int esp_transport_connect_async(esp_transport_handle_t t,const char* h,int p,int timeout){
    if(t->connect)return t->connect(t,h,p,timeout);
    CHECK(timeout==1 && p==443 && !strcmp(h,"weread.qq.com"));++connects;clock_ms+=timeout;
    return fail_connect?-1:clock_ms>=35?1:0;
}
static int esp_transport_read(esp_transport_handle_t t,char* b,int n,int timeout){
    if(t->read)return t->read(t,b,n,timeout);
    CHECK(timeout==1);++reads;clock_ms+=no_data?timeout:3;
    if(no_data)return 0;memset(b,'x',n);return n;
}
static int esp_transport_write(esp_transport_handle_t t,const char* b,int n,int timeout){
    if(t->write)return t->write(t,b,n,timeout);
    CHECK(timeout==1);++writes;clock_ms+=timeout;return write_blocked?0:write_result?write_result:n;
}
static int esp_transport_close(esp_transport_handle_t t){return t->close?t->close(t):0;}
static int esp_transport_destroy(esp_transport_handle_t t){if(t->destroy)t->destroy(t);free(t);++destroys;return 0;}
#include "../firmware/components/thingsos_weidu/reader_transport.inc"
int main(void){
    esp_transport_handle_t t=reader_transport_create(false);CHECK(t && certs==1 && !bundles);
    unsigned turns=0;int result;
    do {reader_transport_step(t);uint64_t before=clock_ms;result=esp_transport_connect_async(t,"weread.qq.com",443,15000);
        CHECK(clock_ms-before<=1);++turns;++clock_ms;}while(!result);
    CHECK(result==1 && turns>10); /* 35 ms connect never becomes a single 15 s hold */
    char b[32];reader_transport_step(t);uint64_t before=clock_ms;unsigned chunks=0;
    while(esp_transport_read(t,b,sizeof(b),15000)>0)++chunks;
    CHECK(errno==EAGAIN && chunks>=3 && clock_ms-before<=12);
    no_data=true;reader_transport_step(t);before=clock_ms;errno=0;
    CHECK(!esp_transport_read(t,b,sizeof(b),15000) && errno==EAGAIN && clock_ms-before==1);
    write_blocked=true;reader_transport_step(t);errno=0;
    CHECK(esp_transport_write(t,b,sizeof(b),15000)==-1 && errno==EAGAIN);
    write_blocked=false;write_result=7;reader_transport_step(t);errno=0;
    CHECK(esp_transport_write(t,b,sizeof(b),15000)==7 && errno==EAGAIN);
    write_result=ESP_TLS_ERR_SSL_WANT_WRITE;reader_transport_step(t);errno=0;
    CHECK(esp_transport_write(t,b,sizeof(b),15000)==-1 && errno==EAGAIN);
    write_result=ESP_TLS_ERR_SSL_WANT_READ;reader_transport_step(t);errno=0;
    CHECK(esp_transport_write(t,b,sizeof(b),15000)==-1 && errno==EAGAIN);
    /* A real failure must not inherit a previous slice's EAGAIN. */
    write_result=-77;reader_transport_step(t);errno=EAGAIN;
    CHECK(esp_transport_write(t,b,sizeof(b),15000)==-77 && errno!=EAGAIN);
    write_result=0;reader_transport_step(t);errno=EAGAIN;
    CHECK(esp_transport_write(t,b,sizeof(b),15000)==sizeof(b) && errno!=EAGAIN);
    fail_connect=true;CHECK(esp_transport_connect_async(t,"weread.qq.com",443,15000)==-1);
    esp_transport_close(t);esp_transport_destroy(t);CHECK(creates==destroys);
    t=reader_transport_create(true);CHECK(t && bundles==1 && certs==1);esp_transport_destroy(t);
    CHECK(creates==destroys);puts("PASS: bounded connect/read/write slices, stream yields, trust and ownership");return 0;
}
