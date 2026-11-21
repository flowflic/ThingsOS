/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Metalio E-Ink4-Plus 板级：I2C、TCA9555、TPS65185、EPD 总线与电源轨、按键、马达、前光。
 * 引脚与时序来自 CloudZao/Metalio-E-INK4-Plus（MIT）main/boards/metalio-e-ink4-plus/config.h
 * 与 epd_board_metalio_eink4_plus.c；驱动按 ESP-IDF 新 I2C master 接口重写。
 *
 * Metalio E-Ink4-Plus board: I2C, TCA9555, TPS65185, EPD bus and rails, keys,
 * motor, frontlight. Pins and sequences come from CloudZao/Metalio-E-INK4-Plus
 * (MIT) config.h and epd_board_metalio_eink4_plus.c; drivers are rewritten on
 * the ESP-IDF I2C master API.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "epd_board.h"
#include "epd_lcd.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define METALIO_PRODUCT_NAME "Metalio E-Ink4-Plus"

/* ---- 引脚 / Pins (hardware V1.3/V1.4) ---- */
#define METALIO_I2C_PORT ((i2c_port_t)0)
#define METALIO_I2C_SDA GPIO_NUM_0
#define METALIO_I2C_SCL GPIO_NUM_1
/// TCA9555 INT#，与 SC7A20H INT1 线与。/ TCA9555 INT#, wire-ANDed with SC7A20H INT1.
#define METALIO_IOE_INT_GPIO GPIO_NUM_2
#define METALIO_TP_INT_GPIO GPIO_NUM_5
#define METALIO_FL_COOL_GPIO GPIO_NUM_6
#define METALIO_FL_WARM_GPIO GPIO_NUM_7
#define METALIO_ANA_SW_GPIO GPIO_NUM_36
#define METALIO_BOOT_GPIO GPIO_NUM_61

#define METALIO_SD_CLK GPIO_NUM_24
#define METALIO_SD_CMD GPIO_NUM_25
#define METALIO_SD_D0 GPIO_NUM_20
#define METALIO_SD_D1 GPIO_NUM_21
#define METALIO_SD_D2 GPIO_NUM_22
#define METALIO_SD_D3 GPIO_NUM_23

/* ---- I2C 地址 / I2C addresses ---- */
#define METALIO_ADDR_TCA9555 0x20
#define METALIO_ADDR_FT6336U 0x38
#define METALIO_ADDR_PCF8563 0x51
#define METALIO_ADDR_BQ27220 0x55
#define METALIO_ADDR_TPS65185 0x68
#define METALIO_ADDR_CX25601N 0x6B
#define METALIO_ADDR_SC7A20H 0x19

/* ---- TCA9555 脚号：bit = 口*8 + 位 / TCA9555 pins: bit = port*8 + bit ---- */
#define METALIO_IO_VCOM_CTRL 0
#define METALIO_IO_PWRUP 1
#define METALIO_IO_WAKEUP 2
#define METALIO_IO_MOTOR 3
#define METALIO_IO_PA_EN 4
#define METALIO_IO_CAM_SCR_EN 5
#define METALIO_IO_PWR_GOOD 7
#define METALIO_IO_VOL_UP 8
#define METALIO_IO_VOL_DOWN 9
#define METALIO_IO_TP_RST 10
#define METALIO_IO_PWR_PULSE 11
#define METALIO_IO_BT_PA_PWR 12
#define METALIO_IO_RTC_INT 14
#define METALIO_IO_POWER 15

/// 厂商固件每次上电写入 TPS65185 易失寄存器的 VCOM；本机无逐台出厂标定值。
/// / VCOM the vendor firmware writes to the volatile TPS65185 registers on each
/// power-up; this model has no per-unit factory calibration.
#define METALIO_VCOM_MV 1200

/// 面板四边保持（不驱动）的物理像素宽度，须为 4 的倍数；0 关闭。对应厂商 config.h 的
/// DISPLAY_WHITE_BORDER_PIXELS，当前与厂商一致取 0。
/// / Physical edge pixels held (not driven) on every scan, multiple of 4; 0 turns
/// the hold off. Mirrors the vendor config.h DISPLAY_WHITE_BORDER_PIXELS, now 0.
#define METALIO_WHITE_BORDER_PIXELS 0

extern const EpdBoardDefinition epd_board_metalio;

i2c_master_bus_handle_t metalio_i2c_bus(void);

/// I2C 总线互斥。/ I2C bus lock (recursive).
bool metalio_i2c_lock(uint32_t timeout_ms);
void metalio_i2c_unlock(void);

/// 寄存器读写助手，内部加总线锁。/ Register helpers; take the bus lock internally.
esp_err_t metalio_i2c_read(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t* data, size_t len);
esp_err_t metalio_i2c_write(i2c_master_dev_handle_t dev, uint8_t reg, const uint8_t* data, size_t len);
esp_err_t metalio_i2c_add(uint8_t addr, uint32_t speed_hz, i2c_master_dev_handle_t* out);
esp_err_t metalio_i2c_probe(uint8_t addr);

bool metalio_ioe_ok(void);
bool metalio_tps_ok(void);
bool metalio_rails_on(void);
/* Read back panel enable outputs after power-off; does not set VCOM. */
esp_err_t metalio_panel_idle_verify(void);

/// 读 TCA 两口输入并清 INT。/ Read both TCA input ports, clearing INT.
esp_err_t metalio_ioe_read(uint8_t* port0, uint8_t* port1);
int metalio_ioe_int_level(void);

esp_err_t metalio_touch_reset(void);
void metalio_motor(bool on);
void metalio_pa(bool on);
/// high=true 为正常路径；false 时 USB 切到虚拟 U 盘。/ true = normal path; false routes USB to the MSC disk.
void metalio_ana_sw(bool high);
/// TCA P1.3 关机脉冲串，与厂商固件同节拍。/ TCA P1.3 shutdown pulse train, vendor timing.
esp_err_t metalio_power_off(void);
/// SC7A20H INT1 设为开漏低有效，避免钳住与 TCA 线与的 GPIO2；芯片复位后须重设。
/// / Make SC7A20H INT1 open-drain active-low so it never clamps GPIO2, which is
/// wire-ANDed with the TCA INT; repeat after any chip reset.
esp_err_t metalio_accel_int_open_drain(void);

/* ---- 扫描时序 / Scan timing (metalio_epd_timing.c) ---- */
#define METALIO_EPD_PCLK_MIN_MHZ 10
#define METALIO_EPD_PCLK_MAX_MHZ 20
typedef enum {
    METALIO_EPD_SCAN_FULL = 0,
    METALIO_EPD_SCAN_FAST,
} metalio_epd_scan_profile_t;
void metalio_epd_set_pclk(int pclk_mhz);
int metalio_epd_pclk_mhz(void);
void metalio_epd_use_scan(metalio_epd_scan_profile_t profile);
/// 开机：按初始像素钟解出扫描参数并初始化 LCD。/ Boot: solve scan params and init the LCD.
void metalio_epd_init_lcd(const lcd_bus_config_t* bus, int bus_width, int width, int height, int pclk_mhz);

/* ---- SD 卡 / SD card (metalio_sd.c) ---- */
typedef struct {
    bool present;
    bool mounted;
    bool needs_format;
    char name[8];
    uint64_t capacity_bytes;
    uint64_t free_bytes;
    esp_err_t error;
} metalio_sd_info_t;
esp_err_t metalio_sd_start_probe(void);
esp_err_t metalio_sd_get_info(metalio_sd_info_t* info);
esp_err_t metalio_sd_remount(void);
esp_err_t metalio_sd_sync(void);
/// 卡座没有 CD 脚：已挂载时以 CMD13 应答判断，未挂载时以最近一次初始化结果判断。
/// / No CD pin: when mounted, CMD13 response decides; otherwise the last init result.
bool metalio_sd_present(void);
/// USB 直连：原始卡初始化，调用前须先 metalio_sd_sync()。/ USB disk: raw card init after metalio_sd_sync().
void* metalio_sd_raw_open(void);
void metalio_sd_raw_close(void* card);

/* ---- 触摸 / Touch (ft6336u.c) ---- */
typedef struct {
    bool touched;
    uint8_t count;
    uint16_t x;
    uint16_t y;
    /// 寄存器 0x00 起 15 字节，与 FocalTech 报点格式一致。/ 15 bytes from register 0x00, FocalTech layout.
    uint8_t raw[15];
} metalio_touch_t;
esp_err_t metalio_touch_init(void);
esp_err_t metalio_touch_read(metalio_touch_t* touch);
esp_err_t metalio_touch_sleep(void);
esp_err_t metalio_touch_wake(void);
int metalio_touch_int_level(void);

/* ---- 电源 / Power (metalio_power.c) ---- */
typedef enum {
    METALIO_CHARGE_UNKNOWN = 0,
    METALIO_CHARGE_NONE,
    METALIO_CHARGE_CHARGING,
    METALIO_CHARGE_FULL,
} metalio_charge_t;
esp_err_t metalio_power_init(void);
/// 0..100；-1 表示电量计不可用。/ 0..100; -1 when the gauge is unavailable.
/// 后台每秒采样的快照；开机/唤醒后约 3s 内为 -1。/ Snapshot of the 1 Hz
/// background sampler; -1 for about 3 s after boot or wake.
int metalio_battery_percent(void);
/// 休眠前停止电量采样。/ Stop battery sampling before sleep.
void metalio_battery_pause(void);
/// 唤醒后清除旧状态并在 3s 后重新校准。/ Reset and recalibrate 3 s after wake.
void metalio_battery_restart(void);
metalio_charge_t metalio_charge_state(void);
void metalio_power_suspend(void);
void metalio_power_resume(void);

/* ---- RTC (pcf8563.c) ---- */
esp_err_t metalio_rtc_init(void);
/// 时间无效（VL 置位）返回 false。/ Returns false while the VL flag marks time invalid.
bool metalio_rtc_get(uint32_t* unix_seconds);
esp_err_t metalio_rtc_set(uint32_t unix_seconds);

/* ---- 前光 / Frontlight (metalio_frontlight.c) ---- */
typedef enum {
    METALIO_FL_OFF = 0,
    METALIO_FL_WARM,
    METALIO_FL_COOL,
    METALIO_FL_BOTH,
} metalio_fl_mode_t;
esp_err_t metalio_frontlight_init(void);
/// brightness 0..100。/ brightness 0..100.
void metalio_frontlight_set(metalio_fl_mode_t mode, unsigned brightness);
/* Constant-total brightness; each channel retains the vendor duty ceiling. */
void metalio_frontlight_mix(bool enabled,unsigned brightness,unsigned warmth);
/* BH1098G clock master, ESP I2S slave. UART and fixed slot mapping from the
   MIT Metalio board reference. Caller owns PA separately on the UI I/O lock. */
esp_err_t metalio_audio_open(bool microphone);
/* UART1 has one reader: Bluetooth worker while enabled; local codec startup
   only after that worker has restored mode1 and relinquished the port. */
esp_err_t metalio_bh_start(void);
esp_err_t metalio_bh_send(const char* command);
int metalio_bh_read(void* data,size_t bytes,unsigned timeout_ms);
void metalio_audio_wireless(bool connected);
esp_err_t metalio_audio_read(int32_t* stereo,size_t bytes,size_t* received);
esp_err_t metalio_audio_write(const int32_t* stereo,size_t bytes,size_t* sent);
void metalio_audio_close(void);
/* Cumulative I2S TX queue overflows (speaker DMA ran dry). Diagnostic only. */
unsigned metalio_audio_tx_underruns(void);

#ifdef __cplusplus
}
#endif
