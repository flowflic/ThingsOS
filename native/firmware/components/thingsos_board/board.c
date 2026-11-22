// SPDX-License-Identifier: Apache-2.0
/* Metalio E-Ink4-Plus board layer. Refresh policy, stagger turns, input
   sampling and font mapping are unchanged from the Read Pico board; the
   hardware calls go through metalio_bsp (TCA9555/TPS65185/FT6336U/BQ27220/
   CX25601N/PCF8563) instead of the Read Pico PMU. */
#include "board.h"
#include "esp_attr.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_sleep.h"
#include "driver/gpio.h"
#include "driver/usb_serial_jtag.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "soc/lp_system_reg.h"
#include "soc/soc.h"
#include <string.h>
#include <math.h>
#include <stdatomic.h>
#include "thingsos/touch_privacy.h"
#include "bootloader_common.h"
#include "e0470_epaper_waveform.h"
#include "epd_highlevel.h"
#include "panel_waveform.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "psa/crypto.h"
#include "sc7a20h.h"
#include "rescue_ascii.h"
#include "thingsos/diagnostics.h"
#include "thingsos/guard.h"
#include "thingsos/touch.h"
#include "thingsos/tilt.h"
#include "tilt_mount.h"
#include "thingsos/touch_queue.h"
#include "thingsos/display_buffer.h"
#include "thingsos/refresh_mask.h"
#include "thingsos/refresh_policy.h"
/* Application hold mask: official non-equal actions, phases and timing remain
   unchanged. It is only used for small in-page updates and the reading view;
   page changes run the vendor's all-pixel GC16 (see panel_waveform.h). */
static uint8_t changed_lut[THINGSOS_PANEL_TURN_PHASES*64];
static EpdWaveformPhases changed_phases;
static const EpdWaveformPhases* changed_ranges[]={&changed_phases};
static const EpdWaveformMode changed_mode={.type=MODE_GC16,.temp_ranges=1,.range_data=changed_ranges};
static const EpdWaveformMode* changed_modes[]={&changed_mode};
static EpdWaveform changed_waveform={.num_modes=1,.num_temp_ranges=1,.mode_data=changed_modes};
static bool mask_ready;
static const EpdWaveform* dark_refresh_wave=&changed_waveform;
static bool dark_ready;
/* Corrected low-flash text view. Reader page turns and ordinary UI frames
   (home tabs, nested menus) share it: both rasterise text/icons onto every
   frame, and `thingsos_panel_text_wave()` alone leaves unchanged black pixels
   undriven, which shows as uneven ink inside one glyph or icon. */
static uint8_t corrected_lut[THINGSOS_PANEL_TURN_PHASES*64];
static EpdWaveformPhases corrected_phases;
static const EpdWaveformPhases* corrected_ranges[]={&corrected_phases};
static const EpdWaveformMode corrected_mode={.type=MODE_GC16,.temp_ranges=1,.range_data=corrected_ranges};
static const EpdWaveformMode* corrected_modes[]={&corrected_mode};
static EpdWaveform corrected_waveform={.num_modes=1,.num_temp_ranges=1,.mode_data=corrected_modes};
static bool corrected_ready;
static EpdiyHighlevelState hl;
/* Resolved once at start: the vendor-aligned all-pixel tables. */
static const EpdWaveform* panel_wave;
static bool touch_ok;
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
static bool full_refresh, transition_refresh,policy_refresh;
static unsigned refresh_mode, refresh_every=15, soft_frames;

static esp_partition_mmap_handle_t font_map;
static const uint8_t* font_data;
static uint32_t font_bytes;
static sc7a20h_handle_t tilt_sensor;
static bool tilt_powered;
static int64_t tilt_retry_at,tilt_capture_at,tilt_trace_until;
/* Sampler-task producer, UI consumer; never touched from an ISR. */
static EXT_RAM_BSS_ATTR thingsos_tilt_queue_t tilt_samples;
/* Input-task wrist-turn detector; the UI claims at most one intent, see tilt.h. */
static thingsos_tilt_stream_t tilt_stream;
static bool tilt_landscape;
static unsigned tilt_level=1;
static unsigned frontlight_mode,frontlight_level;
static bool frontlight_mixed;
static unsigned frontlight_warmth=50;
static esp_timer_handle_t haptic_timer;
#include "clock_recovery.inc"
#include "display_stats.inc"
/* Edge hold width comes from METALIO_WHITE_BORDER_PIXELS (metalio_bsp.h). */
void thingsos_scan_border_hold(int pixels);
#include "scan_fault.h"
/* Journal why a scan failed (DIAGNOSTICS doc, kind 11 stage 5/6):
   a=SCAN_FAULT_* flags, b=longest EOF gap (us), code=phase<<16|lines consumed,
   errno=catch-ups<<16|longest EOF handler (us). */
static void diag_scan_fault(unsigned stage) {
    thingsos_scan_fault_t f;
    if (!thingsos_scan_take_fault(&f)) return;
    thingsos_diag_event(DIAG_FAILURE, stage, f.flags, f.gap_us,
        (int)((f.phase << 16) | (f.consumed & 0xffffu)),
        (int)(((f.catchups > 0xffffu ? 0xffffu : f.catchups) << 16) | (f.isr_us & 0xffffu)));
}
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
        esp_err_t err=sc7a20h_init(metalio_i2c_bus(),&config,&tilt_sensor);
        if(err!=ESP_OK){ESP_LOGW("ThingsOS_tilt","Sensor init failed: %s",esp_err_to_name(err));return false;}
        ESP_LOGI("ThingsOS_tilt","SC7A20H ready: 25 Hz, 2g, low power");
        tilt_powered=true;
    } else tilt_powered=sc7a20h_power_up(tilt_sensor)==ESP_OK;
    /* INT1 shares GPIO2 with the TCA9555 INT; keep it open-drain. */
    metalio_accel_int_open_drain();
    if(tilt_powered)tilt_retry_at=0;
    return tilt_powered;
}
bool thingsos_board_tilt_active(bool active) {
    if(!touch_access)return !active;
    xSemaphoreTake(touch_access,portMAX_DELAY);
    bool was_on=tilt_powered;
    bool ok=tilt_active_locked(active);
    if(!active || (!was_on && tilt_powered)){memset(&tilt_samples,0,sizeof(tilt_samples));memset(&tilt_stream,0,sizeof(tilt_stream));tilt_capture_at=0;
        tilt_trace_until=active?esp_timer_get_time()+300000000:0;}
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
    static int64_t log_at;
    if(esp_timer_get_time()>=log_at) {
        /* Keep a bounded-rate raw pose beyond the first minute of a session;
           V1's 30-line limit hid the later missed left/right attempts. */
        log_at=esp_timer_get_time()+2000000;
        if(err==ESP_OK)ESP_LOGI("ThingsOS_tilt","Raw mg=%d,%d,%d status=0x%02x",sample.x_mg,sample.y_mg,sample.z_mg,sample.status);
        else ESP_LOGW("ThingsOS_tilt","Read failed: %s",esp_err_to_name(err));
    }
    if(err!=ESP_OK || !(sample.status&8))return;
    int before_motion=tilt_stream.state.motion_candidate,before_pending=tilt_stream.pending;
    bool before_calibrated=tilt_stream.state.calibrated,before_armed=tilt_stream.state.armed;
    bool before_posture=tilt_stream.state.posture_pending;
    int before_origin=tilt_stream.state.origin_angle;
    uint64_t sample_ms=(uint64_t)esp_timer_get_time()/1000;
    bool sample_blocked=atomic_load_explicit(&touch_is_down,memory_order_acquire);
    int canonical_x,canonical_y,canonical_z;
    thingsos_metalio_tilt_mount(sample.x_mg,sample.y_mg,sample.z_mg,&canonical_x,&canonical_y,&canonical_z);
    thingsos_tilt_stream_sample(&tilt_stream,canonical_x,canonical_y,canonical_z,
        sample_ms,tilt_landscape,tilt_level,sample_blocked);
    /* Diagnostic beta: one record for every fresh sensor sample during the
       first five minutes of a reading session, enabling exact input replay.
       At 25Hz this stays below 7KB/s on the 460800-baud USB log link. */
    if(esp_timer_get_time()<tilt_trace_until && usb_serial_jtag_is_connected())
        ESP_LOGI("ThingsOS_tilt","Trace ms=%llu raw_mg=%d,%d,%d landscape=%d level=%u blocked=%d calibrated=%d armed=%d origin=%d angle=%d fired=%d mount=1",
            (unsigned long long)sample_ms,sample.x_mg,sample.y_mg,sample.z_mg,
            tilt_landscape,tilt_level,sample_blocked,tilt_stream.state.calibrated,tilt_stream.state.armed,
            tilt_stream.state.origin_angle,tilt_stream.state.angle,tilt_stream.state.fired_direction);
    int64_t sample_norm=(int64_t)sample.x_mg*sample.x_mg+(int64_t)sample.y_mg*sample.y_mg+(int64_t)sample.z_mg*sample.z_mg;
    static int64_t rejected_log_at;
    if((sample_norm<600LL*600 || sample_norm>1800LL*1800) && esp_timer_get_time()>=rejected_log_at) {
        rejected_log_at=esp_timer_get_time()+200000;
        ESP_LOGI("ThingsOS_tilt","Rejected acceleration raw_mg=%d,%d,%d recovery_direction=%d peak=%d origin=%d armed=%d",
            sample.x_mg,sample.y_mg,sample.z_mg,tilt_stream.state.fired_direction,
            tilt_stream.state.peak_angle,tilt_stream.state.origin_angle,tilt_stream.state.armed);
    }
    if(before_motion!=tilt_stream.state.motion_candidate || before_pending!=tilt_stream.pending)
        ESP_LOGI("ThingsOS_tilt","Wrist phase candidate=%d pending=%d roll=%d raw_mg=%d,%d,%d armed=%d",
            tilt_stream.state.motion_candidate,tilt_stream.pending,tilt_stream.state.angle-tilt_stream.state.base_angle,
            sample.x_mg,sample.y_mg,sample.z_mg,tilt_stream.state.armed);
    if(before_calibrated!=tilt_stream.state.calibrated || before_armed!=tilt_stream.state.armed ||
       before_posture!=tilt_stream.state.posture_pending || before_origin!=tilt_stream.state.origin_angle)
        ESP_LOGI("ThingsOS_tilt","State calibrated=%d armed=%d posture=%d origin=%d base=%d angle=%d peak=%d fired=%d raw_mg=%d,%d,%d level=%u landscape=%d touching=%d",
            tilt_stream.state.calibrated,tilt_stream.state.armed,tilt_stream.state.posture_pending,
            tilt_stream.state.origin_angle,tilt_stream.state.base_angle,tilt_stream.state.angle,
            tilt_stream.state.peak_angle,tilt_stream.state.fired_direction,
            sample.x_mg,sample.y_mg,sample.z_mg,tilt_level,tilt_landscape,(int)atomic_load_explicit(&touch_is_down,memory_order_acquire));
    thingsos_tilt_queue_push(&tilt_samples,(thingsos_tilt_input_t){sample.x_mg,sample.y_mg,sample.z_mg,(uint64_t)esp_timer_get_time()/1000});
}
bool thingsos_board_tilt_read_raw(int* x,int* y,int* z,uint64_t* ms) {
    if(!touch_access || xSemaphoreTake(touch_access,0)!=pdTRUE)return false;
    thingsos_tilt_input_t sample;bool ok=thingsos_tilt_queue_pop(&tilt_samples,&sample);
    if(ok){*x=sample.x;*y=sample.y;*z=sample.z;*ms=sample.ms;}
    xSemaphoreGive(touch_access);return ok;
}
static void haptic_off(void* unused){(void)unused;metalio_motor(false);}
/* 上电复用：更新后电源轨保持窗口。/ Rail keepalive window after a frame. */
static esp_timer_handle_t power_idle;
static bool rails_kept;
static bool static_page;
void thingsos_board_static_page(bool enabled){static_page=enabled;}
static void power_idle_off(void* unused){(void)unused;
    if(!rails_kept)return;
    rails_kept=false;
    epd_poweroff();
    esp_err_t idle=metalio_panel_idle_verify();
    if(idle!=ESP_OK)ESP_LOGW("ThingsOS_board","Panel idle after keepalive: %s",esp_err_to_name(idle));
}
esp_err_t thingsos_board_start(void) {
    /* E0470A14-AF 34-pin panel (684x1216, 8-bit bus). Its supplier tables are
       byte-identical to the Read Pico E0470 tables, which stay a provisional
       waveform until a factory LUT exists. VCOM is the vendor value written
       to volatile TPS65185 registers on every rail power-up. */
    epd_init(&epd_board_metalio, &E0470_DISPLAY_8BIT, EPD_LUT_1K);
    if(!metalio_ioe_ok())return ESP_ERR_INVALID_STATE;
    metalio_accel_int_open_drain();
    esp_err_t err=metalio_power_init();
    if(err!=ESP_OK)ESP_LOGW("ThingsOS_board","Power management incomplete: %s",esp_err_to_name(err));
    if(metalio_rtc_init()!=ESP_OK)ESP_LOGW("ThingsOS_board","RTC unavailable");
    if(metalio_frontlight_init()!=ESP_OK)ESP_LOGW("ThingsOS_board","Frontlight unavailable");
    const esp_timer_create_args_t haptic={.callback=haptic_off,.name="haptic"};
    esp_timer_create(&haptic,&haptic_timer);
    const esp_timer_create_args_t idle={.callback=power_idle_off,.name="panel_power"};
    esp_timer_create(&idle,&power_idle);
    epd_set_rotation(EPD_ROT_INVERTED_PORTRAIT);
    /* Vendor-validated 20 MHz; underrun recovery drops to the minimum clock. */
    metalio_epd_set_pclk(METALIO_EPD_PCLK_MAX_MHZ);
    e0470_waveform_init();
    /* Vendor 2.0.76 reference: full GC16/GL16 tables plus the threshold DU.
       The trimmed reference tables are only a fallback if assembly fails. */
    panel_wave=thingsos_panel_waveform();
    /* Reading page turns and small in-page updates are the frames the reader
       actually waits for, and they run through these masked/text views rather
       than the page-change branch. Build them from the trimmed 36-phase table
       so a turn costs 36 frames instead of 48; clean and periodic refreshes
       still use the untrimmed panel_wave. */
    const EpdWaveformPhases* gc_turn=e0470_waveform_phases(thingsos_panel_turn_waveform(),MODE_GC16);
    if(gc_turn && gc_turn->phases==THINGSOS_PANEL_TURN_PHASES && gc_turn->phases<=THINGSOS_PANEL_GC16_PHASES) {
        mask_ready=thingsos_refresh_mask_equal(gc_turn->luts,gc_turn->phases,changed_lut,sizeof(changed_lut));
        changed_phases=*gc_turn;changed_phases.luts=changed_lut;
        changed_waveform.temp_intervals=panel_wave->temp_intervals;
        /* One corrected table serves reader turns and ordinary UI frames;
           the host test asserts the UI entry point is byte-identical. */
        corrected_ready=thingsos_panel_reader_text_wave(gc_turn->luts,gc_turn->phases,corrected_lut,sizeof(corrected_lut));
        corrected_phases=*gc_turn;corrected_phases.luts=corrected_lut;
        corrected_waveform.temp_intervals=panel_wave->temp_intervals;
    }
    const EpdWaveformPhases* gc=e0470_waveform_phases(panel_wave,MODE_GC16);
    const EpdWaveformPhases* gl=e0470_waveform_phases(panel_wave,MODE_GL16);
    const EpdWaveformPhases* du=e0470_waveform_phases(panel_wave,MODE_DU);
    const EpdWaveform* dark=thingsos_panel_dark_waveform();
    dark_ready=dark!=NULL;
    if(dark_ready)dark_refresh_wave=dark;
    ESP_LOGI("ThingsOS_board","Dark refresh full GC16 and DU black maintenance ready=%d",dark_ready);
    ESP_LOGI("ThingsOS_board","Panel waveform GC16=%d GL16=%d DU=%d phases (turn table %d); equal-pixel hold mask ready=%d",
        gc?gc->phases:0,gl?gl->phases:0,du?du->phases:0,
        gc_turn?gc_turn->phases:0,mask_ready);
    ESP_LOGI("ThingsOS_board","Reader white baseline: Pico single white tick, immediate panel power-off, corrected_ready=%d",corrected_ready);
    ESP_LOGI("ThingsOS_board","Black-to-black correction ready=%d phases=%d (reader turns and ordinary UI frames)",
        corrected_ready,corrected_ready?corrected_phases.phases:0);
    /* Vendor 2.0.76 feeder policy: deep prefill plus fixed per-producer rows.
       Each producer owns every second row, so the two 128-slot queues must hold
       at least half of the prefill (96 of the 192 rows). */
    ESP_LOGI("ThingsOS_board","Scan prefill=%d lines, producer rows fixed (2 x 128-slot queues)",
        epd_lcd_prefill_lines());
    epd_set_leading_skip(false);
    hl = epd_hl_init(panel_wave);
    if (!epd_hl_get_framebuffer(&hl)) return ESP_ERR_NO_MEM;
    err = metalio_touch_init();
    if (err != ESP_OK) return err;
    touch_ok=true;
    metalio_sd_start_probe();
    ready = true;
    ESP_LOGI("ThingsOS_board", "Display/touch initialized");
    return ESP_OK;
}
#include "stagger.inc"
#include "lock_wave.inc"
static bool stagger_requested,stagger_forward,reader_background,controls_requested,metrics_background,audio_background;
static EpdRect audio_label_area;
static bool lock_wave_requested,lock_wave_forward;
static bool reader_ui,reader_ui_previous;
static bool ui_navigation_refresh;
static bool menu_refresh;
static bool panel_recovery_pending;
static EpdRect loading_area;
static void (*reader_scan_barrier)(void);
void thingsos_board_scan_barrier(void (*barrier)(void)) {reader_scan_barrier=barrier;}
static void scan_prepare(void) {
    void (*barrier)(void)=reader_scan_barrier;reader_scan_barrier=NULL;
    if(barrier)barrier();
}
#include "keyboard_present.inc"
#include "loading_recovery.inc"
#include "loading_present.inc"
static bool keyboard_pack(const thingsos_canvas_t* c,uint8_t* fb,bool monochrome,bool layout,bool pin) {
    EpdRect bands[2];keyboard_regions(layout,pin,bands);
    for(unsigned i=0;i<2;++i)
        if(!thingsos_panel_pack_rows(c,fb,THINGSOS_PANEL_BYTES,monochrome,bands[i].y,bands[i].height))return false;
    return true;
}

/* Scan prefill before each phase starts: 192 rows, as the Metalio vendor
   2.0.79+ feeders use (each 128-slot queue then holds 96). Feeders block
   instead of spinning when their queue is full (scan_adapter.c). A measured
   224-row prefill only during playback still underran (consumed 512-616/688). */
static int scan_prefill_lines(void){return 192;}
static esp_err_t present(const thingsos_canvas_t* c, bool reader, unsigned keyboard, bool status, bool sleep_clear) {
    if (!ready || !c || !c->pixels || !((c->width==684 && c->height==1216)||(c->width==1216 && c->height==684))) return ESP_ERR_INVALID_STATE;
    bool landscape=c->width==1216;
    /* Returning from a reading control uses the same reader waveform once;
       automatic cleaning resumes on the next ordinary page frame. */
    bool ui_frame=reader_ui || (reader && reader_ui_previous);
    if(landscape)status=false;
    int64_t total_started = esp_timer_get_time();
    uint8_t* fb = epd_hl_get_framebuffer(&hl);
    bool reset_baseline=!presentations || panel_recovery_pending;
    /* Regional updates and overlays defer cleanup to the next page frame.
       Keep wake/suspend separate from the periodic or manual black pass. */
    bool dark_active=c->inverted && !reset_baseline && (dark_ready || mask_ready);
    bool dark_diff=dark_active && (!full_refresh || lock_wave_requested);
    bool dark_count=dark_diff && !sleep_clear && !lock_wave_requested && !keyboard && !status &&
        !reader_background && !ui_frame && !controls_requested;
    /* Changed text and held black can settle at different optical blacks.
       Clear foreground reading frames to a black base before drawing text. */
    bool dark_reader_clean=dark_ready && dark_count && reader;
    bool dark_periodic=dark_ready && dark_count &&
        thingsos_refresh_periodic(false,refresh_mode,soft_frames,refresh_every);
    bool black_base=dark_active && (dark_periodic || dark_reader_clean) &&
        !sleep_clear && !lock_wave_requested && epd_width()==1216 && epd_height()==684;
    dark_diff=dark_diff && !black_base;
    const EpdWaveform* menu_wave = NULL;
    if (menu_refresh && !reader && !keyboard && !status && !dark_active &&
        !reset_baseline && !full_refresh && !sleep_clear && !controls_requested && refresh_mode != 2) {
        menu_wave = thingsos_panel_menu_waveform();
    }
    /* Background work must not drive the whole white page or spend a page-turn
       cleanup. Explicit clean, recovery, transitions and first frame win. */
    bool maintenance=!landscape && reader && reader_background && !reset_baseline && !full_refresh && !policy_refresh &&
        !transition_refresh && !stagger_requested && mask_ready;
    bool static_compare=!landscape && static_page && !reader && !keyboard && !status && !sleep_clear &&
        !reset_baseline && !full_refresh && !policy_refresh && !transition_refresh && !lock_wave_requested &&
        !controls_requested && !menu_wave;
    unsigned changed_first=0,changed_rows=0;
    if(maintenance || static_compare) {
        if(!thingsos_panel_pack_diff(c,fb,THINGSOS_PANEL_BYTES,refresh_mode==1&&!metrics_background,
                                    &changed_first,&changed_rows))return ESP_ERR_INVALID_STATE;
        if(!changed_rows) {
            scan_prepare();
            ESP_LOGI("ThingsOS_board","%s unchanged; scan skipped, soft=%u/%u",audio_background?"Audio differential":static_compare?"Static UI":"Reader background",soft_frames,refresh_every);
            return ESP_OK;
        }
    }
    /* 上一帧可能还在复用窗口内：先停掉空闲断电计时器，避免扫描中途断电。
       / A kept rail from the previous frame must not be cut mid-scan. */
    if(rails_kept){esp_timer_stop(power_idle);rails_kept=false;}
    epd_poweron();
    bool rails = metalio_rails_on();
    if (!rails) {
        ESP_LOGW("ThingsOS_board","Panel rails unavailable; retrying power-on once");
        epd_poweroff();
        vTaskDelay(pdMS_TO_TICKS(100));
        epd_poweron();
        rails = metalio_rails_on();
    }
    if (!rails) {
        ESP_LOGE("ThingsOS_board","Panel rails still unavailable after retry");
        epd_poweroff();
        return ESP_ERR_INVALID_STATE;
    }
    int64_t power_ready = esp_timer_get_time();
    float temperature = epd_board_metalio.get_temperature();
    int64_t temperature_ready = esp_timer_get_time();
    if (!isfinite(temperature) || temperature < 0 || temperature > 50) {
        ESP_LOGE("ThingsOS_board","Panel temperature invalid: %.1f C",(double)temperature);
        epd_poweroff();
        return ESP_ERR_INVALID_STATE;
    }
    /* Between complete scans: retry the validated clock after a recovery. */
    clock_retry_step();
    /* Loading frames use the active panel clock. CKV pulse/underrun errors
       still take the guarded safe-clock path through loading_recover(). */
    /* Balanced text pages use the validated equal-pixel hold GC16 view.
       Periodic cleanup, explicit refresh and page changes run the vendor's
       all-pixel GC16 (see the transition branch below). */
    bool quality_reader=(ui_frame && refresh_mode==2) || thingsos_refresh_reader_quality(reader,false,refresh_mode);
    /* Each keyboard event finishes with one regional scan. Keep
       those frames out of the periodic full-screen cleanup budget. */
    bool editor_frame=keyboard==1 || keyboard==2 || keyboard==6;
    bool keyboard_frame=editor_frame || keyboard==4 || keyboard==5;
    bool clean=black_base || (!dark_diff && (sleep_clear || reset_baseline || full_refresh ||
        (keyboard!=5 && !controls_requested && !menu_wave && thingsos_refresh_automatic_clean(ui_frame,maintenance,keyboard_frame,
                                                                  policy_refresh,refresh_mode,soft_frames,refresh_every))));
    bool ui_cleanup=thingsos_refresh_ui_cleanup(ui_navigation_refresh,reader,keyboard,status);
    /* Normal loading entry/exit uses differential grayscale even when opening
       a book changes navigation/reader policy. Explicit cleans and recovery
       remain in clean/reset_baseline and keep priority. */
    bool transition=keyboard!=5 && !dark_diff && !black_base && ((transition_refresh && !ui_frame)||ui_cleanup);
    bool ui_full_gc16=false;
    bool ui_low_flash=thingsos_refresh_ui_low_flash(reader,keyboard,status,clean,refresh_mode,corrected_ready);
    ui_low_flash |= menu_wave != NULL;
    ui_low_flash|=thingsos_refresh_controls_low_flash(controls_requested,presentations!=0,full_refresh,sleep_clear,corrected_ready,refresh_mode);
    bool static_quality=static_page && !menu_wave && !reader && !keyboard && !status && !sleep_clear &&
        !clean && !controls_requested && refresh_mode==0;
    enum EpdDrawMode mode=(clean || transition || quality_reader)?MODE_GC16:(keyboard || refresh_mode==1)?MODE_DU:MODE_GL16;
    bool monochrome = (!metrics_background && refresh_mode == 1) || editor_frame || keyboard==4;
    bool loading_frame=keyboard==5 && !clean && !transition;
    unsigned polarity=c->inverted?255:0;
    int64_t clear_ready = esp_timer_get_time();
    /* Keep calibrated scan timing; application packing is independent of it. */
    metalio_epd_use_scan(METALIO_EPD_SCAN_FULL);
    /* Refill the existing two 64-entry queues before starting DMA. This is
       the same 127-line threshold already used by safe-clock recovery. */
    epd_lcd_set_prefill_lines(scan_prefill_lines());
    epd_hl_waveform(&hl,panel_wave);
    if(loading_frame) {
        for(int y=loading_area.y;y<loading_area.y+loading_area.height;++y)
            for(int x=loading_area.x;x<loading_area.x+loading_area.width;++x)
                epd_draw_pixel(landscape?683-y:x,landscape?x:y,(c->pixels[(size_t)y*c->width+x]^polarity)&0xf0,fb);
    }
    bool packed = loading_frame || maintenance || static_compare || (epd_width() == 1216 && epd_height() == 684 &&
        (keyboard==4 && !clean && !transition?thingsos_panel_pack_rows(c,fb,THINGSOS_PANEL_BYTES,true,280,420)
         : status && !clean ? thingsos_panel_pack_rows(c,fb,THINGSOS_PANEL_BYTES,monochrome,0,74)
         : editor_frame && !clean
           ? keyboard_pack(c,fb,monochrome,keyboard==1,keyboard==6)
           : thingsos_panel_pack(c,fb,THINGSOS_PANEL_BYTES,monochrome)));
    if (!packed) {
        /* Safe generic fallback if a future BSP uses a different layout. */
        for (int y = 0; y < (status && !clean ? 74 : c->height); y++) {
            for (int x = 0; x < c->width; x++) {
                unsigned char pixel=c->pixels[(size_t)y*c->width+x]^polarity;
                epd_draw_pixel(landscape?683-y:x,landscape?x:y, monochrome ? (pixel<128?0:0xf0) : pixel&0xf0, fb);
            }
        }
    }
    scan_prepare();
    int64_t started=esp_timer_get_time();
    enum EpdDrawError e;
    bool reader_black_corrected=false,ui_black_corrected=false;
    /* Low-flash UI and reader turns still deliver white maintenance to every
       pixel. A pure difference mask would skip unchanged software white that
       carries physical residue. Explicit cleans retain the complete table. */
    bool pixel_mask=!clean && !transition && quality_reader && mask_ready;
    if(pixel_mask)mode=MODE_GC16;
    /* User choice: full refresh stays direct global GC16. Ordinary reader
       turns can stagger GC16 text transitions without weakening cleanup. */
    bool wave=!ui_frame && thingsos_refresh_reader_wave(reader,presentations!=0,transition,clean,stagger_requested,refresh_mode);
    if(reset_baseline) {
        /* An interrupted phase leaves the panel's optical state unknown. */
        ESP_LOGI("ThingsOS_board", "Panel full clear before frame %u", presentations);
        e=panel_redraw_from_white((int)temperature);
    } else if(dark_diff) {
        mode=MODE_GC16;
        epd_hl_waveform(&hl,dark_refresh_wave);
        if(editor_frame)e=keyboard_present(keyboard==1,(int)temperature,true,keyboard==6);
        else if(loading_frame) {
            EpdRect area=landscape?(EpdRect){684-loading_area.y-loading_area.height,
                loading_area.x,loading_area.height,loading_area.width}:loading_area;
            e=epd_hl_update_area(&hl,mode,(int)temperature,area);
        } else if(status)e=epd_hl_update_area(&hl,mode,(int)temperature,(EpdRect){0,0,684,74});
        else if(keyboard==3)e=epd_hl_update_area(&hl,mode,(int)temperature,(EpdRect){36,320,612,340});
        else if(keyboard==4)e=epd_hl_update_area(&hl,mode,(int)temperature,(EpdRect){0,280,684,420});
        else if(maintenance || static_compare)
            e=epd_hl_update_area(&hl,mode,(int)temperature,(EpdRect){0,changed_first,684,changed_rows});
        else e=epd_hl_update_screen(&hl,mode,(int)temperature);
        ESP_LOGI("ThingsOS_board","Dark frame: GC16 phases=%d keyboard=%u status=%d lock=%d result=0x%x",
            dark_ready?THINGSOS_PANEL_GC16_PHASES:changed_phases.phases,keyboard,status,lock_wave_requested,(unsigned)e);
    }
    else if(black_base) {
        /* The vendor DU paths to gray 0 drive only toward black. Repack the
           requested page even on error, so recovery always has its target. */
        memset(fb,0,THINGSOS_PANEL_BYTES);
        if(dark_ready)epd_hl_waveform(&hl,dark_refresh_wave);
        mode=MODE_DU;
        e=epd_hl_update_screen_full(&hl,mode,(int)temperature);
        thingsos_panel_pack(c,fb,THINGSOS_PANEL_BYTES,monochrome);
        if(e==EPD_DRAW_SUCCESS) {
            mode=MODE_GC16;
            epd_hl_waveform(&hl,dark_refresh_wave);
            e=epd_hl_update_screen(&hl,mode,(int)temperature);
        }
        ESP_LOGI("ThingsOS_board","Dark cleanup: periodic=%d reader=%d black maintenance=%d result=0x%x",
            dark_periodic,dark_reader_clean,dark_ready,(unsigned)e);
    }
    else if(lock_wave_requested&&presentations){
        bool lock_ripple_applied=false;
        mode=MODE_GC16;
        e=lock_wave_turn((int)temperature,lock_wave_forward,&lock_ripple_applied);
        clean=true;ui_full_gc16=true;transition=false;
        ESP_LOGI("ThingsOS_board","Lock/wake full GC16 ripple forward=%d applied=%d full_pixels=1 result=0x%x",
                 lock_wave_forward,lock_ripple_applied,(unsigned)e);
    }
    else if (sleep_clear) {
        /* A retained wallpaper needs a physical clear, beyond ordinary GC16.
           Keep the packed target and reset its old-frame baseline to white
           before redrawing; finish both scans before suspend removes power. */
        ESP_LOGI("ThingsOS_board","Sleep wallpaper: full clear then GC16 redraw");
        e=panel_redraw_from_white((int)temperature);
    }
    else if (menu_wave) {
        mode = MODE_GC16;
        epd_hl_waveform(&hl, menu_wave);
        e = epd_hl_update_screen_full(&hl, mode, (int)temperature);
        ESP_LOGI("ThingsOS_board","Menu low-flash GC16: 48 phases, two white pulses, result=0x%x",(unsigned)e);
    }
    else if (static_quality) {
        /* Static search pages need the complete vendor white maintenance over
           the margins too. Regional keyboard updates cannot condition them. */
        mode=MODE_GL16;
        epd_hl_waveform(&hl,panel_wave);
        e=epd_hl_update_screen_full(&hl,mode,(int)temperature);
        ESP_LOGI("ThingsOS_board","Static UI full GL16: vendor table result=0x%x",(unsigned)e);
    }
    else if (ui_low_flash) {
        /* Reuse the validated reading paths, including their equal-white
           maintenance, for both home tabs and nested UI pages. Preserve gray
           icons in balanced mode; do not turn navigation into a full clean.
           Balanced mode uses the corrected table: leaving unchanged black
           icons and labels undriven is the same uneven-ink defect the reader
           saw inside one glyph. Fast mode stays on the DU view. */
        mode=refresh_mode==1?MODE_DU:MODE_GC16;
        ui_black_corrected=thingsos_refresh_ui_corrected(refresh_mode,corrected_ready);
        epd_hl_waveform(&hl,refresh_mode==1?thingsos_panel_fast_waveform()
                                           :ui_black_corrected?&corrected_waveform:panel_wave);
        e=epd_hl_update_screen_full(&hl,mode,(int)temperature);
        epd_hl_waveform(&hl,panel_wave);
        ESP_LOGI("ThingsOS_board","UI low-flash %s: full-pixel white maintenance tab_cleanup=%d black_corrected=%d result=0x%x",
            refresh_mode==1?"DU":"GC16",ui_cleanup,ui_black_corrected,(unsigned)e);
    }
    else if (clean || transition) {
        /* Vendor 2.0.76 conditions every page change with one all-pixel GC16,
           and so does an explicit/periodic clean. The official GC16 drives
           equal pixels through their own black-then-white cycle, which is what
           normalises the whole page background. UI transitions retain the
           complete Metalio 48-phase table; reader transitions and keyboard
           layouts keep their existing trim. Physical epd_clear is reserved
           for startup, sleep and error recovery, not an extra pass per Tab. */
        if (!clean) {
            epd_hl_waveform(&hl, thingsos_panel_navigation_waveform(reader,keyboard!=0));
            ui_full_gc16=!reader&&!keyboard;
            /* Layout changes drive only the input/candidate/key bands. Hold
               the submit/cancel gap as well as the header/nav. Existing GL16
               avoids a global black inversion when switching language. */
            if(keyboard==1) {
                mode=MODE_GL16;
                e=keyboard_present(true,(int)temperature,false,false);
            } else e=epd_hl_update_screen_full(&hl, MODE_GC16, (int)temperature);
            epd_hl_waveform(&hl, panel_wave);
        } else {
            ui_full_gc16=!reader&&!keyboard;
            e=epd_hl_update_screen_full(&hl,MODE_GC16,(int)temperature);
        }
    }
    else if(loading_frame) {
        e=loading_present(c,landscape,(int)temperature,&mode);
    }
    else if(maintenance) {
        /* Preserve grayscale and hold equal pixels, including white. Notes may
           add underlines inside the body; only their changed rows are scanned. */
        mode=MODE_GC16;
        e=EPD_DRAW_SUCCESS;
        if(audio_background && audio_label_area.width>0) {
            /* Software-white halo pixels can retain physical ink from a prior
               label. Deliver the existing 36-phase white tick and black
               maintenance only here. Commit this label before recalculating
               the remaining diff, so it is never scanned a second time. */
            epd_hl_waveform(&hl,corrected_ready?&corrected_waveform:thingsos_panel_turn_waveform());
            e=epd_hl_update_area_full(&hl,MODE_GC16,(int)temperature,audio_label_area);
            ESP_LOGI("ThingsOS_board","Audio label maintenance x=%d y=%d w=%d h=%d phases=36 result=0x%x",
                audio_label_area.x,audio_label_area.y,audio_label_area.width,audio_label_area.height,(unsigned)e);
        }
        if(e==EPD_DRAW_SUCCESS) {
            epd_hl_waveform(&hl,&changed_waveform);
            e=epd_hl_update_area(&hl,mode,(int)temperature,(EpdRect){0,changed_first,684,changed_rows});
        }
        ESP_LOGI("ThingsOS_board","%s rows=%u..%u equal_pixels=held result=0x%x",audio_background?"Audio differential":"Reader background",changed_first,changed_first+changed_rows,(unsigned)e);
    }
    else if (wave) {
        epd_lcd_set_prefill_lines(scan_prefill_lines());
        bool attempted=false;
        e=stagger_turn((int)temperature,stagger_requested?stagger_forward:true,clean,&attempted);
        reader_black_corrected=attempted && reader && !clean && e==EPD_DRAW_SUCCESS;
        if(attempted)mode=MODE_GC16;
        if(!attempted) {
            ESP_LOGW("ThingsOS_wave","Stagger unavailable; using full GC16. internal_free=%u",
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
            /* Low-memory fallback must not reintroduce the missing white tick. */
            e=epd_hl_update_screen_full(&hl,mode,(int)temperature);
        } else if(e!=EPD_DRAW_SUCCESS) {
            /* Any interrupted stagger leaves an unknown optical state. */
            if(e & EPD_DRAW_EMPTY_LINE_QUEUE)clock_underrun();
            metalio_epd_use_scan(METALIO_EPD_SCAN_FULL);
            epd_lcd_set_prefill_lines(scan_prefill_lines());
            e=panel_redraw_from_white((int)temperature);
            clean=true;mode=MODE_GC16;
        }
    }
    else if(quality_reader && !transition && corrected_ready) {
        /* Pico text transitions with one white tick; full-pixel delivery
           keeps that tick from being discarded in unchanged margins.
           Stable black pixels keep the source drive sequence, so a glyph
           that is already black is re-inked evenly instead of showing the
           unevaluated hold of the plain text view. */
        reader_black_corrected=reader && !ui_frame;
        epd_hl_waveform(&hl,&corrected_waveform);
        e=epd_hl_update_screen_full(&hl,MODE_GC16,(int)temperature);
    }
    else if (reader && !status && !keyboard && mode == MODE_DU) {
        /* Diff masks skip software-white pixels that still carry optical
           residue. Deliver the reader's white-only maintenance over all pixels. */
        epd_hl_waveform(&hl,thingsos_panel_fast_waveform());
        e=epd_hl_update_screen_full(&hl,MODE_DU,(int)temperature);
        ESP_LOGI("ThingsOS_board","Reader fast DU: full-pixel white maintenance result=0x%x",(unsigned)e);
    }
    else if (pixel_mask) {
        epd_hl_waveform(&hl,&changed_waveform);
        e = epd_hl_update_screen(&hl,mode,(int)temperature);
    }
    else if (status) e = epd_hl_update_area_full(&hl,mode,(int)temperature,(EpdRect){0,0,684,74});
    else if (keyboard==4) {
        epd_hl_waveform(&hl,thingsos_panel_fast_waveform());
        e=epd_hl_update_area_full(&hl,MODE_DU,(int)temperature,(EpdRect){0,280,684,420});
    }
    else if (keyboard==3) {
        /* Download text/bar only; keep header, buttons and covers untouched. */
        epd_hl_waveform(&hl,&E0470_FOLLOW_WAVEFORM);
        e=epd_hl_update_area(&hl,MODE_DU,(int)temperature,(EpdRect){36,320,612,340});
    }
    else if (editor_frame) {
        /* Fast DU input and GL16 layout changes share one exact-band scan. */
        mode=keyboard==1?MODE_GL16:MODE_DU;
        e=keyboard_present(keyboard==1,(int)temperature,false,keyboard==6);
        ESP_LOGI("ThingsOS_board","Keyboard bands: layout=%d pin=%d scans=1 result=0x%x",keyboard==1,keyboard==6,(unsigned)e);
    }
    else if (reader && !landscape && mode == MODE_GL16) {
        e = epd_hl_update_area(&hl,mode,(int)temperature,(EpdRect){0,0,684,1144});
        if (e == EPD_DRAW_SUCCESS) {
            /* Official follow LUT retains gray and leaves equal pixels alone. */
            epd_hl_waveform(&hl,&E0470_FOLLOW_WAVEFORM);
            e = epd_hl_update_area(&hl,MODE_DU,(int)temperature,(EpdRect){0,1144,684,72});
        }
    } else if (!reader && mode == MODE_GL16) {
        /* Use the same complete table as the user-confirmed manual refresh.
           UI white normalization must not inherit Pico's 36-phase trim. */
        epd_hl_waveform(&hl, thingsos_panel_navigation_waveform(false,false));
        mode=MODE_GC16;ui_full_gc16=true;
        e = epd_hl_update_screen_full(&hl,MODE_GC16,(int)temperature);
        epd_hl_waveform(&hl, panel_wave);
    } else e = epd_hl_update_screen(&hl,mode,(int)temperature);
    epd_hl_waveform(&hl,panel_wave);
    if (e != EPD_DRAW_SUCCESS) {
        ESP_LOGE("ThingsOS_board","Panel draw error=0x%x, mode=%u, reader=%d, keyboard=%d, temperature=%.1f",
            (unsigned)e,(unsigned)mode,reader,keyboard,(double)temperature);
        thingsos_diag_event(DIAG_FAILURE,1,(unsigned)mode,(unsigned)presentations,(int)e,metalio_epd_pclk_mhz());
        diag_scan_fault(5);
        if(keyboard==5) {
            e=loading_recover(c,fb,(int)temperature);
            if(e!=EPD_DRAW_SUCCESS)diag_scan_fault(6);
            clean=true;mode=MODE_GC16;
        } else {
            if(e & EPD_DRAW_EMPTY_LINE_QUEUE)clock_underrun();
            if ((!reset_baseline || !presentations) && ((e & EPD_DRAW_EMPTY_LINE_QUEUE) || black_base)) {
                /* Keep the target image while repairing the uncertain optical
                 * state at the safe clock. Successful recovery arms a later
                 * 20 MHz probe; repeated underruns back off instead of latching. */
                metalio_epd_use_scan(METALIO_EPD_SCAN_FULL);
                epd_lcd_set_prefill_lines(scan_prefill_lines());
                e=panel_redraw_from_white((int)temperature);
                if(e!=EPD_DRAW_SUCCESS)diag_scan_fault(6);
                clean=true; mode=MODE_GC16;
                ESP_LOGW("ThingsOS_board","Safe-clock redraw result=0x%x at %d MHz (retry backoff=%u s)",
                    (unsigned)e,metalio_epd_pclk_mhz(),clock_retry_delay_s());
            }
        }
    }
    panel_recovery_pending=e!=EPD_DRAW_SUCCESS;
    display_last_failed=panel_recovery_pending;
    clock_note_result(e==EPD_DRAW_SUCCESS);
    /* An all-pixel GC16 re-drives every pixel, so it restarts the periodic
       clean counter: counting it as a partial would only repeat the same scan. */
    if (e == EPD_DRAW_SUCCESS) {
        presentations++;
        if(dark_diff && !clean) {
            if(dark_count && soft_frames<refresh_every)++soft_frames;
        } else if(clean || (transition&&!ui_low_flash) || ui_full_gc16 || static_quality)soft_frames=0;
        else if(soft_frames<refresh_every && !(maintenance || keyboard==3 || keyboard_frame || ui_frame || controls_requested))++soft_frames;
        full_refresh=false;
        bool loading_canvas=keyboard==5 && loading_canvas_target(c);
        if(clean || dark_diff || loading_canvas)policy_refresh=false;
        transition_refresh=false;
        if(!keyboard && !controls_requested)reader_ui_previous=reader_ui;
        if(ui_cleanup || loading_canvas)ui_navigation_refresh=false;
        if((!keyboard && !status && !controls_requested) || loading_canvas)menu_refresh=false;
    }
    if(ui_full_gc16){const EpdWaveformPhases* phases=e0470_waveform_phases(panel_wave,MODE_GC16);
        ESP_LOGI("ThingsOS_board","UI full GC16 phases=%d tab_cleanup=%d temperature=%.1f C result=0x%x",phases?phases->phases:0,ui_cleanup,(double)temperature,(unsigned)e);}
    /* The first clean frame whitens the edges; from then on they are held
       when METALIO_WHITE_BORDER_PIXELS is non-zero. */
    if (e == EPD_DRAW_SUCCESS && presentations == 1) thingsos_scan_border_hold(METALIO_WHITE_BORDER_PIXELS);
    if (transition && e == EPD_DRAW_SUCCESS) {
        unsigned rows=0,columns=0;
        for (int y=0;y<epd_height();++y) rows+=hl.dirty_lines[y]!=0;
        for (int x=0;x<epd_width();++x)
            columns+=((hl.dirty_columns[x/2]>>(4*(x%2)))&15)!=0;
        ESP_LOGI("ThingsOS_perf","GC16 scope rows=%u/%d columns=%u/%d full=%d equal_hold=%d",
            rows,epd_height(),columns,epd_width(),1,pixel_mask && !clean);
    }
    ESP_LOGI("ThingsOS_board","Refresh %s: %lld ms, clean=%d, soft=%u/%u, transition=%d, reader_ui=%d, policy_pending=%d, pclk=%d MHz black_corrected=%d ui_black_corrected=%d",
             mode==MODE_DU?"DU":mode==MODE_GL16?"GL16":"GC16",
             (long long)((esp_timer_get_time()-started)/1000),clean,soft_frames,refresh_every,transition,
             ui_frame,policy_refresh,metalio_epd_pclk_mhz(),reader_black_corrected && !clean,
             ui_black_corrected && e==EPD_DRAW_SUCCESS);
    ESP_LOGI("ThingsOS_perf", "Present total=%lld us power=%lld temperature=%lld clear=%lld pack=%lld update=%lld packed=%d",
        (long long)(esp_timer_get_time()-total_started),
        (long long)(power_ready-total_started),(long long)(temperature_ready-power_ready),
        (long long)(clear_ready-temperature_ready),(long long)(started-clear_ready),
        (long long)(esp_timer_get_time()-started),packed);
    /* Release panel HV+VCOM rails immediately and verify idle after every
       completed frame (including Library and status refreshes), preventing idle
       speckles when leaving Cubox/Reader or holding static UI pages. */
    rails_kept=false;
    esp_timer_stop(power_idle);
    epd_poweroff();
    esp_err_t idle_error=metalio_panel_idle_verify();
    if(idle_error!=ESP_OK)return idle_error;
    if(e==EPD_DRAW_SUCCESS)display_record_success(total_started);
    if((reader || static_page || editor_frame) && e==EPD_DRAW_SUCCESS)ESP_LOGI("ThingsOS_board","Static page rails released immediately; idle verified");
    return e==EPD_DRAW_SUCCESS?ESP_OK:ESP_FAIL;
}
esp_err_t thingsos_board_present(const thingsos_canvas_t* c) { return present(c,false,false,false,false); }
esp_err_t thingsos_board_present_lock_transition(const thingsos_canvas_t* c,bool forward){
    lock_wave_requested=true;lock_wave_forward=forward;full_refresh=true;
    /* The retained wallpaper and the first unlocked page share full cleanup
       and immediate rail-off; neither may use the ordinary UI reuse window. */
    esp_err_t result=present(c,false,0,false,true);
    lock_wave_requested=false;return result;
}
esp_err_t thingsos_board_present_controls(const thingsos_canvas_t* c) {
    controls_requested=true;
    esp_err_t result=present(c,false,0,false,false);
    controls_requested=false;
    return result;
}
esp_err_t thingsos_board_present_reader(const thingsos_canvas_t* c) { return present(c,true,false,false,false); }
esp_err_t thingsos_board_present_reader_image(const thingsos_canvas_t* c) {
    return present(c,true,false,false,false);
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
    /* Reuse changed-row / equal-pixel hold maintenance. Keep the gray tracks
       even if the reader's separate fast mode is selected. Explicit cleanup,
       first-frame and error recovery keep their normal priority. */
    metrics_background=true;
    esp_err_t result=thingsos_board_present_reader_background(c);
    metrics_background=false;
    return result;
}
esp_err_t thingsos_board_present_audio_diff(const thingsos_canvas_t* c) {
    /* Compare the complete target, but scan changed rows with an equal-pixel
       hold waveform. This preserves gray controls and touches no equal pixels,
       including inside the bounding band of the changed audio controls. */
    audio_background=true;
    esp_err_t result=thingsos_board_present_metrics(c);
    audio_background=false;
    return result;
}
esp_err_t thingsos_board_present_audio_diff_clean(const thingsos_canvas_t* c,int x,int y,int w,int h) {
    if(!c || c->width!=684 || c->height!=1216 || x<0 || y<0 || w<=0 || h<=0 ||
       x>c->width-w || y>c->height-h)return ESP_ERR_INVALID_ARG;
    audio_label_area=(EpdRect){x,y,w,h};
    esp_err_t result=thingsos_board_present_audio_diff(c);
    audio_label_area=(EpdRect){0};
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
esp_err_t thingsos_board_present_pin_input(const thingsos_canvas_t* c) { return present(c,false,6,false,false); }
esp_err_t thingsos_board_present_progress(const thingsos_canvas_t* c) { return present(c,false,3,false,false); }
esp_err_t thingsos_board_present_audio(const thingsos_canvas_t* c) { return present(c,false,4,false,false); }
esp_err_t thingsos_board_present_status(const thingsos_canvas_t* c) { return present(c,false,false,true,false); }
esp_err_t thingsos_board_present_loading(const thingsos_canvas_t* c,int x,int y,int w,int h) {
    if(!c || x<0 || y<0 || w<=0 || h<=0 || x>c->width-w || y>c->height-h)return ESP_ERR_INVALID_ARG;
    loading_area=(EpdRect){x,y,w,h};
    return present(c,false,5,false,false);
}
void thingsos_board_refresh_policy(unsigned mode,unsigned every) {
    if(mode>2 || (every!=5 && every!=10 && every!=15 && every!=20)) return;
    if(refresh_mode!=mode || refresh_every!=every) {soft_frames=0;policy_refresh=true;}
    refresh_mode=mode;refresh_every=every;
}
void thingsos_board_idle(void) {
    /* Every completed update already powers the panel down. */
}
void thingsos_board_full_refresh(void) {
    full_refresh=true;
}
void thingsos_board_transition_refresh(void) { transition_refresh=true; }
void thingsos_board_menu_refresh(void) { menu_refresh=true; }
/* Queue normalization with the next full UI frame. Do not scan the old page
   here: navigation owns the target framebuffer and should need only one pass. */
void thingsos_board_cleanup_area(int x,int y,int width,int height) {
    if(x>=0&&y>=0&&width>0&&height>0&&x<=684-width&&y<=1216-height)ui_navigation_refresh=true;
}
/* Bezel keys report outside the panel at Y≈1600, X≈100 HOME / 200 PREV /
   300 NEXT. Move them onto the Read Pico key band (y≥1300, centres 80/240/400)
   so thingsos_keys_step() keeps one layout: PREV left, HOME middle (short =
   menu, hold = home), NEXT right. Anything else off-panel is dropped. */
#define METALIO_KEY_BAND_Y 1400
static bool map_bezel(uint16_t* x,uint16_t* y) {
    if(*y<1216)return true;
    if(*y<1500 || *y>1700)return false;
    static const uint16_t centre[3]={100,200,300},target[3]={240,80,400};
    for(unsigned i=0;i<3;++i)
        if(*x+50>=centre[i] && *x<=centre[i]+50){*x=target[i];*y=METALIO_KEY_BAND_Y;return true;}
    return false;
}
static bool touch_read(thingsos_touch_t* t) {
    static unsigned touch_errors;
    static int64_t touch_recover_at;
    if (!touch_ok || !t) return false;
    esp_err_t err = metalio_touch_read(t);
    if (err != ESP_OK) {
        int64_t now_us = esp_timer_get_time();
        if (++touch_errors >= 5 && now_us >= touch_recover_at) {
            touch_recover_at = now_us + 5000000;
            ESP_LOGW("ThingsOS_touch", "Touch read failed %u times (%s); resetting controller",
                     touch_errors, esp_err_to_name(err));
            esp_err_t wake_err = metalio_touch_wake();
            thingsos_diag_event(DIAG_FAILURE, 1, 2, touch_errors, (int)err, (int)wake_err);
        }
        return false;
    }
    touch_errors = 0;
    t->touched = thingsos_touch_primary(t->raw, &t->x, &t->y) && map_bezel(&t->x,&t->y);
    atomic_store_explicit(&touch_is_down,t->touched || t->count>1,memory_order_release);
    static bool previous;
    static thingsos_touch_privacy_t privacy;
    bool log_allowed=thingsos_touch_log_allowed(&privacy,
        atomic_load_explicit(&private_input,memory_order_acquire),t->touched);
    if (previous != t->touched) {
        if(log_allowed)ESP_LOGI("ThingsOS_touch", "%s count=%u x=%u y=%u irq=%d",
                 t->touched ? "down" : "up", t->count, t->x, t->y, metalio_touch_int_level());
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
            thingsos_touch_t t={0};
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
    xSemaphoreTake(touch_access,portMAX_DELAY);
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
bool thingsos_board_touch(thingsos_touch_t* t,uint64_t* sampled_ms) {
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
int thingsos_board_battery(void) {
    /* Snapshot of the BSP's 1 Hz sampler; -1 while settling after boot/wake. */
    return metalio_battery_percent();
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
/* PCF8563 keeps UTC across power-off; the VL flag marks it invalid until set. */
bool thingsos_board_time(uint32_t* seconds) {
    return seconds && metalio_rtc_get(seconds);
}
esp_err_t thingsos_board_set_time(uint32_t seconds) {
    if (seconds < 1577836800) return ESP_ERR_INVALID_ARG;
    return metalio_rtc_set(seconds);
}

/* ESP32-S31 ROM download request through the LP system register. */
void thingsos_board_rom_download(void) {
    SET_PERI_REG_MASK(LP_SYSTEM_REG_SYS_CTRL_REG,LP_SYSTEM_REG_FORCE_DOWNLOAD_BOOT);
    esp_restart();
}
void thingsos_board_restart(void) {
    esp_restart();
}

/* POWER is TCA P1.7 and the volume keys P1.0/P1.1, all active low. The TCA
   INT (GPIO2) falls on any input change, so the port is read only after an
   edge or while a key is held. Long press fires once at 3 s while held. */
#define POWER_LONG_US 3000000
#define KEY_DEBOUNCE_US 30000
static bool keys_seen,power_down,power_long_sent,power_armed;
static int64_t power_since,side_since[2],keys_poll_at;
static bool side_down[2];
static unsigned power_event,side_event;
static void keys_poll(void) {
    bool held=power_down || side_down[0] || side_down[1];
    int64_t now=esp_timer_get_time();
    /* Other I2C callers (e.g. pwr_good) read TCA IN0..IN1 and clear INT#.
       Keep a bounded 100 ms fallback poll so key edges are never missed. */
    if(keys_seen && !held && metalio_ioe_int_level() && now-keys_poll_at<100000)return;
    keys_poll_at=now;
    uint8_t p1=0xFF;
    if(metalio_ioe_read(NULL,&p1)!=ESP_OK)return;
    bool pressed=!(p1&(1u<<(METALIO_IO_POWER&7)));
    if(!keys_seen) {
        /* A power key still held from power-on is not a request. */
        keys_seen=true;power_down=pressed;power_armed=!pressed;power_since=now;
    } else if(pressed!=power_down && now-power_since>=KEY_DEBOUNCE_US) {
        power_down=pressed;power_since=now;
        if(!pressed) {
            if(power_armed && !power_long_sent)power_event=power_event==2?2:1;
            power_armed=true;power_long_sent=false;
        }
    }
    if(power_down && power_armed && !power_long_sent && now-power_since>=POWER_LONG_US) {
        power_long_sent=true;power_event=2;
    }
    for(unsigned i=0;i<2;++i) {
        bool down=!(p1&(1u<<((i?METALIO_IO_VOL_DOWN:METALIO_IO_VOL_UP)&7)));
        if(down!=side_down[i] && now-side_since[i]>=KEY_DEBOUNCE_US) {
            side_down[i]=down;side_since[i]=now;
            if(down)side_event=i+1;
        }
    }
}
unsigned thingsos_board_power_key(void) {
    keys_poll();
    unsigned action=power_event;power_event=0;
    if(action)ESP_LOGI("ThingsOS_power","Power key %s",action==2?"long":"short");
    return action;
}
unsigned thingsos_board_side_key(void) {
    keys_poll();
    unsigned action=side_event;side_event=0;
    return action;
}
/* The BOOT button doubles as the AI key at runtime. It is active low with an
   external/pull-up and shares GPIO61. A key already held at power-on must not
   start a recording: wait for its release before arming the short press. */
#define AI_KEY_SHORT_US 800000
static bool ai_key_ready,ai_key_down,ai_key_armed;
static int64_t ai_key_change_at,ai_key_down_at;
static unsigned ai_key_event;
static void ai_key_poll(void) {
    static bool configured;
    if(!configured) {
        gpio_config_t io={
            .pin_bit_mask=1ULL<<METALIO_BOOT_GPIO,
            .mode=GPIO_MODE_INPUT,
            .pull_up_en=GPIO_PULLUP_ENABLE,
            .pull_down_en=GPIO_PULLDOWN_DISABLE,
            .intr_type=GPIO_INTR_DISABLE,
        };
        if(gpio_config(&io)!=ESP_OK)return;
        configured=true;
    }
    bool down=gpio_get_level(METALIO_BOOT_GPIO)==0;
    int64_t now=esp_timer_get_time();
    if(!ai_key_ready) {
        ai_key_ready=true;ai_key_down=down;ai_key_armed=!down;
        ai_key_change_at=now;if(down)ai_key_down_at=now;
        return;
    }
    if(down!=ai_key_down && now-ai_key_change_at>=KEY_DEBOUNCE_US) {
        ai_key_down=down;ai_key_change_at=now;
        if(down)ai_key_down_at=now;
        else {
            if(ai_key_armed && now-ai_key_down_at<AI_KEY_SHORT_US)ai_key_event=1;
            ai_key_armed=true;
        }
    }
}
unsigned thingsos_board_ai_key(void) {
    ai_key_poll();
    unsigned action=ai_key_event;ai_key_event=0;
    if(action)ESP_LOGI("ThingsOS_ai","BOOT/AI key short press");
    return action;
}
esp_err_t thingsos_board_power_off(void) {
    thingsos_diag_event(DIAG_POWER,0,0,0,0,0);
    thingsos_diag_poll(true);
    metalio_frontlight_set(METALIO_FL_OFF,0);
    return metalio_power_off();
}
void thingsos_board_frontlight(unsigned mode,unsigned level) {
    frontlight_mixed=false;
    frontlight_mode=mode>3?0:mode;frontlight_level=level>100?100:level;
    metalio_frontlight_set((metalio_fl_mode_t)frontlight_mode,frontlight_level);
}
void thingsos_board_frontlight_mix(bool enabled,unsigned level,unsigned warmth) {
    frontlight_mixed=true;frontlight_mode=enabled?3:0;
    frontlight_level=level>100?100:level;frontlight_warmth=warmth>100?100:warmth;
    metalio_frontlight_mix(enabled,frontlight_level,frontlight_warmth);
}
void thingsos_board_haptic(unsigned ms) {
    if(!ms || !haptic_timer)return;
    esp_timer_stop(haptic_timer);
    metalio_motor(true);
    esp_timer_start_once(haptic_timer,(uint64_t)ms*1000);
}
esp_err_t thingsos_board_sd_info(thingsos_sd_info_t* info) { return metalio_sd_get_info(info); }
esp_err_t thingsos_board_sd_remount(void) { return metalio_sd_remount(); }
esp_err_t thingsos_board_sd_sync(void) { return metalio_sd_sync(); }
esp_err_t thingsos_board_suspend(const thingsos_canvas_t* sleep_frame) {
    thingsos_board_trace_stage(THINGSOS_TRACE_SUSPEND_BEGIN);
    thingsos_diag_event(DIAG_SLEEP,0,metalio_battery_percent(),metalio_charge_state(),0,0);
    thingsos_diag_poll(true);
    metalio_battery_pause();
    if(!thingsos_board_tilt_active(false))return ESP_FAIL;
    thingsos_board_touch_buffer(false);
    esp_err_t err=thingsos_board_present_lock_transition(sleep_frame,true);
    rails_kept=false;
    esp_timer_stop(power_idle);
    epd_poweroff();
    if(err!=ESP_OK) return err;
    metalio_frontlight_set(METALIO_FL_OFF,0);
    err=metalio_touch_sleep();
    if(err!=ESP_OK) return err;
    wifi_mode_t mode;
    err=esp_wifi_get_mode(&mode);
    if(err!=ESP_OK && err!=ESP_ERR_WIFI_NOT_INIT)return err;
    if(err==ESP_OK && mode!=WIFI_MODE_NULL) {
        err=esp_wifi_stop();
        if(err!=ESP_OK&&err!=ESP_ERR_WIFI_NOT_STARTED)return err;
        /* Resume with AP disabled; the application re-evaluates its page policy. */
        if(mode==WIFI_MODE_APSTA) {
            err=esp_wifi_set_mode(WIFI_MODE_STA);
            if(err!=ESP_OK)return err;
        }
    }
    thingsos_board_trace_stage(THINGSOS_TRACE_SUSPEND_DONE);
    return ESP_OK;
}
esp_err_t thingsos_board_resume(void) {
    thingsos_board_trace_stage(THINGSOS_TRACE_RESUME_BEGIN);
    thingsos_diag_event(DIAG_WAKE,0,esp_sleep_get_wakeup_causes(),metalio_charge_state(),0,0);
    metalio_battery_restart();
    esp_err_t err=metalio_touch_wake();
    if(err!=ESP_OK)thingsos_diag_event(DIAG_FAILURE,1,1,0,(int)err,0);
    keys_poll_at=0;
    if(frontlight_mixed)metalio_frontlight_mix(frontlight_mode!=0,frontlight_level,frontlight_warmth);
    else metalio_frontlight_set((metalio_fl_mode_t)frontlight_mode,frontlight_level);
    full_refresh=true;
    thingsos_board_trace_stage(THINGSOS_TRACE_RESUME_DONE);
    return err;
}
#include "sleep_wait.inc"

const char* thingsos_board_charge(void) {
    switch(metalio_charge_state()) {
    case METALIO_CHARGE_CHARGING:return "正在充电";
    case METALIO_CHARGE_FULL:return "电量已满";
    case METALIO_CHARGE_NONE:return "未充电";
    default:return "状态未知";
    }
}

bool thingsos_board_charging(void) {
    return metalio_charge_state() == METALIO_CHARGE_CHARGING;
}
