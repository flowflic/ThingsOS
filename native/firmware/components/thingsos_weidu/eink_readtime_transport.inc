/* SPDX-License-Identifier: Apache-2.0
 * Bounded E-Ink transport, separately compiled by the host fault-injection test.
 */
typedef struct {
    char *data;
    size_t used;
    bool overflow;
} rt_response_t;
static esp_err_t rt_event(esp_http_client_event_t *event) {
    (void)event;
    if (abort_requested)
        return ESP_FAIL;
    return ESP_OK;
}
/* HTTP owns no SD mutex. A batch becomes .sent immediately before the first
   POST body byte. Connect/clock/cooldown/config failures leave it pending.
   Once claimed, even an empty/failed write stays unknown conservatively. */
static thingsos_weidu_error_t rt_http(const char *url, const char *body,
                                      const thingsos_rt_credentials_t *credentials,
                                      const thingsos_rt_batch_t *claim, cJSON **result) {
    *result = NULL;
    if (abort_requested)
        return WD_CANCEL;
    bool native = !strncmp(url, "https://i.weread.qq.com/", sizeof("https://i.weread.qq.com/") - 1);
    bool wechat =
        !strncmp(url, "https://open.weixin.qq.com/", sizeof("https://open.weixin.qq.com/") - 1) ||
        !strncmp(url, "https://long.open.weixin.qq.com/",
                 sizeof("https://long.open.weixin.qq.com/") - 1);
    if (!native && !wechat)
        return WD_PROTOCOL;
    /* SDK QR JSON also contains the server's base64 image (~68 KiB). The UI
       uses uuid to render locally, but still has to receive that JSON first.
       Keep ordinary replies small; only this anonymous QR GET gets more PSRAM. */
    static const char qr_endpoint[]="https://open.weixin.qq.com/connect/sdk/qrconnect?";
    bool qr_request=!body&&!strncmp(url,qr_endpoint,sizeof(qr_endpoint)-1);
    size_t capacity=qr_request?128u*1024u:16u*1024u;
    char *memory = thingsos_psram_calloc(1, capacity);
    if (!memory)
        return WD_MEMORY;
    rt_response_t response = {memory, 0, false};
    esp_http_client_config_t config = {.url = url,
                                       .timeout_ms = 15000,
                                       .disable_auto_redirect = true,
                                       .crt_bundle_attach = esp_crt_bundle_attach,
                                       .event_handler = rt_event,
                                       .user_data = &response,
                                       .method = body ? HTTP_METHOD_POST : HTTP_METHOD_GET};
    esp_http_client_handle_t h = esp_http_client_init(&config);
    if (!h) {
        free(memory);
        return WD_MEMORY;
    }
    esp_http_client_set_header(
        h, "User-Agent",
        "WeRead/2.1.2 WRBrand/Onyx wr_eink Dalvik/2.1.0 (Linux; U; Android 11; BOOX Build/onyx)");
    if (native) {
        esp_http_client_set_header(h, "baseapi", "30");
        esp_http_client_set_header(h, "appver", "2.1.2.10245900");
        esp_http_client_set_header(h, "basever", "2.1.2.10245900");
        esp_http_client_set_header(h, "osver", "11");
        esp_http_client_set_header(h, "channelId", "900");
        if (credentials) {
            esp_http_client_set_header(h, "vid", credentials->vid);
            esp_http_client_set_header(h, "accessToken", credentials->access);
        }
    }
    if (body)
        esp_http_client_set_header(h, "Content-Type", "application/json");
    thingsos_weidu_error_t e = WD_NETWORK;
    int status=0;
    int length = body ? (int)strlen(body) : 0;
    esp_err_t connected = esp_http_client_open(h, length);
    if (connected == ESP_OK) {
        bool permitted = !abort_requested;
        if (permitted && claim) {
            permitted = thingsos_rt_claim(client->root, work->reading.owner, claim);
            if (!permitted)
                e = WD_STORAGE;
        }
        if (permitted) {
            int sent = 0;
            while (sent < length && !abort_requested) {
                int n = esp_http_client_write(h, body + sent, length - sent);
                if (n <= 0)
                    break;
                sent += n;
            }
            if (sent == length && !abort_requested && esp_http_client_fetch_headers(h) >= 0) {
                /* Manual transport APIs deliver read() bytes directly. */
                response.used = 0;
                char scratch[512];
                int n;
                while ((n = esp_http_client_read(h, scratch, sizeof(scratch))) > 0) {
                    if ((size_t)n >= capacity - response.used) {
                        response.overflow = true;
                        break;
                    }
                    memcpy(memory + response.used, scratch, (size_t)n);
                    response.used += (size_t)n;
                    memory[response.used] = 0;
                    if (abort_requested)
                        break;
                }
                status = esp_http_client_get_status_code(h);
                if (status == 401 || status == 403)
                    e = WD_AUTH;
                else if (status == 429)
                    e = WD_LIMIT;
                else if(status==200&&response.overflow)
                    e=WD_LIMIT;
                else if (status == 200 && n == 0 && !response.overflow && !abort_requested) {
                    *result = cJSON_ParseWithLength(memory, response.used);
                    e = *result ? WD_OK : WD_PROTOCOL;
                    cJSON *code = cJSON_GetObjectItemCaseSensitive(*result, "errCode");
                    if (!code)
                        code = cJSON_GetObjectItemCaseSensitive(*result, "errcode");
                    if (cJSON_IsNumber(code) && code->valuedouble != 0)
                        e = code->valueint == -2012 ? WD_AUTH : WD_PROTOCOL;
                }
            }
        }
    } else {
        int tls_error=0;
        esp_http_client_get_and_clear_last_tls_error(h,&tls_error,NULL);
        if(tls_error==MBEDTLS_ERR_SSL_ALLOC_FAILED || tls_error==-MBEDTLS_ERR_SSL_ALLOC_FAILED)
            e=WD_MEMORY;
    }
    if (abort_requested)
        e = WD_CANCEL;
    /* No URL query, response body, QR identifier or credentials in logs. */
    if(qr_request)ESP_LOGI("ThingsOS_readtime","QR response status=%d bytes=%u budget=%u overflow=%d result=%u",
        status,(unsigned)response.used,(unsigned)capacity,response.overflow,(unsigned)e);
    else if(e!=WD_OK&&e!=WD_CANCEL)ESP_LOGW("ThingsOS_readtime","HTTP response native=%d status=%d bytes=%u budget=%u overflow=%d result=%u",
        native,status,(unsigned)response.used,(unsigned)capacity,response.overflow,(unsigned)e);
    esp_http_client_close(h);
    esp_http_client_cleanup(h);
    free(memory);
    return e;
}
