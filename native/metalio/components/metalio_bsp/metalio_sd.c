/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Metalio microSD：SDMMC 4 位（GPIO20–25 IOMUX），探测、挂载、失效锁存、同步卸载与 USB 直连原始卡。
 * 状态机与 Read Pico read_pico_sd.c（Apache-2.0，mindreset）的接口语义一致；
 * 卡座没有 CD 脚，在位判断改为 CMD13 应答。
 *
 * Metalio microSD: 4-bit SDMMC (GPIO20–25 IOMUX) probe, mount, invalidation
 * latch, sync-unmount and raw card for USB disk mode. The state machine keeps
 * the interface semantics of Read Pico read_pico_sd.c (Apache-2.0, mindreset);
 * the slot has no CD pin, so presence comes from the CMD13 response.
 */
#include "metalio_bsp.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdmmc_cmd.h"

#define SD_MOUNT_POINT "/sdcard"
// 已挂载时每秒最多查一次 CMD13。/ At most one CMD13 check per second while mounted.
#define PRESENCE_CHECK_US 1000000

static const char* TAG = "metalio_sd";
static sdmmc_card_t* card;
static int probe_state;
static portMUX_TYPE state_lock = portMUX_INITIALIZER_UNLOCKED;
static bool media_invalidated;
static bool last_present = true;
static int64_t presence_checked_at;
static metalio_sd_info_t cached_info;
#include "metalio_sd_dma.inc"

static sdmmc_slot_config_t slot_config(void) {
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.clk = METALIO_SD_CLK;
    slot.cmd = METALIO_SD_CMD;
    slot.d0 = METALIO_SD_D0;
    slot.d1 = METALIO_SD_D1;
    slot.d2 = METALIO_SD_D2;
    slot.d3 = METALIO_SD_D3;
    slot.cd = GPIO_NUM_NC;
    slot.wp = GPIO_NUM_NC;
    slot.width = 4;
    slot.flags = 0;
    return slot;
}

static void release_pin_holds(void) {
    const gpio_num_t pins[] = { METALIO_SD_CLK, METALIO_SD_CMD, METALIO_SD_D0,
                                METALIO_SD_D1,  METALIO_SD_D2,  METALIO_SD_D3 };
    for (size_t i = 0; i < sizeof(pins) / sizeof(pins[0]); ++i) gpio_hold_dis(pins[i]);
}

bool metalio_sd_present(void) {
    sdmmc_card_t* current = card;
    if (current == NULL) return last_present;
    int64_t now = esp_timer_get_time();
    if (now - presence_checked_at < PRESENCE_CHECK_US) return last_present;
    presence_checked_at = now;
    last_present = sdmmc_get_status(current) == ESP_OK;
    if (!last_present) ESP_LOGW(TAG, "card stopped answering CMD13");
    return last_present;
}

// 失效只影响快照，消费者释放句柄前不得卸载卡。/ Invalidation changes snapshots only; consumers close handles before unmount.
static void observe_media_locked(bool present) {
    if (!present && (cached_info.mounted || probe_state == 1)) media_invalidated = true;
    cached_info.present = present;
    if (!present || media_invalidated) {
        memset(&cached_info, 0, sizeof(cached_info));
        cached_info.present = present;
        cached_info.error = present ? ESP_ERR_INVALID_STATE : ESP_ERR_NOT_FOUND;
    }
}

static void publish_info(const metalio_sd_info_t* info) {
    bool present = info->present;
    portENTER_CRITICAL(&state_lock);
    cached_info = *info;
    observe_media_locked(present);
    probe_state = 2;
    portEXIT_CRITICAL(&state_lock);
}

static void fill_info(metalio_sd_info_t* info, esp_err_t mount_err) {
    memset(info, 0, sizeof(*info));
    info->present = mount_err == ESP_OK || (mount_err != ESP_ERR_TIMEOUT
                                            && mount_err != ESP_ERR_NOT_FOUND
                                            && mount_err != ESP_ERR_INVALID_RESPONSE);
    last_present = info->present;
    info->error = mount_err;
    info->needs_format = info->present && mount_err != ESP_OK && mount_err != ESP_ERR_NOT_FINISHED;
    if (mount_err != ESP_OK || card == NULL) return;
    info->mounted = true;
    info->needs_format = false;
    strlcpy(info->name, card->cid.name, sizeof(info->name));
    info->capacity_bytes = (uint64_t)card->csd.capacity * card->csd.sector_size;
    uint64_t total_bytes = 0;
    info->error = esp_vfs_fat_info(SD_MOUNT_POINT, &total_bytes, &info->free_bytes);
}

static esp_err_t mount_card(void) {
    if (card != NULL) return ESP_OK;
    release_pin_holds();
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;
    esp_err_t dma_err=sd_dma_prepare(&host);
    if(dma_err!=ESP_OK)return dma_err;
    sdmmc_slot_config_t slot = slot_config();
    const esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 8,
        .allocation_unit_size = 16 * 1024,
    };
    esp_err_t err = esp_vfs_fat_sdmmc_mount(SD_MOUNT_POINT, &host, &slot, &mount_config, &card);
    // 上电后首次时钟协商常超时，等卡就绪再挂一次。/ First negotiate after power-up often times out; retry once.
    if (err == ESP_ERR_TIMEOUT || err == ESP_ERR_INVALID_RESPONSE) {
        card = NULL;
        vTaskDelay(pdMS_TO_TICKS(200));
        err = esp_vfs_fat_sdmmc_mount(SD_MOUNT_POINT, &host, &slot, &mount_config, &card);
    }
    if (err != ESP_OK) {
        card = NULL;
        ESP_LOGW(TAG, "mount failed: %s", esp_err_to_name(err));
    } else {
        presence_checked_at = esp_timer_get_time();
        mkdir(SD_MOUNT_POINT "/fonts", 0777);
    }
    return err;
}

static void close_card(void) {
    if (card != NULL) {
        esp_err_t err = esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, card);
        if (err != ESP_OK) ESP_LOGW(TAG, "unmount %s", esp_err_to_name(err));
        card = NULL;
    }
}

static void probe_task(void* arg) {
    (void)arg;
    metalio_sd_info_t info;
    esp_err_t err = mount_card();
    fill_info(&info, err);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "mounted %s, capacity=%llu MB, free=%llu MB", info.name,
                 (unsigned long long)(info.capacity_bytes / (1024 * 1024)),
                 (unsigned long long)(info.free_bytes / (1024 * 1024)));
    }
    publish_info(&info);
    vTaskDelete(NULL);
}

esp_err_t metalio_sd_start_probe(void) {
    portENTER_CRITICAL(&state_lock);
    esp_err_t err = ESP_OK;
    if (probe_state == 1) err = ESP_ERR_NOT_FINISHED;
    else if (media_invalidated) err = cached_info.error;
    else if (probe_state == 2) err = cached_info.mounted ? ESP_OK
                                   : cached_info.error != ESP_OK ? cached_info.error : ESP_FAIL;
    else {
        memset(&cached_info, 0, sizeof(cached_info));
        cached_info.present = true;
        cached_info.error = ESP_ERR_NOT_FINISHED;
        probe_state = 1;
    }
    bool start = err == ESP_OK && probe_state == 1;
    portEXIT_CRITICAL(&state_lock);
    if (!start) return err;
    if (xTaskCreate(probe_task, "sd_probe", 4096, NULL, 5, NULL) != pdPASS) {
        portENTER_CRITICAL(&state_lock);
        probe_state = 0;
        cached_info.error = ESP_ERR_NO_MEM;
        portEXIT_CRITICAL(&state_lock);
        return ESP_ERR_NO_MEM;
    }
    return ESP_ERR_NOT_FINISHED;
}

esp_err_t metalio_sd_get_info(metalio_sd_info_t* info) {
    if (info == NULL) return ESP_ERR_INVALID_ARG;
    bool present = probe_state == 2 && cached_info.mounted ? metalio_sd_present() : true;
    portENTER_CRITICAL(&state_lock);
    if (probe_state == 2 && (cached_info.mounted || media_invalidated)) observe_media_locked(present);
    *info = cached_info;
    esp_err_t err = media_invalidated ? info->error
                  : probe_state == 0  ? ESP_ERR_INVALID_STATE
                  : probe_state == 1  ? ESP_ERR_NOT_FINISHED
                                      : info->error;
    portEXIT_CRITICAL(&state_lock);
    return err;
}

static bool begin_operation(void) {
    portENTER_CRITICAL(&state_lock);
    bool ready = probe_state != 1;
    if (ready) probe_state = 1;
    portEXIT_CRITICAL(&state_lock);
    return ready;
}

esp_err_t metalio_sd_remount(void) {
    if (!begin_operation()) return ESP_ERR_NOT_FINISHED;
    close_card();
    portENTER_CRITICAL(&state_lock);
    memset(&cached_info, 0, sizeof(cached_info));
    media_invalidated = false;
    last_present = true;
    probe_state = 0;
    portEXIT_CRITICAL(&state_lock);
    return metalio_sd_start_probe();
}

esp_err_t metalio_sd_sync(void) {
    if (!begin_operation()) return ESP_ERR_NOT_FINISHED;
    close_card();
    metalio_sd_info_t info = { .present = true, .error = ESP_ERR_INVALID_STATE };
    publish_info(&info);
    return ESP_OK;
}

void* metalio_sd_raw_open(void) {
    if (card != NULL) return NULL;
    release_pin_holds();
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;
    if(sd_dma_prepare(&host)!=ESP_OK)return NULL;
    sdmmc_slot_config_t slot = slot_config();
    sdmmc_card_t* raw = calloc(1, sizeof(sdmmc_card_t));
    if (raw == NULL) return NULL;
    esp_err_t err = sdmmc_host_init();
    if (err == ESP_ERR_INVALID_STATE) {
        sdmmc_host_deinit();
        err = sdmmc_host_init();
    }
    if (err == ESP_OK) err = sdmmc_host_init_slot(host.slot, &slot);
    if (err == ESP_OK) err = sdmmc_card_init(&host, raw);
    if (err == ESP_OK) err = sdmmc_get_status(raw);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "raw card init: %s", esp_err_to_name(err));
        sdmmc_host_deinit();
        free(raw);
        return NULL;
    }
    return raw;
}

void metalio_sd_raw_close(void* raw) {
    if (raw == NULL) return;
    sdmmc_host_deinit();
    free(raw);
}
