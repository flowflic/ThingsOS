/* SPDX-License-Identifier: Apache-2.0
 * Stateful nonblocking HTTPS progress. Entry and exit own thingsos_io_lock.
 * EAGAIN resumes the same SDK request; a failed POST is never restarted here.
 */
static esp_err_t reader_http_perform(esp_http_client_handle_t connection,transfer_t* transfer) {
    uint64_t started=now_ms(NULL);unsigned steps=0,max_step=0;
    esp_err_t result=ESP_FAIL;
    for(;;){
        if(abort_requested){transfer->error=WD_CANCEL;break;}
        if(now_ms(NULL)-started>=60000){result=ESP_ERR_TIMEOUT;break;}
        uint64_t step=now_ms(NULL);
        reader_transport_step(transfer->transport);
        result=esp_http_client_perform(connection);++steps;
        unsigned elapsed=(unsigned)(now_ms(NULL)-step);if(elapsed>max_step)max_step=elapsed;
        if(transfer->error!=WD_OK){result=ESP_FAIL;break;}
        if(transfer->reader_ready){result=ESP_OK;break;}
        if(result!=ESP_ERR_HTTP_EAGAIN)break;
        /* Give the UI priority between nonblocking progress calls. In contrast,
           a blocking read outside this mutex can starve PSRAM scan producers. */
        xSemaphoreGive(thingsos_io_lock);
        vTaskDelay(1);
        xSemaphoreTake(thingsos_io_lock,portMAX_DELAY);
    }
    ESP_LOGI("ThingsOS_weidu","Serialized HTTPS steps=%u max_step=%u ms",steps,max_step);
    return result;
}
