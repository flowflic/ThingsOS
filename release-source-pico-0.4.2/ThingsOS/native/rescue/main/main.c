// SPDX-License-Identifier: Apache-2.0
#include <stdlib.h>
#include <string.h>
#include "board.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "network.h"
#include "single_update.h"
#include "read_pico_sd.h"
#include "esp_system.h"
#include "read_pico_pmu.h"
#include "thingsos/diagnostics.h"

void app_main(void) {
    thingsos_diag_start();
    /* Single-slot installer owns SD and Flash before HTTP/display activity.
       Failed/missing media keeps factory rescue selected and exposes recovery. */
    bool board_ready=false;
    if(esp_psram_is_initialized())board_ready=thingsos_board_start()==ESP_OK;
    if(board_ready) {
        read_pico_sd_info_t info={0};
        for(unsigned i=0;i<100;++i) {
            esp_err_t sd=read_pico_sd_get_info(&info);
            if(sd!=ESP_ERR_NOT_FINISHED)break;
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        esp_err_t installed=thingsos_single_install();
        thingsos_diag_poll(true);
        if(installed==ESP_OK){thingsos_board_healthy();thingsos_diag_health();ESP_LOGI("ThingsOS_rescue","SD install verified; starting sole main application");esp_restart();}
        ESP_LOGI("ThingsOS_rescue","Installer idle/recovery: %s",esp_err_to_name(installed));
    }
    /* Recovery networking remains available without SD or the font partition. */
    esp_err_t error = thingsos_network_start(true);
    if (error != ESP_OK) {
        ESP_LOGE(
            "ThingsOS_rescue", "Recovery AP failed: %s; use ROM USB download",
            esp_err_to_name(error));
        while (true) {
            thingsos_board_idle();
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
    bool panel = false;
    thingsos_canvas_t canvas = {.width=684,.height=1216};
    if (esp_psram_is_initialized()) {
        canvas.pixels = heap_caps_malloc(684u * 1216u, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (canvas.pixels && board_ready) {
            memset(canvas.pixels, 255, 684u * 1216u);
            thingsos_board_ascii(&canvas, "ThingsOS independent rescue", 36, 160);
            thingsos_board_ascii(&canvas, "Connect WiFi then open browser", 36, 240);
            thingsos_board_ascii(&canvas, thingsos_network_ssid(), 36, 350);
            thingsos_board_ascii(&canvas, "WiFi password:", 36, 420);
            thingsos_board_ascii(&canvas, thingsos_network_password(), 36, 470);
            thingsos_board_ascii(&canvas, "http://192.168.4.1", 36, 580);
            thingsos_board_ascii(&canvas, "Access token:", 36, 670);
            thingsos_board_ascii(&canvas, thingsos_network_token(), 36, 720);
            thingsos_board_ascii(&canvas, "Single app recovery installer", 36, 840);
            panel = thingsos_board_present(&canvas) == ESP_OK;
        }
    }

    ESP_LOGW("ThingsOS_rescue", "Recovery HTTP ready; panel=%d; SD writes disabled", panel);
    /* This factory application is never selected as an OTA target or overwritten by the service. */
    unsigned idle_ticks = 0;
    bool asleep = false;
    while (true) {
        if (++idle_ticks == 300) {thingsos_board_healthy();thingsos_diag_health();}
        if (!thingsos_network_busy() && panel &&
            xSemaphoreTake(thingsos_io_lock, pdMS_TO_TICKS(50)) == pdTRUE) {
            if(!asleep)thingsos_diag_poll(false);
            unsigned key = thingsos_board_power_key();
            if (key == 2) read_pico_pmu_power_off();
            if (key == 1) {
                if (asleep) {
                    thingsos_board_resume();
                    memset(canvas.pixels, 255, 684u * 1216u);
                    thingsos_board_ascii(&canvas, "ThingsOS independent rescue", 36, 160);
                    thingsos_board_ascii(&canvas, thingsos_network_ssid(), 36, 350);
                    thingsos_board_ascii(&canvas, thingsos_network_password(), 36, 470);
                    thingsos_board_ascii(&canvas, "http://192.168.4.1", 36, 580);
                    thingsos_board_ascii(&canvas, thingsos_network_token(), 36, 720);
                    thingsos_board_present(&canvas);
                    asleep = false;
                } else {
                    memset(canvas.pixels, 255, 684u * 1216u);
                    asleep = thingsos_board_suspend(&canvas) == ESP_OK;
                }
            }
            xSemaphoreGive(thingsos_io_lock);
        }
        thingsos_board_idle();
        if (asleep)
            thingsos_board_sleep_wait();
        else
            vTaskDelay(pdMS_TO_TICKS(100));
    }
}
