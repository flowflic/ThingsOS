/*
 * SPDX-License-Identifier: MIT
 *
 * BQ27220 电量计与 CX25601N 充电芯片。寄存器、默认参数（VREG 4400mV、ICHG 1000mA）、
 * 欠压 BATFET 关断与再充 kick 策略逐条移植自 CloudZao/Metalio-E-INK4-Plus
 * boards/common/power/{bq27220_gauge.cc,cx25601n.cc} 与 periph/metalio_power.cc
 * （MIT，Copyright (c) 2025 Shenzhen Xinzhi Future Technology Co., Ltd.），
 * 改为 ESP-IDF 新 I2C master 接口的 C 实现；电池安全策略未作改动。
 *
 * BQ27220 gauge and CX25601N charger. Registers, defaults (VREG 4400 mV, ICHG
 * 1000 mA), undervoltage BATFET shutdown and recharge kick are ported line by
 * line from CloudZao/Metalio-E-INK4-Plus boards/common/power/{bq27220_gauge.cc,
 * cx25601n.cc} and periph/metalio_power.cc (MIT, Copyright (c) 2025 Shenzhen
 * Xinzhi Future Technology Co., Ltd.) as C on the ESP-IDF I2C master API. The
 * battery safety policy is unchanged.
 */
#include "metalio_bsp.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define TAG "metalio_power"

/* ---- BQ27220 ---- */
#define BQ_REG_VOLTAGE 0x08
#define BQ_REG_CURRENT 0x0C
#define BATTERY_EMPTY_MV 3300
#define BATTERY_FULL_MV 4330
#define FILTER_SIZE 60
#define SOC_STEP_US (30LL * 1000 * 1000)
#define FULL_HOLD_US (30LL * 1000 * 1000)
#define FULL_CONFIRM_MV 4330
#define FULL_EXIT_MV 4280

/* ---- CX25601N ---- */
#define REG_ICHG_LO 0x02
#define REG_ICHG_HI 0x03
#define REG_VREG_LO 0x04
#define REG_VREG_HI 0x05
#define REG_IINDPM_LO 0x06
#define REG_IINDPM_HI 0x07
#define REG_IPRECHG_LO 0x10
#define REG_IPRECHG_HI 0x11
#define REG_ITERM_LO 0x12
#define REG_ITERM_HI 0x13
#define REG_CHG_CTRL0 0x14
#define REG_CHG_TMR 0x15
#define REG_CHG_CTRL1 0x16
#define REG_CHG_CTRL3 0x18
#define REG_STATUS1 0x1E
#define REG_PART_INFO 0x38
#define REG_UNLOCK 0x70

#define VREG_MIN_MV 3840
#define VREG_MAX_MV 4800
#define VREG_STEP_MV 10
#define ICHG_STEP_MA 80
#define IINDPM_STEP_MA 20
#define DEFAULT_VREG_MV 4400
#define DEFAULT_ICHG_MA 1000
// 无适配器且 ≤3.1V 时切断 BATFET。/ BATFET shutdown below 3.1 V without input.
#define UV_BATFET_SHUTDOWN_MV 3100

#define CHG_STAT_NOT 0

static i2c_master_dev_handle_t s_bq, s_cx;
static bool s_bq_ready, s_cx_ready;
static int s_bq_errors;
static SemaphoreHandle_t s_cx_lock;
static TaskHandle_t s_vreg_task;
static bool s_vreg_suspended;
static uint32_t s_vreg_target_mv, s_vreg_now_mv;
static bool s_uv_shutdown_done;

static float s_filter[FILTER_SIZE];
static int s_filter_idx;
static float s_filter_sum;
static bool s_filter_primed;
static int s_displayed = -1;
static int64_t s_last_step_us, s_full_since_us;
static int s_last_dir;
static bool s_charging;

/* ---- BQ27220 ---- */

static bool bq_read_u16(uint8_t reg, uint16_t* out) {
    uint8_t b[2];
    if (metalio_i2c_read(s_bq, reg, b, 2) != ESP_OK) {
        if (++s_bq_errors >= 5) s_bq_ready = false;
        return false;
    }
    s_bq_errors = 0;
    *out = (uint16_t)(b[0] | (b[1] << 8));
    return true;
}

static bool bq_begin(void) {
    if (s_bq == NULL && metalio_i2c_add(METALIO_ADDR_BQ27220, 100000, &s_bq) != ESP_OK) return false;
    uint16_t mv = 0;
    // 标准命令每秒最多两次。/ Standard commands at most twice per second.
    for (int i = 0; i < 2; ++i) {
        if (i) vTaskDelay(pdMS_TO_TICKS(500));
        if (bq_read_u16(BQ_REG_VOLTAGE, &mv) && mv >= 2500 && mv <= 5000) {
            s_bq_ready = true;
            ESP_LOGI(TAG, "BQ27220 online, %u mV", mv);
            return true;
        }
    }
    return false;
}

static int32_t battery_mv(void) {
    uint16_t mv = 0;
    return s_bq_ready && bq_read_u16(BQ_REG_VOLTAGE, &mv) ? mv : 0;
}

static int32_t battery_ma(void) {
    uint16_t raw = 0;
    return s_bq_ready && bq_read_u16(BQ_REG_CURRENT, &raw) ? (int16_t)raw : 0;
}

static float filter_push(float sample) {
    if (!s_filter_primed) {
        for (int i = 0; i < FILTER_SIZE; ++i) s_filter[i] = sample;
        s_filter_sum = sample * FILTER_SIZE;
        s_filter_idx = 0;
        s_filter_primed = true;
        return sample;
    }
    s_filter_sum -= s_filter[s_filter_idx];
    s_filter[s_filter_idx] = sample;
    s_filter_sum += sample;
    s_filter_idx = (s_filter_idx + 1) % FILTER_SIZE;
    return s_filter_sum / FILTER_SIZE;
}

static int mono_step(int target, bool charging) {
    const int64_t now = esp_timer_get_time();
    if (s_displayed < 0) {
        s_displayed = target;
        s_last_step_us = now;
        return s_displayed;
    }
    if (now - s_last_step_us < SOC_STEP_US) return s_displayed;
    if (charging ? target > s_displayed : target < s_displayed) {
        s_displayed += charging ? 1 : -1;
        s_last_step_us = now;
    }
    return s_displayed;
}

// 电压线性 SOC + 60 点滑动平均 + 每 30s 单向一档；满电需 ≥4.33V 持续 30s。
// / Voltage-linear SOC, 60-sample moving average, one step per 30 s in one
// direction; 100% needs ≥4.33 V held for 30 s.
int metalio_battery_percent(void) {
    static unsigned retry;
    if (!s_bq_ready && (retry++ % 10 != 0 || !bq_begin())) return -1;
    uint16_t mv = 0;
    if (!bq_read_u16(BQ_REG_VOLTAGE, &mv)) return s_displayed;
    float raw = mv >= BATTERY_FULL_MV ? 100.0f
              : mv <= BATTERY_EMPTY_MV
                  ? 0.0f
                  : (float)(mv - BATTERY_EMPTY_MV) * 100.0f / (BATTERY_FULL_MV - BATTERY_EMPTY_MV);
    int target = (int)(filter_push(raw) + 0.5f);
    if (target < 0) target = 0;
    if (target > 100) target = 100;
    const int64_t now = esp_timer_get_time();
    bool full_ok = false;
    if (mv >= FULL_CONFIRM_MV) {
        if (s_full_since_us == 0) s_full_since_us = now;
        full_ok = now - s_full_since_us >= FULL_HOLD_US;
    } else {
        s_full_since_us = 0;
    }
    if (target >= 100 && !full_ok) target = 99;
    if (s_displayed >= 100 && mv >= FULL_EXIT_MV) target = 100;
    int32_t ma = battery_ma();
    bool charging = ma > 5, discharging = ma < -5;
    int dir = charging ? 1 : discharging ? -1 : 0;
    if (s_displayed >= 0 && dir != s_last_dir) s_last_step_us = now;
    s_last_dir = dir;
    int level = mono_step(target, charging);
    if (full_ok && target >= 100 && s_displayed >= 99) level = s_displayed = 100;
    s_charging = charging && level < 100;
    return level;
}

/* ---- CX25601N ---- */

static esp_err_t cx_read(uint8_t reg, uint8_t* val) {
    return metalio_i2c_read(s_cx, reg, val, 1);
}

static esp_err_t cx_write(uint8_t reg, uint8_t val) {
    return metalio_i2c_write(s_cx, reg, &val, 1);
}

static esp_err_t cx_update(uint8_t reg, uint8_t mask, uint8_t shift, uint8_t field) {
    uint8_t cur = 0;
    esp_err_t err = cx_read(reg, &cur);
    if (err != ESP_OK) return err;
    uint8_t m = (uint8_t)(mask << shift);
    return cx_write(reg, (uint8_t)((cur & (uint8_t)~m) | ((field << shift) & m)));
}

static esp_err_t cx_bits(uint8_t reg, uint8_t mask, uint8_t shift, uint8_t* field) {
    uint8_t cur = 0;
    esp_err_t err = cx_read(reg, &cur);
    if (err == ESP_OK) *field = (uint8_t)((cur >> shift) & mask);
    return err;
}

static esp_err_t cx_set_vreg(uint32_t mv) {
    if (mv < VREG_MIN_MV) mv = VREG_MIN_MV;
    if (mv > VREG_MAX_MV) mv = VREG_MAX_MV;
    uint32_t code = mv / VREG_STEP_MV;
    esp_err_t err = cx_update(REG_VREG_LO, 0x1F, 3, (uint8_t)(code & 0x1F));
    if (err == ESP_OK) err = cx_update(REG_VREG_HI, 0x0F, 0, (uint8_t)((code >> 5) & 0x0F));
    if (err == ESP_OK) s_vreg_now_mv = mv;
    return err;
}

static esp_err_t cx_set_iindpm(uint32_t ma) {
    uint32_t code = ma / IINDPM_STEP_MA;
    if (code < 5) code = 5;
    if (code > 150) code = 150;
    esp_err_t err = cx_update(REG_IINDPM_LO, 0x0F, 4, (uint8_t)(code & 0x0F));
    if (err == ESP_OK) err = cx_update(REG_IINDPM_HI, 0x0F, 0, (uint8_t)((code >> 4) & 0x0F));
    return err;
}

static esp_err_t cx_set_ichg(uint32_t ma) {
    uint32_t code = ma / ICHG_STEP_MA;
    if (code < 1) code = 1;
    if (code > 38) code = 38;
    esp_err_t err = cx_update(REG_ICHG_LO, 0x03, 6, (uint8_t)(code & 0x03));
    if (err == ESP_OK) err = cx_update(REG_ICHG_HI, 0x0F, 0, (uint8_t)((code >> 2) & 0x0F));
    if (err == ESP_OK) err = cx_set_iindpm(code * ICHG_STEP_MA);
    return err;
}

static void cx_unlock_private(bool enable) {
    if (!enable) {
        cx_write(REG_UNLOCK, 0x00);
        return;
    }
    for (int i = 0; i < 10; i++) {
        cx_write(REG_UNLOCK, 0x00);
        cx_write(REG_UNLOCK, 0x50);
        cx_write(REG_UNLOCK, 0x57);
        cx_write(REG_UNLOCK, 0x44);
        uint8_t v = 0;
        if (cx_read(REG_UNLOCK, &v) == ESP_OK && v == 0x03) return;
    }
    ESP_LOGW(TAG, "CX25601N private register unlock failed");
}

static esp_err_t cx_defaults(void) {
    esp_err_t err = cx_update(REG_CHG_TMR, 0x01, 6, 0);           // DIS_DPDM
    if (err == ESP_OK) err = cx_update(REG_CHG_CTRL1, 0x01, 4, 0); // HIZ off
    if (err == ESP_OK) err = cx_update(REG_CHG_CTRL1, 0x03, 0, 0); // watchdog off
    if (err == ESP_OK) err = cx_update(REG_IPRECHG_LO, 0x0F, 4, 0x0C);
    if (err == ESP_OK) err = cx_update(REG_IPRECHG_HI, 0x01, 0, 0x00);
    if (err == ESP_OK) err = cx_update(REG_ITERM_LO, 0x1F, 3, 0x06);
    if (err == ESP_OK) err = cx_update(REG_ITERM_HI, 0x01, 0, 0x00);
    if (err == ESP_OK) err = cx_set_vreg(DEFAULT_VREG_MV);
    if (err == ESP_OK) err = cx_set_ichg(500);
    if (err != ESP_OK) return err;
    cx_unlock_private(true);
    cx_write(0x86, 0x06);
    cx_write(0x3A, 0x10);
    cx_write(0x46, 0x20);
    cx_unlock_private(false);
    cx_update(REG_CHG_CTRL0, 0x01, 0, 1);
    cx_update(REG_CHG_CTRL3, 0x01, 2, 0);
    cx_update(0x1A, 0x01, 7, 1);
    cx_update(0x23, 0x07, 2, 0x07);
    cx_update(0x24, 0x01, 3, 1);
    return cx_update(REG_CHG_CTRL1, 0x01, 5, 1); // EN_CHG
}

static void cx_kick_recharge(int32_t vbat) {
    ESP_LOGI(TAG, "recharge kick: vbat=%ldmV < VREG=%lumV", (long)vbat, (unsigned long)s_vreg_target_mv);
    xSemaphoreTake(s_cx_lock, portMAX_DELAY);
    cx_set_vreg(s_vreg_target_mv);
    cx_update(REG_CHG_CTRL0, 0x01, 2, 0);
    cx_update(REG_CHG_CTRL1, 0x01, 5, 0);
    cx_update(REG_CHG_CTRL1, 0x01, 4, 0);
    xSemaphoreGive(s_cx_lock);
    vTaskDelay(pdMS_TO_TICKS(200));
    xSemaphoreTake(s_cx_lock, portMAX_DELAY);
    cx_update(REG_CHG_CTRL1, 0x01, 4, 0);
    cx_update(REG_CHG_CTRL1, 0x01, 5, 1);
    xSemaphoreGive(s_cx_lock);
    vTaskDelay(pdMS_TO_TICKS(500));
}

static void cx_vreg_task(void* arg) {
    (void)arg;
    for (;;) {
        const int32_t peek = battery_mv();
        const bool below = peek >= 2500 && (uint32_t)peek < s_vreg_target_mv;
        vTaskDelay(pdMS_TO_TICKS(below ? 2000 : 10000));

        xSemaphoreTake(s_cx_lock, portMAX_DELAY);
        if (s_vreg_now_mv != s_vreg_target_mv) cx_set_vreg(s_vreg_target_mv);
        uint8_t stat = 0, vbus = 0;
        const bool have_stat = cx_bits(REG_STATUS1, 0x03, 3, &stat) == ESP_OK;
        const bool have_vbus = cx_bits(REG_STATUS1, 0x07, 0, &vbus) == ESP_OK;
        xSemaphoreGive(s_cx_lock);

        const int32_t vbat = battery_mv();
        const int32_t ima = battery_ma() > 0 ? battery_ma() : 0;
        const bool vbat_below = vbat >= 2500 && (uint32_t)vbat < s_vreg_target_mv;

        if (!s_uv_shutdown_done && vbat >= 2500 && vbat <= UV_BATFET_SHUTDOWN_MV) {
            if (have_vbus && vbus == 0) {
                ESP_LOGW(TAG, "UV protect: vbat=%ldmV -> BATFET shutdown", (long)vbat);
                xSemaphoreTake(s_cx_lock, portMAX_DELAY);
                if (cx_write(REG_CHG_CTRL3, 0x01) == ESP_OK) s_uv_shutdown_done = true;
                xSemaphoreGive(s_cx_lock);
            }
        } else if (s_uv_shutdown_done && vbat > UV_BATFET_SHUTDOWN_MV + 100) {
            s_uv_shutdown_done = false;
        }

        xSemaphoreTake(s_cx_lock, portMAX_DELAY);
        cx_update(REG_CHG_CTRL0, 0x01, 2, vbat_below ? 0 : 1); // EN_TERM
        xSemaphoreGive(s_cx_lock);

        if (have_stat && stat == CHG_STAT_NOT && ima < 5 && vbat_below) cx_kick_recharge(vbat);
    }
}

esp_err_t metalio_power_init(void) {
    bq_begin();
    if (metalio_i2c_probe(METALIO_ADDR_CX25601N) != ESP_OK) {
        ESP_LOGW(TAG, "CX25601N not found; charger left at hardware defaults");
        return s_bq_ready ? ESP_OK : ESP_ERR_NOT_FOUND;
    }
    if (s_cx == NULL && metalio_i2c_add(METALIO_ADDR_CX25601N, 100000, &s_cx) != ESP_OK) return ESP_FAIL;
    if (s_cx_lock == NULL) s_cx_lock = xSemaphoreCreateMutex();
    if (s_cx_lock == NULL) return ESP_ERR_NO_MEM;
    uint8_t part = 0;
    esp_err_t err = cx_read(REG_PART_INFO, &part);
    if (err == ESP_OK) err = cx_defaults();
    if (err == ESP_OK) err = cx_set_ichg(DEFAULT_ICHG_MA);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "CX25601N init failed: %s", esp_err_to_name(err));
        return err;
    }
    s_vreg_target_mv = DEFAULT_VREG_MV;
    s_cx_ready = true;
    ESP_LOGI(TAG, "CX25601N PART=0x%02X VREG=%d mV ICHG=%d mA", part, DEFAULT_VREG_MV, DEFAULT_ICHG_MA);
    if (s_vreg_task == NULL
        && xTaskCreate(cx_vreg_task, "cx25601n", 3072, NULL, 5, &s_vreg_task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

#include "charge_status.inc"
metalio_charge_t metalio_charge_state(void) {
    if (!s_cx_ready) return METALIO_CHARGE_UNKNOWN;
    uint8_t raw = 0;
    static esp_err_t last_error = ESP_OK;
    static int last_state = -1;
    xSemaphoreTake(s_cx_lock, portMAX_DELAY);
    esp_err_t err = cx_read(REG_STATUS1, &raw);
    xSemaphoreGive(s_cx_lock);
    if (err != ESP_OK) {
        if (err != last_error) ESP_LOGW(TAG, "charge status read failed: %s", esp_err_to_name(err));
        last_error = err;
        return METALIO_CHARGE_UNKNOWN;
    }
    metalio_charge_t state = charge_status_decode(raw, s_displayed);
    if ((int)state != last_state || last_error != ESP_OK)
        ESP_LOGI(TAG, "charge status: REG1E=0x%02X CHG_STAT=%u VBUS_STAT=%u state=%u battery=%d%%",
                 raw, (raw >> 3) & 3, raw & 7, (unsigned)state, s_displayed);
    last_state = state;
    last_error = ESP_OK;
    return state;
}

void metalio_power_suspend(void) {
    if (s_vreg_task == NULL || s_vreg_suspended) return;
    // 先拿锁，确保任务不在 I2C 事务中途被挂起。/ Take the lock so the task is never suspended mid-transaction.
    xSemaphoreTake(s_cx_lock, portMAX_DELAY);
    vTaskSuspend(s_vreg_task);
    xSemaphoreGive(s_cx_lock);
    s_vreg_suspended = true;
}

void metalio_power_resume(void) {
    if (s_vreg_task == NULL || !s_vreg_suspended) return;
    s_vreg_suspended = false;
    vTaskResume(s_vreg_task);
}
