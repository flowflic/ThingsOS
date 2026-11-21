/* SPDX-License-Identifier: Apache-2.0
 * Metalio update lifetime. VCOM_CTRL is an enable only, never a voltage or
 * EEPROM write. Match the reference board's stable bias during an update.
 */
static void board_set_ctrl(epd_ctrl_state_t* state,const epd_ctrl_state_t* const mask) {
    if(mask->ep_output_enable)gpio_set_level(EPD_XOE,state->ep_output_enable?1:0);
    if(mask->ep_mode)gpio_set_level(EPD_MODE,state->ep_mode?1:0);
}
static esp_err_t panel_bias_ready(epd_ctrl_state_t* state) {
    state->ep_output_enable=false;state->ep_mode=false;
    gpio_set_level(EPD_XOE,0);gpio_set_level(EPD_MODE,0);
    s_p0|=BIT_OF(METALIO_IO_VCOM_CTRL);
    esp_err_t error=tca_commit();
    if(error!=ESP_OK)return error;
    esp_rom_delay_us(VCOM_SETTLE_US);
    state->ep_output_enable=true;
    gpio_set_level(EPD_XOE,1);
    return ESP_OK;
}
static void board_poweroff(epd_ctrl_state_t* state) {
    state->ep_output_enable=false;state->ep_mode=false;
    gpio_set_level(EPD_XOE,0);gpio_set_level(EPD_MODE,0);
    /* Preserve the E0470A14-AF XOE-low-before-rails-off safety interval. */
    esp_rom_delay_us(12);
    if(s_ioe_ok)tps_rails_down();
    s_rails_on=false;
}
esp_err_t metalio_panel_idle_verify(void) {
    uint8_t output=0xff;
    esp_err_t error=metalio_i2c_read(s_tca,TCA_REG_OUT0,&output,1);
    int xoe=gpio_get_level(EPD_XOE),mode=gpio_get_level(EPD_MODE);
    unsigned bias=BIT_OF(METALIO_IO_VCOM_CTRL)|BIT_OF(METALIO_IO_PWRUP)|BIT_OF(METALIO_IO_WAKEUP);
    bool idle=error==ESP_OK && !(output&bias) && !xoe && !mode;
    ESP_LOGI(TAG,"Panel idle verified=%d read=%d bias_ctrl=0x%02x XOE=%d MODE=%d",idle,error,output&bias,xoe,mode);
    return error!=ESP_OK?error:idle?ESP_OK:ESP_ERR_INVALID_STATE;
}
