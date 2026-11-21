/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PCF8563 RTC（I2C 0x51）：UTC 秒读写。VL 位置位表示掉过电，时间不可信。
 * PCF8563 RTC (I2C 0x51): UTC seconds. A set VL bit means power was lost and the time is invalid.
 */
#include "metalio_bsp.h"

#include <time.h>

#include "esp_log.h"

#define TAG "pcf8563"

#define REG_CTRL2 0x01
#define REG_SECONDS 0x02

// 2020-01-01，早于它的时间视为未设置。/ 2020-01-01; anything earlier counts as unset.
#define MIN_VALID_UNIX 1577836800u

static i2c_master_dev_handle_t s_dev;

static uint8_t bcd2bin(uint8_t v) {
    return (uint8_t)((v >> 4) * 10 + (v & 0x0F));
}

static uint8_t bin2bcd(unsigned v) {
    return (uint8_t)(((v / 10) << 4) | (v % 10));
}

// 不依赖时区的 UTC 换算（days_from_civil）。/ Timezone-free UTC conversion (days_from_civil).
static int64_t days_from_civil(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

esp_err_t metalio_rtc_init(void) {
    if (s_dev != NULL) return ESP_OK;
    esp_err_t err = metalio_i2c_add(METALIO_ADDR_PCF8563, 100000, &s_dev);
    if (err != ESP_OK) return err;
    uint8_t ctrl2 = 0;
    // 关闭闹钟/定时器中断，避免 RTC_INT 拉住 TCA INT。/ Disable alarm/timer interrupts so RTC_INT never holds TCA INT.
    err = metalio_i2c_write(s_dev, REG_CTRL2, &ctrl2, 1);
    if (err != ESP_OK) ESP_LOGW(TAG, "not responding: %s", esp_err_to_name(err));
    return err;
}

bool metalio_rtc_get(uint32_t* unix_seconds) {
    if (unix_seconds == NULL || s_dev == NULL) return false;
    uint8_t r[7];
    if (metalio_i2c_read(s_dev, REG_SECONDS, r, sizeof(r)) != ESP_OK) return false;
    if (r[0] & 0x80) return false;
    unsigned sec = bcd2bin(r[0] & 0x7F), min = bcd2bin(r[1] & 0x7F), hour = bcd2bin(r[2] & 0x3F);
    unsigned day = bcd2bin(r[3] & 0x3F), month = bcd2bin(r[5] & 0x1F);
    int year = 2000 + bcd2bin(r[6]) + ((r[5] & 0x80) ? 100 : 0);
    if (month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 || min > 59 || sec > 59) return false;
    int64_t t = days_from_civil(year, month, day) * 86400 + hour * 3600 + min * 60 + sec;
    if (t < MIN_VALID_UNIX || t > UINT32_MAX) return false;
    *unix_seconds = (uint32_t)t;
    return true;
}

esp_err_t metalio_rtc_set(uint32_t unix_seconds) {
    if (s_dev == NULL) return ESP_ERR_INVALID_STATE;
    if (unix_seconds < MIN_VALID_UNIX) return ESP_ERR_INVALID_ARG;
    time_t t = (time_t)unix_seconds;
    struct tm tm;
    gmtime_r(&t, &tm);
    int year = tm.tm_year + 1900;
    uint8_t r[7] = {
        bin2bcd((unsigned)tm.tm_sec),
        bin2bcd((unsigned)tm.tm_min),
        bin2bcd((unsigned)tm.tm_hour),
        bin2bcd((unsigned)tm.tm_mday),
        (uint8_t)tm.tm_wday,
        (uint8_t)(bin2bcd((unsigned)tm.tm_mon + 1) | (year >= 2100 ? 0x80 : 0)),
        bin2bcd((unsigned)(year % 100)),
    };
    // 写秒寄存器同时清 VL。/ Writing seconds also clears VL.
    return metalio_i2c_write(s_dev, REG_SECONDS, r, sizeof(r));
}
