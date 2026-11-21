/* SPDX-License-Identifier: Apache-2.0
 * Public ESP-IDF transport composition. Reading jobs serialize display access;
 * cache channels run independently. Waits are 1 ms, with 10 ms read slices.
 * The caller owns this transport separately from esp_http_client. */
typedef struct {esp_transport_handle_t tls;uint64_t until;} reader_transport_t;
static reader_transport_t* reader_transport_data(esp_transport_handle_t t) {
    return esp_transport_get_context_data(t);
}
static void reader_transport_step(esp_transport_handle_t t) {
    if(t)reader_transport_data(t)->until=now_ms(NULL)+10;
}
static bool reader_transport_yield(esp_transport_handle_t t) {
    if(now_ms(NULL)<reader_transport_data(t)->until)return false;
    errno=EAGAIN;return true;
}
static int reader_transport_connect(esp_transport_handle_t t,const char* host,int port,int timeout) {
    (void)timeout;
    int result=esp_transport_connect_async(reader_transport_data(t)->tls,host,port,1);
    return result;
}
static int reader_transport_read(esp_transport_handle_t t,char* data,int n,int timeout) {
    (void)timeout;if(reader_transport_yield(t))return 0;
    int result=esp_transport_read(reader_transport_data(t)->tls,data,n,1);
    if(!result)errno=EAGAIN;
    return result;
}
static int reader_transport_write(esp_transport_handle_t t,const char* data,int n,int timeout) {
    (void)timeout;if(reader_transport_yield(t))return -1;
    /* esp_http_client's async POST state resumes a short write only when
       errno is EAGAIN. Preserve its byte count and cursor; never restart POST.
       TLS WANT_* values are transient as well, despite being negative. */
    errno=0;
    int result=esp_transport_write(reader_transport_data(t)->tls,data,n,1);
    if(!result || result==ESP_TLS_ERR_SSL_WANT_READ || result==ESP_TLS_ERR_SSL_WANT_WRITE){errno=EAGAIN;return -1;}
    if(result>0 && result<n)errno=EAGAIN;
    return result;
}
static int reader_transport_poll(esp_transport_handle_t t,int timeout) {(void)t;(void)timeout;return 1;}
static int reader_transport_close(esp_transport_handle_t t) {
    return esp_transport_close(reader_transport_data(t)->tls);
}
static int reader_transport_destroy(esp_transport_handle_t t) {
    reader_transport_t* state=reader_transport_data(t);
    if(state){esp_transport_destroy(state->tls);free(state);}
    return 0;
}
static esp_transport_handle_t reader_transport_create(bool bundle) {
    esp_transport_handle_t t=esp_transport_init();
    reader_transport_t* state=calloc(1,sizeof(*state));
    if(!t || !state){if(t)esp_transport_destroy(t);free(state);return NULL;}
    state->tls=esp_transport_ssl_init();
    if(!state->tls){esp_transport_destroy(t);free(state);return NULL;}
    if(bundle)esp_transport_ssl_crt_bundle_attach(state->tls,esp_crt_bundle_attach);
    else esp_transport_ssl_set_cert_data(state->tls,weread_root_pem,strlen(weread_root_pem));
    esp_transport_ssl_set_addr_family(state->tls,ESP_TLS_AF_INET);
    esp_transport_set_context_data(t,state);
    esp_transport_set_default_port(t,443);
    esp_transport_set_func(t,NULL,reader_transport_read,reader_transport_write,reader_transport_close,
        reader_transport_poll,reader_transport_poll,reader_transport_destroy);
    esp_transport_set_async_connect_func(t,reader_transport_connect);
    return t;
}
