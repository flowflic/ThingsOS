// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "cst836u.h"
#include "esp_err.h"
#include "thingsos/graphics.h"
#include "thingsos/tilt.h"
esp_err_t thingsos_board_start(void);
/* Main task only; false powers ODR down. Chip coordinates in mg; reader core maps screen orientation. */
bool thingsos_board_tilt_active(bool active);
bool thingsos_board_tilt_read_raw(int* x,int* y,int* z,uint64_t* sampled_ms);
void thingsos_board_tilt_configure(bool landscape,unsigned level);
bool thingsos_board_tilt_event(int* direction,uint64_t* at,thingsos_tilt_t* state);
esp_err_t thingsos_board_present(const thingsos_canvas_t* canvas);
esp_err_t thingsos_board_present_reader_ui(const thingsos_canvas_t* canvas);
esp_err_t thingsos_board_present_reader(const thingsos_canvas_t* canvas);
esp_err_t thingsos_board_present_loading(const thingsos_canvas_t* canvas);
esp_err_t thingsos_board_present_reader_background(const thingsos_canvas_t* canvas);
esp_err_t thingsos_board_present_reader_stagger(const thingsos_canvas_t* canvas,bool forward);
/* Main-thread resource handoff after packing, before any DMA panel scan. */
void thingsos_board_scan_barrier(void (*barrier)(void));
esp_err_t thingsos_board_present_keyboard(const thingsos_canvas_t* canvas);
esp_err_t thingsos_board_present_input(const thingsos_canvas_t* canvas);
esp_err_t thingsos_board_present_progress(const thingsos_canvas_t* canvas);
esp_err_t thingsos_board_present_status(const thingsos_canvas_t* canvas);
/* Boot-local count of aborted LCD updates and incomplete physical clears. Main task only. */
unsigned thingsos_board_scan_underruns(void);
uint32_t thingsos_board_last_underrun_seconds(void);
unsigned thingsos_board_panel_clock(void);
bool thingsos_board_panel_clock_latched(void);
unsigned thingsos_board_panel_clock_recoveries(void);
uint32_t thingsos_board_panel_clock_cooldown_s(void);
void thingsos_board_pclk_allow_recovery(bool allowed);
uint32_t thingsos_board_last_refresh_ms(void);
esp_err_t thingsos_board_present_metrics(const thingsos_canvas_t* canvas);
void thingsos_board_touch_buffer(bool enabled);
void thingsos_board_private_input(bool enabled);
bool thingsos_board_touch_pending(void);
/* Real contacts/queued DOWNs, not sampler mutex contention. */
bool thingsos_board_touch_contact_pending(void);
bool thingsos_board_touch(cst836u_touch_t* touch, uint64_t* sampled_ms);
bool thingsos_board_font(thingsos_font_t* font);
/* Valid PMU sample only. Connected includes active charging and charge complete. */
bool thingsos_board_power_status(unsigned* percent, bool* charge_connected);
void thingsos_board_ascii(thingsos_canvas_t* canvas, const char* text, int x, int y);

void thingsos_board_healthy(void);
bool thingsos_board_time(uint32_t*);
esp_err_t thingsos_board_set_time(uint32_t);
void thingsos_board_full_refresh(void);
void thingsos_board_transition_refresh(void);
/* Next frame: drive equal pixels inside this portrait rectangle as well. */
void thingsos_board_cleanup_area(int x,int y,int width,int height);
void thingsos_board_rom_download(void);

void thingsos_board_refresh_policy(unsigned mode,unsigned every);
void thingsos_board_idle(void);

unsigned thingsos_board_power_key(void);
/* Full GC16 ripple completes before powering down; resume animates its next frame. */
esp_err_t thingsos_board_suspend(const thingsos_canvas_t* sleep_frame);
esp_err_t thingsos_board_resume(void);
/* Next successful frame uses a full GC16 ripple in the unlock direction. */
void thingsos_board_unlock_refresh(void);
void thingsos_board_sleep_wait(void);

const char* thingsos_board_charge(void);

#include "thingsos/usb_disk.h"
esp_err_t thingsos_board_usb_start(void);
void thingsos_board_usb_status(thingsos_usb_status_t*);
esp_err_t thingsos_board_usb_stop(void);
