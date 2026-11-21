/* SPDX-License-Identifier: Apache-2.0 */
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"audio DMA:%d: %s\n",__LINE__,#x);exit(1);}}while(0)
typedef int esp_err_t;
enum {ESP_OK,ESP_ERR_NO_MEM=257,ESP_ERR_INVALID_STATE=259};
enum {I2S_NUM_0,I2S_ROLE_SLAVE,I2S_DATA_BIT_WIDTH_32BIT=32,I2S_SLOT_MODE_STEREO=2,
      I2S_GPIO_UNUSED=-1,GPIO_NUM_42=42,GPIO_NUM_43=43,GPIO_NUM_44=44,GPIO_NUM_45=45};
enum {MALLOC_CAP_INTERNAL=1,MALLOC_CAP_DMA=2,MALLOC_CAP_8BIT=4};
typedef struct {unsigned dma_desc_num,dma_frame_num;bool auto_clear_after_cb;} i2s_chan_config_t;
typedef struct {int clk_cfg,slot_cfg;struct {int mclk,bclk,ws,dout,din;} gpio_cfg;} i2s_std_config_t;
#define I2S_CHANNEL_DEFAULT_CONFIG(a,b) (i2s_chan_config_t){0}
#define I2S_STD_CLK_DEFAULT_CONFIG(hz) (hz)
#define I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(bits,slots) ((bits)*(slots))
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
typedef struct {bool used,enabled;size_t allocated;i2s_chan_config_t config;} fake_channel;
typedef fake_channel* i2s_chan_handle_t;
static fake_channel channel;
static size_t capacity,largest;
static unsigned creates,initializes,enables,deletes,disables,fail_create;
static int init_error,enable_error;
static bool microphone_expected;
static unsigned attempted_frames[3],attempted_desc[3];
static size_t heap_caps_get_free_size(unsigned caps){CHECK(caps==7);return capacity-channel.allocated;}
static size_t heap_caps_get_largest_free_block(unsigned caps){CHECK(caps==7);return largest;}
static esp_err_t i2s_new_channel(const i2s_chan_config_t* c,i2s_chan_handle_t* tx,i2s_chan_handle_t* rx){
    CHECK(!channel.used&&creates<3);attempted_frames[creates]=c->dma_frame_num;attempted_desc[creates++]=c->dma_desc_num;
    CHECK(c->auto_clear_after_cb&&c->dma_desc_num>=2);
    CHECK(microphone_expected?(!tx&&rx):(tx&&!rx));
    if(fail_create){--fail_create;return ESP_ERR_NO_MEM;}
    channel=(fake_channel){.used=true,.allocated=256,.config=*c};*(tx?tx:rx)=&channel;return ESP_OK;
}
static esp_err_t i2s_channel_init_std_mode(i2s_chan_handle_t c,const i2s_std_config_t* s){
    CHECK(c==&channel&&c->used);++initializes;
    CHECK(s->clk_cfg==16000&&s->slot_cfg==64&&s->gpio_cfg.mclk==-1&&s->gpio_cfg.bclk==42&&s->gpio_cfg.ws==43);
    CHECK(s->gpio_cfg.din==(microphone_expected?45:-1)&&s->gpio_cfg.dout==(microphone_expected?-1:44));
    if(init_error)return init_error;
    size_t block=c->config.dma_frame_num*8,required=256+c->config.dma_desc_num*(block+32);
    c->allocated=capacity/2; /* Partial SDK allocation must be released on failure. */
    if(block>largest||required>capacity)return ESP_ERR_NO_MEM;
    c->allocated=required;return ESP_OK;
}
static esp_err_t i2s_channel_enable(i2s_chan_handle_t c){
    CHECK(c->used&&!c->enabled);++enables;if(enable_error)return enable_error;c->enabled=true;return ESP_OK;
}
static esp_err_t i2s_channel_disable(i2s_chan_handle_t c){CHECK(c->enabled);c->enabled=false;++disables;return ESP_OK;}
static esp_err_t i2s_del_channel(i2s_chan_handle_t c){CHECK(c->used&&!c->enabled);*c=(fake_channel){0};++deletes;return ESP_OK;}
#include "../metalio/components/metalio_bsp/audio_dma.inc"
static void reset(size_t total,size_t block,bool mic){
    metalio_audio_close();CHECK(!channel.used&&!audio_channel&&!audio_enabled);
    capacity=total;largest=block;creates=initializes=enables=deletes=disables=fail_create=0;
    init_error=enable_error=0;microphone_expected=mic;
}
int main(void){
    reset(20000,4096,false);CHECK(audio_i2s_open(false)==ESP_OK&&creates==1&&!rx);
    /* Speaker: 64 ms ring first (8 KiB instead of 12 KiB). */
    CHECK(channel.config.dma_desc_num==8&&channel.config.dma_frame_num==128);
    metalio_audio_close();CHECK(disables==1&&deletes==1&&!channel.allocated);
    reset(20000,1024,true);CHECK(audio_i2s_open(true)==ESP_OK&&creates==2&&rx);
    CHECK(channel.config.dma_desc_num==12&&channel.config.dma_frame_num==128&&deletes==1&&!disables);
    /* Speaker DMA pressure falls back from 64 ms to 48 ms. */
    reset(8000,4096,false);CHECK(audio_i2s_open(false)==ESP_OK&&creates==2);
    CHECK(channel.config.dma_desc_num==6&&channel.config.dma_frame_num==128&&deletes==1);
    CHECK(attempted_desc[0]==8&&attempted_frames[0]==128&&attempted_desc[1]==6&&attempted_frames[1]==128);
    reset(2000,1024,false);CHECK(audio_i2s_open(false)==ESP_ERR_NO_MEM&&creates==2&&deletes==2&&!disables);
    CHECK(!channel.allocated&&!audio_channel);
    reset(20000,4096,false);init_error=ESP_ERR_INVALID_STATE;
    CHECK(audio_i2s_open(false)==ESP_ERR_INVALID_STATE&&creates==1&&deletes==1&&!enables&&!disables);
    reset(20000,4096,true);enable_error=ESP_ERR_INVALID_STATE;
    CHECK(audio_i2s_open(true)==ESP_ERR_INVALID_STATE&&creates==1&&deletes==1&&!disables);
    reset(20000,4096,false);fail_create=1;
    CHECK(audio_i2s_open(false)==ESP_OK&&creates==2&&deletes==0);
    metalio_audio_close();metalio_audio_close();CHECK(deletes==1&&disables==1&&!audio_channel);
    puts("PASS: real I2S allocation lifecycle; full ring, fragmentation, DMA pressure, partial cleanup, fatal errors and RX/TX pins");
    return 0;
}
