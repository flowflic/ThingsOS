// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "metalio_bsp.h"
#include "esp_err.h"
#include "thingsos/graphics.h"
#include "thingsos/system_monitor.h"
#include "thingsos/tilt.h"
/* Board-neutral touch sample: FocalTech 15-byte report plus the decoded primary contact. */
typedef metalio_touch_t thingsos_touch_t;
esp_err_t thingsos_board_start(void);
/* Main task only; false powers ODR down. Chip coordinates in mg; reader core maps screen orientation. */
bool thingsos_board_tilt_active(bool active);
bool thingsos_board_tilt_read_raw(int* x,int* y,int* z,uint64_t* sampled_ms);
void thingsos_board_tilt_configure(bool landscape,unsigned level);
bool thingsos_board_tilt_event(int* direction,uint64_t* at,thingsos_tilt_t* state);
esp_err_t thingsos_board_present(const thingsos_canvas_t* canvas);
void thingsos_board_static_page(bool enabled);
/* Full 48-phase all-pixel ripple for lock entry/wake, independent of reader mode. */
esp_err_t thingsos_board_present_lock_transition(const thingsos_canvas_t* canvas,bool forward);
/* Control sheet open/adjust/close: reading-style white maintenance, defer the
   periodic clean until the gesture is complete. Explicit refresh still wins. */
esp_err_t thingsos_board_present_controls(const thingsos_canvas_t* canvas);
esp_err_t thingsos_board_present_reader(const thingsos_canvas_t* canvas);
esp_err_t thingsos_board_present_reader_image(const thingsos_canvas_t* canvas);
esp_err_t thingsos_board_present_reader_ui(const thingsos_canvas_t* canvas);
esp_err_t thingsos_board_present_reader_background(const thingsos_canvas_t* canvas);
/* Live metrics: grayscale changed-row maintenance; unchanged pixels hold. */
esp_err_t thingsos_board_present_metrics(const thingsos_canvas_t* canvas);
/* Main-task snapshot: configured PCLK and last successful present duration. */
void thingsos_board_display_stats(thingsos_system_display_t* out);
esp_err_t thingsos_board_present_reader_stagger(const thingsos_canvas_t* canvas,bool forward);
/* Main-thread resource handoff after packing, before any DMA panel scan. */
void thingsos_board_scan_barrier(void (*barrier)(void));
esp_err_t thingsos_board_present_keyboard(const thingsos_canvas_t* canvas);
esp_err_t thingsos_board_present_input(const thingsos_canvas_t* canvas);
/* Same single DU input scan, restricted to PIN feedback and submit bands. */
esp_err_t thingsos_board_present_pin_input(const thingsos_canvas_t* canvas);
esp_err_t thingsos_board_present_progress(const thingsos_canvas_t* canvas);
/* Binary audio counters/progress, portrait rows 280..700 only; no timer GC16. */
esp_err_t thingsos_board_present_audio(const thingsos_canvas_t* canvas);
esp_err_t thingsos_board_present_audio_diff(const thingsos_canvas_t* canvas);
/* Local 36-phase black/white maintenance only around a changing label, then
 * difference-only delivery of the rest of the frame. No full-window clear. */
esp_err_t thingsos_board_present_audio_diff_clean(const thingsos_canvas_t* canvas,int x,int y,int w,int h);
esp_err_t thingsos_board_present_status(const thingsos_canvas_t* canvas);
/* Main-task modal updates. Only this logical rectangle is submitted; a
   pending explicit clean/recovery still wins. Outputs are off on return. */
esp_err_t thingsos_board_present_loading(const thingsos_canvas_t*,int,int,int,int);
void thingsos_board_touch_buffer(bool enabled);
bool thingsos_board_touch_pending(void);
/* Real contacts/queued DOWNs, not sampler mutex contention. */
bool thingsos_board_touch_contact_pending(void);
bool thingsos_board_touch(thingsos_touch_t* touch, uint64_t* sampled_ms);
bool thingsos_board_font(thingsos_font_t* font);
unsigned thingsos_board_battery(void);
void thingsos_board_ascii(thingsos_canvas_t* canvas, const char* text, int x, int y);

void thingsos_board_healthy(void);
bool thingsos_board_time(uint32_t*);
esp_err_t thingsos_board_set_time(uint32_t);
void thingsos_board_full_refresh(void);
void thingsos_board_transition_refresh(void);
/* Next frame: drive equal pixels inside this portrait rectangle as well.
   No-op on this board: every non-reader frame already drives all pixels. */
void thingsos_board_cleanup_area(int x,int y,int width,int height);
void thingsos_board_menu_refresh(void);
void thingsos_board_rom_download(void);
/* Plain software restart into the normal boot path. */
void thingsos_board_restart(void);

void thingsos_board_refresh_policy(unsigned mode,unsigned every);
void thingsos_board_idle(void);

/* 0 none, 1 short press, 2 long press (held 3 s). */
unsigned thingsos_board_power_key(void);
/* Volume keys: 0 none, 1 previous page (Vol+), 2 next page (Vol-). */
unsigned thingsos_board_side_key(void);
/* BOOT/AI key: 1 on a short release, 0 otherwise. Held at power-on is ignored. */
unsigned thingsos_board_ai_key(void);
/* Finish the full GC16 ripple (or recovery clear/redraw) before powering down. */
esp_err_t thingsos_board_suspend(const thingsos_canvas_t* sleep_frame);
esp_err_t thingsos_board_resume(void);
void thingsos_board_sleep_wait(void);
/* Power IC shutdown; returns only on failure (e.g. still on USB power). */
esp_err_t thingsos_board_power_off(void);

const char* thingsos_board_charge(void);
/* True while the charger is actively filling the cell (not just plugged in). */
bool thingsos_board_charging(void);

/* Metalio frontlight: mode 0 off, 1 warm, 2 cool, 3 both; level 0..100. */
void thingsos_board_frontlight(unsigned mode,unsigned level);
void thingsos_board_frontlight_mix(bool enabled,unsigned level,unsigned warmth);
/* Vibration pulse; 0 ms is ignored. */
void thingsos_board_haptic(unsigned ms);
/* Suppress digit-revealing touch coordinates while a private keypad is open. */
void thingsos_board_private_input(bool enabled);

/* SD card ownership, shared by UI, network transfer, WeRead and USB disk. */
typedef metalio_sd_info_t thingsos_sd_info_t;
esp_err_t thingsos_board_sd_info(thingsos_sd_info_t* info);
esp_err_t thingsos_board_sd_remount(void);
esp_err_t thingsos_board_sd_sync(void);

#include "thingsos/usb_disk.h"
esp_err_t thingsos_board_usb_start(void);
void thingsos_board_usb_status(thingsos_usb_status_t*);
esp_err_t thingsos_board_usb_stop(void);
