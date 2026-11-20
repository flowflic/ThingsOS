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
#include "board.h"
#include "esp_system.h"
#include "thingsos/diagnostics.h"

void app_main(void) {
    thingsos_diag_start();
    /* Single-slot installer owns SD and Flash before HTTP/display activity.
       Failed/missing media keeps factory rescue selected and exposes recovery. */
    bool board_ready=false;
    if(esp_psram_is_initialized())board_ready=thingsos_board_start()==ESP_OK;
    if(board_ready) {
        thingsos_sd_info_t info={0};
        for(unsigned i=0;i<100;++i) {
            esp_err_t sd=thingsos_board_sd_info(&info);
            if(sd!=ESP_ERR_NOT_FINISHED)break;
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        thingsos_diag_poll(true);
        esp_err_t installed=thingsos_single_install();
        if(installed==ESP_OK){thingsos_diag_health();thingsos_board_healthy();ESP_LOGI("ThingsOS_rescue","SD install verified; starting sole main application");esp_restart();}
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
    thingsos_canvas_t canvas = { .width=684, .height=1216 };
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
            /* ASCII fallback font: Chinese reason is available on the web. */
            const char* reason=thingsos_diag_reason_text();
            const char* ascii=!strcmp(reason,"连续启动未确认")?"Repeated unconfirmed starts":
                !strcmp(reason,"手动进入")?"Manual rescue request":
                !strcmp(reason,"更新安装失败")?"Update installation failed":
                !strcmp(reason,"固件更新安装")?"Firmware update installer":"Rescue reason not recorded";
            thingsos_board_ascii(&canvas,ascii,36,910);
            thingsos_board_ascii(&canvas, "Download diagnostics from web",36,980);
            panel = thingsos_board_present(&canvas) == ESP_OK;
        }
    }

    ESP_LOGW("ThingsOS_rescue", "Recovery HTTP ready; panel=%d; user file writes disabled; diagnostic snapshots enabled", panel);
    /* This factory application is never selected as an OTA target or overwritten by the service. */
    unsigned idle_ticks = 0;
    bool asleep = false;
    while (true) {
        if (++idle_ticks == 300) {thingsos_board_healthy();thingsos_diag_health();}
        if (!asleep) thingsos_network_maintenance();
        if (!thingsos_network_busy() && panel &&
            xSemaphoreTake(thingsos_io_lock, pdMS_TO_TICKS(50)) == pdTRUE) {
            thingsos_diag_poll(false);
            unsigned key = thingsos_board_power_key();
            if (key == 2) thingsos_board_power_off();
            if (key == 1) {
                if (asleep) {
                    thingsos_board_resume();
                    thingsos_network_wake();
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
                    thingsos_network_sleep();
                    asleep = thingsos_board_suspend(&canvas) == ESP_OK;
                    if (!asleep) thingsos_network_wake();
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
