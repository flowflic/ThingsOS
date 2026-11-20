/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Metalio 板级：I2C 总线、TCA9555、TPS65185 与 epdiy 板定义。
 * 上电时序、TCA 口分配与关机脉冲取自 CloudZao/Metalio-E-INK4-Plus（MIT）
 * epd_board_metalio_eink4_plus.c；VCOM 在一次完整更新期间稳定使能，结束后随电源关闭。
 *
 * Metalio board: I2C bus, TCA9555, TPS65185 and the epdiy board definition.
 * Power-up sequence, TCA port map and shutdown pulse come from
 * CloudZao/Metalio-E-INK4-Plus (MIT) epd_board_metalio_eink4_plus.c. VCOM is
 * stable across a complete update, then disabled with the panel rails.
 */
#include "metalio_bsp.h"

#include <string.h>

#include "driver/gpio.h"
#include "epdiy.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define TAG "metalio"

#define EPD_D0 GPIO_NUM_8
#define EPD_D1 GPIO_NUM_9
#define EPD_D2 GPIO_NUM_10
#define EPD_D3 GPIO_NUM_11
#define EPD_D4 GPIO_NUM_12
#define EPD_D5 GPIO_NUM_13
#define EPD_D6 GPIO_NUM_14
#define EPD_D7 GPIO_NUM_15
#define EPD_XCL GPIO_NUM_16
#define EPD_XLE GPIO_NUM_17
#define EPD_XOE GPIO_NUM_18
#define EPD_XSTL GPIO_NUM_4
#define EPD_MODE GPIO_NUM_19
#define EPD_SPV GPIO_NUM_35
#define EPD_BORDER GPIO_NUM_37
#define EPD_CKV GPIO_NUM_40

#define I2C_SLOW_HZ 100000
#define I2C_TIMEOUT_MS 100

#define TCA_REG_IN0 0
#define TCA_REG_OUT0 2
#define TCA_REG_CFG0 6

#define TPS_REG_ENABLE 0x01
#define TPS_REG_VCOM1 0x03
#define TPS_REG_VCOM2 0x04
#define TPS_REG_INT1 0x07
#define TPS_REG_INT2 0x08
#define TPS_REG_UPSEQ0 0x09
#define TPS_REG_UPSEQ1 0x0A
#define TPS_REG_PG 0x0F
#define TPS_REG_REVID 0x10

#define BIT_OF(io) ((uint8_t)(1u << ((io) & 7)))

// Port0：VCOM/PWRUP/WAKEUP/马达/PA/屏电为输出，PWR_GOOD 等为输入。
// / Port0: VCOM/PWRUP/WAKEUP/motor/PA/panel rail are outputs; PWR_GOOD and the rest are inputs.
#define P0_OUTPUTS                                                                                 \
    (BIT_OF(METALIO_IO_VCOM_CTRL) | BIT_OF(METALIO_IO_PWRUP) | BIT_OF(METALIO_IO_WAKEUP)          \
     | BIT_OF(METALIO_IO_MOTOR) | BIT_OF(METALIO_IO_PA_EN) | BIT_OF(METALIO_IO_CAM_SCR_EN))
// Port1：TP_RST、蓝牙/功放电源、关机脉冲为输出；音量、RTC_INT、电源键为输入。
// / Port1: TP_RST, BT/PA rail and shutdown pulse are outputs; volume, RTC_INT and power key are inputs.
#define P1_OUTPUTS                                                                                 \
    (BIT_OF(METALIO_IO_TP_RST) | BIT_OF(METALIO_IO_BT_PA_PWR) | BIT_OF(METALIO_IO_PWR_PULSE))

// VCOM_CTRL 拉高后到扫描开始的等待。/ Settle time after raising VCOM_CTRL before scanning.
#define VCOM_SETTLE_US 2000
#define PWR_PULSE_COUNT 15
#define PWR_PULSE_INTERVAL_MS 100

static i2c_master_bus_handle_t s_bus;
static SemaphoreHandle_t s_bus_lock;
static i2c_master_dev_handle_t s_tca;
static i2c_master_dev_handle_t s_tps;
static bool s_ioe_ok, s_tps_ok, s_rails_on;
// 端口输出锁存。/ Port output latches.
static uint8_t s_p0 = BIT_OF(METALIO_IO_CAM_SCR_EN);
static uint8_t s_p1 = BIT_OF(METALIO_IO_TP_RST) | BIT_OF(METALIO_IO_BT_PA_PWR);

static const lcd_bus_config_t s_lcd_bus = {
    .clock = EPD_XCL,
    .ckv = EPD_CKV,
    .leh = EPD_XLE,
    .start_pulse = EPD_XSTL,
    .stv = EPD_SPV,
    .data = { EPD_D0, EPD_D1, EPD_D2, EPD_D3, EPD_D4, EPD_D5, EPD_D6, EPD_D7,
              EPD_D7, EPD_D7, EPD_D7, EPD_D7, EPD_D7, EPD_D7, EPD_D7, EPD_D7 },
};

/* ---- I2C ---- */

i2c_master_bus_handle_t metalio_i2c_bus(void) {
    return s_bus;
}

bool metalio_i2c_lock(uint32_t timeout_ms) {
    if (s_bus_lock == NULL) return true;
    return xSemaphoreTakeRecursive(s_bus_lock, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void metalio_i2c_unlock(void) {
    if (s_bus_lock != NULL) xSemaphoreGiveRecursive(s_bus_lock);
}

esp_err_t metalio_i2c_add(uint8_t addr, uint32_t speed_hz, i2c_master_dev_handle_t* out) {
    if (s_bus == NULL || out == NULL) return ESP_ERR_INVALID_STATE;
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = speed_hz,
        // BQ27220 可拉伸时钟到 4ms。/ BQ27220 may stretch the clock up to 4 ms.
        .scl_wait_us = 20000,
    };
    return i2c_master_bus_add_device(s_bus, &cfg, out);
}

esp_err_t metalio_i2c_probe(uint8_t addr) {
    if (s_bus == NULL) return ESP_ERR_INVALID_STATE;
    if (!metalio_i2c_lock(I2C_TIMEOUT_MS)) return ESP_ERR_TIMEOUT;
    esp_err_t err = i2c_master_probe(s_bus, addr, 50);
    metalio_i2c_unlock();
    return err;
}

esp_err_t metalio_i2c_read(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t* data, size_t len) {
    if (dev == NULL) return ESP_ERR_INVALID_STATE;
    if (!metalio_i2c_lock(I2C_TIMEOUT_MS)) return ESP_ERR_TIMEOUT;
    esp_err_t err = i2c_master_transmit_receive(dev, &reg, 1, data, len, I2C_TIMEOUT_MS);
    metalio_i2c_unlock();
    return err;
}

esp_err_t metalio_i2c_write(i2c_master_dev_handle_t dev, uint8_t reg, const uint8_t* data, size_t len) {
    if (dev == NULL || len > 15) return ESP_ERR_INVALID_ARG;
    uint8_t buf[16];
    buf[0] = reg;
    memcpy(buf + 1, data, len);
    if (!metalio_i2c_lock(I2C_TIMEOUT_MS)) return ESP_ERR_TIMEOUT;
    esp_err_t err = i2c_master_transmit(dev, buf, len + 1, I2C_TIMEOUT_MS);
    metalio_i2c_unlock();
    return err;
}

// USB/软件复位不会给从机断电；半途卡住的从机会拉死 SDA，先手动打 9 个时钟。
// / USB/soft reset does not power-cycle slaves; a stuck slave holds SDA, so clock it free.
static void i2c_bus_recover(void) {
    gpio_hold_dis(METALIO_I2C_SDA);
    gpio_hold_dis(METALIO_I2C_SCL);
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << METALIO_I2C_SDA) | (1ULL << METALIO_I2C_SCL),
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    gpio_set_level(METALIO_I2C_SDA, 1);
    gpio_set_level(METALIO_I2C_SCL, 1);
    esp_rom_delay_us(20);
    for (int i = 0; i < 9 && gpio_get_level(METALIO_I2C_SDA) == 0; ++i) {
        gpio_set_level(METALIO_I2C_SCL, 0);
        esp_rom_delay_us(5);
        gpio_set_level(METALIO_I2C_SCL, 1);
        esp_rom_delay_us(5);
    }
    gpio_set_level(METALIO_I2C_SDA, 0);
    esp_rom_delay_us(5);
    gpio_set_level(METALIO_I2C_SCL, 1);
    esp_rom_delay_us(5);
    gpio_set_level(METALIO_I2C_SDA, 1);
    esp_rom_delay_us(5);
    if (!gpio_get_level(METALIO_I2C_SDA) || !gpio_get_level(METALIO_I2C_SCL)) {
        ESP_LOGE(TAG, "I2C recover failed SDA=%d SCL=%d", gpio_get_level(METALIO_I2C_SDA),
                 gpio_get_level(METALIO_I2C_SCL));
    }
}

static esp_err_t i2c_start(void) {
    if (s_bus != NULL) return ESP_OK;
    if (s_bus_lock == NULL) s_bus_lock = xSemaphoreCreateRecursiveMutex();
    if (s_bus_lock == NULL) return ESP_ERR_NO_MEM;
    i2c_bus_recover();
    i2c_master_bus_config_t cfg = {
        .i2c_port = METALIO_I2C_PORT,
        .sda_io_num = METALIO_I2C_SDA,
        .scl_io_num = METALIO_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t err = i2c_new_master_bus(&cfg, &s_bus);
    if (err != ESP_OK) return err;
    for (uint8_t addr = 0x08; addr < 0x78; ++addr) {
        if (i2c_master_probe(s_bus, addr, 20) == ESP_OK) ESP_LOGI(TAG, "I2C ACK 0x%02X", addr);
    }
    return ESP_OK;
}

/* ---- TCA9555 ---- */

static esp_err_t tca_write_port(int port, uint8_t value) {
    return metalio_i2c_write(s_tca, (uint8_t)(TCA_REG_OUT0 + port), &value, 1);
}

static esp_err_t tca_commit(void) {
    if (!s_ioe_ok) return ESP_ERR_INVALID_STATE;
    if (!metalio_i2c_lock(I2C_TIMEOUT_MS)) return ESP_ERR_TIMEOUT;
    esp_err_t err = tca_write_port(0, s_p0);
    if (err == ESP_OK) err = tca_write_port(1, s_p1);
    metalio_i2c_unlock();
    if (err != ESP_OK) ESP_LOGW(TAG, "TCA write: %s", esp_err_to_name(err));
    return err;
}

static esp_err_t tca_set(int io, bool high) {
    uint8_t* port = io < 8 ? &s_p0 : &s_p1;
    if (high) *port |= BIT_OF(io);
    else *port &= (uint8_t)~BIT_OF(io);
    return tca_write_port(io < 8 ? 0 : 1, *port);
}

esp_err_t metalio_ioe_read(uint8_t* port0, uint8_t* port1) {
    if (!s_ioe_ok) return ESP_ERR_INVALID_STATE;
    uint8_t in[2] = { 0xFF, 0xFF };
    esp_err_t err = metalio_i2c_read(s_tca, TCA_REG_IN0, in, 2);
    if (err == ESP_OK) {
        if (port0) *port0 = in[0];
        if (port1) *port1 = in[1];
    }
    return err;
}

int metalio_ioe_int_level(void) {
    return gpio_get_level(METALIO_IOE_INT_GPIO);
}

static bool tca_start(void) {
    if (metalio_i2c_add(METALIO_ADDR_TCA9555, I2C_SLOW_HZ, &s_tca) != ESP_OK) return false;
    uint8_t cfg[2] = { (uint8_t)~P0_OUTPUTS, (uint8_t)~P1_OUTPUTS };
    s_p0 = BIT_OF(METALIO_IO_CAM_SCR_EN);
    s_p1 = BIT_OF(METALIO_IO_TP_RST) | BIT_OF(METALIO_IO_BT_PA_PWR);
    uint8_t out[2] = { s_p0, s_p1 };
    // 先写输出锁存再改方向，避免切输出瞬间出现错误电平。/ Latch outputs before switching direction.
    if (metalio_i2c_write(s_tca, TCA_REG_OUT0, out, 2) != ESP_OK) return false;
    if (metalio_i2c_write(s_tca, TCA_REG_CFG0, cfg, 2) != ESP_OK) return false;
    uint8_t back[2] = { 0 };
    if (metalio_i2c_read(s_tca, TCA_REG_CFG0, back, 2) != ESP_OK || back[0] != cfg[0]
        || back[1] != cfg[1]) {
        ESP_LOGE(TAG, "TCA9555 config readback %02X%02X != %02X%02X", back[0], back[1], cfg[0], cfg[1]);
        return false;
    }
    uint8_t in[2];
    metalio_i2c_read(s_tca, TCA_REG_IN0, in, 2);  // 清上电残留 INT / clear stale INT
    ESP_LOGI(TAG, "TCA9555 ready IN=%02X%02X", in[0], in[1]);
    return true;
}

bool metalio_ioe_ok(void) {
    return s_ioe_ok;
}

esp_err_t metalio_touch_reset(void) {
    if (!s_ioe_ok) return ESP_ERR_INVALID_STATE;
    esp_err_t err = tca_set(METALIO_IO_TP_RST, false);
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(10));
    err = tca_set(METALIO_IO_TP_RST, true);
    // FT6336U 复位后约 300ms 才应答。/ FT6336U answers about 300 ms after reset.
    vTaskDelay(pdMS_TO_TICKS(300));
    return err;
}

void metalio_motor(bool on) {
    if (s_ioe_ok) tca_set(METALIO_IO_MOTOR, on);
}

void metalio_pa(bool on) {
    if (s_ioe_ok) tca_set(METALIO_IO_PA_EN, on);
}

void metalio_ana_sw(bool high) {
    static bool ready;
    if (!ready) {
        gpio_reset_pin(METALIO_ANA_SW_GPIO);
        gpio_set_direction(METALIO_ANA_SW_GPIO, GPIO_MODE_OUTPUT);
        ready = true;
    }
    gpio_set_level(METALIO_ANA_SW_GPIO, high ? 1 : 0);
}

esp_err_t metalio_power_off(void) {
    if (!s_ioe_ok) return ESP_ERR_INVALID_STATE;
    ESP_LOGI(TAG, "power-off pulse train x%d", PWR_PULSE_COUNT);
    for (int i = 0; i < PWR_PULSE_COUNT; ++i) {
        tca_set(METALIO_IO_PWR_PULSE, true);
        vTaskDelay(pdMS_TO_TICKS(PWR_PULSE_INTERVAL_MS));
        tca_set(METALIO_IO_PWR_PULSE, false);
        vTaskDelay(pdMS_TO_TICKS(PWR_PULSE_INTERVAL_MS));
    }
    // 仍在运行说明电源芯片没有响应（例如 USB 供电）。/ Still running: the power IC ignored it (e.g. USB power).
    return ESP_FAIL;
}

esp_err_t metalio_accel_int_open_drain(void) {
    i2c_master_dev_handle_t dev = NULL;
    esp_err_t err = metalio_i2c_add(METALIO_ADDR_SC7A20H, I2C_SLOW_HZ, &dev);
    if (err != ESP_OK) return err;
    // CTRL_REG6：INT_PP_OD=1 开漏，H_LACTIVE=1 低有效（厂商取值）。/ CTRL_REG6: open-drain, active-low (vendor value).
    uint8_t ctrl6 = 0x03;
    err = metalio_i2c_write(dev, 0x25, &ctrl6, 1);
    i2c_master_bus_rm_device(dev);
    return err;
}

/* ---- TPS65185 ---- */

static esp_err_t tps_write(uint8_t reg, uint8_t value) {
    return metalio_i2c_write(s_tps, reg, &value, 1);
}

static esp_err_t tps_read(uint8_t reg, uint8_t* value) {
    return metalio_i2c_read(s_tps, reg, value, 1);
}

bool metalio_tps_ok(void) {
    return s_tps_ok;
}

bool metalio_rails_on(void) {
    return s_rails_on;
}

static uint8_t pwr_good(void) {
    uint8_t p0 = 0;
    metalio_ioe_read(&p0, NULL);
    return p0 & BIT_OF(METALIO_IO_PWR_GOOD);
}

/* ---- epdiy board ---- */

static void ctrl_gpio_low(gpio_num_t pin) {
    gpio_reset_pin(pin);
    /* Keep the input buffer enabled on the two control outputs so the idle
       verification reads the pin level, not a disabled-input constant zero. */
    gpio_set_direction(pin, (pin==EPD_XOE || pin==EPD_MODE)?GPIO_MODE_INPUT_OUTPUT:GPIO_MODE_OUTPUT);
    gpio_set_level(pin, 0);
}

static void board_init(uint32_t epd_row_width) {
    (void)epd_row_width;
    // 深睡 gpio_hold 会跨复位保留；并行脚锁低会让 TPS 升压失败。
    // / Deep-sleep gpio_hold survives reset; latched-low bus pins break the TPS boost.
    static const gpio_num_t bus_pins[] = {
        EPD_D0, EPD_D1, EPD_D2, EPD_D3, EPD_D4, EPD_D5, EPD_D6, EPD_D7, EPD_XCL, EPD_XLE,
        EPD_XOE, EPD_XSTL, EPD_SPV, EPD_CKV, EPD_MODE, EPD_BORDER,
        METALIO_FL_COOL_GPIO, METALIO_FL_WARM_GPIO,
    };
    for (size_t i = 0; i < sizeof(bus_pins) / sizeof(bus_pins[0]); ++i) gpio_hold_dis(bus_pins[i]);

    ESP_ERROR_CHECK(i2c_start());
    s_ioe_ok = tca_start();
    if (!s_ioe_ok) ESP_LOGE(TAG, "TCA9555 not ready; panel power unavailable");
    if (metalio_i2c_add(METALIO_ADDR_TPS65185, I2C_SLOW_HZ, &s_tps) != ESP_OK) s_tps = NULL;
    metalio_ana_sw(true);

    gpio_config_t intr = {
        .pin_bit_mask = 1ULL << METALIO_IOE_INT_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&intr);

    ctrl_gpio_low(EPD_XOE);
    ctrl_gpio_low(EPD_MODE);
    ctrl_gpio_low(EPD_BORDER);

    const EpdDisplay_t* display = epd_get_display();
    metalio_epd_init_lcd(&s_lcd_bus, display->bus_width, display->width, display->height,
                         display->bus_speed);
    ESP_LOGI(TAG, "EPD bus ready %dx%d bus=%d", display->width, display->height, display->bus_width);
}

static void tps_rails_down(void) {
    s_p0 &= (uint8_t)~(BIT_OF(METALIO_IO_VCOM_CTRL) | BIT_OF(METALIO_IO_PWRUP));
    tca_commit();
    vTaskDelay(pdMS_TO_TICKS(1));
    s_p0 &= (uint8_t)~BIT_OF(METALIO_IO_WAKEUP);
    tca_commit();
}

#include "metalio_panel_lifecycle.inc"

// 上电：WAKEUP 进 STANDBY → 写上电时序 → PWRUP 上升沿 → 等 PWR_GOOD → 使能各轨与 VCOM 值 → 开 OE。
// 失败时拉低所有控制脚，不保持高压。
// / Power-up: WAKEUP to STANDBY, write the up-sequence, PWRUP rising edge, wait
// for PWR_GOOD, enable rails and the VCOM value, then OE. On failure every
// control pin goes low; high voltage is never held.
static void board_poweron(epd_ctrl_state_t* state) {
    if (s_rails_on) return;
    if (!s_ioe_ok || s_tps == NULL) {
        ESP_LOGE(TAG, "poweron skipped: TCA/TPS unavailable");
        return;
    }
    state->ep_output_enable = false;
    state->ep_mode = false;
    gpio_set_level(EPD_XOE, 0);
    gpio_set_level(EPD_MODE, 0);

    s_p0 |= BIT_OF(METALIO_IO_WAKEUP);
    s_p0 &= (uint8_t)~(BIT_OF(METALIO_IO_PWRUP) | BIT_OF(METALIO_IO_VCOM_CTRL));
    tca_commit();
    vTaskDelay(pdMS_TO_TICKS(50));

    uint8_t revid = 0;
    esp_err_t probe = ESP_FAIL;
    for (int i = 0; i < 3 && probe != ESP_OK; ++i) {
        probe = tps_read(TPS_REG_REVID, &revid);
        if (probe != ESP_OK) vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (probe == ESP_OK) {
        tps_write(TPS_REG_UPSEQ0, 0xE4);
        tps_write(TPS_REG_UPSEQ1, 0x55);
    }

    bool good = false;
    for (int retry = 0; retry < 3 && !good; ++retry) {
        if (retry > 0) {
            ESP_LOGW(TAG, "TPS startup retry %d/3", retry + 1);
            s_p0 &= (uint8_t)~BIT_OF(METALIO_IO_PWRUP);
            tca_commit();
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        s_p0 |= BIT_OF(METALIO_IO_PWRUP);
        tca_commit();
        for (int ms = 0; ms < 200; ++ms) {
            if (pwr_good()) {
                good = true;
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
    if (good && probe == ESP_OK) {
        good = tps_write(TPS_REG_ENABLE, 0x3F) == ESP_OK;
        // VCOM 以 10mV 为单位写入易失寄存器，不置 PROG 位，不改芯片内 EEPROM。
        // / VCOM goes to the volatile registers in 10 mV steps; PROG is never set, EEPROM untouched.
        unsigned code = METALIO_VCOM_MV / 10;
        if (good) good = tps_write(TPS_REG_VCOM2, (uint8_t)((code >> 8) & 1)) == ESP_OK;
        if (good) good = tps_write(TPS_REG_VCOM1, (uint8_t)(code & 0xFF)) == ESP_OK;
        uint8_t pg = 0;
        for (int tries = 0; good && tries < 200; ++tries) {
            if (tps_read(TPS_REG_PG, &pg) == ESP_OK && (pg & 0xFA) == 0xFA) break;
            if (tries == 199) good = false;
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
    if (!good) {
        uint8_t int1 = 0, int2 = 0;
        tps_read(TPS_REG_INT1, &int1);
        tps_read(TPS_REG_INT2, &int2);
        ESP_LOGE(TAG, "TPS power-up failed REVID=0x%02X INT1=0x%02X INT2=0x%02X", revid, int1, int2);
        tps_rails_down();
        s_tps_ok = false;
        return;
    }
    /* Keep VCOM stable through all phases, like the Metalio reference driver.
       Raising it on every ep_mode edge adds repeated bias transients. */
    if (panel_bias_ready(state) != ESP_OK) {
        tps_rails_down();s_tps_ok=false;s_rails_on=false;return;
    }
    s_tps_ok = true;
    s_rails_on = true;
    ESP_LOGI(TAG, "TPS65185 rails up REVID=0x%02X VCOM=%dmV", revid, METALIO_VCOM_MV);
}

static void board_deinit(void) {
    if (s_rails_on) board_poweroff(epd_ctrl_state());
    epd_lcd_deinit();
}

static void board_measure_vcom(epd_ctrl_state_t* state) {
    board_poweron(state);
}

// 厂商固件同样固定 25°C；波形表只有 0–50°C 一档。/ The vendor firmware also uses 25 °C; the waveform has one 0–50 °C range.
static float board_temperature(void) {
    return 25.0f;
}

// 不接受运行时改 VCOM。/ Runtime VCOM changes are refused.
static void board_set_vcom(int value) {
    if (value != METALIO_VCOM_MV) ESP_LOGW(TAG, "ignored VCOM request %d mV", value);
}

const EpdBoardDefinition epd_board_metalio = {
    .init = board_init,
    .deinit = board_deinit,
    .set_ctrl = board_set_ctrl,
    .poweron = board_poweron,
    .poweroff = board_poweroff,
    .measure_vcom = board_measure_vcom,
    .get_temperature = board_temperature,
    .set_vcom = board_set_vcom,
    .gpio_set_direction = NULL,
    .gpio_read = NULL,
    .gpio_write = NULL,
};
