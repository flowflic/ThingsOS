/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * 冷/暖双路前光（YX6016N EN/PWM，GPIO6 冷、GPIO7 暖），LEDC 10 位 PWM。
 * 频率与占空比映射沿用 CloudZao/Metalio-E-INK4-Plus frontlight.cc（MIT）。
 *
 * Cool/warm frontlight (YX6016N EN/PWM, GPIO6 cool, GPIO7 warm) on 10-bit LEDC
 * PWM. Frequency and duty mapping follow CloudZao/Metalio-E-INK4-Plus
 * frontlight.cc (MIT).
 */
#include "metalio_bsp.h"

#include "driver/ledc.h"
#include "esp_log.h"

#define TAG "frontlight"

#define FL_MODE LEDC_LOW_SPEED_MODE
#define FL_TIMER LEDC_TIMER_0
#define FL_CH_COOL LEDC_CHANNEL_0
#define FL_CH_WARM LEDC_CHANNEL_1
#define FL_DUTY_MAX 1023
#define FL_FREQ_HZ 20000

static bool s_ready;

esp_err_t metalio_frontlight_init(void) {
    if (s_ready) return ESP_OK;
    const ledc_timer_config_t timer = {
        .speed_mode = FL_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = FL_TIMER,
        .freq_hz = FL_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t err = ledc_timer_config(&timer);
    if (err != ESP_OK) return err;
    ledc_channel_config_t ch = {
        .gpio_num = METALIO_FL_COOL_GPIO,
        .speed_mode = FL_MODE,
        .channel = FL_CH_COOL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = FL_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    err = ledc_channel_config(&ch);
    if (err != ESP_OK) return err;
    ch.gpio_num = METALIO_FL_WARM_GPIO;
    ch.channel = FL_CH_WARM;
    err = ledc_channel_config(&ch);
    if (err != ESP_OK) return err;
    s_ready = true;
    return ESP_OK;
}

void metalio_frontlight_set(metalio_fl_mode_t mode, unsigned brightness) {
    if (!s_ready) return;
    if (brightness > 100) brightness = 100;
    // 厂商 2.0.68-debug 起把实际上限减半（100% 对应占空比 511）。/ Vendor 2.0.68-debug halves the ceiling (100% = duty 511).
    const uint32_t duty = FL_DUTY_MAX * brightness / 200;
    const bool cool = mode == METALIO_FL_COOL || mode == METALIO_FL_BOTH;
    const bool warm = mode == METALIO_FL_WARM || mode == METALIO_FL_BOTH;
    ledc_set_duty(FL_MODE, FL_CH_COOL, cool ? duty : 0);
    ledc_update_duty(FL_MODE, FL_CH_COOL);
    ledc_set_duty(FL_MODE, FL_CH_WARM, warm ? duty : 0);
    ledc_update_duty(FL_MODE, FL_CH_WARM);
}
void metalio_frontlight_mix(bool enabled,unsigned brightness,unsigned warmth) {
    if(!s_ready)return;
    if(brightness>100)brightness=100;
    if(warmth>100)warmth=100;
    uint32_t total=enabled?FL_DUTY_MAX*brightness/200:0;
    uint32_t warm=total*warmth/100,cool=total-warm;
    ledc_set_duty(FL_MODE,FL_CH_COOL,cool);ledc_update_duty(FL_MODE,FL_CH_COOL);
    ledc_set_duty(FL_MODE,FL_CH_WARM,warm);ledc_update_duty(FL_MODE,FL_CH_WARM);
}
