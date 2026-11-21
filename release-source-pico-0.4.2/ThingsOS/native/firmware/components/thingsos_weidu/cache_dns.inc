/* SPDX-License-Identifier: Apache-2.0
 * Resolve on lwIP's owning task. Caller and pending DNS callback each own one
 * reference so cancellation/timeout cannot free memory still used by lwIP.
 */
typedef struct {atomic_uint refs;atomic_bool done;bool resolved;char host[128];} cache_dns_t;
static void cache_dns_release(cache_dns_t* q) {
    if(atomic_fetch_sub(&q->refs,1)==1)free(q);
}
static void cache_dns_found(const char* host,const ip_addr_t* address,void* context) {
    (void)host;cache_dns_t* q=context;q->resolved=address && !ip_addr_isany(address);
    atomic_store_explicit(&q->done,true,memory_order_release);cache_dns_release(q);
}
static void cache_dns_start(void* context) {
    cache_dns_t* q=context;ip_addr_t address;
    err_t result=dns_gethostbyname_addrtype(q->host,&address,cache_dns_found,q,LWIP_DNS_ADDRTYPE_IPV4);
    if(result!=ERR_INPROGRESS)cache_dns_found(q->host,result==ERR_OK?&address:NULL,q);
}
static thingsos_weidu_error_t cache_dns_ready(const char* host,bool (*stop)(void*),void* context) {
    cache_dns_t* q=calloc(1,sizeof(*q));if(!q)return WD_MEMORY;
    if(strlcpy(q->host,host,sizeof(q->host))>=sizeof(q->host)){free(q);return WD_LIMIT;}
    atomic_init(&q->refs,2);atomic_init(&q->done,false);
    if(tcpip_callback(cache_dns_start,q)!=ERR_OK){cache_dns_release(q);cache_dns_release(q);return WD_DNS;}
    uint64_t at=now_ms(NULL);thingsos_weidu_error_t result=WD_DNS;
    for(;;) {
        if(abort_requested || (stop && stop(context))){result=WD_CANCEL;break;}
        if(atomic_load_explicit(&q->done,memory_order_acquire)){result=q->resolved?WD_OK:WD_DNS;break;}
        if(now_ms(NULL)-at>=5000)break;
        vTaskDelay(1);
    }
    cache_dns_release(q);return result;
}
