/* SPDX-License-Identifier: Apache-2.0 */
static esp_err_t upload(httpd_req_t* req) {
    if (!authorized(req)) return reject(req, "401 Unauthorized", "Token required");
    if (!service_enabled) return reject(req, "503 Service Unavailable", "Transfer service stopped");
    char name[256],directory[512];
    int kind;
    if (!filename(req, name, &kind) || !request_directory(req,directory) || req->content_len <= 0 ||
        req->content_len > (kind==1 ? 32 * 1024 * 1024 : 64 * 1024 * 1024))
        return reject(req, "400 Bad Request", "Invalid name/type/size");
    ESP_LOGI("ThingsOS_network", "Upload request: %s declared=%u kind=%d",
             name,(unsigned)req->content_len,kind);
    if (manage_job) return reject(req, "409 Conflict", "File management pending");
    if (!sd_ready() || !lock())
        return reject(req, "503 Service Unavailable", "SD unavailable or busy");
    if(!claim_transfer()){unlock();return reject(req,"409 Conflict","Device operation in progress");}
    upload_cancel_requested=false;
    char final[768], temp[600], backup[768], collision[16] = { 0 };
    httpd_req_get_hdr_value_str(req, "X-ThingsOS-Collision", collision, sizeof(collision));
    bool replacing = false;
    snprintf(final, sizeof(final), "%s/%s", directory, name);
    snprintf(temp, sizeof(temp), "%s/.incoming-%08lx.part", directory, (unsigned long)esp_random());
    mkdir(directory, 0775);
    struct stat existing;
    uint64_t total, free_space;
    if (stat(final, &existing) == 0) {
        if (!strcmp(collision, "both")) {
            unsigned copy = 1;
            char alternative[192];
            do {
                if (!thingsos_collision_name(name, copy++, alternative)) break;
                snprintf(final, sizeof(final), "%s/%s", directory, alternative);
            } while (stat(final, &existing) == 0 && copy < 10000);
            if (copy >= 10000 || stat(final, &existing) == 0) {
                busy = false;
                unlock();
                return reject(req, "409 Conflict", "Cannot create unique name");
            }
        } else if (!strcmp(collision, "replace") && S_ISREG(existing.st_mode))
            replacing = true;
        else {
            busy = false;
            unlock();
            ESP_LOGI("ThingsOS_network", "Upload name conflict before body: %s declared=%u",
                     name,(unsigned)req->content_len);
            return reject(
                req, "409 Conflict", "Name exists; cancel, keep both, or confirm replacement");
        }
    }
    if (esp_vfs_fat_info("/sdcard", &total, &free_space) != ESP_OK ||
        free_space < (uint64_t)req->content_len + 65536) {
        busy = false;
        unlock();
        return reject(req, "507 Insufficient Storage", "SD space unavailable");
    }
    transfer_set(name, "保存阅读记录，准备上传", 0, true, false, false);
    unlock();
    bool prepared = pause_reader();
    if (!lock()) {
        busy = false;
        return reject(req, "503 Service Unavailable", "IO unavailable");
    }
    if (!prepared || !sd_ready()) {
        busy = false;
        unlock();
        transfer_set(NULL, "阅读记录未保存，上传未开始", 0, false, true, false);
        return reject(req, "503 Service Unavailable", "Reader save failed or SD unavailable");
    }
    enum { UPLOAD_CHUNK_BYTES=16384 };
    char* chunk=heap_caps_malloc(UPLOAD_CHUNK_BYTES,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!chunk) {
        busy=false;unlock();
        transfer_set(NULL,"上传缓冲不足，上传未开始",0,false,true,false);
        return reject(req,"503 Service Unavailable","Upload PSRAM buffer unavailable");
    }
    FILE* file = fopen(temp, "wb");
    bool ok = file != NULL;
    /* 16 KiB chunks amortize the IO lock, HTTP receive and FATFS calls.
       A 32 KiB stdio buffer coalesces sector writes; modem sleep is disabled
       only while this upload is active so the receive window stays open. */
    wifi_ps_type_t previous_ps = WIFI_PS_MIN_MODEM;
    bool ps_changed = false;
    uint64_t upload_started = esp_timer_get_time();
    uint64_t receive_us=0,io_wait_us=0,write_us=0,sync_us=0;
    unsigned received_bytes=0,receive_calls=0,write_calls=0,slow_receives=0;
    uint64_t max_receive_us=0;
    if (file) {
        setvbuf(file, NULL, _IOFBF, 32768);
        if (esp_wifi_get_ps(&previous_ps) == ESP_OK)
            ps_changed = esp_wifi_set_ps(WIFI_PS_NONE) == ESP_OK;
    }
    transfer_set(name, "正在上传", 0, true, false, false);
    unlock();
    int remain = req->content_len;
    size_t buffered=0;
    while (ok && remain > 0) {
        if (upload_cancel_requested || !sd_ready()) {
            ok = false;
            break;
        }
        int64_t step_started=esp_timer_get_time();
        int received =
            httpd_req_recv(req, chunk+buffered,
                remain > UPLOAD_CHUNK_BYTES-buffered ? UPLOAD_CHUNK_BYTES-buffered : remain);
        uint64_t receive_step=esp_timer_get_time()-step_started;
        receive_us+=receive_step;++receive_calls;
        if(receive_step>max_receive_us)max_receive_us=receive_step;
        if(receive_step>=200000)++slow_receives;
        if (received <= 0) {
            ok = false;
            break;
        }
        received_bytes+=(unsigned)received;
        remain-=received;buffered+=(size_t)received;
        /* Consume short TCP reads promptly. Use the existing 16 KiB PSRAM
           buffer before locking/writing/yielding, rather than per MSS. */
        if(buffered<UPLOAD_CHUNK_BYTES&&remain>0)continue;
        step_started=esp_timer_get_time();
        bool locked=lock();
        io_wait_us+=esp_timer_get_time()-step_started;
        if (!locked) {
            ok = false;
            break;
        }
        step_started=esp_timer_get_time();
        bool written = fwrite(chunk, 1, buffered, file) == buffered;
        ++write_calls;
        write_us+=esp_timer_get_time()-step_started;
        unlock();
        if (!written) {
            ok = false;
            break;
        }
        buffered=0;
        transfer_set(
            NULL, NULL, (unsigned)((uint64_t)(req->content_len - remain) * 100 / req->content_len),
            true, false, false);
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    heap_caps_free(chunk);
    if (!lock()) {
        busy = false;
        if (ps_changed) esp_wifi_set_ps(previous_ps);
        transfer_set(NULL, "IO 锁不可用", 0, false, true, false);
        return ESP_FAIL;
    }
    if (file) {
        int64_t sync_started=esp_timer_get_time();
        if (fflush(file) || fsync(fileno(file))) ok = false;
        if (fclose(file)) ok = false;
        sync_us=esp_timer_get_time()-sync_started;
    }
    snprintf(backup, sizeof(backup), "%.511s/.replace-%.191s.bak", directory, name);
    if(upload_cancel_requested)ok=false;
    bool moved = false;
    if (ok && replacing) {
        moved = rename(final, backup) == 0;
        ok = moved;
    }
    if (ok) ok = sd_ready() && rename(temp, final) == 0;
    if (moved) {
        if (ok)
            unlink(backup);
        else if (rename(backup, final))
            transfer_set(NULL, "覆盖恢复失败，旧文件保留在隐藏备份中", 0, false, true, false);
    }
    if (!ok) unlink(temp);
    busy = false;
    unlock();
    if (ps_changed) esp_wifi_set_ps(previous_ps);
    uint64_t elapsed_us = esp_timer_get_time() - upload_started;
    unsigned kb_per_second = elapsed_us
        ? (unsigned)((uint64_t)received_bytes * 1000 / elapsed_us) : 0;
    ESP_LOGI("ThingsOS_network", "Upload %s: %u bytes in %llu ms (%u KB/s)",
             name, received_bytes,
             (unsigned long long)(elapsed_us / 1000), kb_per_second);
    ESP_LOGI("ThingsOS_network", "Upload timing: ok=%d received=%u/%u calls=%u recv=%llu ms io_wait=%llu ms write=%llu ms sync=%llu ms",
             ok,received_bytes,(unsigned)req->content_len,receive_calls,
             (unsigned long long)(receive_us/1000),(unsigned long long)(io_wait_us/1000),
             (unsigned long long)(write_us/1000),(unsigned long long)(sync_us/1000));
    ESP_LOGI("ThingsOS_network", "Upload batches: writes=%u slow_recv_200ms=%u max_recv=%llu ms buffer=%u",
             write_calls,slow_receives,(unsigned long long)(max_receive_us/1000),UPLOAD_CHUNK_BYTES);
    transfer_set(
        NULL, ok ? "上传完成" : "上传中断，临时文件已清理", ok ? 100 : 0, false, !ok, false);
    return ok ? httpd_resp_sendstr(req, "Upload complete")
              : reject(
                    req, "500 Internal Server Error",
                    "Upload interrupted; temporary file discarded");
}
