/* SPDX-License-Identifier: Apache-2.0 */
typedef struct {
    httpd_req_t* req;
    const esp_partition_t* target;
    esp_ota_handle_t handle;
    psa_hash_operation_t sha;
    size_t received;
    esp_http_client_handle_t download;
    size_t download_bytes;
    const char* version;
    const char* stage;
    int error;
    FILE* source;
} ota_context_t;
static void ota_abort(void* p);
static bool ota_result(ota_context_t* c,const char* stage,esp_err_t error) {
    if(error==ESP_OK)return true;
    c->stage=stage;c->error=error;
    ESP_LOGE("ThingsOS_ota","%s failed: %s; received=%u",stage,esp_err_to_name(error),(unsigned)c->received);
    return false;
}
static bool ota_running_valid(void) {
    const esp_partition_t* running=esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    return startup_healthy || (running && esp_ota_get_state_partition(running,&state)==ESP_OK && state==ESP_OTA_IMG_VALID);
}
static int ota_read(void* p, uint8_t* data, size_t n) {
    ota_context_t* c = p;
    c->stage=c->source?"sd-read":"download";
    int read;
    if(c->source) {
        if(!lock()){ota_result(c,"sd-read-lock",ESP_ERR_TIMEOUT);return -1;}
        read=(int)fread(data,1,n,c->source);
        bool failed=ferror(c->source)!=0;unlock();
        if(failed){ota_result(c,"sd-read",ESP_FAIL);return -1;}
    }else read=c->download?esp_http_client_read(c->download,(char*)data,(int)n):httpd_req_recv(c->req,(char*)data,n);
    if (read > 0) {
        c->received += (size_t)read;
        transfer_set(
            NULL, NULL, (unsigned)(c->received * 100 / ((c->download||c->source)?c->download_bytes:(size_t)c->req->content_len)), true, false, true);
    }
    if(read<=0)c->error=read?read:ESP_ERR_INVALID_SIZE;
    return read;
}
static bool ota_header(void* p, const uint8_t* data, size_t n) {
    ota_context_t* c=p;
    if (n !=
        sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t))
        return false;
    esp_image_header_t image;
    esp_app_desc_t description;
    memcpy(&image, data, sizeof(image));
    memcpy(
        &description, data + sizeof(image) + sizeof(esp_image_segment_header_t),
        sizeof(description));
    bool ok=(!c->version || (memchr(description.version,0,sizeof(description.version)) && !strcmp(description.version,c->version))) &&
           image.magic == ESP_IMAGE_HEADER_MAGIC && image.chip_id == ESP_CHIP_ID_ESP32S3 &&
           description.magic_word == ESP_APP_DESC_MAGIC_WORD &&
           !strncmp(description.project_name, "ThingsOS_native", sizeof(description.project_name));
    return ota_result(c,"header",ok?ESP_OK:ESP_ERR_INVALID_ARG);
}
static bool ota_hash_begin(void* p) {
    ota_context_t* c = p;
    c->sha = (psa_hash_operation_t)PSA_HASH_OPERATION_INIT;
    return ota_result(c,"hash-init",psa_crypto_init() == PSA_SUCCESS &&
           psa_hash_setup(&c->sha, PSA_ALG_SHA_256) == PSA_SUCCESS?ESP_OK:ESP_FAIL);
}
static bool ota_hash_update(void* p, const uint8_t* data, size_t n) {
    ota_context_t* c=p;
    return ota_result(c,"hash-update",psa_hash_update(&c->sha,data,n)==PSA_SUCCESS?ESP_OK:ESP_FAIL);
}
static bool ota_hash_finish(void* p, uint8_t digest[32]) {
    size_t n = 0;
    ota_context_t* c=p;
    c->stage="digest";
    return ota_result(c,"hash-finish",psa_hash_finish(&c->sha,digest,32,&n)==PSA_SUCCESS && n==32?ESP_OK:ESP_FAIL);
}
static void ota_hash_abort(void* p) {
    psa_hash_abort(&((ota_context_t*)p)->sha);
}
static bool ota_begin(void* p, size_t n) {
    ota_context_t* c = p;
    if (!lock()) return ota_result(c,"erase-lock",ESP_ERR_TIMEOUT);
    /* Erase as blocks arrive. A bulk 4 MiB erase after opening HTTPS leaves
       the CDN connection unread for the duration of the whole erase. */
    bool ok = ota_result(c,"erase",esp_ota_begin(c->target,c->download?OTA_WITH_SEQUENTIAL_WRITES:n,&c->handle));
    if(!ok && c->handle){esp_ota_abort(c->handle);c->handle=0;}
    unlock();
    return ok;
}
static bool ota_write(void* p, const uint8_t* data, size_t n) {
    ota_context_t* c=p;
    if (!lock()) return ota_result(c,"write-lock",ESP_ERR_TIMEOUT);
    bool ok = ota_result(c,"write",esp_ota_write(c->handle,data,n));
    unlock();
    vTaskDelay(pdMS_TO_TICKS(1));
    return ok;
}
static bool ota_end(void* p) {
    ota_context_t* c = p;
    if (!lock()) {
        ota_result(c,"verify-lock",ESP_ERR_TIMEOUT);
        ota_abort(p);
        return false;
    }
    esp_err_t error = esp_ota_end(c->handle);
    unlock();
    c->handle = 0;
    return ota_result(c,"verify-image",error);
}
static void ota_abort(void* p) {
    ota_context_t* c = p;
    if (lock()) {
        esp_ota_abort(c->handle);
        unlock();
    }
    c->handle = 0;
}
static bool ota_activate(void* p) {
    ota_context_t* c=p;
    if (!lock()) return ota_result(c,"activate-lock",ESP_ERR_TIMEOUT);
    bool ok = ota_result(c,"activate",esp_ota_set_boot_partition(c->target));
    if(ok&&rescue_mode){thingsos_single_clear_intent();thingsos_board_healthy();}
    unlock();
    return ok;
}
static esp_err_t ota_stage_upload(httpd_req_t* req,const uint8_t hash[32]);
static esp_err_t ota(httpd_req_t* req) {
    if (!authorized(req)) return reject(req, "401 Unauthorized", "Token required");
    if(!service_enabled&&!rescue_mode)return reject(req,"503 Service Unavailable","Transfer service stopped");
    if(!rescue_mode && !ota_running_valid())return reject(req,"409 Conflict","Startup health verification in progress; retry later");
    const esp_partition_t *target = esp_ota_get_next_update_partition(NULL),
                          *running = esp_ota_get_running_partition();
    if (!target || !running) return reject(req, "400 Bad Request", "No inactive slot");
    thingsos_update_request_t request = { .bytes = req->content_len,
                                          .header_bytes = sizeof(esp_image_header_t) +
                                                          sizeof(esp_image_segment_header_t) +
                                                          sizeof(esp_app_desc_t),
                                          .capacity = target->size,
                                          .running_address = running->address,
                                          .target_address = target->address,
                                          .target_is_ota =
                                              target->type == ESP_PARTITION_TYPE_APP &&
                                              target->subtype >= ESP_PARTITION_SUBTYPE_APP_OTA_0 &&
                                              target->subtype < ESP_PARTITION_SUBTYPE_APP_OTA_MAX };
    char hash[65];
    if (httpd_req_get_hdr_value_str(req, "X-ThingsOS-SHA256", hash, sizeof(hash)) != ESP_OK ||
        strlen(hash) != 64)
        return reject(req, "400 Bad Request", "SHA256 required");
    for (size_t i = 0; i < 32; i++) {
        int high = hex(hash[i * 2]), low = hex(hash[i * 2 + 1]);
        if (high < 0 || low < 0) return reject(req, "400 Bad Request", "Invalid SHA256");
        request.expected_sha256[i] = (uint8_t)(high * 16 + low);
    }
    if (!rescue_mode) return ota_stage_upload(req,request.expected_sha256);
    if (manage_job) return reject(req, "409 Conflict", "File management pending");
    if (!lock()) return reject(req, "409 Conflict", "Device busy");
    if(!claim_transfer()){unlock();return reject(req,"409 Conflict","Device operation in progress");}
    unlock();
    transfer_set("ThingsOS 应用包", "救援程序正在写入主应用槽", 0, true, false, true);
    if (!pause_reader()) {
        busy = false;
        transfer_set(NULL, "阅读记录未保存，升级未开始", 0, false, true, true);
        return reject(req, "503 Service Unavailable", "Reader save failed; current slot unchanged");
    }
    ota_context_t context = { .req = req, .target = target };
    thingsos_update_ops_t ops = { &context,        ota_read,        ota_header,     ota_hash_begin,
                                  ota_hash_update, ota_hash_finish, ota_hash_abort, ota_begin,
                                  ota_write,       ota_end,         ota_abort,      ota_activate };
    bool ok = thingsos_update_run(&request, &ops);
    busy = false;
    transfer_set(
        NULL, ok ? "校验通过，重启并检查启动健康" : "校验或写入失败，启动槽未改变", ok ? 100 : 0,
        false, !ok, true);
    if (!ok)
        return reject(
            req, "422 Unprocessable Content",
            "Verification/write failed; current boot slot unchanged");
    esp_err_t result =
        httpd_resp_sendstr(req, "Verified. Rebooting into main application; no version rollback.");
    xTaskCreate(reboot_task, "ota_reboot", 2048, NULL, 5, NULL);
    return result;
}

static esp_err_t ota_stage_upload(httpd_req_t* req,const uint8_t hash[32]) {
    if(req->content_len<288 || (size_t)req->content_len>THINGSOS_SINGLE_CAPACITY)
        return reject(req,"400 Bad Request","Application exceeds main slot");
    if(manage_job || !lock())return reject(req,"409 Conflict","Device busy");
    bool claimed=claim_transfer();unlock();
    if(!claimed)return reject(req,"409 Conflict","Device operation in progress");
    if(!pause_reader()){busy=false;return reject(req,"503 Service Unavailable","Reader save failed");}
    uint8_t* block=heap_caps_malloc(8192,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    FILE* f=NULL;bool ok=block!=NULL;
    if(ok&&lock()) {
        uint64_t total=0,available=0;
        ok=sd_ready()&&esp_vfs_fat_info("/sdcard",&total,&available)==ESP_OK&&available>=(uint64_t)req->content_len+65536;
        if(ok){mkdir("/sdcard/.thingsos",0775);f=fopen(THINGSOS_UPDATE_PART,"wb");}
        unlock();ok=f!=NULL;
    }else ok=false;
    ota_context_t context={.req=req};size_t received=0;
    transfer_set("ThingsOS 应用包","正在接收固件到 SD 卡",0,true,false,true);
    while(ok&&received<(size_t)req->content_len) {
        size_t n=(size_t)req->content_len-received;if(n>8192)n=8192;
        int got=ota_read(&context,block,n);
        if(got<=0){ok=false;break;}
        if(!lock()){ok=false;break;}
        ok=sd_ready()&&fwrite(block,1,(size_t)got,f)==(size_t)got;unlock();received+=(size_t)got;
    }
    if(f&&lock()) {
        if(ok)ok=fflush(f)==0&&fsync(fileno(f))==0;
        if(fclose(f))ok=false;
        if(ok){remove(THINGSOS_UPDATE_FILE);ok=rename(THINGSOS_UPDATE_PART,THINGSOS_UPDATE_FILE)==0;}
        if(!ok)remove(THINGSOS_UPDATE_PART);
        unlock();
    }else ok=false;
    free(block);
    if(ok&&lock()){ok=thingsos_single_stage(received,hash,NULL)==ESP_OK;unlock();}else ok=false;
    busy=false;
    transfer_set(NULL,ok?"校验通过，进入升级程序":"接收或校验失败，原固件保留",ok?100:0,false,!ok,true);
    if(!ok)return reject(req,"422 Unprocessable Content","SD reception/verification failed; main unchanged");
    esp_err_t result=httpd_resp_sendstr(req,"Verified on SD. Rebooting into installer; keep SD inserted.");
    xTaskCreate(reboot_task,"stage_reboot",2048,NULL,5,NULL);
    return result;
}
