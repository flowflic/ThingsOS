/* SPDX-License-Identifier: Apache-2.0
 * Independently implemented IDF driver. Pin map, BH1098G AT mode and I2S slot
 * format identified from CloudZao/Metalio-E-INK4-Plus (MIT), config.h and
 * metalio_audio.cc / bt_audio_codec.cc. Reference sources remain read-only.
 * PA/TCA is deliberately NOT touched here: the application serializes PA
 * changes with display power transitions using its existing I/O mutex. */
#include "metalio_bsp.h"
#include "driver/uart.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdatomic.h>
static bool uart_ready;
static atomic_bool mode_ready,wireless;
/* Diagnostic: the driver reports a TX queue overflow when every DMA buffer was
   sent before the application refilled one, i.e. the speaker played a cleared
   (silent) buffer. Counted in the ISR; the audio service samples deltas. */
static atomic_uint tx_underruns;
#include "audio_dma.inc"
static bool IRAM_ATTR tx_overflow(i2s_chan_handle_t handle,i2s_event_data_t* event,void* context){
    (void)handle;(void)event;(void)context;atomic_fetch_add(&tx_underruns,1);return false;
}
unsigned metalio_audio_tx_underruns(void){return atomic_load(&tx_underruns);}
esp_err_t metalio_bh_start(void){
    if(!uart_ready){
        uart_config_t config={.baud_rate=115200,.data_bits=UART_DATA_8_BITS,.parity=UART_PARITY_DISABLE,
          .stop_bits=UART_STOP_BITS_1,.flow_ctrl=UART_HW_FLOWCTRL_DISABLE,.source_clk=UART_SCLK_DEFAULT};
        esp_err_t e=uart_driver_install(UART_NUM_1,4096,0,0,NULL,0);if(e!=ESP_OK)return e;
        e=uart_param_config(UART_NUM_1,&config);if(e!=ESP_OK){uart_driver_delete(UART_NUM_1);return e;}
        e=uart_set_pin(UART_NUM_1,GPIO_NUM_39,GPIO_NUM_38,UART_PIN_NO_CHANGE,UART_PIN_NO_CHANGE);if(e!=ESP_OK){uart_driver_delete(UART_NUM_1);return e;}
        uart_ready=true;
    }
    return ESP_OK;
}
esp_err_t metalio_bh_send(const char* command){
    esp_err_t e=metalio_bh_start();if(e!=ESP_OK)return e;
    size_t n=strlen(command);return uart_write_bytes(UART_NUM_1,command,n)==(int)n?ESP_OK:ESP_FAIL;
}
int metalio_bh_read(void* data,size_t bytes,unsigned timeout_ms){
    if(!uart_ready)return -1;
    return uart_read_bytes(UART_NUM_1,data,bytes,pdMS_TO_TICKS(timeout_ms));
}
void metalio_audio_wireless(bool connected){atomic_store(&wireless,connected);atomic_store(&mode_ready,false);}
static esp_err_t bh_mode(void){
    if(atomic_load(&wireless)||atomic_load(&mode_ready))return ESP_OK;
    esp_err_t e=metalio_bh_start();if(e!=ESP_OK)return e;
    const char* command[]={"AT+RX=2\r\n","AT+MODE=1\r\n"};
    uint8_t response[160];unsigned seen=0;
    uart_flush_input(UART_NUM_1);
    for(unsigned i=0;i<2;++i){
        int n=(int)strlen(command[i]);if(uart_write_bytes(UART_NUM_1,command[i],n)!=n)return ESP_FAIL;
        vTaskDelay(pdMS_TO_TICKS(i?500:700));
        int got=uart_read_bytes(UART_NUM_1,response,sizeof(response),pdMS_TO_TICKS(30));if(got>0)seen+=(unsigned)got;
    }
    mode_ready=true;
    ESP_LOGI("ThingsOS_audio","BH mode1 sent UART TX=39 RX=38 response_bytes=%u; I2S clock still requires real data validation",seen);
    return ESP_OK;
}
esp_err_t metalio_audio_open(bool microphone){
    if(microphone&&atomic_load(&wireless))return ESP_ERR_INVALID_STATE;
    metalio_audio_close();esp_err_t e=bh_mode();if(e!=ESP_OK)return e;
    e=audio_i2s_open(microphone);
    if(e==ESP_OK&&!microphone){
        /* DMA callbacks may only change while the channel is stopped. A failed
           registration keeps playback; only the diagnostic is lost. */
        const i2s_event_callbacks_t callbacks={.on_send_q_ovf=tx_overflow};
        if(i2s_channel_disable(audio_channel)==ESP_OK){
            audio_enabled=false;
            esp_err_t r=i2s_channel_register_event_callback(audio_channel,&callbacks,NULL);
            if(r!=ESP_OK)ESP_LOGW("ThingsOS_audio","TX underrun callback unavailable: %s",esp_err_to_name(r));
            e=i2s_channel_enable(audio_channel);
            if(e==ESP_OK)audio_enabled=true;else metalio_audio_close();
        }
    }
    return e;
}
esp_err_t metalio_audio_read(int32_t* data,size_t bytes,size_t* count){
    if(!audio_channel||!rx)return ESP_ERR_INVALID_STATE;
    esp_err_t error=i2s_channel_read(audio_channel,data,bytes,count,120);
    if(error!=ESP_OK)mode_ready=false;
    return error;
}
esp_err_t metalio_audio_write(const int32_t* data,size_t bytes,size_t* count){
    if(!audio_channel||rx)return ESP_ERR_INVALID_STATE;
    esp_err_t error=i2s_channel_write(audio_channel,data,bytes,count,120);
    if(error!=ESP_OK)mode_ready=false;
    return error;
}
