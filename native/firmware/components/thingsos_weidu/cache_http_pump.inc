/* SPDX-License-Identifier: Apache-2.0
 * Each cache task owns its HTTP/TLS channel. Serialize individual SDK steps
 * with panel scans so chapter prefetch cannot contend for PSRAM during DMA.
 * Release the mutex between steps; EAGAIN resumes the same SDK request.
 */
static esp_err_t cache_http_perform(esp_http_client_handle_t connection,transfer_t* transfer) {
    uint64_t started=now_ms(NULL),budget=transfer->budget_ms?transfer->budget_ms:transfer->memory?60000:300000;
    unsigned steps=0,max_step=0;esp_err_t result=ESP_FAIL;
    for(;;) {
        if(abort_requested || transfer_stopped(transfer)){transfer->error=WD_CANCEL;break;}
        if(now_ms(NULL)-started>=budget){result=ESP_ERR_TIMEOUT;break;}
        if(xSemaphoreTake(thingsos_io_lock,pdMS_TO_TICKS(10))!=pdTRUE)continue;
        if(abort_requested || transfer_stopped(transfer)) {
            xSemaphoreGive(thingsos_io_lock);
            transfer->error=WD_CANCEL;
            break;
        }
        uint64_t at=now_ms(NULL);reader_transport_step(transfer->transport);
        result=esp_http_client_perform(connection);++steps;
        unsigned elapsed=(unsigned)(now_ms(NULL)-at);if(elapsed>max_step)max_step=elapsed;
        xSemaphoreGive(thingsos_io_lock);
        if(transfer->error!=WD_OK){result=ESP_FAIL;break;}
        if(transfer->reader_ready || transfer->image_redirect_ready){result=ESP_OK;break;}
        if(result!=ESP_ERR_HTTP_EAGAIN)break;
        vTaskDelay(1);
    }
    ESP_LOGI("ThingsOS_weidu","Cache HTTPS steps=%u max_step=%u ms",steps,max_step);
    return result;
}
