// SPDX-License-Identifier: Apache-2.0
/* IMA authenticates with API-key headers, never Basic/Digest. IDF perform()
   tries add_auth() on HTTP 401 and can replace that response with
   ESP_ERR_NOT_SUPPORTED before reading its body. Use the streaming API so
   the HTTP response and IMA error remain available to every caller. */
static char* ima_http_error(int status,const char* body){
    cJSON* remote=cJSON_Parse(body);
    cJSON* code=cJSON_GetObjectItemCaseSensitive(remote,"code");
    const char* detail=string(remote,"msg");char text[192];
    if(status==401)snprintf(text,sizeof(text),"ima 认证失败 (HTTP 401)，请核对 Client ID 和 API Key");
    else if(status==403)snprintf(text,sizeof(text),"ima 拒绝访问 (HTTP 403)，请检查凭证与知识库权限");
    else if(status==429)snprintf(text,sizeof(text),"ima 请求过于频繁 (HTTP 429)，请稍后重试");
    else snprintf(text,sizeof(text),"ima 服务返回 HTTP %d%s%.96s",status,detail?"：":"",detail?detail:"");
    message(text);
    cJSON* error=cJSON_CreateObject();char* result=NULL;
    if(error){cJSON_AddNumberToObject(error,"code",cJSON_IsNumber(code)&&code->valuedouble!=0?code->valuedouble:status);
        cJSON_AddStringToObject(error,"msg",text);result=cJSON_PrintUnformatted(error);cJSON_Delete(error);}
    cJSON_Delete(remote);return result;
}
static char* rpc(void* context,const char* method,const char* body){
    (void)context;if(s->cancel)return NULL;
    char url[192];snprintf(url,sizeof(url),"https://ima.qq.com/openapi/wiki/v1/%s",method);
    char* response=heap_caps_calloc(1,RESPONSE_MAX+1,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!response){message("ima 请求内存不足");return NULL;}
    esp_http_client_config_t c={.url=url,.method=HTTP_METHOD_POST,.crt_bundle_attach=esp_crt_bundle_attach,
        .timeout_ms=15000,.disable_auto_redirect=true,.max_authorization_retries=-1,.buffer_size=2048,.buffer_size_tx=4096};
    esp_http_client_handle_t http=esp_http_client_init(&c);esp_err_t error=ESP_ERR_NO_MEM;
    bool complete=false;int status=0;size_t used=0;
    if(http){
        error=esp_http_client_set_header(http,"Content-Type","application/json; charset=utf-8");
        if(error==ESP_OK)error=esp_http_client_set_header(http,"ima-openapi-clientid",s->config.client);
        if(error==ESP_OK)error=esp_http_client_set_header(http,"ima-openapi-apikey",s->config.key);
        size_t length=strlen(body);
        if(error==ESP_OK)error=esp_http_client_open(http,(int)length);
        for(size_t sent=0;error==ESP_OK&&sent<length;){
            if(s->cancel){error=ESP_FAIL;break;}
            int n=esp_http_client_write(http,body+sent,(int)(length-sent));
            if(n<=0){error=ESP_FAIL;break;}sent+=(unsigned)n;
        }
        if(error==ESP_OK){
            if(esp_http_client_fetch_headers(http)<0)error=ESP_FAIL;
            else status=esp_http_client_get_status_code(http);
        }
        while(error==ESP_OK){
            if(s->cancel){error=ESP_FAIL;break;}
            char overflow;size_t remaining=RESPONSE_MAX-used;
            int n=esp_http_client_read(http,remaining?response+used:&overflow,(int)(remaining>2048?2048:remaining?remaining:1));
            if(n<0){error=ESP_FAIL;break;}
            if(!n){complete=esp_http_client_is_complete_data_received(http);if(!complete)error=ESP_FAIL;break;}
            if(!remaining){error=ESP_ERR_INVALID_SIZE;break;}
            used+=(unsigned)n;
        }
        esp_http_client_close(http);esp_http_client_cleanup(http);
    }
    if(error==ESP_OK&&complete&&status==200)return response;
    char* result=NULL;
    if(status>=400&&status<500)result=ima_http_error(status,response);
    else if(status>=500){char* detail=ima_http_error(status,response);free(detail);} /* A failed add may have committed. */
    else{char failure[160];if(error!=ESP_OK)snprintf(failure,sizeof(failure),"连接 ima 失败 (%s)",esp_err_to_name(error));
        else snprintf(failure,sizeof(failure),"ima 服务返回 HTTP %d",status);message(failure);}
    ima_wipe(response,used);free(response);return result;
}
