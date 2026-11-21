/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * FT6336U 电容触摸（I2C 0x38，INT=GPIO5，RST=TCA P1.2）。寄存器 0x00 起的 15 字节报点
 * 与 FocalTech/CST836U 格式一致，ThingsOS 的 thingsos_touch_primary() 直接解析。
 * 坐标系：屏内竖屏 684×1216；盖板虚拟键在 Y≈1600，X≈100/200/300（厂商实测）。
 *
 * FT6336U capacitive touch (I2C 0x38, INT=GPIO5, RST=TCA P1.2). The 15-byte
 * report from register 0x00 matches the FocalTech/CST836U layout, so ThingsOS's
 * thingsos_touch_primary() parses it unchanged. Coordinates: portrait 684×1216
 * on screen; bezel keys report at Y≈1600, X≈100/200/300 (vendor measurement).
 */
#include "metalio_bsp.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "ft6336u"

#define FT_REG_DEV_MODE 0x00
#define FT_REG_INT_MODE 0xA4
#define FT_REG_POWER_MODE 0xA5
#define FT_REG_FW_VER 0xA6
#define FT_REG_CHIP_ID 0xA3
#define FT_POWER_HIBERNATE 0x03

static i2c_master_dev_handle_t s_dev;

static void configure_int_pin(void) {
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << METALIO_TP_INT_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
}

esp_err_t metalio_touch_init(void) {
    configure_int_pin();
    esp_err_t err = metalio_touch_reset();
    if (err != ESP_OK) return err;
    if (s_dev == NULL) {
        err = metalio_i2c_add(METALIO_ADDR_FT6336U, 400000, &s_dev);
        if (err != ESP_OK) return err;
    }
    uint8_t chip = 0, fw = 0;
    err = metalio_i2c_read(s_dev, FT_REG_CHIP_ID, &chip, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "no ACK at 0x%02X: %s", METALIO_ADDR_FT6336U, esp_err_to_name(err));
        return err;
    }
    metalio_i2c_read(s_dev, FT_REG_FW_VER, &fw, 1);
    // 0：按住期间 INT 保持低（轮询模式，厂商默认）。/ 0: INT stays low while touched (polling, vendor default).
    uint8_t mode = 0;
    metalio_i2c_write(s_dev, FT_REG_INT_MODE, &mode, 1);
    ESP_LOGI(TAG, "chip=0x%02X fw=0x%02X", chip, fw);
    return ESP_OK;
}

esp_err_t metalio_touch_read(metalio_touch_t* t) {
    if (t == NULL) return ESP_ERR_INVALID_ARG;
    memset(t, 0, sizeof(*t));
    esp_err_t err = metalio_i2c_read(s_dev, FT_REG_DEV_MODE, t->raw, sizeof(t->raw));
    if (err != ESP_OK) return err;
    unsigned count = t->raw[2] & 0x0F;
    t->count = (uint8_t)(count > 2 ? 0 : count);
    if (t->count) {
        t->x = (uint16_t)(((t->raw[3] & 0x0F) << 8) | t->raw[4]);
        t->y = (uint16_t)(((t->raw[5] & 0x0F) << 8) | t->raw[6]);
    }
    return ESP_OK;
}

// 休眠后芯片不再应答 I2C，只能复位唤醒。/ After hibernate the chip ignores I2C; only reset wakes it.
esp_err_t metalio_touch_sleep(void) {
    uint8_t mode = FT_POWER_HIBERNATE;
    return metalio_i2c_write(s_dev, FT_REG_POWER_MODE, &mode, 1);
}

esp_err_t metalio_touch_wake(void) {
    esp_err_t err = ESP_FAIL;
    for (int attempt = 0; attempt < 2; ++attempt) {
        err = metalio_touch_reset();
        if (err != ESP_OK) continue;
        uint8_t mode = 0;
        for (int wait = 0; wait < 5; ++wait) {
            err = metalio_i2c_write(s_dev, FT_REG_INT_MODE, &mode, 1);
            if (err == ESP_OK) return ESP_OK;
            vTaskDelay(pdMS_TO_TICKS(30));
        }
    }
    ESP_LOGW(TAG, "touch wake failed: %s", esp_err_to_name(err));
    return err;
}

int metalio_touch_int_level(void) {
    return gpio_get_level(METALIO_TP_INT_GPIO);
}
