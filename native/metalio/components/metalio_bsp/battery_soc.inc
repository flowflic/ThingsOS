// SPDX-License-Identifier: MIT
/*
 * 电压估算电量的纯计算部分（无 I2C、无 RTOS），供 metalio_power.c 与主机测试共用。
 * 放电取点表与“充放电分曲线、切换清滤波、满电确认跨待机保留”的策略来自
 * CloudZao/Metalio-E-INK4-Plus fd3fa03（v2.0.82）main/boards/common/power/
 * bq27220_gauge.cc 与 docs/battery-soc-and-charge.md（MIT，Copyright (c) 2025
 * Shenzhen Xinzhi Future Technology Co., Ltd.）。充电线性区间、满电阈值和
 * 1% 步进沿用本移植原有参数。
 *
 * Pure voltage-SOC math (no I2C, no RTOS) shared by metalio_power.c and host
 * tests. The discharge table and the split charge/discharge curves, filter
 * reset on curve change and standby-persistent full latch follow
 * CloudZao/Metalio-E-INK4-Plus fd3fa03 (v2.0.82) bq27220_gauge.cc and
 * docs/battery-soc-and-charge.md (MIT). The charging range, full thresholds
 * and 1% step keep this port's existing parameters.
 */
#include <stdbool.h>
#include <stdint.h>

#define BATTERY_EMPTY_MV 3300
#define BATTERY_FULL_MV 4330
#define BATTERY_FILTER_SIZE 60
#define BATTERY_SAMPLE_US (1000LL * 1000)
#define BATTERY_STEP_US (30LL * 1000 * 1000)
#define BATTERY_FULL_HOLD_US (30LL * 1000 * 1000)
#define BATTERY_FULL_CONFIRM_MV 4330
#define BATTERY_FULL_EXIT_MV 4280

/* 424860-2000 电芯约 1A 放电至 3.3V、按 1870.7mAh 归一化，每 10% 一点。
   424860-2000 cell, ~1 A discharge to 3.3 V normalised to 1870.7 mAh, every 10%. */
static const uint16_t k_battery_discharge_mv[11] = {
    3300, 3481, 3565, 3624, 3663, 3702, 3757, 3828, 3919, 4021, 4211};

typedef struct {
    float filter[BATTERY_FILTER_SIZE];
    float sum;
    int index;
    bool primed;
    int displayed;          /* -1 until the first sample after a reset */
    int last_dir;           /* +1 charging, -1 discharging, 0 idle */
    int64_t last_step_us, full_since_us, last_sample_us;
    bool full_latched;      /* survives battery_soc_reset() */
} battery_soc_t;

static float battery_soc_curve(uint16_t mv, bool charging) {
    if (mv <= BATTERY_EMPTY_MV) return 0.0f;
    if (charging) {
        if (mv >= BATTERY_FULL_MV) return 100.0f;
        return (float)(mv - BATTERY_EMPTY_MV) * 100.0f / (BATTERY_FULL_MV - BATTERY_EMPTY_MV);
    }
    for (int i = 1; i < 11; ++i) {
        if (mv <= k_battery_discharge_mv[i]) {
            return (float)(i - 1) * 10.0f
                 + (float)(mv - k_battery_discharge_mv[i - 1]) * 10.0f
                       / (float)(k_battery_discharge_mv[i] - k_battery_discharge_mv[i - 1]);
        }
    }
    return 100.0f;
}

/* 清除滤波与显示值，保留满电锁存。/ Clear filter and display; keep the full latch. */
static void battery_soc_reset(battery_soc_t* s) {
    bool latched = s->full_latched;
    *s = (battery_soc_t){0};
    s->displayed = -1;
    s->full_latched = latched;
}

static float battery_soc_filter(battery_soc_t* s, float sample) {
    if (!s->primed) {
        for (int i = 0; i < BATTERY_FILTER_SIZE; ++i) s->filter[i] = sample;
        s->sum = sample * BATTERY_FILTER_SIZE;
        s->index = 0;
        s->primed = true;
        return sample;
    }
    s->sum -= s->filter[s->index];
    s->filter[s->index] = sample;
    s->sum += sample;
    s->index = (s->index + 1) % BATTERY_FILTER_SIZE;
    return s->sum / BATTERY_FILTER_SIZE;
}

static int battery_soc_step(battery_soc_t* s, int target, bool up, int64_t now) {
    if (s->displayed < 0) {
        s->displayed = target;
        s->last_step_us = now;
        return target;
    }
    if (now - s->last_step_us < BATTERY_STEP_US) return s->displayed;
    if (up ? target > s->displayed : target < s->displayed) {
        s->displayed += up ? 1 : -1;
        s->last_step_us = now;
    }
    return s->displayed;
}

/* 一次有效采样（约 1Hz）。/ One valid sample, about 1 Hz. */
static int battery_soc_update(battery_soc_t* s, uint16_t mv, int16_t ma, int64_t now) {
    const bool charging = ma > 5, discharging = ma < -5;
    const int dir = charging ? 1 : discharging ? -1 : 0;
    /* 两条曲线不混合滤波，显示值与步进限制保留。/ Never mix curves in one filter. */
    if (s->displayed >= 0 && charging != (s->last_dir > 0)) s->primed = false;
    int target = (int)(battery_soc_filter(s, battery_soc_curve(mv, charging)) + 0.5f);
    if (target < 0) target = 0;
    if (target > 100) target = 100;

    /* 采样中断超过 2 个周期则重新确认满电。/ A gap restarts full confirmation. */
    if (s->last_sample_us != 0 && now - s->last_sample_us > 2 * BATTERY_SAMPLE_US) s->full_since_us = 0;
    s->last_sample_us = now;
    if (mv >= BATTERY_FULL_CONFIRM_MV) {
        if (s->full_since_us == 0) s->full_since_us = now;
        if (now - s->full_since_us >= BATTERY_FULL_HOLD_US) s->full_latched = true;
    } else {
        s->full_since_us = 0;
    }
    if (mv < BATTERY_FULL_EXIT_MV) s->full_latched = false;
    if (s->full_latched) target = 100;
    else if (target >= 100) target = 99;

    if (s->displayed >= 0 && dir != s->last_dir) s->last_step_us = now;
    s->last_dir = dir;
    int level = battery_soc_step(s, target, charging || s->full_latched, now);
    if (s->full_latched && level >= 99) level = s->displayed = 100;
    return level;
}
