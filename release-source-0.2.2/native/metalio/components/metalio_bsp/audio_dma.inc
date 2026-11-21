/* SPDX-License-Identifier: Apache-2.0 */
static i2s_chan_handle_t audio_channel;
static bool audio_enabled,rx;
void metalio_audio_close(void){
    if(!audio_channel)return;
    if(audio_enabled)i2s_channel_disable(audio_channel);
    i2s_del_channel(audio_channel);audio_channel=NULL;audio_enabled=false;
}
static esp_err_t audio_i2s_open(bool microphone){
    /* Microphone: 96 ms ring, split for fragmentation, then 48 ms. Speaker:
       64 ms then 48 ms. With the decoder stack in main SRAM an MP3 frame decodes
       in ~2 ms and write gaps stay below ~35 ms, while every DMA byte saved stays
       available to the panel line buffers and SD transfers (12 KiB -> 8 KiB).
       Keep the channel count and PCM format unchanged; only allocation failures
       may try another profile. */
    static const struct {unsigned descriptors,frames;} profiles[]={{6,256},{12,128},{8,128},{6,128}};
    esp_err_t e=ESP_ERR_NO_MEM;
    for(unsigned attempt=microphone?0:2;attempt<sizeof(profiles)/sizeof(profiles[0]);++attempt){
        if(microphone&&attempt==2)continue;
        i2s_chan_config_t channel=I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0,I2S_ROLE_SLAVE);
        channel.dma_desc_num=profiles[attempt].descriptors;
        channel.dma_frame_num=profiles[attempt].frames;channel.auto_clear_after_cb=true;
        ESP_LOGI("ThingsOS_audio","I2S allocation attempt=%u DMA=%ux%u free=%u largest=%u",
            attempt+1,channel.dma_desc_num,channel.dma_frame_num,
            (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA|MALLOC_CAP_8BIT),
            (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA|MALLOC_CAP_8BIT));
        const char* stage="channel";
        e=i2s_new_channel(&channel,microphone?NULL:&audio_channel,microphone?&audio_channel:NULL);
        if(e==ESP_OK){
            i2s_std_config_t standard={.clk_cfg=I2S_STD_CLK_DEFAULT_CONFIG(16000),
              .slot_cfg=I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT,I2S_SLOT_MODE_STEREO),
              .gpio_cfg={.mclk=I2S_GPIO_UNUSED,.bclk=GPIO_NUM_42,.ws=GPIO_NUM_43,
                         .dout=microphone?I2S_GPIO_UNUSED:GPIO_NUM_44,
                         .din=microphone?GPIO_NUM_45:I2S_GPIO_UNUSED}};
            stage="standard";e=i2s_channel_init_std_mode(audio_channel,&standard);
            if(e==ESP_OK){stage="enable";e=i2s_channel_enable(audio_channel);}
        }
        if(e==ESP_OK){
            audio_enabled=true;rx=microphone;
            ESP_LOGI("ThingsOS_audio","I2S slave %s 16000Hz stereo32 DMA=%ux%u ring_ms=%u",
                rx?"microphone":"speaker",channel.dma_desc_num,channel.dma_frame_num,
                channel.dma_desc_num*channel.dma_frame_num/16);
            return ESP_OK;
        }
        ESP_LOGW("ThingsOS_audio","I2S %s failed: %s (0x%x) DMA=%ux%u free=%u largest=%u",
            stage,esp_err_to_name(e),(unsigned)e,channel.dma_desc_num,channel.dma_frame_num,
            (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA|MALLOC_CAP_8BIT),
            (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA|MALLOC_CAP_8BIT));
        metalio_audio_close();
        if(e!=ESP_ERR_NO_MEM)break;
    }
    return e;
}
