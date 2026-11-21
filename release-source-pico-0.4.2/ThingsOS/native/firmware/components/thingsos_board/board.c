// SPDX-License-Identifier: Apache-2.0
#include "board.h"
#include "thingsos/diagnostics.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_sleep.h"
#include "driver/usb_serial_jtag.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/soc.h"
#include <string.h>
#include <math.h>
#include <stdatomic.h>
#include "bootloader_common.h"
#include "e0470_epaper_waveform.h"
#include "epd_highlevel.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "psa/crypto.h"
#include "read_pico_board.h"
#include "sc7a20h.h"
#include "read_pico_epd_timing.h"
#include "read_pico_pmu.h"
#include "read_pico_sd.h"
#include "rescue_ascii.h"
#include "thingsos/guard.h"
#include "thingsos/touch.h"
#include "thingsos/tilt.h"
#include "thingsos/touch_queue.h"
#include "thingsos/display_buffer.h"
#include "thingsos/pclk_policy.h"
#include "thingsos/refresh_mask.h"
#include "thingsos/refresh_policy.h"
#define THINGSOS_PANEL_PCLK_MHZ 18
static thingsos_pclk_policy_t pclk_policy;
static bool pclk_recovery_allowed=true;
/* Application hold mask: official non-equal actions, phases and timing remain
   unchanged. Periodic/explicit full cleaning always uses the source waveform. */
static uint8_t changed_lut[E0470_GC16_FRAMES*64];
static EpdWaveformPhases changed_phases;
static const EpdWaveformPhases* changed_ranges[]={&changed_phases};
static const EpdWaveformMode changed_mode={.type=MODE_GC16,.temp_ranges=1,.range_data=changed_ranges};
static const EpdWaveformMode* changed_modes[]={&changed_mode};
static EpdWaveform changed_waveform={.num_modes=1,.num_temp_ranges=1,.mode_data=changed_modes};
static bool mask_ready;
static uint8_t text_lut[E0470_GC16_FRAMES*64];
static EpdWaveformPhases text_phases;
static const EpdWaveformPhases* text_ranges[]={&text_phases};
static const EpdWaveformMode text_mode={.type=MODE_GC16,.temp_ranges=1,.range_data=text_ranges};
static const EpdWaveformMode* text_modes[]={&text_mode};
static EpdWaveform text_waveform={.num_modes=1,.num_temp_ranges=1,.mode_data=text_modes};
static bool text_ready;
static EpdiyHighlevelState hl;
static cst836u_handle_t touch;
static SemaphoreHandle_t touch_access;
static TaskHandle_t touch_task;
static bool touch_buffered;
static thingsos_touch_queue_t touch_samples;
static atomic_bool touch_is_down;
static atomic_bool private_input;
void thingsos_board_private_input(bool enabled) {
    atomic_store_explicit(&private_input,enabled,memory_order_release);
}
static bool ready;
static unsigned presentations;
static bool full_refresh, transition_refresh, policy_refresh;
static bool unlock_refresh;
static bool reader_ui, reader_ui_previous;
static EpdRect cleanup_area;
static unsigned refresh_mode, refresh_every=15, soft_frames;
static int64_t rails_deadline;
static bool paused_wifi, power_key_armed;
static esp_partition_mmap_handle_t font_map;
static const uint8_t* font_data;
static uint32_t font_bytes;
static sc7a20h_handle_t tilt_sensor;
static bool tilt_powered;
static int64_t tilt_retry_at,tilt_capture_at;
static thingsos_tilt_queue_t tilt_samples;
static thingsos_tilt_stream_t tilt_stream;
static bool tilt_landscape;
static unsigned tilt_level=1;
static bool tilt_active_locked(bool active) {
    if(!active) {
        if(tilt_sensor && tilt_powered && sc7a20h_power_down(tilt_sensor)==ESP_OK)tilt_powered=false;
        return !tilt_powered;
    }
    if(tilt_powered)return true;
    if(esp_timer_get_time()<tilt_retry_at)return false;
    tilt_retry_at=esp_timer_get_time()+5000000;
    if(!tilt_sensor) {
        sc7a20h_config_t config=SC7A20H_DEVICE_CONFIG_DEFAULT();
        config.sensor.odr=SC7A20H_ODR_25;
        esp_err_t err=sc7a20h_init(read_pico_i2c_bus(),&config,&tilt_sensor);
        if(err!=ESP_OK){ESP_LOGW("ThingsOS_tilt","Sensor init failed: %s",esp_err_to_name(err));return false;}
        ESP_LOGI("ThingsOS_tilt","SC7A20H ready: 25 Hz, 2g, low power");
        tilt_powered=true;
    } else tilt_powered=sc7a20h_power_up(tilt_sensor)==ESP_OK;
    if(tilt_powered)tilt_retry_at=0;
    return tilt_powered;
}
bool thingsos_board_tilt_active(bool active) {
    if(!touch_access)return !active;
    if(xSemaphoreTake(touch_access,pdMS_TO_TICKS(100))!=pdTRUE) {
        ESP_LOGW("ThingsOS_touch","Sensor state change deferred: input mutex busy");return false;
    }
    bool was_on=tilt_powered;
    bool ok=tilt_active_locked(active);
    if(!active || (!was_on && tilt_powered)){memset(&tilt_samples,0,sizeof(tilt_samples));memset(&tilt_stream,0,sizeof(tilt_stream));tilt_capture_at=0;}
    if(active && touch_task)xTaskNotifyGive(touch_task);
    xSemaphoreGive(touch_access);return ok;
}
void thingsos_board_tilt_configure(bool landscape,unsigned level) {
    if(!touch_access)return;
    xSemaphoreTake(touch_access,portMAX_DELAY);
    if(tilt_landscape!=landscape || tilt_level!=level)memset(&tilt_stream,0,sizeof(tilt_stream));
    tilt_landscape=landscape;tilt_level=level;
    xSemaphoreGive(touch_access);
}
bool thingsos_board_tilt_event(int* direction,uint64_t* at,thingsos_tilt_t* state) {
    if(!touch_access || xSemaphoreTake(touch_access,0)!=pdTRUE)return false;
    *direction=thingsos_tilt_stream_take(&tilt_stream,(uint64_t)esp_timer_get_time()/1000,at);
    *state=tilt_stream.state;
    xSemaphoreGive(touch_access);return true;
}
/* Runs in the input sampler, independent of UI pagination and display scans. */
static void tilt_capture(void) {
    sc7a20h_sample_t sample;
    if(!tilt_powered || esp_timer_get_time()<tilt_capture_at)return;
    tilt_capture_at=esp_timer_get_time()+40000;
    esp_err_t err=sc7a20h_read(tilt_sensor,&sample);
    static int64_t log_at;static unsigned diagnostics;
    if(esp_timer_get_time()>=log_at && diagnostics<30) {
        log_at=esp_timer_get_time()+2000000;++diagnostics;
        if(err==ESP_OK)ESP_LOGI("ThingsOS_tilt","Raw mg=%d,%d,%d status=0x%02x",sample.x_mg,sample.y_mg,sample.z_mg,sample.status);
        else ESP_LOGW("ThingsOS_tilt","Read failed: %s",esp_err_to_name(err));
    }
    if(err!=ESP_OK || !(sample.status&8))return;
    int before_motion=tilt_stream.state.motion_candidate,before_pending=tilt_stream.pending;
    thingsos_tilt_stream_sample(&tilt_stream,sample.x_mg,sample.y_mg,sample.z_mg,
        (uint64_t)esp_timer_get_time()/1000,tilt_landscape,tilt_level,
        atomic_load_explicit(&touch_is_down,memory_order_acquire));
    if(before_motion!=tilt_stream.state.motion_candidate || before_pending!=tilt_stream.pending)
        ESP_LOGI("ThingsOS_tilt","Wrist phase candidate=%d pending=%d roll=%d raw_mg=%d,%d,%d armed=%d",
            tilt_stream.state.motion_candidate,tilt_stream.pending,tilt_stream.state.angle-tilt_stream.state.base_angle,
            sample.x_mg,sample.y_mg,sample.z_mg,tilt_stream.state.armed);
    thingsos_tilt_queue_push(&tilt_samples,(thingsos_tilt_input_t){sample.x_mg,sample.y_mg,sample.z_mg,(uint64_t)esp_timer_get_time()/1000});
}
bool thingsos_board_tilt_read_raw(int* x,int* y,int* z,uint64_t* ms) {
    if(!touch_access || xSemaphoreTake(touch_access,0)!=pdTRUE)return false;
    thingsos_tilt_input_t sample;bool ok=thingsos_tilt_queue_pop(&tilt_samples,&sample);
    if(ok){*x=sample.x;*y=sample.y;*z=sample.z;*ms=sample.ms;}
    xSemaphoreGive(touch_access);return ok;
}
esp_err_t thingsos_board_start(void) {
    /* Official electrical and timing driver; do not call its fixed-VCOM app initializer. */
    epd_init(&epd_board_read_pico, &E0470_DISPLAY, EPD_LUT_1K);
    read_pico_pmu_config_t pmu_config = READ_PICO_PMU_CONFIG_DEFAULT();
    esp_err_t err = read_pico_pmu_init(read_pico_i2c_bus(), &pmu_config);
    if (err != ESP_OK) return err;
    int factory_mv = 0;
    err = read_pico_pmu_vcom_get(&factory_mv);
    if (err != ESP_OK || factory_mv < 500 || factory_mv > 2500) return ESP_ERR_INVALID_STATE;
    ESP_LOGI("ThingsOS_board", "Factory panel calibration read; initializing display and touch");
    /* Apply the factory value to the volatile rail configuration, never change PMU calibration. */
    epd_set_vcom(factory_mv);
    epd_set_rotation(EPD_ROT_INVERTED_PORTRAIT);
    /* The BSP recalculates blanking and CKV for 18 MHz. Queue underruns
       fall back to 12 MHz and may recover after a bounded cooldown unless latched. */
    thingsos_pclk_init(&pclk_policy,THINGSOS_PANEL_PCLK_MHZ,READ_PICO_EPD_PCLK_MIN_MHZ);
    read_pico_epd_set_pclk(THINGSOS_PANEL_PCLK_MHZ);
    ESP_LOGI("ThingsOS_board","Panel PCLK=%d MHz, underrun fallback=%d MHz (max recoveries=%u)",
        read_pico_epd_pclk_mhz(),READ_PICO_EPD_PCLK_MIN_MHZ,THINGSOS_PCLK_MAX_RECOVERIES);
    e0470_waveform_init();
    const EpdWaveformPhases* gc=e0470_waveform_phases(&E0470_WAVEFORM,MODE_GC16);
    if(gc && gc->phases==E0470_GC16_FRAMES && E0470_WAVEFORM.num_temp_ranges==1) {
        mask_ready=thingsos_refresh_mask_equal(gc->luts,gc->phases,changed_lut,sizeof(changed_lut));
        changed_phases=*gc;changed_phases.luts=changed_lut;
        changed_waveform.temp_intervals=E0470_WAVEFORM.temp_intervals;
        text_ready=thingsos_refresh_text_wave(gc->luts,gc->phases,text_lut,sizeof(text_lut));
        text_phases=*gc;text_phases.luts=text_lut;
        text_waveform.temp_intervals=E0470_WAVEFORM.temp_intervals;
    }
    ESP_LOGI("ThingsOS_board","GC16 equal-pixel hold mask ready=%d phases=%d",mask_ready,gc?gc->phases:0);
    epd_set_leading_skip(false);
    hl = epd_hl_init(&E0470_WAVEFORM);
    if (!epd_hl_get_framebuffer(&hl)) return ESP_ERR_NO_MEM;
    cst836u_config_t config = CST836U_CONFIG_DEFAULT();
    config.int_gpio = READ_PICO_TP_INT_GPIO;
    config.reset_fn = read_pico_touch_reset;
    err = cst836u_init(read_pico_i2c_bus(), &config, &touch);
    if (err != ESP_OK) return err;
    err = cst836u_wake(touch);
    if (err != ESP_OK) return err;
    read_pico_sd_start_probe();
    /* HOST_READY carries one reason byte. The official helper also waits for RUNNING. */
    err = read_pico_pmu_report_ready();
    if (err != ESP_OK) {
        ESP_LOGE("ThingsOS_board", "PMU ready handshake failed: %s", esp_err_to_name(err));
        return err;
    }
    read_pico_pmu_drain_events();
    uint8_t raw_keys=1;
    const pmu_snapshot_t* pmu=read_pico_pmu_get();
    err=pmu && pmu->config_ok && pmu->config.key_raw_events ? ESP_OK : read_pico_pmu_cmd(PMU_CMD_CONFIG_SET_KEY_EVENTS,&raw_keys,1);
    if(err!=ESP_OK) ESP_LOGW("ThingsOS_board","PMU raw key events unavailable: %s",esp_err_to_name(err));
    ready = true;
    ESP_LOGI("ThingsOS_board", "Display/touch initialized; PMU RUNNING");
    return ESP_OK;
}
#include "stagger.inc"
static bool stagger_requested,stagger_forward,reader_background;
static void (*reader_scan_barrier)(void);
void thingsos_board_scan_barrier(void (*barrier)(void)) {reader_scan_barrier=barrier;}
static void scan_prepare(void) {
    void (*barrier)(void)=reader_scan_barrier;reader_scan_barrier=NULL;
    if(barrier)barrier();
}
static enum EpdDrawError ui_content_update(int temperature) {
    EpdRect full=epd_full_screen();
    epd_difference_image_cropped(hl.front_fb,hl.back_fb,full,
        hl.difference_fb,hl.dirty_lines,hl.dirty_columns);
    /* Logical portrait x/y map to physical y/x. Keep changed header/nav masks
       and add the entire content region, including software-white old ink. */
    if(!thingsos_refresh_include_rect(hl.dirty_lines,epd_height(),hl.dirty_columns,epd_width(),
        cleanup_area.y,684-cleanup_area.x-cleanup_area.width,cleanup_area.height,cleanup_area.width))
        return EPD_DRAW_INVALID_CROP;
    enum EpdDrawError result=epd_draw_base(full,hl.difference_fb,full,
        MODE_PACKING_1PPB_DIFFERENCE|MODE_GC16,temperature,hl.dirty_lines,hl.dirty_columns,&text_waveform);
    epd_leading_skip_discard();
    if(result==EPD_DRAW_SUCCESS)memcpy(hl.back_fb,hl.front_fb,THINGSOS_PANEL_BYTES);
    ESP_LOGI("ThingsOS_board","Home content regional GC16: area=%d,%d %dx%d scans=1 white_tick=1 result=0x%x",
        cleanup_area.x,cleanup_area.y,cleanup_area.width,cleanup_area.height,(unsigned)result);
    return result;
}
static bool loading_frame;
static bool metrics_frame;
static uint32_t last_refresh_ms;
uint32_t thingsos_board_last_refresh_ms(void) { return last_refresh_ms; }
unsigned thingsos_board_panel_clock(void) {
    return (unsigned)read_pico_epd_pclk_mhz();
}
bool thingsos_board_panel_clock_latched(void) {
    return pclk_policy.latched;
}
unsigned thingsos_board_panel_clock_recoveries(void) {
    return pclk_policy.recovery_attempts;
}
uint32_t thingsos_board_panel_clock_cooldown_s(void) {
    uint64_t rem=thingsos_pclk_cooldown_remaining_ms(&pclk_policy,(uint64_t)(esp_timer_get_time()/1000));
    return (uint32_t)((rem+999)/1000);
}
void thingsos_board_pclk_allow_recovery(bool allowed) {
    pclk_recovery_allowed=allowed;
}
static esp_err_t present(const thingsos_canvas_t* c, bool reader, unsigned keyboard, bool status, bool sleep_transition) {
    if (!ready || !c || !c->pixels || !((c->width==684 && c->height==1216)||(c->width==1216 && c->height==684))) return ESP_ERR_INVALID_STATE;
    bool landscape=c->width==1216;
    bool lock_wave=sleep_transition || unlock_refresh;
    /* The first body frame after dismissing a control surface is also a UI
       transition. Ordinary page turns resume cleanup accounting after it. */
    bool ui_frame=loading_frame || reader_ui || (reader && reader_ui_previous);
    if(landscape)status=false;
    int64_t total_started = esp_timer_get_time();
    uint8_t* fb = epd_hl_get_framebuffer(&hl);
    /* Background work must not drive the whole white page or spend a page-turn
       cleanup. Explicit clean, recovery, transitions and first frame win. */
    bool maintenance=!landscape && (metrics_frame || (reader && reader_background)) && presentations && !full_refresh &&
        !transition_refresh && !stagger_requested && !lock_wave && mask_ready;
    unsigned changed_first=0,changed_rows=0;
    if(maintenance) {
        if(!thingsos_panel_pack_diff(c,fb,THINGSOS_PANEL_BYTES,!metrics_frame && refresh_mode==1,
                                    &changed_first,&changed_rows))return ESP_ERR_INVALID_STATE;
        if(!changed_rows) {
            scan_prepare();
            return ESP_OK;
        }
    }
    epd_poweron();
    /* The PMU answers over I2C while the touch sampler and Wi-Fi drivers run, so
       a single rails read can come back wrong; re-read before power cycling. */
    bool rails = read_pico_rails_on();
    for (unsigned attempt = 0; attempt < 2 && !rails; ++attempt) {
        vTaskDelay(pdMS_TO_TICKS(15));
        rails = read_pico_rails_on();
    }
    if (!rails) {
        ESP_LOGW("ThingsOS_board","Panel rails unavailable; retrying BSP power-on once");
        epd_poweroff();
        vTaskDelay(pdMS_TO_TICKS(100));
        epd_poweron();
        rails = read_pico_rails_on();
    }
    if (!rails) {
        ESP_LOGE("ThingsOS_board","Panel rails still unavailable after retry");
        epd_poweroff();
        return ESP_ERR_INVALID_STATE;
    }
    int64_t power_ready = esp_timer_get_time();
    float temperature = epd_board_read_pico.get_temperature();
    for (unsigned attempt = 0;
         attempt < 2 && (!isfinite(temperature) || temperature < 0 || temperature > 50); ++attempt) {
        ESP_LOGW("ThingsOS_board","Panel temperature read %.1f C; re-reading",(double)temperature);
        vTaskDelay(pdMS_TO_TICKS(15));
        temperature = epd_board_read_pico.get_temperature();
    }
    int64_t temperature_ready = esp_timer_get_time();
    if (!isfinite(temperature) || temperature < 0 || temperature > 50) {
        ESP_LOGE("ThingsOS_board","Panel temperature invalid: %.1f C",(double)temperature);
        epd_poweroff();
        return ESP_ERR_INVALID_STATE;
    }
    /* RAM is lost on reboot; the physical panel retains its last image. */
    if (!presentations) {
        scan_prepare();
        ESP_LOGI("ThingsOS_board", "Panel full clear before frame %u", presentations);
        epd_fullclear(&hl, (int)temperature);
    }
    /* Balanced text pages use the validated equal-pixel hold GC16 view.
       Periodic full-screen cleanup and the official waveform stay unchanged. */
    bool quality_reader=(ui_frame && refresh_mode==2) || thingsos_refresh_reader_quality(reader,false,refresh_mode);
    /* Editor updates preserve static controls; defer periodic cleaning until
       leaving the editor or an explicitly requested clean refresh. */
    bool clean=lock_wave || !presentations || full_refresh ||
        thingsos_refresh_automatic_clean(ui_frame,maintenance,keyboard!=0,policy_refresh,refresh_mode,soft_frames,refresh_every);
    bool transition=transition_refresh && !ui_frame;
    bool full_scope=thingsos_refresh_full_scope(clean,transition);
    enum EpdDrawMode mode=(clean || transition)?MODE_GC16:loading_frame?MODE_GL16:
        quality_reader?MODE_GC16:(keyboard || (!metrics_frame && refresh_mode==1))?MODE_DU:MODE_GL16;
    bool monochrome = !lock_wave && !loading_frame && !metrics_frame && (refresh_mode == 1 || keyboard==1 || keyboard==2);
    int64_t clear_ready = esp_timer_get_time();
    /* Keep official scan timing; application packing is independent of it. */
    read_pico_epd_use_scan(READ_PICO_EPD_SCAN_FULL);
    /* Refill the existing two 64-entry queues before starting DMA. This is
       the same 127-line threshold already used by safe-clock recovery. */
    epd_lcd_set_prefill_lines(127);
    epd_hl_waveform(&hl,&E0470_WAVEFORM);
    bool packed = maintenance || (epd_width() == 1216 && epd_height() == 684 &&
        (status && !full_scope ? thingsos_panel_pack_rows(c,fb,THINGSOS_PANEL_BYTES,monochrome,0,74)
         : (keyboard==1 || keyboard==2) && !full_scope
           ? thingsos_panel_pack_rows(c,fb,THINGSOS_PANEL_BYTES,monochrome,300,254) &&
             thingsos_panel_pack_rows(c,fb,THINGSOS_PANEL_BYTES,monochrome,638,keyboard==1?440:116)
           : thingsos_panel_pack(c,fb,THINGSOS_PANEL_BYTES,monochrome)));
    if (!packed) {
        /* Safe generic fallback if a future BSP uses a different layout. */
        for (int y = 0; y < (status && !full_scope ? 74 : c->height); y++) {
            for (int x = 0; x < c->width; x++) {
                unsigned char pixel=c->pixels[(size_t)y*c->width+x];
                epd_draw_pixel(landscape?683-y:x,landscape?x:y, monochrome ? (pixel<128?0:0xf0) : pixel&0xf0, fb);
            }
        }
    }
    scan_prepare();
    int64_t started=esp_timer_get_time();
    enum EpdDrawError e;
    bool recovered_underrun=false;
    bool pixel_mask=!full_scope && (quality_reader || (!reader && !keyboard && !status && mode==MODE_GL16)) && mask_ready;
    if(pixel_mask)mode=MODE_GC16;
    /* Lock/wake transitions use the complete GC16 waveform in staggered
       bands, independently of reader effects and refresh preferences. */
    bool wave=lock_wave || (!ui_frame && thingsos_refresh_reader_wave(reader,presentations!=0,transition,clean,stagger_requested,refresh_mode));
    if (wave) {
        epd_lcd_set_prefill_lines(127);
        bool attempted=false;
        bool forward=lock_wave?sleep_transition:(stagger_requested?stagger_forward:true);
        if(lock_wave)ESP_LOGI("ThingsOS_board","Lock transition: full GC16 ripple, sleep=%d",sleep_transition);
        e=stagger_turn((int)temperature,forward,clean,&attempted);
        if(attempted)mode=MODE_GC16;
        if(!attempted) {
            ESP_LOGW("ThingsOS_wave","Stagger unavailable; using full GC16. internal_free=%u",
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
            e=epd_hl_update_screen_full(&hl,MODE_GC16,(int)temperature);
            mode=MODE_GC16;
        } else if(e!=EPD_DRAW_SUCCESS) {
            /* Any interrupted stagger leaves an unknown optical state. */
            if(e & EPD_DRAW_EMPTY_LINE_QUEUE) {
                int before_mhz=read_pico_epd_pclk_mhz();
                thingsos_diag_event(DIAG_FAILURE,1,(unsigned)mode,(unsigned)presentations,(int)e,before_mhz);
                thingsos_pclk_on_underrun(&pclk_policy,(uint64_t)(esp_timer_get_time()/1000));
                recovered_underrun=true;
                ESP_LOGW("ThingsOS_board","Ripple underrun: PCLK %d -> %d MHz (latched=%d)",
                    before_mhz,READ_PICO_EPD_PCLK_MIN_MHZ,pclk_policy.latched);
                read_pico_epd_set_pclk(READ_PICO_EPD_PCLK_MIN_MHZ);
            }
            read_pico_epd_use_scan(READ_PICO_EPD_SCAN_FULL);
            epd_lcd_set_prefill_lines(127);
            epd_clear();
            e=epd_hl_update_screen_from_white(&hl,MODE_GC16,(int)temperature);
            clean=true;mode=MODE_GC16;
        }
    }
    else if (full_scope) {
        /* Drive every official GC16 action, including equal white pixels. */
        e = epd_hl_update_screen_full(&hl,MODE_GC16,(int)temperature);
    }
    else if(loading_frame) {
        /* Pico's official GL16 white maintenance must reach equal white
           pixels left behind by the loading overlay. One scan, either orientation. */
        e=epd_hl_update_screen_full(&hl,MODE_GL16,(int)temperature);
    }
    else if(cleanup_area.width && !landscape && !reader && !keyboard && !status && text_ready &&
            epd_get_rotation()==EPD_ROT_INVERTED_PORTRAIT && epd_width()==1216 && epd_height()==684) {
        mode=MODE_GC16;e=ui_content_update((int)temperature);
    }
    else if(maintenance) {
        /* Preserve grayscale and hold equal pixels, including white. Notes may
           add underlines inside the body; only their changed rows are scanned. */
        mode=MODE_GC16;
        epd_hl_waveform(&hl,&changed_waveform);
        e=epd_hl_update_area(&hl,mode,(int)temperature,(EpdRect){0,changed_first,684,changed_rows});
        ESP_LOGI("ThingsOS_board","Reader background rows=%u..%u result=0x%x",changed_first,changed_first+changed_rows,(unsigned)e);
    }
    else if(quality_reader && !transition && text_ready) {
        /* Same strong text transition when the user disables animation. */
        epd_hl_waveform(&hl,&text_waveform);
        e=epd_hl_update_screen_full(&hl,MODE_GC16,(int)temperature);
    }
    else if (transition || pixel_mask) {
        if(pixel_mask) epd_hl_waveform(&hl,&changed_waveform);
        e = epd_hl_update_screen(&hl,mode,(int)temperature);
    }
    else if (status) e = epd_hl_update_area_full(&hl,mode,(int)temperature,(EpdRect){0,0,684,74});
    else if (keyboard==3) {
        /* Download text/bar only; keep header, buttons and covers untouched. */
        epd_hl_waveform(&hl,&E0470_FOLLOW_WAVEFORM);
        e=epd_hl_update_area(&hl,MODE_DU,(int)temperature,(EpdRect){36,320,612,340});
    }
    else if (keyboard==1 || keyboard==2) {
        /* One DU scan covers both input and keyboard. Unchanged rows retain
           their packed baseline; the follow LUT drives only changed pixels. */
        epd_hl_waveform(&hl,&E0470_FOLLOW_WAVEFORM);
        e=epd_hl_update_area(&hl,MODE_DU,(int)temperature,
            keyboard==1?(EpdRect){0,300,684,778}:(EpdRect){0,300,684,454});
        if(keyboard==1)ESP_LOGI("ThingsOS_board","Keyboard layout: black/white DU result=0x%x",(unsigned)e);
    }
    else if (reader && !landscape && mode == MODE_GL16) {
        e = epd_hl_update_area(&hl,mode,(int)temperature,(EpdRect){0,0,684,1144});
        if (e == EPD_DRAW_SUCCESS) {
            /* Official follow LUT retains gray and leaves equal pixels alone. */
            epd_hl_waveform(&hl,&E0470_FOLLOW_WAVEFORM);
            e = epd_hl_update_area(&hl,MODE_DU,(int)temperature,(EpdRect){0,1144,684,72});
        }
    } else if (!reader && mode == MODE_GL16) {
        /* Official GL16's 15->15 white tick needs equal white pixels too.
           A single full-pixel update avoids an extra cleanup/animation frame. */
        e = epd_hl_update_screen_full(&hl,mode,(int)temperature);
    } else e = epd_hl_update_screen(&hl,mode,(int)temperature);
    epd_hl_waveform(&hl,&E0470_WAVEFORM);
    if (e != EPD_DRAW_SUCCESS) {
        ESP_LOGE("ThingsOS_board","Panel draw error=0x%x, mode=%u, reader=%d, keyboard=%d, temperature=%.1f",
            (unsigned)e,(unsigned)mode,reader,keyboard,(double)temperature);
        thingsos_diag_event(DIAG_FAILURE,1,(unsigned)mode,(unsigned)presentations,
            (int)e,read_pico_epd_pclk_mhz());
        if (e & EPD_DRAW_EMPTY_LINE_QUEUE) {
            /* The official BSP safe-clock recovery clears the physical panel,
             * resets only the old-frame baseline and keeps the target image.
             * Bound this retry to one; persistent failures still enter recovery. */
            thingsos_pclk_on_underrun(&pclk_policy,(uint64_t)(esp_timer_get_time()/1000));
            recovered_underrun=true;
            read_pico_epd_set_pclk(READ_PICO_EPD_PCLK_MIN_MHZ);
            read_pico_epd_use_scan(READ_PICO_EPD_SCAN_FULL);
            epd_lcd_set_prefill_lines(127);
            epd_clear();
            e=epd_hl_update_screen_from_white(&hl,MODE_GC16,(int)temperature);
            clean=true; mode=MODE_GC16;
            ESP_LOGW("ThingsOS_board","Safe-clock redraw result=0x%x at %u MHz (latched=%d)",
                (unsigned)e,READ_PICO_EPD_PCLK_MIN_MHZ,pclk_policy.latched);
            if(e!=EPD_DRAW_SUCCESS)
                thingsos_diag_event(DIAG_FAILURE,6,MODE_GC16,0,(int)e,READ_PICO_EPD_PCLK_MIN_MHZ);
        }
    }
    if (e == EPD_DRAW_SUCCESS) {
        unlock_refresh=false;
        if(full_scope || (!reader && !keyboard && !status))cleanup_area.width=0;
        presentations++;soft_frames=clean?0:(maintenance || keyboard==3 || ui_frame)?soft_frames:soft_frames+1;full_refresh=false;transition_refresh=false;
        if(clean)policy_refresh=false;
        if(!keyboard)reader_ui_previous=reader_ui;
    }
    if ((transition || ui_frame) && e == EPD_DRAW_SUCCESS) {
        unsigned rows=0,columns=0;
        for (int y=0;y<epd_height();++y) rows+=hl.dirty_lines[y]!=0;
        for (int x=0;x<epd_width();++x)
            columns+=((hl.dirty_columns[x/2]>>(4*(x%2)))&15)!=0;
        ESP_LOGI("ThingsOS_perf","GC16 scope rows=%u/%d columns=%u/%d full=%d equal_hold=%d",
            rows,epd_height(),columns,epd_width(),full_scope,pixel_mask && !full_scope);
    }
    ESP_LOGI("ThingsOS_board","Refresh %s: %lld ms, clean=%d, soft=%u/%u, transition=%d reader_ui=%d policy_pending=%d",
             mode==MODE_DU?"DU":mode==MODE_GL16?"GL16":"GC16",
             (long long)((esp_timer_get_time()-started)/1000),clean,soft_frames,refresh_every,transition,ui_frame,policy_refresh);
    ESP_LOGI("ThingsOS_perf", "Present total=%lld us power=%lld temperature=%lld clear=%lld pack=%lld update=%lld packed=%d",
        (long long)(esp_timer_get_time()-total_started),
        (long long)(power_ready-total_started),(long long)(temperature_ready-power_ready),
        (long long)(clear_ready-temperature_ready),(long long)(started-clear_ready),
        (long long)(esp_timer_get_time()-started),packed);
    if(e==EPD_DRAW_SUCCESS) {
        int64_t now=esp_timer_get_time();
        last_refresh_ms=(uint32_t)((now-total_started+999)/1000);
        rails_deadline=now+8000000;
        if(!recovered_underrun && !sleep_transition &&
           thingsos_pclk_on_frame_ok(&pclk_policy,(uint64_t)(now/1000),pclk_recovery_allowed)) {
            read_pico_epd_set_pclk(THINGSOS_PANEL_PCLK_MHZ);
            epd_lcd_set_prefill_lines(127);
            ESP_LOGI("ThingsOS_board","Panel PCLK recovered: %u -> %u MHz (attempt %u/%u)",
                READ_PICO_EPD_PCLK_MIN_MHZ,THINGSOS_PANEL_PCLK_MHZ,
                pclk_policy.recovery_attempts,THINGSOS_PCLK_MAX_RECOVERIES);
        }
    }
    else {epd_poweroff();rails_deadline=0;}
    return e == EPD_DRAW_SUCCESS ? ESP_OK : ESP_FAIL;
}
esp_err_t thingsos_board_present(const thingsos_canvas_t* c) { return present(c,false,false,false,false); }
esp_err_t thingsos_board_present_reader(const thingsos_canvas_t* c) { return present(c,true,false,false,false); }
esp_err_t thingsos_board_present_loading(const thingsos_canvas_t* c) {
    loading_frame=true;
    esp_err_t result=present(c,true,false,false,false);
    loading_frame=false;
    return result;
}
esp_err_t thingsos_board_present_reader_ui(const thingsos_canvas_t* c) {
    reader_ui=true;
    esp_err_t result=present(c,true,false,false,false);
    reader_ui=false;
    return result;
}
esp_err_t thingsos_board_present_reader_background(const thingsos_canvas_t* c) {
    reader_background=true;
    esp_err_t result=present(c,true,false,false,false);
    reader_background=false;
    return result;
}
esp_err_t thingsos_board_present_metrics(const thingsos_canvas_t* c) {
    metrics_frame=true;
    esp_err_t result=present(c,false,false,false,false);
    metrics_frame=false;
    return result;
}
esp_err_t thingsos_board_present_reader_stagger(const thingsos_canvas_t* c,bool forward) {
    stagger_requested=true;stagger_forward=forward;
    esp_err_t result=present(c,true,false,false,false);
    stagger_requested=false;
    return result;
}
esp_err_t thingsos_board_present_keyboard(const thingsos_canvas_t* c) { return present(c,false,true,false,false); }
esp_err_t thingsos_board_present_input(const thingsos_canvas_t* c) { return present(c,false,2,false,false); }
esp_err_t thingsos_board_present_progress(const thingsos_canvas_t* c) { return present(c,false,3,false,false); }
esp_err_t thingsos_board_present_status(const thingsos_canvas_t* c) { return present(c,false,false,true,false); }
void thingsos_board_refresh_policy(unsigned mode,unsigned every) {
    if(mode>2 || (every!=5 && every!=10 && every!=15 && every!=20)) return;
    if(refresh_mode!=mode || refresh_every!=every) {soft_frames=0;policy_refresh=true;}
    refresh_mode=mode;refresh_every=every;
}
void thingsos_board_idle(void) {
    if(rails_deadline && esp_timer_get_time()>=rails_deadline) {epd_poweroff();rails_deadline=0;}
}
void thingsos_board_full_refresh(void) { full_refresh=true; }
void thingsos_board_transition_refresh(void) { transition_refresh=true; }
void thingsos_board_cleanup_area(int x,int y,int width,int height) {
    if(width<=0 || height<=0 || x>=684 || y>=1216 || x+width<=0 || y+height<=0)return;
    int right=x+width,bottom=y+height;
    if(x<0)x=0;
    if(y<0)y=0;
    if(right>684)right=684;
    if(bottom>1216)bottom=1216;
    if(cleanup_area.width) {
        if(cleanup_area.x<x)x=cleanup_area.x;
        if(cleanup_area.y<y)y=cleanup_area.y;
        if(cleanup_area.x+cleanup_area.width>right)right=cleanup_area.x+cleanup_area.width;
        if(cleanup_area.y+cleanup_area.height>bottom)bottom=cleanup_area.y+cleanup_area.height;
    }
    cleanup_area=(EpdRect){x,y,right-x,bottom-y};
}
static bool touch_read(cst836u_touch_t* t) {
    if (!touch || !t || cst836u_read(touch, t) != ESP_OK) return false;
    t->touched = thingsos_touch_primary(t->raw, &t->x, &t->y);
    atomic_store_explicit(&touch_is_down,t->touched || t->count>1,memory_order_release);
    static bool previous;
    if (previous != t->touched) {
        if(!atomic_load_explicit(&private_input,memory_order_acquire))
            ESP_LOGI("ThingsOS_touch", "%s count=%u x=%u y=%u event=%u irq=%d",
                 t->touched ? "down" : "up", t->count, t->x, t->y,
                 t->points[0].event, cst836u_int_level(touch));
        previous = t->touched;
    }
    return true;
}
static void touch_sample_task(void* unused) {
    (void)unused;
    for(;;) {
        xSemaphoreTake(touch_access,portMAX_DELAY);
        bool sampling=touch_buffered;
        if(sampling) {
            cst836u_touch_t t={0};
            if(touch_read(&t)) {
                unsigned overflows=touch_samples.overflows;
                thingsos_touch_queue_push(&touch_samples,(thingsos_touch_sample_t){
                    (uint64_t)esp_timer_get_time()/1000,t.x,t.y,t.count,t.touched});
                if(touch_samples.overflows!=overflows)ESP_LOGW("ThingsOS_touch","Input queue overflow; gesture cancelled");
            }
        }
        tilt_capture();
        bool input_active=sampling || tilt_powered;
        xSemaphoreGive(touch_access);
        if(input_active)vTaskDelay(pdMS_TO_TICKS(10));
        else ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
    }
}
void thingsos_board_touch_buffer(bool enabled) {
    if(!touch_access) {
        if(!enabled)return;
        touch_access=xSemaphoreCreateMutex();
        if(!touch_access)return;
    }
    if(xSemaphoreTake(touch_access,pdMS_TO_TICKS(20))!=pdTRUE)return;
    if(enabled && !touch_task && xTaskCreate(touch_sample_task,"touch_input",3072,NULL,3,&touch_task)!=pdPASS) {
        enabled=false;ESP_LOGE("ThingsOS_touch","Input sampler allocation failed");
    }
    if(touch_buffered!=enabled) {
        memset(&touch_samples,0,sizeof(touch_samples));touch_buffered=enabled;
        if(enabled)xTaskNotifyGive(touch_task);
        ESP_LOGI("ThingsOS_touch","Reading input buffer=%d sampler stack margin=%u",enabled,
            touch_task?(unsigned)uxTaskGetStackHighWaterMark(touch_task):0);
    }
    xSemaphoreGive(touch_access);
}
bool thingsos_board_touch_pending(void) {
    if(!touch_access)return false;
    if(xSemaphoreTake(touch_access,0)!=pdTRUE)return true;
    bool pending=touch_buffered && touch_samples.count;
    xSemaphoreGive(touch_access);return pending;
}
bool thingsos_board_touch_contact_pending(void) {
    bool contact=atomic_load_explicit(&touch_is_down,memory_order_acquire);
    if(!touch_access || xSemaphoreTake(touch_access,0)!=pdTRUE)return contact;
    if(touch_buffered)for(unsigned i=0;i<touch_samples.count;++i)
        if(touch_samples.samples[(touch_samples.head+i)%THINGSOS_TOUCH_QUEUE_SIZE].down){contact=true;break;}
    xSemaphoreGive(touch_access);return contact;
}
bool thingsos_board_touch(cst836u_touch_t* t,uint64_t* sampled_ms) {
    if(!touch_access){*sampled_ms=(uint64_t)esp_timer_get_time()/1000;return touch_read(t);}
    if(xSemaphoreTake(touch_access,0)!=pdTRUE)return false;
    bool ok;
    if(touch_buffered) {
        thingsos_touch_sample_t s;
        ok=thingsos_touch_queue_pop(&touch_samples,&s);
        if(ok){memset(t,0,sizeof(*t));t->x=s.x;t->y=s.y;t->count=s.count;t->touched=s.down;*sampled_ms=s.at_ms;}
    } else {*sampled_ms=(uint64_t)esp_timer_get_time()/1000;ok=touch_read(t);}
    xSemaphoreGive(touch_access);return ok;
}
bool thingsos_board_power_status(unsigned* percent, bool* charge_connected) {
    if(read_pico_pmu_poll()!=ESP_OK)return false;
    const pmu_snapshot_t* p = read_pico_pmu_get();
    if(!p || !p->present || !p->status_ok)return false;
    /* A complete charge still means the charger is attached. Never infer this
       from USB enumeration or the instantaneous level of the pulsing CHG pin. */
    *charge_connected=p->charge_state==PMU_CHARGE_CHARGING ||
        p->charge_state==PMU_CHARGE_FULL_INFERRED;
    if((p->flags&PMU_STATUS_BATTERY_VALID) && p->soc_permille<=1000)
        *percent=p->soc_permille/10;
    return true;
}
bool thingsos_board_font(thingsos_font_t* font) {
    if (!font_data) {
        const esp_partition_t* p =
            esp_partition_find_first(ESP_PARTITION_TYPE_DATA, 0x40, "system_font");
        uint8_t h[48];
        if (!p || esp_partition_read(p, 0, h, sizeof(h)) != ESP_OK || memcmp(h, "THFONT01", 8))
            return false;
        font_bytes =
            (uint32_t)h[8] | (uint32_t)h[9] << 8 | (uint32_t)h[10] << 16 | (uint32_t)h[11] << 24;
        if (font_bytes < 64 || font_bytes > p->size - 48) return false;
        const void* mapped;
        if (esp_partition_mmap(
                p, 0, font_bytes + 48, ESP_PARTITION_MMAP_DATA, &mapped, &font_map) != ESP_OK)
            return false;
        unsigned char digest[32];
        font_data = (const uint8_t*)mapped + 48;
        size_t digest_bytes = 0;
        psa_status_t result = psa_crypto_init();
        if (result == PSA_SUCCESS)
            result = psa_hash_compute(
                PSA_ALG_SHA_256, font_data, font_bytes, digest, sizeof(digest), &digest_bytes);
        if (result != PSA_SUCCESS || digest_bytes != 32 || memcmp(digest, h + 12, 32)) {
            esp_partition_munmap(font_map);
            font_data = NULL;
            return false;
        }
    }
    return thingsos_font_memory(font, font_data, font_bytes);
}
void thingsos_board_ascii(thingsos_canvas_t* c, const char* text, int x, int y) {
    for (; *text && x < c->width - 18; text++, x += 18) {
        unsigned v = (unsigned char)*text;
        if (v < 32 || v > 126) v = '?';
        const uint8_t* glyph = thingsos_rescue_ascii[v - 32];
        for (int j = 0; j < 32; j++)
            for (int i = 0; i < 18; i++)
                if (glyph[j * 3 + i / 8] & (1 << (7 - i % 8)))
                    thingsos_rect(c, x + i, y + j, 1, 1, 0);
    }
}

void thingsos_board_healthy(void) {
    rtc_retain_mem_t* memory = bootloader_common_get_rtc_retain_mem();
    thingsos_boot_guard_t guard;
    thingsos_guard_healthy(&guard);
    memcpy(memory->custom, &guard, sizeof(guard));
}
bool thingsos_board_time(uint32_t* seconds) {
    if (!seconds || read_pico_pmu_refresh() != ESP_OK) return false;
    const pmu_snapshot_t* snapshot = read_pico_pmu_get();
    if (!snapshot || !snapshot->time_ok || !snapshot->time_synced ||
        snapshot->unix_sec < 1577836800)
        return false;
    *seconds = snapshot->unix_sec;
    return true;
}
esp_err_t thingsos_board_set_time(uint32_t seconds) {
    if (seconds < 1577836800) return ESP_ERR_INVALID_ARG;
    uint8_t data[4] = { seconds & 255, (seconds >> 8) & 255, (seconds >> 16) & 255,
                        (seconds >> 24) & 255 };
    return read_pico_pmu_cmd(PMU_CMD_TIME_SYNC, data, sizeof(data));
}

/* Official ESP32-S3 ROM boot mechanism; PMU stays powered during the CPU reset. */
void thingsos_board_rom_download(void) {
    uint8_t request[2]={0};
    read_pico_pmu_cmd(PMU_CMD_HOST_REQUEST_RESET,request,sizeof(request));
    REG_WRITE(RTC_CNTL_OPTION1_REG,RTC_CNTL_FORCE_DOWNLOAD_BOOT);
    esp_restart();
}

/* Drain and acknowledge once; long press wins over a queued short press. */
unsigned thingsos_board_power_key(void) {
    unsigned action=0;
    for(unsigned i=0;i<PMU_EVENT_FIFO_DEPTH;++i) {
        if(read_pico_pmu_poll()!=ESP_OK) break;
        const pmu_snapshot_t* p=read_pico_pmu_get();
        if(!p || !p->event_ok || !p->pending_events || !p->event.event_id) break;
        uint8_t type=p->event.type;uint16_t id=p->event.event_id;
        if(power_key_armed && type==PMU_EVT_KEY_SHORT && action!=2) action=1;
        if(power_key_armed && (type==PMU_EVT_KEY_LONG || type==PMU_EVT_KEY_FORCE_OFF)) action=2;
        ESP_LOGI("ThingsOS_power","PMU event %s id=%u",pmu_event_name(type),(unsigned)id);
        if(read_pico_pmu_event_ack(id)!=ESP_OK) break;
    }
    /* Do not interpret the long hold which powered the device on as shutdown. */
    const pmu_snapshot_t* latest=read_pico_pmu_get();
    if(latest && latest->status_ok && !(latest->key_state&1)) power_key_armed=true;
    return action;
}
esp_err_t thingsos_board_suspend(const thingsos_canvas_t* sleep_frame) {
    if(!thingsos_board_tilt_active(false))return ESP_FAIL;
    thingsos_board_touch_buffer(false);
    full_refresh=true;
    esp_err_t err=present(sleep_frame,false,0,false,true);
    epd_poweroff();rails_deadline=0;
    if(err!=ESP_OK) return err;
    err=cst836u_set_mode(touch,CST836U_MODE_DEEPSLEEP);
    if(err!=ESP_OK) return err;
    wifi_mode_t mode;
    paused_wifi=false;
    err=esp_wifi_get_mode(&mode);
    if(err!=ESP_OK && err!=ESP_ERR_WIFI_NOT_INIT)return err;
    if(err==ESP_OK && mode!=WIFI_MODE_NULL) {
        err=esp_wifi_stop();
        if(err!=ESP_OK)return err;
        paused_wifi=true;
        /* Resume with AP disabled; the application re-evaluates its page policy. */
        if(mode==WIFI_MODE_APSTA) {
            err=esp_wifi_set_mode(WIFI_MODE_STA);
            if(err!=ESP_OK)return err;
        }
    }
    return ESP_OK;
}
esp_err_t thingsos_board_resume(void) {
    esp_err_t err=cst836u_wake(touch);
    if(paused_wifi) {esp_wifi_start();paused_wifi=false;}
    full_refresh=true;
    thingsos_board_unlock_refresh();
    return err;
}
void thingsos_board_unlock_refresh(void) { unlock_refresh=true; }
#include "sleep_wait.inc"

const char* thingsos_board_charge(void) {const pmu_snapshot_t* p=read_pico_pmu_get();if(!p || !p->status_ok) return "状态未知";switch(p->charge_state) {case PMU_CHARGE_CHARGING:return "正在充电";case PMU_CHARGE_FULL_INFERRED:return "电量已满";case PMU_CHARGE_NOT_CHARGING:return "未充电";case PMU_CHARGE_FAULT:return "充电异常";default:return "状态未知";}}
