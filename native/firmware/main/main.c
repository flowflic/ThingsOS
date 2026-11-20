// SPDX-License-Identifier: Apache-2.0
#include "thingsos/mem.h"
#include "thingsos/diagnostics.h"
#include <ctype.h>
#include <errno.h>
static void reader_cache_drop_frames(void);
static void cover_retry_clear(void);
#ifdef THINGSOS_HOST_UI
#include "../../tests/device_ui_stubs.h"
#else
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include "board.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_psram.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_attr.h"
#include "esp_app_desc.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/idf_additions.h"
#include <stdatomic.h>
#include "network.h"
#include "weidu_device.h"
#include "system_monitor_device.h"
#include "network_probe.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "qrcodegen.h"
#include "board.h"
#include "thingsos/pagination.h"
#endif
#include <limits.h>
#include <errno.h>
#include "qrcodegen.h"
/* Bring-up tracing for the reader-settings/jump-overlay stall. Off by default;
   build with -DTHINGSOS_BRINGUP_TRACE=1 to see the stage markers on the console. */
#ifndef THINGSOS_BRINGUP_TRACE
#define THINGSOS_BRINGUP_TRACE 0
#endif
#if THINGSOS_BRINGUP_TRACE && !defined(THINGSOS_HOST_UI)
#define WHERE(...) ESP_LOGI("ThingsOS_where",__VA_ARGS__)
#else
#define WHERE(...) ((void)0)
#endif
#include "thingsos/files.h"
#include "thingsos/ima.h"
#include "thingsos/cubox.h"
#include "thingsos/graphics.h"
#include "thingsos/ime.h"
#include "thingsos/input.h"
#include "thingsos/keys.h"
#include "thingsos/manage.h"
#include "thingsos/pagination.h"
#include "thingsos/reader.h"
#include "thingsos/reader_ui.h"
#include "thingsos/tilt.h"
#include "thingsos/runtime.h"
#include "thingsos/stats.h"
#include "thingsos/store.h"
#include "thingsos/ui.h"
#ifndef THINGSOS_HOST_UI
#include "cJSON.h"
#endif
#include "thingsos/audio.h"
#include "thingsos/bluetooth.h"
#include "thingsos/ble_discovery.h"
#include "thingsos/thumbnail_cache.h"
#include "thingsos/cover_cache.h"
#include "thingsos/weidu_reading.h"
#include "thingsos/annotations.h"
#include "thingsos/weidu_mp.h"
#include "thingsos/markdown.h"
#include "lock_pin.h"
#define BOOK_LIMIT THINGSOS_RECORD_BOOKS
/* A failed panel refresh is retried this many times (with a forced full redraw)
   before the device gives up and shows the recovery screen. */
#define REFRESH_RETRY_LIMIT 3
typedef enum {
    RECORD_NOTICE_NONE, RECORD_NOTICE_PREPARING, RECORD_NOTICE_STARTED,
    RECORD_NOTICE_SAVING, RECORD_NOTICE_SAVED, RECORD_NOTICE_START_ERROR,
    RECORD_NOTICE_SAVE_ERROR, RECORD_NOTICE_ERROR
} record_notice_t;
typedef struct {
    char name[192], author[160], path[THINGSOS_RECORD_PATH_BYTES];
    unsigned progress;
    bool epub;
    uint64_t id;
} book_t;
typedef struct {
    thingsos_runtime_t runtime;
    thingsos_ui_page_t page;
    thingsos_font_t font, ui_font;
    unsigned font_retries, ui_font_retries;
    thingsos_canvas_t canvas;
    book_t books[BOOK_LIMIT];
    size_t book_count, list_start, selected;
    bool library_valid, library_order_valid;
    bool weidu_progress_refresh, weidu_deferred;
    thingsos_weidu_action_t weidu_deferred_action;
    thingsos_weidu_book_t weidu_deferred_book;
    uint64_t library_card;
    char library_path[THINGSOS_RECORD_PATH_BYTES];
    size_t library_indices[BOOK_LIMIT];
    uint64_t library_orders[BOOK_LIMIT], library_ids[BOOK_LIMIT];
    size_t library_order_count;
    bool library_order_name;
    thingsos_thumbnail_cache_t thumbnails;
    unsigned cover_load_ms, cover_decode_ms, cover_scale_ms, cover_loads;
    unsigned ui_action_id, ui_action_ms;
    uint64_t ui_action_at;
    bool reader_background_refresh;
    bool library_name_sort, library_cards, library_weidu, library_order_weidu;
    /* Locally hidden books: long-press toggles, and the header control reveals
       them again so they can be restored. Stored beside the reading records. */
    char library_query[97],weidu_query[97];
    char reader_cover_path[320];
    bool wifi_scanned, wifi_symbols_more, wifi_requires_password;
    bool wifi_scan_pending, wifi_scan_running, wifi_scan_valid;
    uint64_t wifi_scan_at;
    uint64_t recent_ids[BOOK_LIMIT];
    size_t recent_count;
    thingsos_epub_t epub;
    thingsos_chapter_t chapter;
    bool epub_open, opened, menu, dirty, keyboard_refresh, input_refresh;
    thingsos_page_position_t* pages;
    size_t page_count;
    bool pagination_valid;
    unsigned indexed_px, indexed_line, indexed_margin, indexed_weight, indexed_indent;
    bool indexed_landscape,indexed_monochrome,indexed_immersive,indexed_vertical;
    thingsos_font_t preview_font;
    bool font_from_reader;
    int font_candidate;
    char font_path[320], ui_font_path[320], preview_path[320];
    unsigned refresh_pages, refresh_mode, reading_frames, auto_lock_minutes;
    unsigned ui_px, ui_weight, ui_refresh_pages, ui_refresh_mode;
    unsigned lock_wallpaper;
    lock_pin_state_t pin;
    book_t wallpaper_book;
    char wallpaper_image[THINGSOS_RECORD_PATH_BYTES];
    unsigned wallpaper_step, wallpaper_kind;
    size_t wallpaper_start;
    book_t wallpaper_candidate;
    char wallpaper_candidate_image[THINGSOS_RECORD_PATH_BYTES];
    bool wallpaper_preview_ok;
    bool lock_snapshot_valid;
    bool force_refresh, transition_refresh, controls_refresh, auto_time, hotspot, service_on, service_pinned, asleep;
    bool transfer_music;
    unsigned refresh_retries;
    uint64_t activity_at;
    uint64_t reader_ready_at;
    uint64_t message_until;
    char file_query[96];
    bool file_more,manage_single_delete,reader_from_files;
    bool file_action_open,cache_names_migrated;
    size_t file_action_index;
    char file_action_path[THINGSOS_RECORD_PATH_BYTES];
    size_t file_return_start;
    bool weidu_covers_pending, weidu_covers_redraw;
    int64_t weidu_covers_retry_at;
    int shelf_cover_x,shelf_cover_y,shelf_cover_w,shelf_cover_h;
    thingsos_ui_page_t lock_return;
    thingsos_ui_page_t menu_root,reader_menu_root;
    unsigned confirm_action, stats_period;
    bool progress_dirty, time_edit;
    struct tm time_preview;
    size_t start, end, history[4096], history_count;
    bool pagination_started, pagination_failed;
    unsigned pagination_chapter;
    size_t pagination_offset, pagination_capacity;
    size_t pagination_done_pages;
    unsigned pagination_saved_chapters;
    uint64_t pagination_checkpoint_at;
    thingsos_chapter_t* pagination_text;
    thingsos_text_cursor_t pagination_cursor;
    const char* pagination_error;
    uint32_t progress_weights[THINGSOS_SPINE_LIMIT];
    uint64_t progress_total;
    uint64_t page_cache_key;
    char page_cache_path[600];
    struct {
        size_t index;
        int x, y, w, h;
    } image_hits[THINGSOS_IMAGE_LIMIT];
    size_t image_hit_count;
    thingsos_text_hits_t text_hits;
    thingsos_annotations_t* annotations;
    thingsos_annotation_t annotation_edit;
    thingsos_text_underline_t annotation_lines[WD_DRAW_LIMIT+THINGSOS_ANNOTATION_LIMIT];
    bool annotation_open,annotation_list,annotation_selecting,annotation_dragging,annotation_loaded,annotation_remote;
    bool annotation_delete_confirm;
    bool annotation_delete_line;
    bool annotation_manual,annotation_auto_job;
    uint64_t annotation_auto_after;
    unsigned annotation_cloud_indices[WD_NOTE_LIMIT];
    bool annotation_storage_error;
    bool annotation_from_notes;
    char annotation_remote_id[64];
    size_t annotation_begin,annotation_end,annotation_anchor,annotation_anchor_end,annotation_page,annotation_next;
    unsigned annotation_list_start;
    unsigned annotation_cloud_count;
    unsigned annotation_status_serial;
    bool annotation_batch,annotation_crc_valid;
    uint32_t annotation_text_crc;
    /* Marker boxes belong to one drawn page: chapter and byte offset must still
       match, or a turn would expose the previous page's targets. */
    size_t text_hits_chapter, text_hits_start;
    /* Footnote window: index into the chapter's footnotes, byte offset of the
       note page on screen and where the next page starts. */
    bool footnote_open;
    size_t footnote_index, footnote_offset, footnote_next;
    int footnote_card_height;
    unsigned chapter_index, px, line_choice, margin_choice, weight_choice, indent_chars;
    bool landscape_enabled,immersive_enabled,vertical_enabled;
    bool tilt_enabled;
    bool ai_key_enabled;
    unsigned tilt_sensitivity;
    /* Metalio frontlight: mode 0 off, 1 warm, 2 cool, 3 both; level 10..100. */
    unsigned light_mode, light_level;
    unsigned light_warmth,light_last_mode,audio_volume;
    bool wifi_enabled,controls_open,controls_wifi_confirm,wifi_pending_off,audio_progress;
    thingsos_ui_page_t app_return,network_return;
    thingsos_audio_status_t audio;
    bool haptic_enabled, light_page;
    thingsos_tilt_t tilt;
    uint64_t tilt_poll_at;
    bool ripple_enabled, ripple_pending, ripple_forward;
    bool lock_wave_pending,lock_wave_forward;
    bool jump;
    bool toc_pending, toc_follow_current,toc_invalidate_pending;
    thingsos_toc_entry_t toc_draw_entries[6];
    size_t toc_draw_count;
    unsigned jump_percent;
    bool jump_pending, jump_by_percent;
    size_t jump_target;
    uint64_t jump_book, jump_layout, jump_started_at, jump_progress_at;
    bool stats_from_reader;
    thingsos_image_view_t image_view;
    size_t image_index;
    unsigned char* image_pixels;
    unsigned image_w, image_h;
    char message[192], fonts[12][192];
    uint64_t volume_overlay_until;
    bool volume_overlay_pending, volume_overlay_drawn;
    record_notice_t record_notice;
    uint64_t record_notice_until;
    bool record_start_pending, record_save_pending, feedback_refresh;
    char record_error[128];
    size_t font_count;
    bool sd_seen, board_ok, font_ok;
    unsigned battery;
    bool charging;
    uint64_t charge_checked;
    uint64_t saved_at;
    thingsos_records_t records;
    thingsos_browser_t browser;
    thingsos_input_t input;
    char manage_paths[BOOK_LIMIT][THINGSOS_RECORD_PATH_BYTES], rename_name[97];
    bool manage_selected[BOOK_LIMIT], delete_confirm;
    size_t manage_count, manage_target;
    thingsos_ui_page_t manage_return;
    uint32_t stats_first, stats_last;
    size_t stats_count;
    bool wifi_join_after;
    thingsos_manage_job_t network_job;
    unsigned transfer_serial, transfer_progress;
    thingsos_transfer_state_t transfer_displayed;
    char read_title[192], read_path[THINGSOS_RECORD_PATH_BYTES], wifi_ssid[33], wifi_password[64];
    bool records_dirty, records_bound, records_blocked;
    uint64_t accounted_ms;
    unsigned editing;
    thingsos_ui_page_t input_return;
    thingsos_wifi_result_t wifi_networks[12];
    size_t wifi_count;
    bool stats_by_day, wifi_connected;
    thingsos_weidu_status_t weidu;
    thingsos_weidu_book_t weidu_books[9];
    bool weidu_shelf_valid;
    bool weidu_cards;
    size_t weidu_count,weidu_total,weidu_start,weidu_selected;
    bool weidu_shelf_member[9];
    bool weidu_detail,weidu_logout_confirm,reader_from_weidu,reader_from_cubox;
    char cubox_query[97];
    thingsos_ui_page_t weidu_detail_return;
    size_t weidu_detail_return_start;
    unsigned mp_mode; /* 0 books, 1 followed accounts, 2 latest articles */
    bool mp_open_pending;
    thingsos_weidu_book_t mp_account;
    /* Short-lived error banner: scoped to the page that raised it and expired by
       time instead of sticking above the whole WeRead shelf. */
    char weidu_banner[128];
    uint64_t weidu_banner_at;
    bool weidu_banner_mp;
    thingsos_weidu_action_t weidu_action;
    uint64_t wifi_requested, clock_refreshed;
    char wd_id[64], wd_notes_id[64], wd_owner[33];
    bool wd_pull_on, wd_push_on, wd_time_on, wd_time_confirmed, wd_consent;
    bool wd_rt_on,wd_rt_session,wd_rt_panel,wd_rt_auth_pending,wd_rt_details;
    bool wd_active, wd_pull_pending, wd_cloud_prompt, wd_sync_panel, wd_notes_open, wd_notes_reader;
    bool wd_long_open, wd_map_attempted, wd_init_pending, wd_local_failure,wd_enter_pending;
    uint64_t wd_started,wd_accounted,wd_report_after;
    uint64_t wd_enter_retry_at;
    unsigned wd_enter_attempts;
    unsigned wd_power_action,wd_power_stage;
    uint64_t wd_power_deadline;
    size_t wd_long_end;
    unsigned wd_serial, wd_kind, wd_note_start, wd_long_page, wd_mapped_chapter;
    thingsos_wd_device_status_t wd_status;
    thingsos_wd_notes_info_t wd_info;
    thingsos_wd_note_t wd_note;
    thingsos_wd_map_t wd_map;
    thingsos_text_underline_t wd_lines[WD_DRAW_LIMIT];
    size_t wd_line_count,wd_draw_total;
    bool wd_thought_open,wd_thought_loading,wd_thought_cached,wd_thought_full;
    thingsos_wd_thought_query_t wd_thought_query;
    thingsos_wd_thought_info_t wd_thought_info;
    thingsos_weidu_error_t wd_thought_error;
    struct { uint32_t idx; char sync[40]; } wd_thought_cursor[16];
    unsigned wd_thought_page,wd_thought_row;
    char wd_thought_quote[256];
    uint32_t wd_thought_heat;
    thingsos_wd_upload_t wd_last;
    bool wd_last_valid;
    bool wd_was_online;
    bool wd_session_moved, wd_cloud_cancelled;
    unsigned wd_anchor_chapter;
    size_t wd_anchor_start;
    uint32_t wd_local_updated;
    char wd_cloud_book[64];
    thingsos_wd_progress_t wd_cloud_progress;

    size_t stats_indices[THINGSOS_RECORD_BOOKS];
} app_t;
static app_t* state;
#define app (*state)
/* UI glyphs own their face/cache; pagination always borrows app.font only.
   Preserve the existing scoped UI sizing/weight policy across both faces. */
static thingsos_font_t* ui_font(void) {
    thingsos_font_t* f=app.ui_font.face?&app.ui_font:&app.font;
    if(f!=&app.font){f->ui_size_percent=app.font.ui_size_percent;f->ui_weight_override=app.font.ui_weight_override;f->dither=app.font.dither;thingsos_font_weight(f,app.font.weight);}
    return f;
}
static thingsos_font_t* font_setting_face(void){return app.font_from_reader?&app.font:&app.ui_font;}
static char* font_setting_path(void){return app.font_from_reader?app.font_path:app.ui_font_path;}
static void memory_log(const char* stage);
static void feature_save(void);
static void feature_opened(void);
static bool pagination_prepare(void);
static bool reader_surface(thingsos_ui_page_t page);
static bool reader_ui_active(void);
static bool alarm_active(void);
static void feature_mount(void);
static void feature_account(void);
static bool feature_draw(void);
static void draw(void);
static void usb_mode_run(void);
static bool usb_begin_requested, usb_end_requested, startup_healthy;
static bool alignment_draw(void);
static bool alignment_tap(int, int);
static void alignment_confirm_draw(void);
static bool preferences_save(void);
static void wallpaper_book_set(book_t*,const char*,const char*);
static bool wallpaper_image_path(const char*);
static bool wallpaper_tap(int,int);
static bool wallpaper_hold(int,int,uint64_t);
static void progress_save(void);
static void progress_load(void);
static bool feature_tap(int, int);
static bool management_draw(void);
static bool management_tap(int, int);
static bool management_hold(int, int);
static bool transfer_blocks_ui(void);
static bool management_rename(void);
static void feature_editor(unsigned);
static bool input_uses_qwerty(void);
static void wifi_password_update(bool layout);
static void wifi_scan(bool force);
static size_t feature_book(uint64_t);
static void feature_toggle_mark(void);
static time_t wall_time(void);
static void navigate(thingsos_ui_page_t);
static void tap(int,int);
static void power_action(unsigned);
static void pin_clear_input(void);
static void pin_lock(void);
static void pin_load(void);
static void pin_wake(void);
static void pin_open(lock_pin_purpose_t);
static void pin_cancel(void);
static void qr_address(const char*);
static void qr_address_box(const char*,int,int);
static uint64_t now(void) {
    return (uint64_t)esp_timer_get_time() / 1000;
}
static bool cache_path_identity(const char* s,char* canonical,size_t cap) {
    return thingsos_weidu_cache_identity(s,canonical,cap);
}
static uint64_t identity(const char* s) {
    thingsos_path_mapper(cache_path_identity);
    return thingsos_path_id(s);
}
static void background_redraw(void);
static void feedback_redraw(void);
#define VOLUME_OVERLAY_HOLD_MS 3000
static void volume_overlay_presented(bool success) {
    /* Start the idle interval after the adjusted volume actually reaches the
       panel. Unrelated music/status redraws must not keep extending it. */
    if(success && app.volume_overlay_pending && app.volume_overlay_drawn) {
        app.volume_overlay_until=now()+VOLUME_OVERLAY_HOLD_MS;
        app.volume_overlay_pending=false;
    }
}
static void say(const char* s) {
    /* Transient notice: hide it after 3 s. A caller that needs a different
       lifetime can override message_until after this call. */
    app.message_until=now()+3000;
    strlcpy(app.message, s, sizeof(app.message));
    app.dirty = true;
}
static void message_expire_poll(void) {
    if(app.message_until && now()>=app.message_until) {
        bool visible=app.message[0]!=0;
        app.message_until=0;app.message[0]=0;
        if(visible)background_redraw();
    }
    if(app.controls_open || app.asleep || app.page==THINGSOS_UI_LOCK)app.volume_overlay_pending=false;
    if(app.volume_overlay_until && !app.volume_overlay_pending && now()>=app.volume_overlay_until) {
        app.volume_overlay_until=0;feedback_redraw();
    }
    if(app.record_notice_until && now()>=app.record_notice_until) {
        app.record_notice_until=0;app.record_notice=RECORD_NOTICE_NONE;feedback_redraw();
    }
}
static void background_redraw(void) {
    /* Never downgrade a foreground redraw already queued by input. */
    if(!app.dirty && app.page==THINGSOS_UI_READING)app.reader_background_refresh=true;
    app.dirty=true;
}
static void feedback_redraw(void) {
    /* A global overlay must not use an audio-progress-only or sheet-only mask. */
    app.audio_progress=false;app.feedback_refresh=true;background_redraw();
}
static void record_feedback_show(record_notice_t notice,const char* error) {
    app.record_notice=notice;
    app.record_notice_until=notice==RECORD_NOTICE_STARTED||notice==RECORD_NOTICE_SAVED?now()+3000:0;
    strlcpy(app.record_error,error?error:"",sizeof(app.record_error));
    app.message[0]=0;app.message_until=0;feedback_redraw();
}
static void record_feedback_poll(const thingsos_audio_status_t* current) {
    if(current->state==THINGSOS_AUDIO_ERROR &&
       (app.record_start_pending||app.record_save_pending||
        app.audio.state==THINGSOS_AUDIO_RECORDING||app.audio.state==THINGSOS_AUDIO_RECORD_PAUSED)) {
        record_notice_t failure=app.record_save_pending?RECORD_NOTICE_SAVE_ERROR:
            app.record_start_pending?RECORD_NOTICE_START_ERROR:RECORD_NOTICE_ERROR;
        app.record_start_pending=app.record_save_pending=false;
        record_feedback_show(failure,current->message);
    } else if(app.record_start_pending &&
              (current->state==THINGSOS_AUDIO_RECORDING||current->state==THINGSOS_AUDIO_RECORD_PAUSED)) {
        app.record_start_pending=false;record_feedback_show(RECORD_NOTICE_STARTED,NULL);
    } else if(app.record_save_pending && current->state==THINGSOS_AUDIO_IDLE) {
        /* IDLE is published by the worker only after WAV sync and close succeed. */
        app.record_save_pending=false;record_feedback_show(RECORD_NOTICE_SAVED,NULL);
    } else if((app.record_start_pending||app.record_save_pending) &&
              (current->state==THINGSOS_AUDIO_IDLE||current->state==THINGSOS_AUDIO_PLAYING||
               current->state==THINGSOS_AUDIO_PLAY_PAUSED)) {
        /* USB/sleep can quiesce a start before it ever reaches RECORDING.
           A superseded command must not leave an undismissable preparing card. */
        record_notice_t failure=app.record_save_pending?RECORD_NOTICE_SAVE_ERROR:RECORD_NOTICE_START_ERROR;
        app.record_start_pending=app.record_save_pending=false;
        record_feedback_show(failure,"音频任务已切换，请检查录音文件");
    }
}
static bool media(void) {
    thingsos_sd_info_t info;
    return thingsos_board_sd_info(&info) == ESP_OK && info.mounted;
}
static void save_state(void) {
    feature_save();
}
static void wd_finish(void);
static void wd_opened(void);
static void wd_forget_map(void);
static void reader_cache_clear(void);
static void reader_annotations_close(void);
static void pagination_checkpoint(void);
static void reader_image_cache_clear(void);
static bool online_open(const thingsos_weidu_book_t*);
static void online_need(unsigned);
static void online_loaded(unsigned);
static void online_close(void);
static void online_poll(void);
static bool online_pending(void);
static bool online_modal_visible(void);
static void wd_prepare(void);
static void release_book(void) {
    online_close();
    app.toc_pending=app.toc_invalidate_pending=false;
    app.jump_pending=false;
    reader_annotations_close();
    pagination_checkpoint();
    wd_finish();wd_forget_map();
    thingsos_decode_release();
    reader_image_cache_clear();
    reader_cache_clear();
    app.ripple_pending=false;
    save_state();
    thingsos_chapter_close(&app.chapter);
    if (app.epub_open) thingsos_epub_close(&app.epub);
    app.epub_open = false;
    app.opened = false;
    app.progress_total = 0;
    free(app.image_pixels);
    app.image_pixels = NULL;
    app.image_view.open = false;
    app.history_count = 0;
    free(app.pages);
    app.pages = NULL;
    app.page_count = 0;
    app.pagination_valid = false;
    app.pagination_started = app.pagination_failed = false;
    app.pagination_done_pages=app.pagination_saved_chapters=0;
    memset(&app.pagination_cursor,0,sizeof(app.pagination_cursor));
    if (app.pagination_text) {
        thingsos_chapter_close(app.pagination_text);
        free(app.pagination_text);
        app.pagination_text = NULL;
    }
}
static bool library_cached_book(const book_t* book) {
    char id[64];return book->epub && thingsos_weidu_cache_id(book->path,id,sizeof(id));
}
static size_t library_visible_count(void) {
    size_t count=0;for(size_t i=0;i<app.book_count;++i)count+=(library_cached_book(&app.books[i])==app.library_weidu && (!app.library_weidu || !app.library_query[0] || strstr(app.books[i].name,app.library_query)));
    return count;
}
static void scan_directory(const char* root,bool cached_only) {
    DIR* d=opendir(root);if(!d)return;
    struct dirent* entry;
    while ((entry = readdir(d)) && app.book_count < BOOK_LIMIT) {
        const char* ext = strrchr(entry->d_name, '.');
        if (!ext || entry->d_name[0] == '.' ||
            (strcasecmp(ext, ".txt") && strcasecmp(ext, ".epub") && !thingsos_markdown_path(entry->d_name)) || strlen(entry->d_name) >= 192)
            continue;
        char cache_id[64],candidate[512];snprintf(candidate,sizeof(candidate),"%s/%s",root,entry->d_name);
        if(cached_only && !thingsos_weidu_cache_id(candidate,cache_id,sizeof(cache_id)))continue;
        if(cached_only) {
            bool duplicate=false;char existing[64];
            for(size_t i=0;i<app.book_count;++i)
                if(thingsos_weidu_cache_id(app.books[i].path,existing,sizeof(existing)) && !strcmp(existing,cache_id))duplicate=true;
            if(duplicate)continue;
        }
        book_t* b = app.books + app.book_count;
        memset(b, 0, sizeof(*b));
        size_t prefix = strlen(root), length = strlen(entry->d_name);
        if (prefix + length + 2 > sizeof(b->path)) continue;
        memcpy(b->path, root, prefix);
        b->path[prefix] = '/';
        memcpy(b->path + prefix + 1, entry->d_name, length + 1);
        struct stat s;
        if (stat(b->path, &s) || !S_ISREG(s.st_mode)) continue;
        strlcpy(b->name, entry->d_name, sizeof(b->name));
        b->epub = !strcasecmp(ext, ".epub");
        b->id = identity(b->path);
        if (b->epub) {
            thingsos_epub_t* metadata = thingsos_psram_calloc(1, sizeof(*metadata));
            if (metadata && thingsos_epub_open(metadata, b->path)) {
                if (strcmp(metadata->title, "未命名书籍"))
                    strlcpy(b->name, metadata->title, sizeof(b->name));
                strlcpy(b->author, metadata->author, sizeof(b->author));
                thingsos_epub_close(metadata);
            }
            free(metadata);
        }
        thingsos_book_record_t* record = thingsos_records_book(&app.records, b->id, NULL, NULL);
        if(record && strcmp(record->path,b->path) && thingsos_weidu_cache_id(b->path,cache_id,sizeof(cache_id))) {
            strlcpy(record->path,b->path,sizeof(record->path));app.records_dirty=true;
        }
        if (record && !strcmp(record->title, entry->d_name))
            strlcpy(b->name, record->title, sizeof(b->name));
        app.book_count++;
    }
    closedir(d);
}
static void scan(void) {
    reader_cache_clear();uint64_t started=now();
    cover_retry_clear();
    thingsos_thumbnail_clear(&app.thumbnails);
    app.weidu_shelf_valid=false;
    app.library_valid=app.library_order_valid=false;app.book_count=app.list_start=0;
    if(!media())return;
    mkdir("/sdcard/books",0775);mkdir("/sdcard/fonts",0775);
    if(!app.cache_names_migrated) {
        unsigned renamed=thingsos_weidu_cache_names_migrate("/sdcard/books");app.cache_names_migrated=true;
        ESP_LOGI("ThingsOS_weidu","Cache filenames migrated=%u",renamed);
    }
    scan_directory(app.records.library_path,false);
    if(strcmp(app.records.library_path,"/sdcard/books"))scan_directory("/sdcard/books",true);
    app.library_card = app.records.card_id;
    strlcpy(app.library_path, app.records.library_path, sizeof(app.library_path));
    app.library_valid = true;
    ESP_LOGI("ThingsOS_perf", "Library scan: %u ms books=%u", (unsigned)(now()-started), (unsigned)app.book_count);
}
static void library_load(void) {
    if (app.library_valid && media() && app.library_card == app.records.card_id &&
        !strcmp(app.library_path, app.records.library_path)) return;
    scan();
}
static const char* font_display_name(const char* filename) {
    /* Legacy SD filenames: display aliases only, never change saved paths. */
    static const struct { const char* file; const char* name; } aliases[] = {
        {"FZPingXYSJW.TTF", "方正屏显雅宋"},
        {"FZS3JW.TTF", "方正宋三"},
        {"FZSongYJW.TTF", "方正宋圆"},
        {"xiawuxinzhisong.ttf", "霞鹜新致雅宋"},
    };
    for (size_t i=0;i<sizeof(aliases)/sizeof(aliases[0]);++i)
        if (!strcasecmp(filename,aliases[i].file)) return aliases[i].name;
    return filename;
}
static void font_scan(void) {
    WHERE("font_scan start");
    app.font_count = 0;
    if (!media()) {WHERE("font_scan no media");return;}
    DIR* d = opendir("/sdcard/fonts");
    if (!d) {WHERE("font_scan no dir");return;}
    struct dirent* e;
    while ((e = readdir(d)) && app.font_count < 12) {
        const char* ext = strrchr(e->d_name, '.');
        if (ext && !strcasecmp(ext, ".ttf") && strlen(e->d_name) < 192)
            strlcpy(app.fonts[app.font_count++], e->d_name, 192);
    }
    closedir(d);
    WHERE("font_scan end count=%u",app.font_count);
}
static size_t recent_book(size_t position) {
    if (position >= app.recent_count) return app.book_count;
    return feature_book(app.recent_ids[position]);
}
static bool reader_chapter_load(unsigned index,thingsos_chapter_t* next) {
    if(!thingsos_epub_chapter(&app.epub,index,next))return false;
    /* Restore the anchor for text-only WeRead caches when the shelf cover is available. */
    if(index==0 && app.epub.weidu_cache && app.reader_cover_path[0] && !next->image_count &&
       thingsos_weidu_cover_chapter(app.epub.chapter_names[index])) {
        unsigned width,height;
        if(!thingsos_file_image_info(app.reader_cover_path,&width,&height))return true;
        char* text=thingsos_psram_realloc(next->text,next->bytes+2);
        if(!text)return false;
        next->text=text;
        next->images[0].offset=next->bytes;
        strcpy(next->images[0].path,"@weidu-cover");strcpy(next->images[0].alt,"封面");
        next->image_count=1;next->cover_image=1;next->text[next->bytes++]='\n';next->text[next->bytes]=0;
    }
    return true;
}
static bool load_chapter_content(unsigned index, bool refresh_images) {
    if (!app.epub_open) return false;
    thingsos_chapter_t* next = thingsos_psram_calloc(1, sizeof(*next));
    if (!next) return false;
    if (!media() || !reader_chapter_load(index, next)) {
        thingsos_chapter_close(next);
        free(next);
        if(app.epub.weidu_online && !refresh_images)online_need(index);
        return false;
    }
    if(refresh_images && next->image_count<=app.chapter.image_count) {
        thingsos_chapter_close(next);
        free(next);
        return false;
    }
    /* A sliced index may have borrowed the live chapter. Restart that page
       before replacing its text; an owned pagination chapter remains valid. */
    if(!app.pagination_text)memset(&app.pagination_cursor,0,sizeof(app.pagination_cursor));
    wd_forget_map();
    thingsos_chapter_close(&app.chapter);
    app.chapter = *next;
    free(next);
    app.chapter_index = index;
    if(app.epub.weidu_online)online_loaded(index);
    app.annotation_crc_valid=false;
    /* Support signal: a chapter with note links reports how many resolved.
       Silence here means the chapter carries no href="#..." footnote anchors. */
    if (app.chapter.footnote_count || app.chapter.underline_count) {
        unsigned resolved = 0;
        for (size_t i = 0; i < app.chapter.footnote_count; ++i)
            if (app.chapter.footnotes[i].note_bytes) ++resolved;
        ESP_LOGI("ThingsOS_reader", "Footnotes: chapter=%u refs=%u resolved=%u", index,
                 (unsigned)app.chapter.footnote_count, resolved);
    }
    app.history_count = 0;
    app.start = 0;
    return true;
}
static bool load_chapter(unsigned index) {
    return load_chapter_content(index,false);
}
/* Persist original spine/absolute text positions so TOC improvements preserve
   existing reading positions and bookmarks across firmware updates. */
static bool reader_restore_source(unsigned source,size_t offset) {
    if(!app.epub_open) {
        if(source || offset>app.chapter.bytes)return false;
        app.start=offset;return true;
    }
    if(app.epub.plain_text && app.epub.text_encoding==1) {
        if(source || offset>app.epub.zip.size)return false;
        size_t lo=0,hi=app.epub.spine_count;
        while(lo+1<hi){size_t mid=(lo+hi)/2;if(app.epub.spine[mid]<=offset)lo=mid;else hi=mid;}
        if(!load_chapter((unsigned)lo))return false;
        app.start=offset-app.chapter.source_start;return app.start<=app.chapter.bytes;
    }
    if(app.chapter.text && app.chapter_index<app.epub.spine_count &&
       app.epub.chapter_sources[app.chapter_index]==source && offset>=app.chapter.source_start &&
       offset<=app.chapter.source_start+app.chapter.bytes &&
       !(offset==app.chapter.source_start+app.chapter.bytes && app.chapter_index+1<app.epub.spine_count &&
         app.epub.chapter_sources[app.chapter_index+1]==source)) {
        app.start=offset-app.chapter.source_start;return true;
    }
    for(size_t i=0;i<app.epub.spine_count;++i) {
        if(app.epub.chapter_sources[i]!=source)continue;
        if(!load_chapter((unsigned)i))return false;
        if(offset==app.chapter.source_start+app.chapter.bytes && i+1<app.epub.spine_count && app.epub.chapter_sources[i+1]==source)continue;
        if(offset>=app.chapter.source_start && offset<=app.chapter.source_start+app.chapter.bytes) {
            app.start=offset-app.chapter.source_start;return true;
        }
    }
    return false;
}
static bool open_book(size_t index) {
    app.reader_from_files=false;
    uint64_t open_at=now(),released_at,content_at,records_at;
    app.reader_from_weidu=false;
    app.reader_from_cubox=false;
#ifndef THINGSOS_HOST_UI
    ESP_LOGI("ThingsOS_reader","Open book: internal DMA largest=%u, PSRAM free=%u",
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
#endif
    thingsos_runtime_overlay(&app.runtime, true, now());
    release_book();
    app.reader_ready_at=0;released_at=now();
    app.selected = index;
    app.chapter_index = 0;
    app.start = 0;
    app.history_count = 0;
    if (index == app.book_count) {
        size_t bytes;
        const unsigned char* text = thingsos_example_text(&bytes);
        app.chapter.text = thingsos_psram_alloc(bytes + 1);
        if (!app.chapter.text) return false;
        memcpy(app.chapter.text, text, bytes);
        app.chapter.text[bytes] = 0;
        app.chapter.bytes = bytes;
        app.runtime.location = (thingsos_location_t){ 1, 0, 0 };
    } else {
        book_t* b = app.books + index;
        if (!media()) return false;
        app.runtime.location = (thingsos_location_t){ b->id, 0, 0 };
        unsigned restored_chapter=0,restored_offset=0;
        char pos[128];
        snprintf(pos,sizeof(pos),"/sdcard/.thingsos/%016llx.pos",(unsigned long long)b->id);
        FILE* position=fopen(pos,"rb");
        if(position) {
            if(fscanf(position,"THINGSOS1 %u %u",&restored_chapter,&restored_offset)!=2)
                restored_chapter=restored_offset=0;
            fclose(position);
        }
        if (b->epub) {
            app.reader_cover_path[0]=0;
            char cache_id[64];
            if(thingsos_weidu_cache_id(b->path,cache_id,sizeof(cache_id)))
                snprintf(app.reader_cover_path,sizeof(app.reader_cover_path),"/sdcard/.thingsos/weidu/covers/%s.jpg",cache_id);
            if (!thingsos_epub_open(&app.epub, b->path)) return false;
            app.epub_open = true;
            thingsos_epub_navigation_fast(&app.epub);
            if (!reader_restore_source(restored_chapter,restored_offset) && !load_chapter(0)) {
                release_book();
                return false;
            }
            if(app.epub.weidu_online && app.start<app.chapter.bytes)
                while(app.start && ((unsigned char)app.chapter.text[app.start]&0xc0)==0x80)--app.start;
        } else {
            bool segmented=false;
            if(!thingsos_markdown_path(b->path)) {
                char text_index[640];
#ifdef THINGSOS_HOST_UI
                snprintf(text_index,sizeof(text_index),"%s.txt-%016llx.idx",host_font,(unsigned long long)b->id);
#else
                mkdir("/sdcard/.thingsos",0775);
                snprintf(text_index,sizeof(text_index),"/sdcard/.thingsos/txt-%016llx.idx",(unsigned long long)b->id);
#endif
                if(!thingsos_txt_open(&app.epub,b->path,text_index))return false;
                segmented=app.epub.text_encoding!=1 || app.epub.zip.size>THINGSOS_XML_BYTES;
                if(segmented) {
                    app.epub_open=true;
                    if(!reader_restore_source(restored_chapter,restored_offset)&&!load_chapter(0)) {
                        release_book();return false;
                    }
                } else thingsos_epub_close(&app.epub); /* Preserve small UTF-8 TXT layout/positions. */
            }
            if(!segmented) {
            FILE* f = fopen(b->path, "rb");
            if (!f) return false;
            if (fseek(f, 0, SEEK_END) || ftell(f) < 0 || ftell(f) > (long)THINGSOS_XML_BYTES) {
                fclose(f);
                return false;
            }
            app.chapter.bytes = (size_t)ftell(f);
            rewind(f);
            app.chapter.text = thingsos_psram_alloc(app.chapter.bytes + 1);
            bool ok = app.chapter.text &&
                      fread(app.chapter.text, 1, app.chapter.bytes, f) == app.chapter.bytes;
            fclose(f);
            if (!ok) {
                release_book();
                return false;
            }
            app.chapter.text[app.chapter.bytes] = 0;
            if(thingsos_markdown_path(b->path)) {
                if(!thingsos_markdown_prepare(b->path,&app.chapter)) {
                    release_book();return false;
                }
                for(size_t img_i=0;img_i<app.chapter.image_count;++img_i) {
                    unsigned iw=0,ih=0;
                    if(thingsos_file_image_info(app.chapter.images[img_i].path,&iw,&ih) && iw && ih) {
                        app.chapter.images[img_i].width=(uint16_t)iw;
                        app.chapter.images[img_i].height=(uint16_t)ih;
                    }
                }
            }
            }
        }
        if(!app.epub_open && restored_offset<app.chapter.bytes) app.start=restored_offset;
    }
    app.reader_menu_root=app.menu_root;
    app.opened = true;
    app.toc_pending=app.epub_open;
    app.pagination_valid = false;
    strlcpy(
        app.read_path, index < app.book_count ? app.books[index].path : "builtin:example",
        sizeof(app.read_path));
    strlcpy(
        app.read_title, index < app.book_count ? app.books[index].name : "Example.txt",
        sizeof(app.read_title));
    content_at=now();feature_opened();records_at=now();
    pagination_prepare();
    ESP_LOGI("ThingsOS_perf","Book prepare release=%u content=%u records=%u index=%u total=%u ms",
        (unsigned)(released_at-open_at),(unsigned)(content_at-released_at),(unsigned)(records_at-content_at),
        (unsigned)(now()-records_at),(unsigned)(now()-open_at));
    if (index < app.book_count) {
        uint64_t id = app.books[index].id;
        size_t existing = 0;
        while (existing < app.recent_count && app.recent_ids[existing] != id) existing++;
        if (existing == app.recent_count && app.recent_count < BOOK_LIMIT) app.recent_count++;
        if (existing >= BOOK_LIMIT) existing = BOOK_LIMIT - 1;
        for (size_t i = existing; i > 0; i--) app.recent_ids[i] = app.recent_ids[i - 1];
        app.recent_ids[0] = id;
    }
    app.runtime.location.chapter = app.chapter_index;
    app.runtime.location.byte_offset = (uint32_t)app.start;
    app.transition_refresh = true;
    app.page = THINGSOS_UI_READING;
    thingsos_runtime_navigate(&app.runtime, THINGSOS_READING, now());
    app.dirty = true;
    wd_opened();
    memory_log("book-open");
    return true;
}
static uint64_t library_read_order(size_t index) {
#ifdef THINGSOS_HOST_UI
    ++host_library_order_reads;
#endif
    thingsos_book_record_t* record = thingsos_records_book(&app.records, app.books[index].id, NULL, NULL);
    return record ? record->order : 0;
}
static bool library_before(size_t a, size_t b) {
    if (!app.library_name_sort) {
        uint64_t aa = app.library_orders[a], bb = app.library_orders[b];
        if (aa != bb) return aa > bb;
    }
    int names = strcmp(app.books[a].name, app.books[b].name);
    return names ? names < 0 : strcmp(app.books[a].path, app.books[b].path) < 0;
}
static size_t library_sorted(size_t position) {
    if (position >= library_visible_count()) return app.book_count;
    if (app.library_order_weidu!=app.library_weidu) app.library_order_valid=false;
    if (app.library_order_count != app.book_count || app.library_order_name != app.library_name_sort)
        app.library_order_valid = false;
    for (size_t i=0;i<app.book_count;++i) {
        uint64_t order=library_read_order(i);
        if (app.library_orders[i]!=order || app.library_ids[i]!=app.books[i].id)
            app.library_order_valid=false;
        app.library_orders[i]=order;app.library_ids[i]=app.books[i].id;
    }
    if (app.library_order_valid) return app.library_indices[position];
    size_t* indices = app.library_indices;
    size_t filtered=0;
    for (size_t i = 0; i < app.book_count; ++i) {
        if(library_cached_book(&app.books[i])!=app.library_weidu || (app.library_weidu && app.library_query[0] && !strstr(app.books[i].name,app.library_query)))continue;
        size_t at = filtered++;
        while (at && library_before(i, indices[at - 1])) {
            indices[at] = indices[at - 1];
            --at;
        }
        indices[at] = i;
    }
    app.library_order_valid = true;
    app.library_order_count=app.book_count;app.library_order_name=app.library_name_sort;app.library_order_weidu=app.library_weidu;
    return indices[position];
}
static size_t library_hero(void) { return library_sorted(0); }
static size_t library_row(size_t row) { return library_sorted(row + 1); }
static size_t library_hit(int x, int y) {
    if (x < 36 || x >= 648) return app.book_count;
    if (app.library_cards) {
        if (y < 330 || y >= 1080) return app.book_count;
        int col = (x - 36) / 208;
        if (col > 2 || (x - 36) % 208 >= 196) return app.book_count;
        return library_sorted(app.list_start + (size_t)(y - 330) / 250 * 3 + (unsigned)col);
    }
    if (y >= 330 && y < 600) return library_hero();
    return y >= 620 && y < 1070 ? library_row(app.list_start + (size_t)(y - 620) / 150) : app.book_count;
}
static const unsigned reader_lines[] = { 125, 145, 175 }, reader_margins[] = { 12, 24, 36 };
static int reader_width(void) {return app.landscape_enabled?1216:684;}
static int reader_height(void) {return app.landscape_enabled?684:1216;}
static int reader_body_top(void) {return app.immersive_enabled?12:90;}
static int reader_body_height(void) {return app.immersive_enabled?reader_height()-24:reader_height()-176;}
/* Reader controls and More rotate together; other app pages keep their layout. */
static bool reader_landscape_frame(void) {
    return app.landscape_enabled && (app.page==THINGSOS_UI_READER_MORE ||
        (app.page==THINGSOS_UI_READING && !app.wd_notes_open && !app.wd_consent && !app.wd_cloud_prompt));
}
static void reader_canvas_geometry(void) {
    bool wide=reader_landscape_frame();app.canvas.width=wide?1216:684;app.canvas.height=wide?684:1216;
}
static bool reader_touch_coordinates(int* x,int* y) {
    if(*x<0 || *x>=684 || *y<0 || *y>=1216)return false;
    if(reader_landscape_frame()) {int raw_x=*x;*x=*y;*y=683-raw_x;}
    return true;
}
static unsigned reader_body_margin(void) {
    return reader_margins[app.margin_choice < 3 ? app.margin_choice : 1];
}
static int reader_body_left(void) {
    /* Optical correction: the left-aligned text block looked left-heavy on the
       panel. Horizontal text keeps that 12 px shift; vertical columns use
       symmetric gutters and distribute their pitch across the full body. */
    return (int)reader_body_margin() + (app.vertical_enabled?0:12);
}
static int reader_body_width(void) {
    return reader_width() - 2 * (int)reader_body_margin();
}
static bool reader_comic_chapter(const thingsos_chapter_t* chapter) {
    if(!app.epub.weidu_online || !chapter->image_count || chapter->cover_image)return false;
    for(size_t i=chapter->heading?chapter->heading_end:0;i<chapter->bytes;++i)
        if(!strchr(" \t\r\n",chapter->text[i]))return false;
    for(size_t i=0;i<chapter->image_count;++i) {
        const thingsos_inline_t* image=&chapter->images[i];
        if(image->width>=684 && image->height>=image->width)return true;
    }
    return false;
}
static int reader_content_left(const thingsos_chapter_t* chapter) {
    return reader_comic_chapter(chapter)?0:reader_body_left();
}
static int reader_content_width(const thingsos_chapter_t* chapter) {
    return reader_comic_chapter(chapter)?reader_width():reader_body_width();
}
/* First-line indent in pixels; zero leaves the source paragraph layout alone. */
static unsigned reader_indent_px(void) {
    return thingsos_reader_indent_px(app.px, app.indent_chars);
}
static bool reader_epub_paragraphs(void) {return app.epub_open&&!app.epub.plain_text;}
/* Annotation body sizes follow the reading font, independent of UI scaling. */
static unsigned reader_annotation_px(void) {return app.px>18?app.px-2:16;}
static size_t reader_annotation_page(thingsos_canvas_t* canvas,const char* text,size_t bytes,size_t start,
                                    int x,int y,int width,int height,unsigned spacing) {
    unsigned scale=app.font.ui_size_percent;app.font.ui_size_percent=0;
    thingsos_font_size(&app.font,reader_annotation_px());
    size_t end=thingsos_text_string_page(&app.font,canvas,text,bytes,start,x,y,width,height,spacing);
    app.font.ui_size_percent=scale;return end;
}
static void reader_annotation_label(const char* text,int x,int y,int width) {
    unsigned scale=app.font.ui_size_percent;app.font.ui_size_percent=0;
    thingsos_font_size(&app.font,reader_annotation_px());
    thingsos_label(&app.font,&app.canvas,text,x,y,width);app.font.ui_size_percent=scale;
}
/* Footnote window geometry: a card over the text column whose height follows
   the note, so a one-line note does not open a mostly empty window. */
#define FOOTNOTE_CARD_LEFT 40
#define FOOTNOTE_CARD_WIDTH (reader_width()-80)
#define FOOTNOTE_CARD_TOP (app.landscape_enabled?100:210)
#define FOOTNOTE_CARD_MAX (app.landscape_enabled?540:660)
#define FOOTNOTE_HEAD_BYTES 118
#define FOOTNOTE_FOOT_BYTES 56
#define FOOTNOTE_TEXT_LEFT (FOOTNOTE_CARD_LEFT + 32)
#define FOOTNOTE_TEXT_WIDTH (FOOTNOTE_CARD_WIDTH - 64)
static size_t page_end(const thingsos_chapter_t* ch, size_t start) {
    unsigned font_errors=app.font.errors;
    thingsos_font_weight(&app.font,app.weight_choice);
    thingsos_font_size(&app.font, app.px);
    size_t end=thingsos_text_page_layout_flow(
        &app.font, NULL, ch, start, reader_content_left(ch), reader_body_top(), reader_content_width(ch), reader_body_height(),
        reader_lines[app.line_choice], reader_indent_px(), reader_epub_paragraphs(), NULL, NULL, NULL,app.vertical_enabled);
    return app.font.errors==font_errors?end:start;
}
static uint64_t cache_hash(uint64_t h,const void* bytes,size_t n) {
    const unsigned char* p=bytes;
    for(size_t i=0;i<n;++i) h=(h^p[i])*1099511628211ULL;
    return h;
}
static void page_cache_titles(bool save) {
    if(!app.epub_open || !app.page_cache_path[0]) return;
    char path[640],temporary[650];
    snprintf(path,sizeof(path),"%s.names",app.page_cache_path);
    snprintf(temporary,sizeof(temporary),"%s.tmp",path);
    size_t bytes=app.epub.spine_count*160;
    if(save) {
        FILE* f=fopen(temporary,"wb");if(!f) return;
        uint64_t header[]={0x31434f54534854ULL,app.page_cache_key,bytes,
            cache_hash(14695981039346656037ULL,app.epub.chapter_names,bytes)};
        bool ok=fwrite(header,sizeof(header),1,f)==1 && fwrite(app.epub.chapter_names,1,bytes,f)==bytes;
        if(fclose(f)) ok=false;
        if(ok) {remove(path);if(rename(temporary,path)) remove(temporary);}
        else remove(temporary);
    } else {
        FILE* f=fopen(path,"rb");if(!f) return;
        uint64_t header[4];char* names=NULL;
        if(fread(header,sizeof(header),1,f)==1 && header[0]==0x31434f54534854ULL &&
           header[1]==app.page_cache_key && header[2]==bytes && (names=thingsos_psram_alloc(bytes))) {
            bool ok=fread(names,1,bytes,f)==bytes && fgetc(f)==EOF && !ferror(f) &&
                header[3]==cache_hash(14695981039346656037ULL,names,bytes);
            for(size_t i=0;ok && i<app.epub.spine_count;++i)
                if(!memchr(names+i*160,0,160)) ok=false;
            if(ok) memcpy(app.epub.chapter_names,names,bytes);
        }
        free(names);fclose(f);
    }
}
static void pagination_checkpoint(void) {
    if(!app.opened || app.pagination_valid || !app.page_cache_path[0] ||
       !app.pagination_done_pages || app.pagination_saved_chapters>=app.pagination_chapter)return;
    unsigned chapters=app.epub_open?(unsigned)app.epub.spine_count:1;
    char path[640];snprintf(path,sizeof(path),"%s.partial",app.page_cache_path);
    if(thingsos_page_checkpoint_save(path,app.page_cache_key,chapters,app.pages,
            app.pagination_done_pages,app.pagination_chapter)) {
        app.pagination_saved_chapters=app.pagination_chapter;
        app.pagination_checkpoint_at=now();
        ESP_LOGI("ThingsOS_reader","Page checkpoint saved chapters=%u pages=%u",
            app.pagination_chapter,(unsigned)app.pagination_done_pages);
    }
}
static bool pagination_prepare(void) {
    if (!app.opened) return false;
    app.font.dither=app.refresh_mode==1;
    /* A remote book has an intentionally incomplete local chapter set.
       Never scan all missing chapters or reuse a whole-EPUB page index. */
    if(app.epub.weidu_online){app.pagination_valid=false;app.pagination_started=true;return true;}
    if (!app.pagination_started || app.indexed_px != app.px ||
        app.indexed_line != app.line_choice || app.indexed_margin != app.margin_choice ||
        app.indexed_indent != app.indent_chars || app.indexed_landscape!=app.landscape_enabled || app.indexed_immersive!=app.immersive_enabled || app.indexed_vertical!=app.vertical_enabled || app.indexed_monochrome!=(app.refresh_mode==1)) {
        pagination_checkpoint();
        if (app.pagination_text) {
            thingsos_chapter_close(app.pagination_text);
            free(app.pagination_text);
            app.pagination_text = NULL;
        }
        free(app.pages); app.pages = NULL; app.page_count = 0;
        app.pagination_capacity = app.pagination_offset = app.pagination_chapter = 0;
        app.pagination_done_pages=app.pagination_saved_chapters=0;
        app.pagination_checkpoint_at=now();
        memset(&app.pagination_cursor,0,sizeof(app.pagination_cursor));
        app.pagination_error=NULL;
        app.pagination_failed = app.pagination_valid = false;
        app.pagination_started = true;
        app.indexed_px = app.px; app.indexed_line = app.line_choice;
        app.indexed_margin = app.margin_choice;
        app.indexed_weight = app.weight_choice;
        app.indexed_indent = app.indent_chars;app.indexed_landscape=app.landscape_enabled;app.indexed_immersive=app.immersive_enabled;app.indexed_monochrome=app.refresh_mode==1;
        app.indexed_vertical=app.vertical_enabled;
        uint64_t key=14695981039346656037ULL;
    /* Synthetic weight changes coverage only; hinted advances and page breaks
       stay identical, so preserve the existing whole-book position index. */
    unsigned layout[]={app.epub.plain_text?21u:app.epub_open?20u:19u,app.px,app.line_choice,app.margin_choice,app.indent_chars,(unsigned)reader_width(),(unsigned)reader_body_height()};
        key=cache_hash(key,layout,sizeof(layout));
        if(app.vertical_enabled){const uint32_t flow=0x56525432;key=cache_hash(key,&flow,sizeof(flow));}
        /* Fast mode now paginates with the same gray advances as balanced,
           but keeps its own salt so page caches written with the retired
           mono-hinted advances are never reused. */
        if(app.refresh_mode==1) {const uint32_t glyph_mode=0x44495448;key=cache_hash(key,&glyph_mode,sizeof(glyph_mode));}
        key=cache_hash(key,&app.font.signature,sizeof(app.font.signature));
        if(app.epub.plain_text)key=cache_hash(key,&app.epub.text_signature,sizeof(app.epub.text_signature));
        else if(app.epub_open) {
            for(size_t i=0;i<app.epub.zip.count;++i) {
                thingsos_zip_entry_t* z=&app.epub.zip.entries[i];
                key=cache_hash(key,z->name,strlen(z->name));
                key=cache_hash(key,&z->crc,sizeof(z->crc));
                key=cache_hash(key,&z->bytes,sizeof(z->bytes));
            }
        } else {
            unsigned md=app.chapter.markdown_index?1:0;key=cache_hash(key,&md,sizeof(md));
            key=cache_hash(key,app.chapter.text,app.chapter.bytes);
            key=cache_hash(key,app.chapter.images,app.chapter.image_count*sizeof(app.chapter.images[0]));
        }
        app.page_cache_key=key;app.page_cache_path[0]=0;
        if(media() && app.font.signature && strncmp(app.read_path,"builtin:",8)) {
#ifdef THINGSOS_HOST_UI
            snprintf(app.page_cache_path,sizeof(app.page_cache_path),"%s.pages",host_font);
#else
            mkdir("/sdcard/.thingsos",0775);
            snprintf(app.page_cache_path,sizeof(app.page_cache_path),
                "/sdcard/.thingsos/pages-%016llx.dat",(unsigned long long)app.runtime.location.book_id);
#endif
            if(thingsos_page_cache_load(app.page_cache_path,key,&app.pages,&app.page_count)) {
                unsigned chapters=app.epub_open?(unsigned)app.epub.spine_count:1;
                if(app.pages[app.page_count-1].chapter==chapters-1) {
                    app.pagination_valid=true;app.pagination_capacity=app.page_count;
                    page_cache_titles(false);
                    ESP_LOGI("ThingsOS_reader","Page index cache hit: pages=%u",(unsigned)app.page_count);
                } else {free(app.pages);app.pages=NULL;app.page_count=0;}
            }
            if(!app.pagination_valid) {
                char partial[640];unsigned completed=0;
                snprintf(partial,sizeof(partial),"%s.partial",app.page_cache_path);
                unsigned chapters=app.epub_open?(unsigned)app.epub.spine_count:1;
                if(thingsos_page_checkpoint_load(partial,key,chapters,&app.pages,&app.page_count,&completed)) {
                    app.pagination_capacity=app.pagination_done_pages=app.page_count;
                    app.pagination_chapter=app.pagination_saved_chapters=completed;
                    ESP_LOGI("ThingsOS_reader","Page checkpoint resumed chapters=%u pages=%u",completed,(unsigned)app.page_count);
                }
            }
        }
    }
    return true;
}
#ifdef THINGSOS_HOST_UI
static bool pagination_fail_allocate;
#endif
static void* pagination_allocate(void* block,size_t bytes) {
#ifdef THINGSOS_HOST_UI
    return pagination_fail_allocate?NULL:realloc(block,bytes);
#else
    /* Index buffers must never exhaust the internal DMA/driver heap. */
    void* p=heap_caps_realloc(block,bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!p) {
        reader_cache_drop_frames();
        p=heap_caps_realloc(block,bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    }
    return p;
#endif
}
typedef struct {uint64_t until;unsigned rows;} pagination_budget_t;
static bool pagination_continue(void* context) {
    pagination_budget_t* b=context;
    if(!b->rows || now()>=b->until)return false;
    --b->rows;return true;
}
static bool pagination_step_impl(bool sliced) {
    if(!pagination_prepare()) return false;
    if(app.epub.weidu_online)return false;
    if (app.pagination_failed) return false;
    if (app.pagination_valid) return true;
    unsigned chapters = app.epub_open ? (unsigned)app.epub.spine_count : 1;
    const char* failure="chapter";
    if(app.pagination_chapter>=chapters)goto failed;
    const thingsos_chapter_t* text = &app.chapter;
    if (app.epub_open) {
        if (!app.pagination_text && app.pagination_chapter != app.chapter_index) {
            app.pagination_text = pagination_allocate(NULL,sizeof(*app.pagination_text));
            failure="memory";
            if(!app.pagination_text)goto failed;
            memset(app.pagination_text,0,sizeof(*app.pagination_text));
            failure="chapter";
            if(!reader_chapter_load(app.pagination_chapter,app.pagination_text))goto failed;
        }
        if(app.pagination_text) text = app.pagination_text;
    }
    failure="limit";
    if (app.page_count >= 65536) goto failed;
    if (app.page_count == app.pagination_capacity) {
        size_t capacity = app.pagination_capacity ? app.pagination_capacity * 2 : 256;
        failure="memory";
        void* expanded = pagination_allocate(app.pages, capacity * sizeof(*app.pages));
        if (!expanded) goto failed;
        app.pages = expanded; app.pagination_capacity = capacity;
    }
    failure="layout";
    unsigned font_errors=app.font.errors;
    thingsos_font_weight(&app.font,app.weight_choice);thingsos_font_size(&app.font,app.px);
    pagination_budget_t budget={now()+8,4};bool complete=true;
    if(!sliced)memset(&app.pagination_cursor,0,sizeof(app.pagination_cursor));
    size_t end=thingsos_text_page_resume_flow(&app.font,NULL,text,app.pagination_offset,
        reader_body_left(),reader_body_top(),reader_body_width(),reader_body_height(),
        reader_lines[app.line_choice],reader_indent_px(),reader_epub_paragraphs(),NULL,NULL,NULL,
        sliced?pagination_continue:NULL,&budget,&complete,&app.pagination_cursor,app.vertical_enabled);
    if(app.font.errors!=font_errors)goto failed;
    if(!complete) {
        if(!app.pagination_cursor.active)goto failed;
        return true;
    }
    if (end <= app.pagination_offset && end < text->bytes) goto failed;
    app.pages[app.page_count++] = (thingsos_page_position_t){
        app.pagination_chapter, (uint32_t)app.pagination_offset };
    app.pagination_offset = end;
    if (end >= text->bytes) {
#ifndef THINGSOS_HOST_UI
        ESP_LOGI("ThingsOS_reader", "Page index chapter %u/%u: pages=%u",
                 app.pagination_chapter + 1, chapters, (unsigned)app.page_count);
#endif
        if (app.pagination_text) {
            thingsos_chapter_close(app.pagination_text);
            free(app.pagination_text); app.pagination_text = NULL;
        }
        app.pagination_offset = 0;
        ++app.pagination_chapter;
        app.pagination_done_pages=app.page_count;
        if (app.pagination_chapter == chapters) {
            app.pagination_valid = true;
            if(app.page_cache_path[0])
                if(thingsos_page_cache_save(app.page_cache_path,app.page_cache_key,app.pages,app.page_count)) {
                    page_cache_titles(true);
                    char partial[640];snprintf(partial,sizeof(partial),"%s.partial",app.page_cache_path);remove(partial);
                }
#ifndef THINGSOS_HOST_UI
            ESP_LOGI("ThingsOS_reader", "Page index ready: chapters=%u pages=%u", chapters,
                     (unsigned)app.page_count);
#endif
        } else if(now()-app.pagination_checkpoint_at>=5000)pagination_checkpoint();
    }
    return true;
failed:
    app.pagination_error=failure;
    if (app.pagination_text) {
        thingsos_chapter_close(app.pagination_text);
        free(app.pagination_text); app.pagination_text = NULL;
    }
    app.pagination_failed = true;
    memset(&app.pagination_cursor,0,sizeof(app.pagination_cursor));
    ESP_LOGE("ThingsOS_reader","Page index failed reason=%s chapter=%u pages=%u",
        failure,app.pagination_chapter,(unsigned)app.page_count);
    memory_log("pagination-failed");
    return false;
}
#ifdef THINGSOS_HOST_UI
static bool pagination_step(void) {return pagination_step_impl(false);}
static bool paginate(void) {
    do {
        if (!pagination_step()) return false;
        vTaskDelay(pdMS_TO_TICKS(1));
    } while (!app.pagination_valid);
    return true;
}
#endif
static size_t current_page(void) {
    return thingsos_page_find(app.pages, app.page_count, app.chapter_index, (uint32_t)app.start);
}
static unsigned book_progress(void) {
    if (app.pagination_valid)
        return thingsos_book_percent(current_page(), app.page_count);
    /* Until the index is ready, estimate from uncompressed spine sizes and
       decoded chapter position. Page counts are shown only when complete. */
    uint64_t offset = app.start < app.chapter.bytes ? app.start : app.chapter.bytes;
    if (!app.epub_open)
        return app.chapter.bytes ? (unsigned)(offset * 100 / app.chapter.bytes) : 0;
    if(app.epub.plain_text) {
        uint64_t first=app.epub.spine[app.chapter_index];
        uint64_t end=app.chapter_index+1<app.epub.spine_count?app.epub.spine[app.chapter_index+1]:app.epub.zip.size;
        if(app.chapter.bytes)first+=offset*(end-first)/app.chapter.bytes;
        return app.epub.zip.size?(unsigned)(first*100/app.epub.zip.size):0;
    }
    if (!app.progress_total) {
        for (size_t ch = 0; ch < app.epub.spine_count; ++ch) {
            const char* path = app.epub.items[app.epub.spine[ch]].path;
            uint32_t weight = 1;
            for (size_t i = 0; i < app.epub.zip.count; ++i)
                if (!strcmp(path, app.epub.zip.entries[i].name)) {
                    weight = app.epub.zip.entries[i].bytes;
                    if (!weight) weight = 1;
                    break;
                }
            app.progress_weights[ch] = weight;
            app.progress_total += weight;
        }
    }
    uint64_t before = 0;
    for (unsigned ch = 0; ch < app.chapter_index; ++ch) before += app.progress_weights[ch];
    if (app.chapter.bytes)
        before += offset * app.progress_weights[app.chapter_index] / app.chapter.bytes;
    return app.progress_total ? (unsigned)(before * 100 / app.progress_total) : 0;
}
static bool go_page(size_t n) {
    if (!pagination_prepare() || !app.pagination_valid || n >= app.page_count) return false;
    app.footnote_open = false;
    thingsos_page_position_t p = app.pages[n];
    if (p.chapter != app.chapter_index && !load_chapter(p.chapter)) return false;
    app.start = p.offset;
    app.runtime.location.chapter = p.chapter;
    app.runtime.location.byte_offset = p.offset;
    app.history_count = 0;
    app.dirty = true;
    return true;
}
#include "reader_jump_job.inc"
static void font_preview(int candidate) {
    char selected_path[320]="";
    if(candidate>=0 && (size_t)candidate<app.font_count)
        snprintf(selected_path,sizeof(selected_path),"/sdcard/fonts/%s",app.fonts[candidate]);
    if(candidate==app.font_candidate && app.preview_font.face &&
       !strcmp(selected_path,app.preview_path)) {app.dirty=true;return;}
    thingsos_font_close(&app.preview_font);
    app.font_candidate = candidate;
    app.preview_path[0] = 0;
    /* The active face already owns its signature and glyph caches. Opening
       its preview again would reread/hash an entire SD font before showing UI. */
    if(font_setting_face()->face && (candidate==-1 || selected_path[0]) &&
       !strcmp(selected_path,font_setting_path())) {
        strlcpy(app.preview_path,selected_path,sizeof(app.preview_path));
        app.dirty=true;return;
    }
    bool ok = candidate < 0 ? thingsos_board_font(&app.preview_font) : false;
    if (candidate >= 0 && (size_t)candidate < app.font_count) {
        char path[320];
        snprintf(path, sizeof(path), "/sdcard/fonts/%s", app.fonts[candidate]);
        ok = media() && thingsos_font_allowed(path) && thingsos_font_file(&app.preview_font, path);
        if (ok) strlcpy(app.preview_path, path, sizeof(app.preview_path));
    }
    if (!ok) {
        app.font_candidate = -2;
        say("字体读取失败，保留当前字体");
    }
    app.dirty = true;
}
static void reader_font_back(void) {
    bool reading=app.font_from_reader && app.opened;
    thingsos_font_close(&app.preview_font);
    navigate(reading?THINGSOS_UI_READING:THINGSOS_UI_SETTINGS);
    if(reading) {
        app.menu=true;app.jump=false;
        thingsos_runtime_overlay(&app.runtime,true,now());
    }
}
static void font_preview_current(void) {
    int candidate = -1;
    for (size_t i = 0; i < app.font_count; ++i) {
        char path[320];
        snprintf(path, sizeof(path), "/sdcard/fonts/%s", app.fonts[i]);
        if (!strcmp(path, font_setting_path())) candidate = (int)i;
    }
    font_preview(candidate);
}
#include "reader_image_cache.inc"
/* Only trim nearly white strips that contain no artwork. Keep a safety gutter
   and bound the crop per edge even on unusually sparse pages. */
static void reader_comic_trim(unsigned char* pixels,unsigned* width,unsigned* height) {
    unsigned w=*width,h=*height,left=w,top=h,right=0,bottom=0;
    for(unsigned y=0;y<h;++y) {
        const unsigned char* row=pixels+(size_t)y*w;
        for(unsigned x=0;x<w;++x)if(row[x]<248) {
            if(x<left)left=x;
            if(y<top)top=y;
            if(x>=right)right=x+1;
            bottom=y+1;
        }
    }
    if(right<=left || bottom<=top)return;
    unsigned trim[]={left,w-right,top,h-bottom};
    for(unsigned i=0;i<4;++i) {
        unsigned limit=(i<2?w:h)/8;
        trim[i]=trim[i]>4?trim[i]-4:0;
        if(trim[i]>limit)trim[i]=limit;
    }
    unsigned cropped_w=w-trim[0]-trim[1],cropped_h=h-trim[2]-trim[3];
    if(cropped_w==w && cropped_h==h)return;
    for(unsigned y=0;y<cropped_h;++y)
        memmove(pixels+(size_t)y*cropped_w,pixels+(size_t)(y+trim[2])*w+trim[0],cropped_w);
    *width=cropped_w;*height=cropped_h;
}
static bool image_load(
    const thingsos_inline_t* img, unsigned char** pixels, unsigned* w, unsigned* h) {
    if (!media() || (!app.epub_open && app.reader_from_weidu)) return false;
    unsigned char* bytes = NULL;
    size_t len;
    const char* local_image=NULL;
    char online_image[640];
    if(app.epub_open && thingsos_weidu_stream_image_path(&app.epub,img->path,online_image,sizeof(online_image)))
        local_image=online_image;
    if(!app.epub_open && app.chapter.markdown_index) {
        const char* slash=strrchr(app.read_path,'/');size_t prefix=slash?(size_t)(slash-app.read_path)+1:0;
        char checked[256];
        if(prefix && strlen(img->path)>prefix && !strncmp(app.read_path,img->path,prefix) &&
           thingsos_markdown_image_path(app.read_path,img->path+prefix,checked) && !strcmp(checked,img->path))
            local_image=img->path;
    }
    if(!app.epub_open && !local_image){
        ESP_LOGW("ThingsOS_reader","Inline image rejected path=%s read=%s",img->path,app.read_path);
        return false;
    }
    bool comic=reader_comic_chapter(&app.chapter);
    if(reader_image_cached(img->path,comic,pixels,w,h))return true;
#ifndef THINGSOS_HOST_UI
    if(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)<2600u*1024u ||
       heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)<1600u*1024u)reader_cache_drop_frames();
#endif
    if(local_image || (app.epub.weidu_cache && !strcmp(img->path,"@weidu-cover") && app.reader_cover_path[0])) {
        FILE* cover_file=fopen(local_image?local_image:app.reader_cover_path,"rb");
        if(!cover_file){
            ESP_LOGW("ThingsOS_reader","Inline image open failed errno=%d path=%s",errno,img->path);
            return false;
        }
        bool read_ok=!fseek(cover_file,0,SEEK_END);long length=read_ok?ftell(cover_file):-1;
        read_ok=length>0 && length<=THINGSOS_MEMBER_BYTES && !fseek(cover_file,0,SEEK_SET);
        if(read_ok){len=(size_t)length;bytes=thingsos_image_buffer(len);read_ok=bytes && fread(bytes,1,len,cover_file)==len;}
        fclose(cover_file);
        if(!read_ok){
            ESP_LOGW("ThingsOS_reader","Inline image read failed len=%ld path=%s",length,img->path);
            free(bytes);return false;
        }
    } else if (!thingsos_zip_read(&app.epub.zip, img->path, THINGSOS_MEMBER_BYTES, &bytes, &len))
        return false;
    /* A bounded oversample keeps dialogue strokes before removing the gutter.
       The JPEG work image still fits within the existing 1 MiB image budget. */
    unsigned bw=comic?840u:(unsigned)app.canvas.width,bh=comic?1200u:(unsigned)app.canvas.height;
    bool ok = thingsos_decode_reader_image(bytes,len,bw,bh,pixels,w,h);
    if(ok && comic)reader_comic_trim(*pixels,w,h);
    if(ok)reader_image_remember(img->path,comic,*pixels,*w,*h);
    if(!ok)ESP_LOGW("ThingsOS_reader","Inline image decode failed bytes=%u path=%s",(unsigned)len,img->path);
    free(bytes);
    return ok;
}
/* One scaled blit for both placements: contain keeps the whole image inside the
   box, cover fills the box and clips the overflow. Sampling is nearest-neighbour
   or bilinear; out-of-canvas pixels are clipped. */
static void paint_image_scaled(
    thingsos_canvas_t* c, const unsigned char* pixels, unsigned w, unsigned h, int x, int y,
    unsigned ow, unsigned oh, bool smooth, bool reader_tone) {
    if (!c) return;
    thingsos_image_paint_clipped(
        c, pixels, w, h, x, y, ow, oh, 0, 0, c->width, c->height, smooth, reader_tone);
}
static void paint_image_fit(
    thingsos_canvas_t* c, const unsigned char* pixels, unsigned w, unsigned h, int x, int y, int bw,
    int bh, bool upscale, bool smooth, bool reader_tone) {
    if (!c || !c->pixels || c->width<=0 || c->height<=0 || !pixels || bw<=0 || bh<=0) return;
    thingsos_size_t fit;
    bool fitted=upscale?thingsos_image_contain(w,h,bw,bh,&fit):thingsos_image_fit(w,h,bw,bh,&fit);
    if(!fitted)return;
    paint_image_scaled(c,pixels,w,h,x+(bw-(int)fit.width)/2,y+(bh-(int)fit.height)/2,
                       fit.width,fit.height,smooth,reader_tone);
}
/* Aspect-fill for full-bleed wallpaper: scale by the larger axis and center-crop
   the overflow so the canvas is covered edge to edge. */
static void paint_image_cover(
    thingsos_canvas_t* c, const unsigned char* pixels, unsigned w, unsigned h, int x, int y, int bw,
    int bh, bool smooth) {
    if (!c || !c->pixels || !pixels || !w || !h || bw<=0 || bh<=0) return;
    unsigned ow, oh;
    if((uint64_t)w*(unsigned)bh>(uint64_t)h*(unsigned)bw) {
        oh=(unsigned)bh;ow=(unsigned)((uint64_t)w*(unsigned)bh/h);
    } else {
        ow=(unsigned)bw;oh=(unsigned)((uint64_t)h*(unsigned)bw/w);
    }
    if(!ow)ow=1;
    if(!oh)oh=1;
    paint_image_scaled(c,pixels,w,h,x+((int)bw-(int)ow)/2,y+((int)bh-(int)oh)/2,ow,oh,smooth,false);
}
static void paint_image(thingsos_canvas_t* c,const unsigned char* pixels,unsigned w,unsigned h,
                        int x,int y,int bw,int bh) {
    paint_image_fit(c,pixels,w,h,x,y,bw,bh,false,false,false);
}
static void label(const char* text, int x, int baseline, unsigned size, int width) {
    thingsos_font_size(ui_font(), size);
    thingsos_label(ui_font(), &app.canvas, text, x, baseline, width);
}
static void centered_label(const char* text, int x, int baseline, unsigned size, int width) {
    thingsos_font_size(ui_font(), size);
    thingsos_label_centered(ui_font(), &app.canvas, text, x, baseline, width);
}
static bool picture(
    void* context, const thingsos_inline_t* img, thingsos_canvas_t* canvas, int x, int y, int w,
    int h) {
    bool cover=app.chapter.cover_image && img==app.chapter.images+app.chapter.cover_image-1;
    bool first_image=app.image_hit_count==0;
    if(cover)x=(canvas->width-w)/2;
    unsigned char* pixels = NULL;
    unsigned iw, ih;
    uint64_t load_at = first_image ? now() : 0;
    bool ok = image_load(img, &pixels, &iw, &ih);
    bool comic=reader_comic_chapter(&app.chapter);
    thingsos_size_t fit;
    if(ok && comic && thingsos_image_contain(iw,ih,(unsigned)w,(unsigned)h,&fit)) {
        x+=(w-(int)fit.width)/2;y+=(h-(int)fit.height)/2;
        w=(int)fit.width;h=(int)fit.height;
    }
    if (app.image_hit_count < THINGSOS_IMAGE_LIMIT) {
        size_t index = app.image_hit_count++;
        app.image_hits[index].index = (size_t)(img - app.chapter.images);
        app.image_hits[index].x = x;
        app.image_hits[index].y = y;
        app.image_hits[index].w = w;
        app.image_hits[index].h = h;
    }
    if (ok) {
        uint64_t paint_at = first_image ? now() : 0;
        if(cover || (!comic && app.epub_open && (!img->width || !img->height)))
            paint_image_fit(canvas,pixels,iw,ih,x,y,w,h,true,true,true);
        else if(app.epub_open)paint_image_scaled(canvas,pixels,iw,ih,x,y,(unsigned)w,(unsigned)h,true,true);
        else {
            thingsos_size_t fit;
            if(thingsos_image_fit_width(iw,ih,(unsigned)w,&fit)) {
                thingsos_canvas_t clip=*canvas;if(y+h<clip.height)clip.height=y+h;
                paint_image_scaled(&clip,pixels,iw,ih,x,y,fit.width,fit.height,true,true);
                if(fit.height>(unsigned)h && h>42) {
                    thingsos_rect(canvas,x,y+h-32,w,32,255);
                    label("长图，点击后上下拖动查看",x,y+h-8,20,w);
                }
            }
        }
        if(first_image)ESP_LOGI("ThingsOS_perf", "Reader image load=%u ms paint=%u ms path=%s",
                               (unsigned)(paint_at-load_at), (unsigned)(now()-paint_at), img->path);
        free(pixels);
    } else {
        ESP_LOGW("ThingsOS_reader","Inline image unavailable: %s", img->path);
        thingsos_rect(canvas, x, y, w, 2, 140);
        label("图片暂时无法显示，正文可继续阅读", x, y + 80, 24, w);
    }
    thingsos_font_size(&app.font, app.px);
    return ok;
}
#define COVER_WIDTH 148u
#define COVER_HEIGHT 188u
static struct {uint64_t key,until;} cover_retry[24];
static void cover_retry_clear(void) {memset(cover_retry,0,sizeof(cover_retry));}
static void cover_paint(const thingsos_thumbnail_t* cover,int x,int y,int w,int h) {
    paint_image_fit(&app.canvas,cover->pixels,cover->width,cover->height,x,y,w,h,true,false,false);
}
static bool cover_has_content(const unsigned char* pixels,unsigned w,unsigned h) {
    if(!pixels || !w || !h)return false;
    /* A decoded white placeholder must not become a successful thumbnail. */
    for(size_t i=0;i<(size_t)w*h;++i)if(pixels[i]<248)return true;
    return false;
}
#include "thumbnail_ui.inc"
static bool cover_from_mode(book_t* b,int x,int y,int w,int h,const char* source,bool original) {
    const thingsos_thumbnail_t* cached = original?NULL:thingsos_thumbnail_find(&app.thumbnails, b->id, COVER_WIDTH, COVER_HEIGHT);
    uint64_t cover_version=original?0:lock_cover_stamp(b,source);
    if(cached && cached->version && cover_version && cached->version!=cover_version)cached=NULL;
    if (cached && (cached->pixels ? cover_has_content(cached->pixels,cached->width,cached->height) : !source)) {
        if (cached->pixels) cover_paint(cached,x,y,w,h);
        else {
            thingsos_rect(&app.canvas, x, y, w, h, 225);
            label(b->name, x + 6, y + 36, 22, w - 12);
        }
        return cached->pixels!=NULL;
    }
    if(!original && (cached=thumbnail_disk_load(b,source))!=NULL) {
        cover_paint(cached,x,y,w,h);return true;
    }
    uint64_t retry_key=cache_hash(b->id,source?source:"",source?strlen(source):0);
    retry_key=cache_hash(retry_key,&cover_version,sizeof(cover_version));
    unsigned retry_slot=(unsigned)(retry_key%24);
    if(!original && cover_retry[retry_slot].key==retry_key && now()<cover_retry[retry_slot].until) {
        thingsos_rect(&app.canvas,x,y,w,h,225);label(b->name,x+6,y+36,22,w-12);return false;
    }
    uint64_t load_at = now();
    ++app.cover_loads;
    unsigned char *raw = NULL, *pixels = NULL;
    size_t bytes;
    unsigned iw, ih;
    bool ok = false, missing = false;
    size_t raw_limit=original?1024u*1024u:THINGSOS_MEMBER_BYTES;
    unsigned decode_ms = 0;
    if(source) {
        FILE* f=fopen(source,"rb");
        if(f) {
            if(!fseek(f,0,SEEK_END)) { long n=ftell(f);rewind(f);
                if(n>0 && (size_t)n<=raw_limit) {bytes=(size_t)n;raw=thingsos_image_buffer(bytes);
                    if(raw && fread(raw,1,bytes,f)==bytes)ok=original?thingsos_decode_wallpaper(raw,bytes,&pixels,&iw,&ih):thingsos_decode_thumbnail(raw,bytes,COVER_WIDTH,COVER_HEIGHT,&pixels,&iw,&ih);}
            }
            fclose(f);
        }
    }
    if(ok && !cover_has_content(pixels,iw,ih)) {
        ESP_LOGW("ThingsOS_cover","Blank external cover: %s; trying EPUB",b->path);
        free(pixels);pixels=NULL;ok=false;
    }
    if (!ok && b->epub) {
        free(raw);raw=NULL;
        thingsos_epub_t* e = thingsos_psram_calloc(1, sizeof(*e));
        if (e && thingsos_epub_open(e, b->path)) {
            missing = !e->cover[0] && !e->cover_document[0];
            if (thingsos_epub_resolve_cover(e))ok=thingsos_zip_read(&e->zip,e->cover,raw_limit,&raw,&bytes);
            thingsos_epub_close(e);
        }
        free(e);
        /* The ZIP directory, manifest and TOC arrays are no longer needed.
           Release them before stb_image requests its large decode buffers. */
        if(ok) {
            uint64_t decode_at=now();
            ok=original?thingsos_decode_wallpaper(raw,bytes,&pixels,&iw,&ih):thingsos_decode_thumbnail(raw,bytes,COVER_WIDTH,COVER_HEIGHT,&pixels,&iw,&ih);
            decode_ms=(unsigned)(now()-decode_at);
        }
    } else if (!ok) {
        free(raw);raw=NULL;
        char path[THINGSOS_RECORD_PATH_BYTES];
        strlcpy(path, b->path, sizeof(path));
        char* ext = strrchr(path, '.');
        if (ext) {
            strcpy(ext, ".png");
            FILE* f = fopen(path, "rb");
            bool png_missing = !f && errno == ENOENT;
            if (!f) {
                strcpy(ext, ".jpg");
                f = fopen(path, "rb");
                missing = png_missing && !f && errno == ENOENT;
            }
            if (f) {
                if (!fseek(f, 0, SEEK_END)) {
                    long n = ftell(f);
                    if (n > 0 && (size_t)n <= raw_limit) {
                        bytes = (size_t)n;
                        raw = thingsos_image_buffer(bytes);
                        rewind(f);
                        if (raw && fread(raw, 1, bytes, f) == bytes) {
                            uint64_t decode_at = now();
                            ok = original?thingsos_decode_wallpaper(raw,bytes,&pixels,&iw,&ih):thingsos_decode_thumbnail(raw,bytes,COVER_WIDTH,COVER_HEIGHT,&pixels,&iw,&ih);
                            decode_ms = (unsigned)(now()-decode_at);
                        }
                    }
                }
                fclose(f);
            }
        }
    }
    if(ok && !cover_has_content(pixels,iw,ih)) {
        ESP_LOGW("ThingsOS_cover","Blank embedded cover: %s; using title placeholder",b->path);
        free(pixels);pixels=NULL;ok=false;
    }
    unsigned elapsed = (unsigned)(now()-load_at);
    app.cover_load_ms += elapsed >= decode_ms ? elapsed-decode_ms : 0;
    app.cover_decode_ms += decode_ms;
    uint64_t scale_at = now();
    free(raw);raw=NULL;
    if(original) {
        if(ok)paint_image_fit(&app.canvas,pixels,iw,ih,x,y,w,h,true,true,false);
        else {
            cached=thingsos_thumbnail_find(&app.thumbnails,b->id,COVER_WIDTH,COVER_HEIGHT);
            if(cached && cached->pixels)cover_paint(cached,x,y,w,h);
            else {thingsos_rect(&app.canvas,x,y,w,h,225);label(b->name,x+6,y+36,22,w-12);}
        }
        ESP_LOGI("ThingsOS_cover","Wallpaper original=%d source=%ux%u target=%dx%d",ok,ok?iw:0,ok?ih:0,w,h);
        free(pixels);app.cover_scale_ms+=(unsigned)(now()-scale_at);
        return ok;
    }
    if(!original) {
        if(ok)cover_retry[retry_slot].until=0;
        else if(!missing || source) {
            cover_retry[retry_slot].key=retry_key;cover_retry[retry_slot].until=now()+15000;
        }
    }
    /* Keep transient decode/allocation failures retryable; missing covers can
       be negative-cached until the next media/content invalidation. */
    cached = ok || (missing && !source) ? thingsos_thumbnail_store(&app.thumbnails, b->id, COVER_WIDTH,
        COVER_HEIGHT, ok ? pixels : NULL, ok ? iw : 0, ok ? ih : 0) : NULL;
    if(cached)thingsos_thumbnail_version(&app.thumbnails,b->id,COVER_WIDTH,COVER_HEIGHT,cover_version);
    if (ok) {
        if(cached)thumbnail_disk_save(b,source,cached);
        if (cached) cover_paint(cached,x,y,w,h);
        else paint_image(&app.canvas, pixels, iw, ih, x, y, w, h);
    }
    else {
        thingsos_rect(&app.canvas, x, y, w, h, 225);
        label(b->name, x + 6, y + 36, 22, w - 12);
    }
    free(raw);
    free(pixels);
    app.cover_scale_ms += (unsigned)(now()-scale_at);
    return ok;
}
static void cover_from(book_t* b,int x,int y,int w,int h,const char* source) {
    cover_from_mode(b,x,y,w,h,source,false);
}
#ifdef THINGSOS_HOST_UI
static char host_lock_cache_root[512];
#endif
/* Cached wallpaper pixels depend on the rendering policy as well as the source. */
static const uint64_t wallpaper_render_version=2;
static uint64_t lock_cover_stamp(book_t* b,const char* source) {
    uint64_t parts[5]={thingsos_cover_source_stamp(b->path),thingsos_cover_source_stamp(source),0,0,wallpaper_render_version};
    if(!b->epub) {
        char path[THINGSOS_RECORD_PATH_BYTES];strlcpy(path,b->path,sizeof(path));
        char* ext=strrchr(path,'.');
        if(ext) {strcpy(ext,".png");parts[2]=thingsos_cover_source_stamp(path);strcpy(ext,".jpg");parts[3]=thingsos_cover_source_stamp(path);}
    }
    if(!(parts[0]|parts[1]|parts[2]|parts[3]))return 0;
    return cache_hash(14695981039346656037ULL,parts,sizeof(parts));
}
static bool cover_wallpaper(book_t* b) {
    char id[64],source[640];bool cached=thingsos_weidu_cache_id(b->path,id,sizeof(id));
    if(cached)snprintf(source,sizeof(source),"/sdcard/.thingsos/weidu/covers/%s.jpg",id);
    reader_cache_clear();
    memory_log("lock-cover-before");
    const char* root="/sdcard/.thingsos/lock-covers";
#ifdef THINGSOS_HOST_UI
    root=host_lock_cache_root;
#endif
    char cache[640];int n=snprintf(cache,sizeof(cache),"%s/%016llx%s.gray",root,
        (unsigned long long)b->id,app.canvas.inverted?"-dark":"");
    uint64_t stamp=lock_cover_stamp(b,cached?source:NULL);
    bool use_cache=media() && root[0] && n>0 && (size_t)n<sizeof(cache) && stamp;
    bool shown=true;
    if(use_cache && thingsos_cover_cache_load(cache,stamp,&app.canvas,0,72,684,869)) {
        ESP_LOGI("ThingsOS_cover","Wallpaper SD cache hit");
    } else {
        thingsos_rect(&app.canvas,0,72,684,869,255);
        bool original=cover_from_mode(b,0,72,684,869,cached?source:NULL,true);
        shown=original;
        if(use_cache && original && stamp==lock_cover_stamp(b,cached?source:NULL)) {
#ifndef THINGSOS_HOST_UI
            mkdir("/sdcard/.thingsos",0775);
#endif
            mkdir(root,0775);
            bool saved=thingsos_cover_cache_save(cache,stamp,&app.canvas,0,72,684,869);
            ESP_LOGI("ThingsOS_cover","Wallpaper SD cache saved=%d",saved);
        }
    }
    memory_log("lock-cover-after");
    return shown;
}
static void cached_mark(int x,int y,int w,int h) {
    int band = h < 160 ? 30 : 36;
    /* Opaque ink band stays legible on every cover without hiding the title. */
    thingsos_rect(&app.canvas,x,y+h-band,w,band,255);
    thingsos_font_size(ui_font(),22);
    centered_label("已缓存",x,y+h-(band-22)/2-4,22,w);
    for(int row=y+h-band;row<y+h;++row) for(int col=x;col<x+w;++col)
        app.canvas.pixels[(size_t)row*app.canvas.width+col]=255-app.canvas.pixels[(size_t)row*app.canvas.width+col];
}
static void cover(book_t* b,int x,int y,int w,int h) {
    char id[64],source[640];bool cached=thingsos_weidu_cache_id(b->path,id,sizeof(id));
    if(cached)snprintf(source,sizeof(source),"/sdcard/.thingsos/weidu/covers/%s.jpg",id);
    cover_from(b,x,y,w,h,cached?source:NULL);if(cached)cached_mark(x,y,w,h);
}
static void nav(void) {
    unsigned active = app.page == THINGSOS_UI_WEIDU                                      ? 1
                      : app.page == THINGSOS_UI_APPS || app.page == THINGSOS_UI_SYSTEM || app.page == THINGSOS_UI_TRANSFER || app.page == THINGSOS_UI_WIFI || app.page==THINGSOS_UI_RECORDER || app.page==THINGSOS_UI_MUSIC || app.page==THINGSOS_UI_ALARM || app.page==THINGSOS_UI_WALLPAPER || app.page==THINGSOS_UI_FILES || app.page==THINGSOS_UI_CUBOX || (app.page==THINGSOS_UI_PASSWORD&&(app.input_return==THINGSOS_UI_FILES||app.input_return==THINGSOS_UI_CUBOX)) ? 2
                      : app.page == THINGSOS_UI_LIBRARY || app.page==THINGSOS_UI_RECENT ? 0
                                                                                         : 3;
    thingsos_draw_nav(ui_font(), &app.canvas, active);
}
static void header(const char* title) {
    bool back = app.page != THINGSOS_UI_LIBRARY && app.page != THINGSOS_UI_RECENT &&
                app.page != THINGSOS_UI_WEIDU &&
                app.page != THINGSOS_UI_SETTINGS && app.page != THINGSOS_UI_APPS;
    if (back) label("<", 36, 155, 36, 60);
    if (app.page == THINGSOS_UI_FILES || app.page == THINGSOS_UI_WIFI || app.page == THINGSOS_UI_PASSWORD || app.page == THINGSOS_UI_CUBOX) label(title, 112, 160, 40, app.page==THINGSOS_UI_CUBOX?340:492);
    else if (back) centered_label(title, 96, 160, 40, app.canvas.width-192);
    /* The library, WeRead and transfer pages carry their own tab/status row at
       the same height, so a big title there only collides with it. */
    else if (app.page != THINGSOS_UI_LIBRARY && app.page != THINGSOS_UI_WEIDU &&
             app.page != THINGSOS_UI_TRANSFER) label(title, 36, 160, 52, 600);
    thingsos_rect(&app.canvas, 36, 210, app.canvas.width-72, 1, 170);
}
static void button(const char* text, int y) {
    thingsos_rect(&app.canvas, 36, y, app.canvas.width-72, 2, 170);
    centered_label(text, 48, y + 54, 30, app.canvas.width-96);
}
static void font_option(const char* text,int y,bool selected) {
    if(selected) {
        thingsos_rect(&app.canvas,36,y+2,612,78,232);
        thingsos_rect(&app.canvas,36,y+2,4,78,0);
        /* Pixel check mark stays visible in fonts without the check glyph. */
        for(int i=0;i<9;++i)thingsos_rect(&app.canvas,590+i,y+39+i,3,3,0);
        for(int i=0;i<17;++i)thingsos_rect(&app.canvas,598+i,y+47-i,3,3,0);
    }
    thingsos_rect(&app.canvas,36,y,612,2,170);
    centered_label(text,56,y+54,30,selected?516:572);
}
static void setting_row(const char* title, const char* value, int y) {
    thingsos_rect(&app.canvas, 36, y, 612, 1, 170);
    label(title, 48, y + 54, 28, 240);
    thingsos_font_size(ui_font(), 21);
    thingsos_label_right(ui_font(), &app.canvas, value, 308, y + 54, 328);
}
static void setting_choices(const char* title, const char* const choices[], unsigned count, unsigned selected, int y) {
    thingsos_rect(&app.canvas, 36, y, 612, 1, 170);
    label(title, 48, y + 54, 26, 168);
    int width = 420 / (int)count;
    for (unsigned i = 0; i < count; ++i) {
        int x = 228 + (int)i * width;
        centered_label(choices[i], x, y + 54, 25, width);
        if (i == selected) thingsos_rect(&app.canvas, x + width / 5, y + 70, width * 3 / 5, 3, 0);
    }
}
static void setting_options(const char* title, const char* const choices[3], unsigned selected, int y) {
    setting_choices(title, choices, 3, selected, y);
}
/* Choice index for a tap on a setting_choices() row; -1 left of the choices. */
static int setting_choice_hit(int x, unsigned count) {
    if (x < 228) return -1;
    int index = (x - 228) * (int)count / 420;
    return index >= (int)count ? (int)count - 1 : index;
}
/* All More rows share 28 px labels, 24 px segmented choices and 22 px hints. */
#include "reader_refresh.inc"
static bool reader_more_cloud(void);
static int reader_more_row_left(unsigned row) {return reader_landscape_frame() && (row==2||row==4)?630:36;}
static int reader_more_row_top(unsigned row) {return reader_landscape_frame()?(row>=3?500:row==1?400:300):(row==0?420:row==2?520:row==1?620:row==3?720:820);}
static int reader_more_row_width(void) {return reader_landscape_frame()?550:612;}
static int reader_more_toggle_hit(int x,int y) {
    for(unsigned row=0;row<5;++row) {
        int left=reader_more_row_left(row)+reader_more_row_width()-192,top=reader_more_row_top(row)-12;
        if(x>=left && x<left+192 && y>=top && y<top+76)return (int)row;
    }
    return -1;
}
static bool reader_more_choice(int x,unsigned row) {return x>=reader_more_row_left(row)+reader_more_row_width()-96;}
static void reader_more_switch(int x,int y,bool on) {
    static const char* const names[]={"关闭","开启"};
    reader_refresh_segments(names,x,y,176,2,on?1:0);
}
static unsigned reader_more_sensitivity_choice(int x) {
    int start=reader_more_row_left(2)+reader_more_row_width()-184;
    if(x<start)return 0;
    if(x>=start+176)return 2;
    return (unsigned)(x-start)*3/176;
}
static void reader_more_sensitivity(int x,int y) {
    static const char* const names[]={"低","中","高"};
    reader_refresh_segments(names,x,y,176,3,app.tilt_sensitivity);
}
static bool reader_more_cloud(void) {return app.opened && app.wd_id[0];}
static int reader_more_note_top(void) {return reader_landscape_frame()?600:1080;}
static int reader_more_back_top(void) {return reader_more_note_top();}
static int reader_more_footer_width(void) {return reader_landscape_frame()?reader_more_row_width():296;}
static int reader_more_back_left(void) {return reader_landscape_frame()?630:352;}
static int reader_more_sync_top(void) {return reader_landscape_frame()?400:920;}
static int reader_more_refresh_left(bool interval) {return reader_landscape_frame() && interval?630:36;}
static int reader_more_refresh_top(bool interval) {return reader_landscape_frame()?200:(interval?320:220);}
static void reader_more_footer(const char* text,int x,int y,int width) {
    thingsos_rect(&app.canvas,x,y,width,66,0);thingsos_rect(&app.canvas,x+2,y+2,width-4,62,255);
    centered_label(text,x,y+44,28,width);
}
static void reader_more_draw(void) {
    unsigned scale=app.font.ui_size_percent,weight=app.font.ui_weight_override;
    app.font.ui_size_percent=app.font.ui_weight_override=0;thingsos_font_weight(&app.font,0);
    bool wide=reader_landscape_frame();int width=reader_more_row_width(),hint=88,separator=98;
    label("<",36,150,30,60);centered_label("更多",96,154,34,app.canvas.width-192);
    thingsos_rect(&app.canvas,36,wide?180:198,app.canvas.width-72,1,180);
    for(unsigned interval=0;interval<2;++interval) {
        int x=reader_more_refresh_left(interval!=0),y=reader_more_refresh_top(interval!=0);
        label(interval?"全刷间隔":"刷新模式",x,y+40,28,width-336);
        reader_refresh_segments(interval?NULL:reader_refresh_modes,x+width-320,y,312,interval?4:3,
            interval?(app.refresh_pages-5)/5:app.refresh_mode);
        const char* detail=interval?(app.refresh_mode==2?"清晰模式每页全刷，此项暂不生效":"按设定翻页次数完整刷新"):
            app.refresh_mode==1?"黑白快速，优先翻页速度":app.refresh_mode==2?"每页全刷，优先清理残影":"保留灰度，兼顾细节与速度";
        label(detail,x,y+hint,22,width);
        thingsos_rect(&app.canvas,x,y+separator,width,1,204);
    }
    for(unsigned row=0;row<5;++row) {
        int x=reader_more_row_left(row),y=reader_more_row_top(row);
        label(row==4?"竖向排版":row==3?"沉浸模式":row==2?"翻页灵敏度":row?"横屏阅读":"晃动翻页",x,y+40,28,width-212);
        if(row==2)reader_more_sensitivity(x+width-184,y);
        else reader_more_switch(x+width-184,y,row==4?app.vertical_enabled:row==3?app.immersive_enabled:row?app.landscape_enabled:app.tilt_enabled);
        label(row==4?"字从上往下，列从右往左":row==3?"隐藏状态与进度，扩展正文区域":row==2?"低档更稳 · 中档默认 · 高档更灵敏":row?"旋转屏幕，独立于文字排版方向":"右侧下转下一页，左侧下转上一页",x,y+hint,22,width);
        thingsos_rect(&app.canvas,x,y+separator,width,1,204);
    }
    if(reader_more_cloud()) {
        int x=wide?630:36,y=reader_more_sync_top();
        static const char* const names[]={"手动","自动"};
        label("划线与想法同步",x,y+40,28,width-212);
        reader_refresh_segments(names,x+width-184,y,176,2,app.annotation_manual?0:1);
        label(app.annotation_manual?"立即同步":"联网空闲后同步微读笔记",x,y+hint,app.annotation_manual?24:22,width);
        thingsos_rect(&app.canvas,x,y+separator,width,1,204);
    }
    reader_more_footer("我的划线和想法",36,reader_more_note_top(),reader_more_footer_width());
    reader_more_footer("返回阅读设置",reader_more_back_left(),reader_more_back_top(),reader_more_footer_width());
    app.font.ui_size_percent=scale;app.font.ui_weight_override=weight;
}
static void library_view_control(bool cards) {
    /* Pixel icons have no dependency on the font's symbol coverage. */
    if (cards) {
        for (int row = 0; row < 3; ++row) {
            thingsos_rect(&app.canvas, 597, 243 + row * 16, 31, 3, 0);
            thingsos_rect(&app.canvas, 586, 241 + row * 16, 6, 6, 0);
        }
    } else {
        for (int row = 0; row < 2; ++row) for (int col = 0; col < 3; ++col) {
            int x = 586 + col * 14, y = 241 + row * 21;
            thingsos_rect(&app.canvas, x, y, 11, 17, 0);
            thingsos_rect(&app.canvas, x + 2, y + 2, 7, 13, 255);
        }
    }
}
static void feature_editor(unsigned);
static bool input_uses_qwerty(void);
static void wifi_password_update(bool layout);
static void search_control(int offset_x) {
    for(int y=241;y<279;++y)for(int x=434;x<472;++x) {
        unsigned ink=0;
        for(int sy=0;sy<4;++sy)for(int sx=0;sx<4;++sx) {
            int dx=(x-449)*8+sx*2+1,dy=(y-256)*8+sy*2+1;
            int rr=dx*dx+dy*dy;
            if((rr>=80*80 && rr<=100*100) ||
               (dx>=64 && dx<=160 && dy>=64 && dy<=160 && abs(dx-dy)<=13))++ink;
        }
        if(ink)thingsos_rect(&app.canvas,x+offset_x,y,1,1,255-ink*255/16);
    }
}
static void toolbar_stroke(int x0,int y0,int x1,int y1) {
    int vx=(x1-x0)*8,vy=(y1-y0)*8,len=vx*vx+vy*vy;
    for(int y=(y0<y1?y0:y1)-2;y<=(y0>y1?y0:y1)+2;++y)
        for(int x=(x0<x1?x0:x1)-2;x<=(x0>x1?x0:x1)+2;++x) {
            unsigned ink=0;
            for(int sy=0;sy<4;++sy)for(int sx=0;sx<4;++sx) {
                int dx=(x-x0)*8+sx*2+1,dy=(y-y0)*8+sy*2+1;
                int dot=dx*vx+dy*vy;
                if(dot<0)dot=0;
                if(dot>len)dot=len;
                int ex=dx-(len?vx*dot/len:0),ey=dy-(len?vy*dot/len:0);
                ink+=ex*ex+ey*ey<=12*12;
            }
            if(ink) {
                unsigned char shade=(unsigned char)(255-ink*255/16);
                if(shade<app.canvas.pixels[(size_t)y*684+x])thingsos_rect(&app.canvas,x,y,1,1,shade);
            }
        }
}
static void library_controls(void) {
    library_view_control(app.library_cards);
    if(app.library_weidu && app.page==THINGSOS_UI_LIBRARY)search_control(0);
    /* Sorting: time face for recent reading, ordered bars for book names. */
    if (app.library_name_sort) {
        for (int row = 0; row < 3; ++row)
            thingsos_rect(&app.canvas, (396) + 116, 241 + row * 17, 12 + row * 7, 4, 0);
    } else {
        /* 4x coverage sampling keeps the clock rim smooth at device resolution. */
        for(int y=240;y<280;++y)for(int x=502;x<542;++x) {
            unsigned ink=0;
            for(int sy=0;sy<4;++sy)for(int sx=0;sx<4;++sx) {
                int dx=(x-522)*8+sx*2+1,dy=(y-260)*8+sy*2+1;
                int rr=dx*dx+dy*dy;
                if((rr>=132*132 && rr<=152*152) ||
                   (dx>=-10 && dx<=10 && dy>=-72 && dy<=10) ||
                   (dy>=-10 && dy<=10 && dx>=0 && dx<=66))++ink;
            }
            if(ink)thingsos_rect(&app.canvas,x,y,1,1,255-ink*255/16);
        }
    }
    toolbar_stroke(550,245,550,273);
    toolbar_stroke(543,267,550,274);
    toolbar_stroke(550,274,557,267);
}
/* Approved 36x24px Wi-Fi mark: three round arcs and a dot; offline adds a slash.
   Sample coverage directly, with no bitmap/font allocation or extra network task. */
static bool status_wifi_ink(float x,float y,bool connected) {
    bool ink=false;
    static const float half[]={15,10,5},base[]={3,9,15},curve[]={4.0f/225,0.03f,0.08f};
    for(unsigned i=0;i<3;++i) {
        float dx=x-18,dy=y-(base[i]+curve[i]*dx*dx),slope=2*curve[i]*dx;
        if(dx>=-half[i] && dx<=half[i] && dy*dy<=1.5625f*(1+slope*slope))ink=true;
        float end_y=base[i]+curve[i]*half[i]*half[i];
        float ex=dx<0?dx+half[i]:dx-half[i],ey=y-end_y;
        if(ex*ex+ey*ey<=1.5625f)ink=true;
    }
    float dx=x-18,dy=y-21.5f;
    if(dx*dx+dy*dy<=2.89f)ink=true;
    if(!connected) {
        float at=((x-5)*26+(y-3)*19)/1037;
        if(at<0)at=0;
        if(at>1)at=1;
        dx=x-(5+26*at);dy=y-(3+19*at);float distance=dx*dx+dy*dy;
        if(distance<=12.25f)ink=false;
        if(distance<=1.5625f)ink=true;
    }
    return ink;
}
static void status_wifi_draw(int left,bool connected) {
    for(int y=0;y<24;++y)for(int x=0;x<36;++x) {
        unsigned ink=0;
        for(unsigned sy=0;sy<4;++sy)for(unsigned sx=0;sx<4;++sx)
            ink+=status_wifi_ink(x+(sx+0.5f)*0.25f,y+(sy+0.5f)*0.25f,connected);
        if(ink)thingsos_rect(&app.canvas,left+x,24+y,1,1,255-ink*255/16);
    }
}
#include "status_power.inc"
#include "feedback_icons.inc"
/* Approved V3: smooth 30x36 mic, capsule lowered 3 px towards the support. */
static void status_mic_draw(int left) {
    feedback_mic_draw(left,18,30,36);
}
static void draw_status(void) {
    if(app.page==THINGSOS_UI_READING && app.immersive_enabled && !(app.annotation_open && app.annotation_list) &&
       !app.wd_notes_open && !app.wd_consent && !app.wd_cloud_prompt)return;
    bool prior_dither=app.font.dither;
    app.font.dither=(reader_surface(app.page)?app.refresh_mode:app.ui_refresh_mode)==1;
    int shift=app.canvas.width-684;
    thingsos_rect(&app.canvas,0,0,app.canvas.width,74,255);
    char status[96];
    snprintf(status, sizeof(status), "%u%%", app.battery);
    thingsos_font_size(ui_font(),24);
    thingsos_label_right(ui_font(),&app.canvas,status,508+shift,46,104);
    int battery_width=0;
    for(const char* p=status;*p;++p) {
        int advance=thingsos_glyph(ui_font(),NULL,(unsigned char)*p,0,0);
        if(battery_width+advance>104)break;
        battery_width+=advance;
    }
    /* Keep the approved default position; larger UI fonts can expand left. */
    int wifi_left=612-battery_width-44;if(wifi_left>504)wifi_left=504;
    wifi_left+=shift;
    if(app.audio.state==THINGSOS_AUDIO_RECORDING||app.audio.state==THINGSOS_AUDIO_RECORD_PAUSED)
        status_mic_draw(wifi_left-48);
    status_wifi_draw(wifi_left,thingsos_network_connected());
    status_battery_draw(620+shift,24,app.battery,app.charging);
    time_t clock = wall_time();
    struct tm date;
    if (clock >= 1577836800 && localtime_r(&clock, &date) && date.tm_year <= 199) {
        strftime(status, sizeof(status), "%m-%d %H:%M", &date);
        if(app.page==THINGSOS_UI_READING && (app.menu || app.image_view.open)) {
            strftime(status,sizeof(status),"%m-%d",&date);label(status,116,28,18,100);
            strftime(status,sizeof(status),"%H:%M",&date);label(status,116,54,18,100);
        } else label(status,28,46,20,app.page==THINGSOS_UI_READING?190:300);
    }
    if(app.page==THINGSOS_UI_READING && app.opened && app.epub_open) {
        char title[192];
        if(app.chapter.heading && app.chapter.heading_end<=app.chapter.bytes &&
           app.chapter.heading_end>app.chapter.heading_start) {
            size_t n=app.chapter.heading_end-app.chapter.heading_start;
            if(n>=sizeof(title))n=sizeof(title)-1;
            while(n && ((unsigned char)app.chapter.text[app.chapter.heading_start+n]&0xc0)==0x80)--n;
            memcpy(title,app.chapter.text+app.chapter.heading_start,n);title[n]=0;
            for(size_t i=0;i<n;++i)if(title[i]=='\n'||title[i]=='\r')title[i]=' ';
        } else if(!app.epub.plain_text && app.epub.chapter_names && app.chapter_index<app.epub.spine_count)
            strlcpy(title,app.epub.chapter_names[app.chapter_index],sizeof(title));
        else strlcpy(title,app.read_title,sizeof(title));
        int title_width=wifi_left-230;if(title_width>248+shift)title_width=248+shift;
        centered_label(title,218,46,24,title_width);
    }
    app.font.dither=prior_dither;
}
#include "reader_cache.inc"
#include "reader_effect.inc"
/* Font errors must not send an otherwise working reader into the recovery AP.
   Retry with a fresh face, then the built-in face. Keep input/network alive if
   even that fails; the glyph renderer marks unrenderable characters visibly. */
static bool font_frame_ready(unsigned before) {
    if(app.font.errors==before){app.font_retries=0;return true;}
    reader_cache_clear();
    if(app.font_retries>=2)return true;
    ++app.font_retries;
    thingsos_font_t replacement={0};
    bool custom=app.font_retries==1 && app.font_path[0];
    bool loaded=custom?thingsos_font_file(&replacement,app.font_path):thingsos_board_font(&replacement);
    if(!loaded && custom){custom=false;loaded=thingsos_board_font(&replacement);}
    if(loaded) {
        thingsos_font_close(&app.font);app.font=replacement;
        if(!custom) {
            bool changed=app.font_path[0]!=0;
            app.font_path[0]=0; /* Session fallback only; no NVS write. */
            if(changed)say("字体绘制异常，已临时使用内置字体");
        }
        app.pagination_started=app.pagination_valid=app.pagination_failed=false;
    }
    app.dirty=true;app.force_refresh=true;
    return false;
}
static bool ui_font_frame_ready(unsigned before) {
    if(app.ui_font.errors==before){app.ui_font_retries=0;return true;}
    if(app.ui_font_retries>=2)return true;
    ++app.ui_font_retries;thingsos_font_t replacement={0};
    bool custom=app.ui_font_retries==1 && app.ui_font_path[0];
    bool loaded=custom?thingsos_font_file(&replacement,app.ui_font_path):thingsos_board_font(&replacement);
    if(!loaded&&custom){custom=false;loaded=thingsos_board_font(&replacement);}
    if(loaded){thingsos_font_close(&app.ui_font);app.ui_font=replacement;
        if(!custom){bool changed=app.ui_font_path[0]!=0;app.ui_font_path[0]=0;if(changed)say("界面字体异常，已临时使用内置字体");}}
    app.dirty=app.force_refresh=true;return false;
}
static void memory_log(const char* stage) {
#ifdef THINGSOS_HOST_UI
    (void)stage;
#else
    ESP_LOGI("ThingsOS_memory","stage=%s internal_free=%u internal_min=%u internal_largest=%u dma_free=%u dma_largest=%u psram_free=%u psram_min=%u psram_largest=%u covers=%u frames=%u main_stack_remaining=%u",
        stage,(unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA|MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA|MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
        (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM),
        (unsigned)app.thumbnails.bytes,(unsigned)((!!reader_frames[0].pixels+!!reader_frames[1].pixels)*READER_CACHE_BYTES),
        (unsigned)uxTaskGetStackHighWaterMark(NULL));
#endif
}

#include "weidu_ui.inc"
#include "weidu_online_ui.inc"
static void wd_own_thought_enter(const char*,const thingsos_wd_note_t*);
#include "weidu_reading_ui.inc"
#include "annotations_ui.inc"
static size_t reader_toc_count(void) {
    return app.epub_open ? (app.epub.section_toc_count?app.epub.section_toc_count:app.epub.spine_count)
        :(app.page_count+9)/10;
}
static thingsos_toc_entry_t reader_toc_entry(size_t n) {
    if(app.epub.section_toc_count && n<app.epub.section_toc_count)return app.epub.section_toc[n];
    thingsos_toc_entry_t entry={0};entry.chapter=(uint32_t)n;
    if(n<app.epub.spine_count) {
        entry.level=app.epub.chapter_levels[n];strlcpy(entry.title,app.epub.chapter_names[n],sizeof(entry.title));
    }
    return entry;
}
#include "reader_toc_job.inc"
#include "usb_ui.inc"
#include "file_picture.inc"
#include "reader_navigation.inc"
static bool ima_draw(void);
static bool bt_ui_draw(void);
static bool bt_ui_tap(int,int);
static bool bt_ui_key(thingsos_key_action_t);
static void bt_ui_poll(void);
static bool ima_tap(int,int);
static bool ima_key(thingsos_key_action_t);
static void ima_poll(void);
static void ima_overlay(void);
static void ima_save_pending(void);
static void ima_recording_upload(size_t);
static bool cubox_draw(void);
static bool cubox_tap(int,int);
static bool cubox_key(thingsos_key_action_t);
static bool cubox_scroll(int);
static void cubox_enter(void);
static void cubox_poll(void);
static void cubox_search_submit(void);
static bool app_cubox_region(void);
static void app_cubox_presented(bool);
static esp_err_t app_cubox_present(void);
#include "apps_controls.inc"
#include "bluetooth_ui.inc"
#include "ima_ui.inc"
#include "cubox_ui.inc"
#include "audio_feedback.inc"
#include "lock_pin.inc"
static void draw(void) {
    if(app.pin.required && app.page!=THINGSOS_UI_LOCK &&
       (app.page!=THINGSOS_UI_LOCK_PIN || app.pin.purpose!=PIN_UNLOCK))pin_wake();
    app.volume_overlay_drawn=false;
    WHERE("draw start page=%d",(int)app.page);
    reader_canvas_geometry();
    if(app.page==THINGSOS_UI_LOCK && app.lock_snapshot_valid&&!alarm_active())return;
    app.font.ui_size_percent = app.page == THINGSOS_UI_READING ? 0 :
        (app.ui_px ? app.ui_px : 30) * 100 / 30;
    app.font.ui_weight_override = app.page == THINGSOS_UI_READING ? 0 : app.ui_weight + 1;
    app.font.dither=(reader_surface(app.page)?app.refresh_mode:app.ui_refresh_mode)==1;
    if(app.page==THINGSOS_UI_READING)thingsos_font_weight(&app.font,0);
    else thingsos_font_weight(ui_font(),0);
    app.cover_load_ms = app.cover_decode_ms = app.cover_scale_ms = app.cover_loads = 0;
    if(alarm_active()){
        memset(app.canvas.pixels,255,READER_CACHE_BYTES);alarm_overlay();
        app.font.ui_size_percent=app.font.ui_weight_override=0;return;
    }
    if(pin_input_frame()){pin_input_draw();return;}
    if(app.page==THINGSOS_UI_PASSWORD && input_uses_qwerty() &&
       (app.keyboard_refresh || app.input_refresh) && !app.force_refresh && !app.transition_refresh) {
        wifi_password_update(app.keyboard_refresh);
        goto draw_message;
    }
    bool restored_reader=false;
    if(app.page==THINGSOS_UI_READING && !(app.annotation_open && app.annotation_list) &&
       !app.wd_notes_open && !app.wd_consent && !app.wd_cloud_prompt) {
        reader_annotations_prepare();
        thingsos_font_size(&app.font,app.px);thingsos_font_weight(&app.font,app.weight_choice);
        restored_reader=reader_cache_restore();
        thingsos_font_weight(&app.font,0);
    }
    if(restored_reader) {
        /* Restore the cached body once, without clearing it immediately before
           that copy. Outer controls still start white and use live state. */
        size_t begin=(size_t)reader_body_top()*reader_width();
        size_t end=(size_t)(reader_body_top()+reader_body_height())*reader_width();
        memset(app.canvas.pixels,255,begin);
        memset(app.canvas.pixels+end,255,READER_CACHE_BYTES-end);
    } else memset(app.canvas.pixels, 255, READER_CACHE_BYTES);
    if(app.page!=THINGSOS_UI_LOCK)draw_status();
    if(app.page==THINGSOS_UI_LOCK_PIN){pin_draw();return;}
    if(usb_draw()) goto draw_message;
    if(app_draw()) goto draw_message;
    if(reader_annotations_draw())goto draw_message;
    if(wd_draw()) goto draw_message;
    if(weidu_draw()) goto draw_message;
    if(cubox_draw()) goto draw_message;
    if (management_draw()) goto draw_message;
    if (alignment_draw()) goto draw_message;
    if (feature_draw()) goto draw_message;
    switch (app.page) {
        case THINGSOS_UI_LIBRARY:
        case THINGSOS_UI_RECENT:
            header(app.page == THINGSOS_UI_RECENT ? "最近阅读" : "书库");
            size_t visible_count =
                app.page == THINGSOS_UI_RECENT ? app.recent_count : library_visible_count();
            if(app.page==THINGSOS_UI_LIBRARY && visible_count)library_sorted(0);
            char summary[100];
            if(app.page==THINGSOS_UI_LIBRARY) {
                centered_label("本地",264,154,28,80);centered_label("微读",344,154,28,80);
                centered_label("统计",566,154,26,82);
                thingsos_rect(&app.canvas,app.library_weidu?352:272,181,64,3,0);
            }
            snprintf(summary, sizeof(summary), "%s · %u 本",app.page==THINGSOS_UI_LIBRARY && app.library_weidu?"微读离线":"本地书籍", (unsigned)visible_count);
            label(summary, 36, 276, 24,470);
            library_controls();
            if (!visible_count)
                label(
                    app.page == THINGSOS_UI_RECENT ? "暂无阅读记录"
                    : app.library_weidu && app.library_query[0] ? "没有找到匹配书名，点击搜索修改或清空"
                    : media() && app.library_weidu ? "暂无微读离线书籍，请先在微读缓存"
                    : media()                      ? "当前目录没有书籍"
                                                   : "未检测到 SD 卡",
                    36, 350, 30, 612);
            if (app.library_cards && app.page == THINGSOS_UI_LIBRARY) {
                for (size_t i = 0; i < 9 && i + app.list_start < visible_count; ++i) {
                    size_t index = app.library_indices[app.list_start + i];
                    int x = 36 + (int)(i % 3) * 208, y = 330 + (int)(i / 3) * 250;
                    cover(&app.books[index], x + 24, y, 148, 188);
                    centered_label(app.books[index].name, x + 4, y + 227, 26, 188);
                }
                nav();
                break;
            }
            size_t hero = app.book_count;
            if (app.page == THINGSOS_UI_LIBRARY && visible_count) {
                hero = app.library_indices[0];
                book_t* b = &app.books[hero];
                thingsos_book_record_t* record =
                    thingsos_records_book(&app.records, b->id, NULL, NULL);
                cover(b, 36, 344, 124, 174);
                label(b->name, 188, 389, 32, 460);
                label(b->author, 188, 440, 24, 460);
                snprintf(
                    summary, sizeof(summary), "%s · %u%%", b->epub ? "EPUB" : thingsos_markdown_path(b->path)?"MD":"TXT",
                    record ? (unsigned)record->percent : 0);
                label(summary, 188, 486, 24, 460);
                thingsos_rect(&app.canvas, 188, 508, 460, 5, 220);
                thingsos_rect(
                    &app.canvas, 188, 508, record ? (int)(460 * record->percent / 100) : 0, 5, 0);
            }
            int base = app.page == THINGSOS_UI_LIBRARY ? 620 : 300;
            size_t rows = app.page == THINGSOS_UI_LIBRARY ? 3 : 4;
            size_t list_count = app.page == THINGSOS_UI_LIBRARY && visible_count ? visible_count - 1
                                                                                 : visible_count;
            for (size_t i = 0; i < rows && i + app.list_start < list_count; ++i) {
                size_t index = app.page == THINGSOS_UI_RECENT ? recent_book(app.list_start + i)
                                                              : app.library_indices[app.list_start + i + 1];
                if (index >= app.book_count) continue;
                book_t* b = &app.books[index];
                int y = base + (int)i * 150;
                thingsos_book_record_t* record =
                    thingsos_records_book(&app.records, b->id, NULL, NULL);
                cover(b, 36, y, 94, 130);
                label(b->name, 154, y + 42, 30, 490);
                label(b->author, 154, y + 80, 22, 490);
                if (record && record->order)
                    snprintf(
                        summary, sizeof(summary), "%s · 第 %u 页 · %u%%", b->epub ? "EPUB" : thingsos_markdown_path(b->path)?"MD":"TXT",
                        record->page ? (unsigned)record->page : 1, (unsigned)record->percent);
                else
                    snprintf(summary, sizeof(summary), "%s · 未读", b->epub ? "EPUB" : thingsos_markdown_path(b->path)?"MD":"TXT");
                label(summary, 154, y + 115, 22, 490);
                thingsos_rect(&app.canvas, 36, y + 143, 612, 1, 200);
            }
            if (app.page == THINGSOS_UI_RECENT && visible_count)
                label("清空最近阅读", 36, 1055, 26, 612);
            nav();
            break;
        case THINGSOS_UI_READING: {
            if (app.indexed_px != app.px || app.indexed_line != app.line_choice ||
                app.indexed_margin != app.margin_choice ||
                app.indexed_indent != app.indent_chars || app.indexed_landscape!=app.landscape_enabled || app.indexed_immersive!=app.immersive_enabled || app.indexed_vertical!=app.vertical_enabled || app.indexed_monochrome!=(app.refresh_mode==1)) {
                app.pagination_valid = app.pagination_failed = false;
            }
            thingsos_font_size(&app.font, app.px);
            thingsos_font_weight(&app.font,app.weight_choice);
            app.image_hit_count = 0;
            if(!restored_reader)app.text_hits.count = app.text_hits.line_count = app.text_hits.glyph_count = 0;
            bool restored=restored_reader;
            if (!restored) app.end = thingsos_text_page_layout_flow(
                &app.font, &app.canvas, &app.chapter, app.start, reader_content_left(&app.chapter), reader_body_top(),
                reader_content_width(&app.chapter), reader_body_height(),
                reader_lines[app.line_choice], reader_indent_px(), reader_epub_paragraphs(), picture, NULL,
                &app.text_hits,app.vertical_enabled);
            /* Render pixels and hit regions together. Cached pages restore both;
               measure again only if optional hit-region allocation failed. */
            {
                if(restored && !reader_cache_restored_hits) thingsos_text_page_layout_flow(
                    &app.font, NULL, &app.chapter, app.start, reader_content_left(&app.chapter), reader_body_top(),
                    reader_content_width(&app.chapter), reader_body_height(), reader_lines[app.line_choice], reader_indent_px(),
                    reader_epub_paragraphs(), NULL, NULL, &app.text_hits,app.vertical_enabled);
                app.text_hits_chapter = app.chapter_index;
                app.text_hits_start = app.start;
            }
            thingsos_font_weight(&app.font,0);
            char info[96];
            unsigned progress = book_progress();
            if(app.epub.plain_text) {
                if(app.pagination_valid)snprintf(info,sizeof(info),"%u / %u 页 · %u%%",
                    (unsigned)current_page()+1,(unsigned)app.page_count,progress);
                else snprintf(info,sizeof(info),"全书约 %u%%",progress);
            } else if (app.pagination_valid)
                snprintf(info, sizeof(info), "第 %u / %u 章     %u / %u 页 · %u%%", app.chapter_index + 1,
                    app.epub_open ? (unsigned)app.epub.spine_count : 1, (unsigned)current_page() + 1,
                    (unsigned)app.page_count, progress);
            else snprintf(info, sizeof(info), "第 %u / %u 章 · 全书约 %u%%", app.chapter_index + 1,
                    app.epub_open ? (unsigned)app.epub.spine_count : 1,
                    progress);
            if(!app.immersive_enabled) {
            label(info, 36, reader_height()-43, 24, reader_width()-72);
            thingsos_rect(&app.canvas, 36, reader_height()-24, reader_width()-72, 6, 220);
            thingsos_rect(&app.canvas, 36, reader_height()-24, (int)((reader_width()-72) * progress / 100), 6, 0);
            }
            reader_annotations_selection_draw();
            if (app.menu) {
                if(!app.jump)thingsos_draw_reader_menu(
                    ui_font(), &app.canvas,
                    (thingsos_reader_style_t){ app.px, app.line_choice, app.margin_choice,
                        app.weight_choice,app.indent_chars,app.ripple_enabled,app.wd_id[0]!=0 });
                if(!app.image_view.open) {
                    thingsos_rect(&app.canvas,16,8,96,64,255);
                    for(int d=0;d<=16;d++) {
                        thingsos_rect(&app.canvas,40+d,36-d,5,5,0);
                        thingsos_rect(&app.canvas,40+d,36+d,5,5,0);
                    }
                }
                if(app.jump)reader_jump_draw();
            }
            if (app.image_view.open && app.image_pixels) {
                thingsos_rect(&app.canvas, 0, 74, reader_width(), reader_height()-74, 255);
                label("‹ 返回阅读      图片查看", 36, 140, 32, 612);
                thingsos_size_t fit;
                thingsos_image_fit_width(app.image_w,app.image_h,reader_width()-72,&fit);
                unsigned zw = fit.width * app.image_view.zoom_percent / 100,
                         zh = fit.height * app.image_view.zoom_percent / 100;
                thingsos_image_paint_clipped(
                    &app.canvas, app.image_pixels, app.image_w, app.image_h,
                    36 - (int)app.image_view.pan_x, 190 - (int)app.image_view.pan_y, zw, zh, 36,
                    190, reader_width() - 36, reader_height() - 236,
                    zw < app.image_w || zh < app.image_h, true);
                snprintf(
                    info, sizeof(info), "−        %u%%       ＋       适应宽度",
                    app.image_view.zoom_percent);
                label(info, 36, reader_height()-136, 28, reader_width()-72);
                label("拖动查看 · 返回保留原阅读位置", 36, reader_height()-66, 24, reader_width()-72);
            }
            /* Footnote window: floats over the page, never changes pagination.
               Its height follows the note so a one-line note does not open a
               mostly empty card; longer notes paginate inside the card. */
            if (app.footnote_open && app.chapter.footnotes &&
                app.footnote_index < app.chapter.footnote_count) {
                const thingsos_footnote_t* note = app.chapter.footnotes + app.footnote_index;
                const char* body = app.chapter.notes ? app.chapter.notes + note->note_offset : "";
                size_t bytes = note->note_bytes;
                thingsos_font_size(&app.font, app.px);
                const int full = app.landscape_enabled?300:420;
                int body_height = full;
                size_t end_at_full = reader_annotation_page(
                    NULL, body, bytes, app.footnote_offset, FOOTNOTE_TEXT_LEFT, 0,
                    FOOTNOTE_TEXT_WIDTH, full, 165);
                for (int probe = 150; probe < full; probe += 30)
                    if (reader_annotation_page(NULL, body, bytes,
                                                  app.footnote_offset, FOOTNOTE_TEXT_LEFT, 0,
                                                  FOOTNOTE_TEXT_WIDTH, probe, 165) == end_at_full) {
                        body_height = probe;
                        break;
                    }
                int height = FOOTNOTE_HEAD_BYTES + body_height + FOOTNOTE_FOOT_BYTES;
                if (height > FOOTNOTE_CARD_MAX) height = FOOTNOTE_CARD_MAX;
                app.footnote_card_height = height;
                thingsos_rect(&app.canvas, FOOTNOTE_CARD_LEFT, FOOTNOTE_CARD_TOP,
                              FOOTNOTE_CARD_WIDTH, height, 0);
                thingsos_rect(&app.canvas, FOOTNOTE_CARD_LEFT + 4, FOOTNOTE_CARD_TOP + 4,
                              FOOTNOTE_CARD_WIDTH - 8, height - 8, 255);
                label("脚注", FOOTNOTE_CARD_LEFT + 32, FOOTNOTE_CARD_TOP + 58, 30, 240);
                char mark[48];
                size_t mark_bytes = note->end - note->begin;
                if (mark_bytes >= sizeof(mark)) mark_bytes = sizeof(mark) - 1;
                if (note->begin + mark_bytes <= app.chapter.bytes) {
                    memcpy(mark, app.chapter.text + note->begin, mark_bytes);
                    mark[mark_bytes] = 0;
                    label(mark, FOOTNOTE_CARD_LEFT + FOOTNOTE_CARD_WIDTH - 84, FOOTNOTE_CARD_TOP + 56, 26, 80);
                }
                thingsos_rect(&app.canvas, FOOTNOTE_CARD_LEFT + 32, FOOTNOTE_CARD_TOP + 84,
                              FOOTNOTE_CARD_WIDTH - 64, 2, 0);
                thingsos_font_size(&app.font, app.px);
                app.footnote_next = reader_annotation_page(
                    &app.canvas, body, bytes, app.footnote_offset, FOOTNOTE_TEXT_LEFT,
                    FOOTNOTE_CARD_TOP + FOOTNOTE_HEAD_BYTES, FOOTNOTE_TEXT_WIDTH, body_height,
                    165);
                label(app.footnote_next < bytes ? "点击窗口翻页 · 点击空白处关闭"
                                                : "点击空白处关闭",
                      FOOTNOTE_CARD_LEFT + 32,
                      FOOTNOTE_CARD_TOP + height - FOOTNOTE_FOOT_BYTES + 34, 22,
                      FOOTNOTE_CARD_WIDTH - 64);
                thingsos_font_size(&app.font, app.px);
            }
            break;
        }
        case THINGSOS_UI_CHAPTERS: {
            header("章节目录");
            label(app.read_title, 36, 255, 24, 612);
            reader_control("章节",36,280,292,true);
            reader_control("书签",356,280,292,false);
            size_t count = reader_toc_count();
            size_t current=reader_toc_current();
            app.toc_draw_count=0;
            for (size_t i = 0; i < 6 && i + app.list_start < count; ++i) {
                char line[240];
                unsigned ch = (unsigned)(i + app.list_start);
                thingsos_toc_entry_t target=reader_toc_entry(ch);
                app.toc_draw_entries[app.toc_draw_count++]=target;
                if (app.epub_open)
                    snprintf(line,sizeof(line),"%s",target.title);
                else
                    snprintf(
                        line, sizeof(line), "分段 %u", ch + 1);
                size_t first =
                    app.epub_open ? thingsos_page_find(app.pages, app.page_count, target.chapter, target.offset) : ch * 10;
                int y=350+(int)i*100;
                if(ch==current) {
                    thingsos_rect(&app.canvas,36,y,594,84,app.refresh_mode==1?255:235);
                    /* DU cannot retain gray levels. A sparse, stable pattern
                       keeps the same light highlight in fast reading mode. */
                    if(app.refresh_mode==1)for(int py=y;py<y+84;py+=2)
                        for(int px=36+(py%4==0?0:2);px<630;px+=4)thingsos_rect(&app.canvas,px,py,1,1,0);
                }
                unsigned depth=app.epub_open?target.level:0;
                if(depth>4)depth=4; /* Keep deep titles readable on 684 px panel. */
                int inset=(int)depth*28;
                label(line,36+inset,y+54,27,470-inset);
                bool known=app.pages && first<app.page_count &&
                    (!app.epub_open || app.pages[first].chapter==target.chapter);
                char page[32];
                if(known) snprintf(page,sizeof(page),"%u 页",(unsigned)first+1);
                else strlcpy(page,"—",sizeof(page));
                thingsos_font_size(ui_font(),24);
                thingsos_label_right(ui_font(),&app.canvas,page,518,y+54,112);
                thingsos_rect(&app.canvas,36,y+83,594,1,210);
            }
            if(count>6) {
                thingsos_rect(&app.canvas,650,350,6,600,220);
                int thumb=(int)(600*6/count);if(thumb<24) thumb=24;
                size_t start=app.list_start<count-6?app.list_start:count-6;
                thingsos_rect(&app.canvas,650,350+(int)(start*(600-thumb)/(count-6)),6,thumb,0);
            }
            button("返回阅读", 1000);
            break;
        }
        case THINGSOS_UI_SETTINGS:
            /* The selected bottom Tab already names this page. */

            label("阅读、连接与设备", 36, 135, 24, 612);
            setting_row("界面字体", app.ui_font_path[0] ? "自定义字体" : "内置雅宋", 170);
            char setting[80];
            snprintf(setting, sizeof(setting), "%u px · 刷新模式 %u", app.ui_px ? app.ui_px : 30, app.ui_refresh_mode + 1);
            setting_row("显示", setting, 244);
            setting_row("WiFi 与传输", thingsos_network_connected() ? "已连接" : "未连接", 318);
            setting_row("蓝牙",bt_ui_summary(),392);
            setting_row("AI 按键",app.ai_key_enabled?"录音 · 自动保存":"已关闭",466);
            setting_row("日期与时间", app.auto_time ? "自动校时" : "手动时间", 540);
            snprintf(setting, sizeof(setting), "%u 分钟自动锁屏", app.auto_lock_minutes);
            setting_row("电源与锁屏", setting, 614);
            setting_row("文件目录", app.records.library_path, 688);
            setting_row("USB 直连模式", "已关闭", 762);
            setting_row("固件升级", esp_app_get_description()->version, 836);
            setting_row("恢复", "救援 / 回退 / USB", 910);
            setting_row("日志", "异常记录 / 导出诊断包", 984);
            nav();
            break;
        case THINGSOS_UI_READER_MORE:
            reader_more_draw();
            break;
        case THINGSOS_UI_FONTS:
            header(app.font_from_reader?"阅读字体":"界面字体");
            label("点选预览，确认后应用", 36, 265, 24, 500);
            label("扫描", 552, 265, 24, 96);
            font_option("方正屏显雅宋 · 内置",290,app.font_candidate==-1);
            for (size_t i = 0; i < 4 && i + app.list_start < app.font_count; ++i) {
                char line[240];
                strlcpy(line,font_display_name(app.fonts[i + app.list_start]),sizeof(line));
                font_option(line,390+(int)i*95,app.font_candidate==(int)(i+app.list_start));
            }
            {
                thingsos_font_t* preview=app.preview_font.face?&app.preview_font:
                    app.font_candidate!=-2 && !strcmp(app.preview_path,font_setting_path())?font_setting_face():NULL;
                if(preview) {
                unsigned prior_size=preview->size,prior_weight=preview->weight;
                unsigned prior_ui_size=preview->ui_size_percent,prior_ui_weight=preview->ui_weight_override;
                bool prior_dither=preview->dither;
                preview->dither=(reader_surface(app.page)?app.refresh_mode:app.ui_refresh_mode)==1;
                preview->ui_size_percent=preview->ui_weight_override=0;
                thingsos_font_size(preview, app.font_from_reader?app.px:app.ui_px);
                thingsos_font_weight(preview,app.font_from_reader?app.weight_choice:app.ui_weight);
                thingsos_label(
                    preview, &app.canvas, "山川与日月，文字与时光。", 36, 890, 612);
                thingsos_label(
                    preview, &app.canvas, app.font_from_reader?"Example Test · 阅读字体预览":"Example Test · 界面字体预览", 36, 960, 612);
                thingsos_font_size(preview,prior_size);thingsos_font_weight(preview,prior_weight);
                preview->ui_size_percent=prior_ui_size;preview->ui_weight_override=prior_ui_weight;
                preview->dither=prior_dither;
                }
            }
            weidu_button("取消",36,1015,292);
            weidu_button("应用",356,1015,292);
            if(!app.font_from_reader)nav();
            break;
        case THINGSOS_UI_UPGRADE:
        case THINGSOS_UI_TRANSFER:
        case THINGSOS_UI_RECOVERY:
            /* Those pages are drawn by alignment_draw/upgrade_draw before this
               switch runs; keep the case only so the enum stays exhaustive. */
            break;
        default:
            header("功能尚在接入");
            nav();
            break;
    }
draw_message:
    reader_annotations_card_draw();
    if(app.page==THINGSOS_UI_READING && app.wd_thought_open)wd_thought_draw();
    if(app.page==THINGSOS_UI_LOCK) {
        app.font.ui_size_percent=app.font.ui_weight_override=0;
        app.lock_snapshot_valid=true;
        return;
    }
    alignment_confirm_draw();
    app_preview_draw();
    if(app.controls_open)app_controls_draw();
    online_modal_draw();
    if (app.message[0] && app.page != THINGSOS_UI_WIFI && app.page != THINGSOS_UI_TRANSFER && !(app.page == THINGSOS_UI_PASSWORD && input_uses_qwerty())) {
        bool launcher=app.page==THINGSOS_UI_APPS&&!app.controls_open;
        int message_y = launcher?82:app.controls_open?app.canvas.height-110:app.page == THINGSOS_UI_PASSWORD ? 1090 : 215;
        thingsos_rect(&app.canvas, 24, message_y, 636, launcher?60:75, 235);
        label(app.message, 36, message_y + (launcher?39:45), 24, 612);
    }
    if((app.volume_overlay_pending || app.volume_overlay_until>now()) && !app.controls_open)
        volume_overlay_draw();
    record_feedback_draw();
    alarm_overlay();
    /* Pagination/cache work between frames must use reading settings. */
    app.font.ui_size_percent = app.font.ui_weight_override = 0;
    app.font.dither=app.refresh_mode==1;
    if(app.page==THINGSOS_UI_LOCK)app.lock_snapshot_valid=true;
}
static void network_hotspot_policy(void);
static bool home_tab(thingsos_ui_page_t page) {
    return page==THINGSOS_UI_LIBRARY || page==THINGSOS_UI_WEIDU ||
        page==THINGSOS_UI_APPS || page==THINGSOS_UI_SETTINGS;
}
static bool reader_surface(thingsos_ui_page_t page) {
    return app.opened && (page==THINGSOS_UI_READING || page==THINGSOS_UI_CHAPTERS ||
        page==THINGSOS_UI_BOOKMARKS || (page==THINGSOS_UI_FONTS && app.font_from_reader) || page==THINGSOS_UI_READER_MORE ||
        (page==THINGSOS_UI_PASSWORD && (app.editing==4 || app.editing==6) && app.input_return==THINGSOS_UI_READING));
}
static bool reader_ui_active(void) {
    if(alarm_active())return true;
    return reader_surface(app.page) && (app.page!=THINGSOS_UI_READING || app.menu || app.jump ||
        app.image_view.open || app.footnote_open || app.annotation_open || app.annotation_selecting ||
        app.wd_notes_open || app.wd_thought_open || app.wd_consent || app.wd_cloud_prompt ||
        app.wd_sync_panel || app.confirm_action);
}
static bool home_navigation_page(thingsos_ui_page_t page) {
    return home_tab(page) || page==THINGSOS_UI_TRANSFER || page==THINGSOS_UI_DISPLAY || page==THINGSOS_UI_TIME ||
        page==THINGSOS_UI_POWER || page==THINGSOS_UI_WIFI || page==THINGSOS_UI_PASSWORD ||
        page==THINGSOS_UI_UPGRADE || page==THINGSOS_UI_RECOVERY || page==THINGSOS_UI_LOGS || page==THINGSOS_UI_USB ||
        page==THINGSOS_UI_FILES || page==THINGSOS_UI_FONTS || page==THINGSOS_UI_WALLPAPER ||
        page==THINGSOS_UI_AI_SETTINGS || page==THINGSOS_UI_ALARM;
}
static bool buffered_touch_page(thingsos_ui_page_t page) {
    return home_navigation_page(page) || page==THINGSOS_UI_LOCK_PIN || page==THINGSOS_UI_READER_MORE ||
        page==THINGSOS_UI_CHAPTERS || page==THINGSOS_UI_BOOKMARKS ||
        page==THINGSOS_UI_STATS || page==THINGSOS_UI_AI_SETTINGS ||
        (page==THINGSOS_UI_READING && !app.image_view.open);
}
static bool reader_input_awaiting_release(const thingsos_keys_t* keys,bool pressed,bool held,bool dragging) {
    return (keys->key && !keys->long_sent && !keys->cancelled) || (pressed && !held && !dragging);
}
static bool reader_input_can_idle(const thingsos_keys_t* keys,bool contact,bool pressed,bool sampled_contact) {
    return !contact && !pressed && !keys->key && !sampled_contact;
}
static bool ui_static_panel_page(void) {
    /* Audio controls remain static between worker acknowledgements/timer ticks.
       Use vendor white maintenance and verified immediate rail-off on them too. */
    return app_media.preview || app.page==THINGSOS_UI_MUSIC || app.page==THINGSOS_UI_RECORDER ||
        app.page==THINGSOS_UI_WEIDU || app.page==THINGSOS_UI_CUBOX ||
        (app.page==THINGSOS_UI_PASSWORD && (app.input_return==THINGSOS_UI_WEIDU || app.input_return==THINGSOS_UI_CUBOX));
}
static bool ui_background_frame_yields(bool queued_input) {
    /* A contact accepted by the UI may paint before its release. An unrelated
       redraw must first dispatch queued input, including a complete DOWN/UP
       that arrived while fonts were being laid out. */
    return !app.ui_action_at && queued_input;
}
static void navigate(thingsos_ui_page_t page) {
    thingsos_diag_event(DIAG_PAGE,0,(unsigned)app.page,(unsigned)page,0,0);
    if(page==THINGSOS_UI_LOCK && (app.pin.enabled || app.pin.fault))pin_lock();
    if(app.pin.required && page!=THINGSOS_UI_LOCK && page!=THINGSOS_UI_LOCK_PIN)page=THINGSOS_UI_LOCK_PIN;
    thingsos_board_private_input(page==THINGSOS_UI_LOCK_PIN);
    if(page!=THINGSOS_UI_LOCK_PIN)pin_clear_input();
    bool lock_edge=(page==THINGSOS_UI_LOCK)!=(app.page==THINGSOS_UI_LOCK);
    app_controls_wifi_return=false;
    app_controls_bt_return=false;
    if((page==THINGSOS_UI_USB||page==THINGSOS_UI_UPGRADE||page==THINGSOS_UI_RECOVERY)&&!thingsos_ima_quiesce_locked()){say("上传尚未结束，请稍后重试");return;}
    if((page==THINGSOS_UI_USB||page==THINGSOS_UI_UPGRADE||page==THINGSOS_UI_RECOVERY)&&!thingsos_cubox_quiesce_locked()){say("Cubox 下载尚未结束，请稍后重试");return;}
    if(ima_ui&&page!=app.page)ima_ui->view=0;
    if(page!=THINGSOS_UI_CUBOX && page!=THINGSOS_UI_READING && page!=THINGSOS_UI_PASSWORD && thingsos_cubox_pairing())thingsos_cubox_pair(false);
    if((page==THINGSOS_UI_USB||page==THINGSOS_UI_UPGRADE||page==THINGSOS_UI_RECOVERY)&&!thingsos_audio_quiesce_locked()){say("音频保存失败，请检查 SD 卡");return;}
    if(page==THINGSOS_UI_WIFI&&app.page!=THINGSOS_UI_PASSWORD&&app.page!=THINGSOS_UI_WIFI)app.network_return=app.page;
    if((page==THINGSOS_UI_SYSTEM||page==THINGSOS_UI_TRANSFER||page==THINGSOS_UI_FILES||page==THINGSOS_UI_RECORDER||page==THINGSOS_UI_MUSIC||page==THINGSOS_UI_CUBOX)&&app.page!=page&&app.page!=THINGSOS_UI_LOCK)app.app_return=app.page;
    app.controls_open=app.controls_wifi_confirm=false;app.audio_progress=false;
    WHERE("navigate -> %d from %d",(int)page,(int)app.page);
    if(lock_edge){app.lock_wave_pending=true;app.lock_wave_forward=page==THINGSOS_UI_LOCK;}
    app.light_page=false;
    bool source_reader=reader_surface(app.page);
    if(page==THINGSOS_UI_FONTS && app.page!=page)app.font_from_reader=reader_surface(app.page);
    if(page!=THINGSOS_UI_READING)app.jump_pending=false;
    bool reading=reader_surface(app.page) && reader_surface(page);
    bool regional=app.page!=page && home_navigation_page(app.page) && home_navigation_page(page);
    bool tabs=home_tab(app.page) && home_tab(page);
    if (app.page != page && !tabs && !lock_edge && !source_reader && !reader_surface(page) &&
        app.page != THINGSOS_UI_LOCK_PIN && page != THINGSOS_UI_LOCK_PIN) {
        thingsos_board_menu_refresh();
    }
    if(regional && !reading) {
        /* Keep the navigation hint until the destination frame is presented. */
        thingsos_board_cleanup_area(0,tabs?90:74,684,tabs?1008:1024);
    }
    ESP_LOGI("ThingsOS_nav","Navigate from=%u to=%u stage=begin",(unsigned)app.page,(unsigned)page);
    if(page!=THINGSOS_UI_FILES)app.file_action_open=app.file_more=false;
    if(page!=THINGSOS_UI_READING){app.annotation_selecting=app.annotation_dragging=false;}
    if(page!=THINGSOS_UI_READING && !(page==THINGSOS_UI_PASSWORD && app.editing==6))app.annotation_open=false;
    if(page==THINGSOS_UI_CHAPTERS && app.epub_open) {
        app.toc_pending=true;app.toc_draw_count=0;
    }
    if(page==THINGSOS_UI_READING && app.opened)app.menu_root=app.reader_menu_root;
    if(page==THINGSOS_UI_LIBRARY || page==THINGSOS_UI_SETTINGS || page==THINGSOS_UI_APPS)
        app.menu_root=page;
    else if(page==THINGSOS_UI_WEIDU)
        app.menu_root=app.weidu_detail && app.weidu_detail_return!=THINGSOS_UI_WEIDU
            ?THINGSOS_UI_LIBRARY:THINGSOS_UI_WEIDU;
    app.reader_background_refresh=false;
    thingsos_tilt_reset(&app.tilt);
    thingsos_board_tilt_active(false);
    ESP_LOGI("ThingsOS_nav","Navigate stage=sensor-ready");
    app.footnote_open = false;
    if(app.wd_active && page!=THINGSOS_UI_READING && page!=THINGSOS_UI_CHAPTERS && page!=THINGSOS_UI_BOOKMARKS && page!=THINGSOS_UI_FONTS && page!=THINGSOS_UI_READER_MORE)wd_finish();
    if(page==THINGSOS_UI_READING && app.opened && !app.wd_active)wd_opened();
    if(page!=THINGSOS_UI_READING && page!=THINGSOS_UI_WEIDU){wd_thought_close();app.wd_notes_open=app.wd_long_open=false;app.wd_sync_panel=false;app.wd_rt_panel=false;app.wd_consent=false;}

    if(app.page==THINGSOS_UI_TRANSFER&&page!=THINGSOS_UI_TRANSFER) {
        /* Leaving the page ends an incidental visit; an explicit "开启传书"
           keeps serving until the button is used again (or the device sleeps). */
        if(!app.service_pinned){app.service_on=false;thingsos_network_service(false);}
    }
    if(page==THINGSOS_UI_TRANSFER&&app.page!=THINGSOS_UI_TRANSFER) {
        app.service_on=true;thingsos_network_service(true);
    }
    if(page!=THINGSOS_UI_FILES && page!=THINGSOS_UI_LOCK)file_picture_release();
    if(app.page!=page || page!=THINGSOS_UI_LOCK)app.lock_snapshot_valid=false;
    app.ripple_pending=false;
    if (page != THINGSOS_UI_READING) reader_cache_clear();
    if (app.page != page && !regional && !reading)
        app.transition_refresh = true;
    app.keyboard_refresh = false;
    app.input_refresh = false;
    if (page == THINGSOS_UI_WIFI && app.page != THINGSOS_UI_PASSWORD && app.page != THINGSOS_UI_WIFI) wifi_scan(false);
    if (page != THINGSOS_UI_READING) preferences_save();
    if (page == THINGSOS_UI_STATS) app.stats_from_reader = app.page == THINGSOS_UI_READING;
    if (app.page == THINGSOS_UI_READING && page != THINGSOS_UI_READING) save_state();
    if (page != THINGSOS_UI_WIFI && page != THINGSOS_UI_PASSWORD) {
        memset(app.wifi_password, 0, sizeof(app.wifi_password));
        memset(&app.input, 0, sizeof(app.input));
    }
    app.menu = false;
    app.jump = false;
    if(page==THINGSOS_UI_SYSTEM) {if(app.page!=page||!system_ui.visible)app_system_begin();}
    else app_system_leave();
    app.page = page;
    /* A remote search may finish while its editor is open. Its query-owned
       result invalidates the shelf without repainting the keyboard. Consume
       it on return, including Cancel, before drawing the result page. */
    if(page==THINGSOS_UI_WEIDU && app.weidu_query[0] && !app.mp_mode &&
       !app.weidu_detail && !app.weidu_shelf_valid)weidu_shelf_load();
    reader_canvas_geometry();
    network_hotspot_policy();
    app.list_start = page==THINGSOS_UI_FONTS && app.font_candidate>=0 ? (unsigned)app.font_candidate/4*4 :
        page==THINGSOS_UI_CHAPTERS?reader_toc_center_start():0;
    if(page==THINGSOS_UI_CHAPTERS)app.toc_follow_current=true;
    thingsos_runtime_navigate(
        &app.runtime,
        page == THINGSOS_UI_READING    ? THINGSOS_READING
        : page == THINGSOS_UI_LIBRARY  ? THINGSOS_LIBRARY
        : page == THINGSOS_UI_TRANSFER ? THINGSOS_TRANSFER
                                       : THINGSOS_SETTINGS,
        now());
    app.dirty = true;
    ESP_LOGI("ThingsOS_nav","Navigate stage=ready page=%u",(unsigned)app.page);
}
static void power_action(unsigned action);
static void unlock_screen(void);
static bool lock_due(uint64_t time) {
    return time >= app.activity_at &&
           time - app.activity_at > app.auto_lock_minutes * 60000ULL;
}
// clang-format off
#include "network_ui.inc"
#include "files_ui.inc"
#include "features.inc"
#include "stats_ui.inc"
#include "alignment.inc"
#include "management.inc"
// clang-format on
static void unlock_screen(void) {
    if(app.pin.enabled || app.pin.fault || app.pin.required){pin_wake();return;}
    thingsos_ui_page_t target = app.lock_return;
    if (target == THINGSOS_UI_LOCK || (target == THINGSOS_UI_READING && !app.opened))
        target = THINGSOS_UI_LIBRARY;
    navigate(target);
    app.activity_at = now();
    app.force_refresh = true;
    app.lock_wave_pending=true;app.lock_wave_forward=false;
}
static esp_err_t present_lock_transition(void){
    esp_err_t e=thingsos_board_present_lock_transition(&app.canvas,app.lock_wave_forward);
    if(e==ESP_OK)app.lock_wave_pending=false;
    return e;
}
static void power_action(unsigned action) {
    if(alarm_active()&&action){alarm_finish(false);if(action==1)return;}
    if(action && app.jump_pending)reader_jump_cancel();
    thingsos_board_tilt_active(false);thingsos_tilt_reset(&app.tilt);
    if (!action) return;
    if(app.wd_power_action){
        app.wd_power_action=0;app.wd_power_stage=0;
        if(action==1){
            thingsos_runtime_overlay(&app.runtime,app.menu || app.annotation_open || app.annotation_selecting || app.page!=THINGSOS_UI_READING,now());
            if(app.opened && app.page==THINGSOS_UI_READING && !app.wd_active)wd_opened();
            say("已取消息屏，阅读可继续");return;
        }
        if(thingsos_weidu_busy())thingsos_weidu_cancel();
    }
    if (action == 1 && app.asleep) {
        app.asleep = false;
        if (thingsos_board_resume() != ESP_OK) say("触控唤醒失败，请重启");
        network_wake_after_sleep();
        thingsos_runtime_overlay(
            &app.runtime, app.menu || app.page != THINGSOS_UI_READING || app.image_view.open,
            now());
        app.activity_at = now();
        app.dirty = true;
        app.force_refresh = true;
        app.lock_wave_pending=true;app.lock_wave_forward=false;
        if(app.pin.required || app.pin.enabled || app.pin.fault)pin_wake();
        else if(app.page==THINGSOS_UI_LOCK_PIN)navigate(THINGSOS_UI_POWER);
        else if(app.page==THINGSOS_UI_LOCK)unlock_screen();
        network_hotspot_policy();
        if(app.opened && app.page==THINGSOS_UI_READING && !app.wd_active)wd_opened();
        return;
    }
    if (action == 1 || action == 2) {
        if(!thingsos_ima_quiesce_locked()){say("上传尚未结束，暂时无法休眠");return;}
        if(!thingsos_cubox_quiesce_locked()){say("Cubox 下载尚未结束，暂时无法休眠");return;}
        if(!thingsos_audio_quiesce_locked()){say("录音尚未保存，暂时无法休眠");return;}
        app.controls_open=false;
        if(app.wd_active){
            if(!app.wd_cloud_prompt && app.wd_push_on && thingsos_network_connected() && wall_time()>=WD_SYNC_MIN_TIME){
                app.wd_power_action=action;app.wd_power_stage=1;app.wd_power_deadline=now()+60000;
                thingsos_runtime_overlay(&app.runtime,true,now());
                say(action==2?"正在同步阅读数据，完成后关机；短按电源取消":"正在同步阅读数据，完成后息屏；短按电源取消");
                return;
            }
            wd_finish();
        }
        save_state();
        pin_clear_input();
        app.volume_overlay_until=0;app.volume_overlay_pending=false;
        thingsos_runtime_overlay(&app.runtime, true, now());
        if (!app.asleep) {
            memset(app.canvas.pixels, 255, 684u * 1216u);
            app.canvas.width=684;app.canvas.height=1216;
            lock_draw(action);
            app.ripple_pending = false;
            app.lock_wave_pending=false;
            thingsos_network_sleep();
            if (thingsos_board_suspend(&app.canvas) != ESP_OK && action != 2) {
                thingsos_board_resume();
                network_wake_after_sleep();
                reader_canvas_geometry();
                say("息屏失败，请重试");
                app.dirty = true;
                thingsos_runtime_overlay(
                    &app.runtime,
                    app.menu || app.page != THINGSOS_UI_READING || app.image_view.open, now());
                return;
            }
        }
        app.asleep = true;
        pin_lock();
        app.hotspot = false;
    app.dirty = false;
    WHERE("draw end page=%d",(int)app.page);
        if (action == 2 && thingsos_board_power_off() != ESP_OK) {
            app.asleep = false;
            thingsos_board_resume();
            network_wake_after_sleep();
            say("关机失败，请重试");
            app.dirty = true;
            thingsos_runtime_overlay(
                &app.runtime, app.menu || app.page != THINGSOS_UI_READING || app.image_view.open,
                now());
        }
    }
}
static void auto_sleep_poll(uint64_t time) {
    if(!alarm_active() && !app.asleep && !app.wd_power_action && app.page!=THINGSOS_UI_LOCK &&
       !thingsos_network_busy() && !thingsos_ima_pairing() && !thingsos_cubox_pairing() && lock_due(time))power_action(1);
}
/* Keep the main task responsive and WiFi alive until the one-shot exit upload
   completes. No duration replay is queued after an error or timeout. */
static void reading_power_poll(void) {
    if(!app.wd_power_action)return;
    bool expired=now()>=app.wd_power_deadline;
    if(app.wd_power_stage==1 && (!thingsos_weidu_busy() || expired)){
        app.wd_power_stage=2;wd_finish();
        thingsos_wd_device_poll(!expired);
    }
    if(app.wd_power_stage==2 && (!thingsos_weidu_busy() || expired)){
        unsigned action=app.wd_power_action;
        if(expired && thingsos_weidu_busy())thingsos_weidu_cancel();
        app.wd_power_action=app.wd_power_stage=0;
        ESP_LOGI("ThingsOS_weidu","Power sync finished timeout=%d",expired);
        power_action(action);
    }
}
static void next_page(bool forward) {
    if(online_modal_visible())return;
    app.reader_background_refresh=false;
    if(app.wd_consent||app.wd_cloud_prompt||app.wd_notes_open||app.wd_thought_open)return;
    if (!app.opened) return;
    if(!pagination_prepare())return;
    app.footnote_open = false;
    unsigned before_chapter=app.chapter_index;
    size_t before_start=app.start;
    bool before_image=app.image_hit_count!=0;
    if (!app.pagination_valid) {
        if (forward && app.end > app.start && app.end < app.chapter.bytes) {
            if (app.history_count < 4096) app.history[app.history_count++] = app.start;
            app.start = app.end;
        } else if (!forward && app.history_count) {
            app.start = app.history[--app.history_count];
        } else if (app.epub_open && forward && app.chapter_index + 1 < app.epub.spine_count) {
            if (!load_chapter(app.chapter_index + 1)) { if(!app.epub.weidu_online)say("章节无法打开"); return; }
        } else if (app.epub_open && !forward && app.chapter_index) {
            if (!load_chapter(app.chapter_index - 1)) { if(!app.epub.weidu_online)say("章节无法打开"); return; }
            if(app.pages && app.pagination_chapter>app.chapter_index) {
                size_t last=thingsos_page_find(app.pages,app.page_count,app.chapter_index,UINT32_MAX);
                app.start=app.pages[last].offset;
                goto page_changed;
            }
            size_t offset = 0;
            while (offset < app.chapter.bytes) {
                size_t end = page_end(&app.chapter, offset);
                if (end <= offset || end >= app.chapter.bytes) break;
                offset = end;
                vTaskDelay(pdMS_TO_TICKS(1));
            }
            app.start = offset;
        } else { say(forward ? "已到最后一页" : "已到第一页"); return; }
        goto page_changed;
    }
    size_t page = current_page();
    if (forward && page + 1 < app.page_count)
        go_page(page + 1);
    else if (!forward && page)
        go_page(page - 1);
    else
        say(forward ? "已到最后一页" : "已到第一页");
page_changed:
    app.ripple_pending=app.ripple_enabled && !before_image &&
        (before_chapter!=app.chapter_index || before_start!=app.start);
    app.ripple_forward=forward;
    /* The board counts all successful updates for the global refresh policy. */
    app.runtime.location.chapter = app.chapter_index;
    app.runtime.location.byte_offset = (uint32_t)app.start;
    thingsos_runtime_activity(&app.runtime, now());
    app.activity_at = now();
    app.dirty = true;
}
/* Release-only body gesture. Coordinates already follow the displayed reader
   orientation. Keep modal/drag owners ahead of page turning. */
static bool reader_swipe_page(int x,int y,int dx,int dy) {
    if(app.page!=THINGSOS_UI_READING || !app.opened || app.asleep || app.pin.required ||
       app.controls_open || app.jump_pending || app.annotation_dragging || app.wd_power_action ||
       reader_ui_active() || online_modal_visible())return false;
    if(x<0 || x>=reader_width() || y<reader_body_top() ||
       y>=reader_body_top()+reader_body_height() || abs(dy)<=40 || abs(dy)<=abs(dx))return false;
    app.ui_action_at=now();++app.ui_action_id;
    app.message[0]=0;app.activity_at=now();thingsos_runtime_activity(&app.runtime,app.activity_at);
    next_page(dy<0);
    return true;
}
static size_t reader_image_hit(int x,int y) {
    if(app.page!=THINGSOS_UI_READING || !app.opened || app.asleep || app.pin.required ||
       app.controls_open || app.jump_pending || app.wd_power_action || reader_ui_active() ||
       online_modal_visible() || app.text_hits_chapter!=app.chapter_index || app.text_hits_start!=app.start)
        return SIZE_MAX;
    if(x<0 || x>=reader_width() || y<reader_body_top() || y>=reader_body_top()+reader_body_height())return SIZE_MAX;
    for(size_t hit=0;hit<app.image_hit_count;++hit) {
        size_t index=app.image_hits[hit].index;
        /* Cover-page taps keep their existing navigation ownership. */
        if(index>=app.chapter.image_count || app.chapter.cover_image==index+1)continue;
        if(x>=app.image_hits[hit].x && x<app.image_hits[hit].x+app.image_hits[hit].w &&
           y>=app.image_hits[hit].y && y<app.image_hits[hit].y+app.image_hits[hit].h)return index;
    }
    return SIZE_MAX;
}
static unsigned reader_hold_delay(int x,int y) {
    return reader_image_hit(x,y)!=SIZE_MAX?1000:weidu_hold_delay(x,y);
}
static bool reader_image_hold(int x,int y,uint64_t elapsed) {
    if(elapsed<1000)return false;
    size_t index=reader_image_hit(x,y);if(index==SIZE_MAX)return false;
    if(image_load(app.chapter.images+index,&app.image_pixels,&app.image_w,&app.image_h)) {
        thingsos_image_open(&app.image_view,app.runtime.location);app.image_index=index;
        thingsos_runtime_image_overlay(&app.runtime,true,now());app.dirty=true;
    }else say("图片加载失败，请长按重试");
    return true;
}
static bool reader_content_hold(int x,int y,uint64_t elapsed) {
    /* Image ownership also prevents nearby glyph selection before 1 second. */
    if(reader_image_hit(x,y)!=SIZE_MAX)return reader_image_hold(x,y,elapsed);
    return reader_annotations_hold(x,y);
}
/* HOME returns to the owning menu, even after reader subpages or lock/wake. */
static void reader_more_back(void) {
    navigate(THINGSOS_UI_READING);app.menu=true;
    thingsos_runtime_overlay(&app.runtime,true,now());
}
static void reader_tilt_poll(bool touching) {
    bool active=app.tilt_enabled && app.opened && app.page==THINGSOS_UI_READING && !app.annotation_open && !app.annotation_selecting &&
        !app.asleep && !app.wd_power_action && !app.controls_open && !app.menu && !app.jump && !app.image_view.open &&
        !app.footnote_open && !app.wd_notes_open && !app.wd_thought_open &&
        !app.wd_cloud_prompt && !app.wd_consent && !app.confirm_action && !alarm_active() && !transfer_blocks_ui();
    static uint64_t gate_log_at;
    if(!active && app.tilt_enabled && app.page==THINGSOS_UI_READING && now()>=gate_log_at) {
        gate_log_at=now()+2000;
        ESP_LOGI("ThingsOS_tilt","Blocked touching=%d menu=%d annotation=%d selection=%d jump=%d image=%d footnote=%d notes=%d thought=%d cloud=%d consent=%d power=%d",
            touching,app.menu,app.annotation_open,app.annotation_selecting,app.jump,app.image_view.open,app.footnote_open,app.wd_notes_open,app.wd_thought_open,app.wd_cloud_prompt,app.wd_consent,app.wd_power_action);
    }
    if(!active) {thingsos_board_tilt_active(false);thingsos_tilt_reset(&app.tilt);return;}
    thingsos_board_tilt_configure(app.landscape_enabled,app.tilt_sensitivity);
    if(!thingsos_board_tilt_active(true)){thingsos_tilt_reset(&app.tilt);return;}
    if(touching || app.dirty || now()<app.tilt_poll_at)return;
    app.tilt_poll_at=now()+40;
    int direction=0;uint64_t gesture_at=0;
    if(!thingsos_board_tilt_event(&direction,&gesture_at,&app.tilt))return;
    static uint64_t diagnostic_at;
    if(now()>=diagnostic_at) {
        diagnostic_at=now()+1000;
        ESP_LOGI("ThingsOS_tilt","Input detector calibrated=%d armed=%d angle=%d sample_age=%u origin=%d base=%d peak=%d fired=%d posture=%d level=%u landscape=%d filtered_mg=%d,%d,%d",
            app.tilt.calibrated,app.tilt.armed,app.tilt.angle-app.tilt.base_angle,(unsigned)(now()-app.tilt.last),
            app.tilt.origin_angle,app.tilt.base_angle,app.tilt.peak_angle,app.tilt.fired_direction,
            app.tilt.posture_pending,app.tilt_sensitivity,app.landscape_enabled,app.tilt.x,app.tilt.y,app.tilt.z);
    }
    if(direction) {
        ESP_LOGI("ThingsOS_tilt","Gesture direction=%d event_age=%u sample_age=%u",
            direction,(unsigned)(now()-gesture_at),(unsigned)(now()-app.tilt.last));
        thingsos_runtime_activity(&app.runtime,now());app.activity_at=now();next_page(direction>0);
    }
}
/* The sampler mutex may be busy while no finger is present. A completed
   drag also leaves dragging latched until the next contact; neither is a hold. */
static void reader_tilt_input_poll(bool pressed,bool key_held) {
    reader_tilt_poll(pressed || key_held || thingsos_board_touch_contact_pending());
}
static void return_to_menu(void) {
    if(app.page==THINGSOS_UI_READING && app.reader_from_cubox) {
        release_book();app.reader_from_cubox=false;navigate(THINGSOS_UI_CUBOX);return;
    }
    if(app.page==THINGSOS_UI_READING && app.reader_from_files) {
        release_book();app.reader_from_files=false;
        size_t first=app.file_return_start;navigate(THINGSOS_UI_FILES);app.list_start=first;return;
    }
    thingsos_ui_page_t target=app.menu_root;
    if(app.page==THINGSOS_UI_READING || app.page==THINGSOS_UI_CHAPTERS || app.page==THINGSOS_UI_BOOKMARKS)
        target=app.reader_menu_root;
    if(target!=THINGSOS_UI_LIBRARY && target!=THINGSOS_UI_WEIDU &&
       target!=THINGSOS_UI_APPS && target!=THINGSOS_UI_SETTINGS)target=THINGSOS_UI_LIBRARY;
    wd_thought_close();
    app.wd_notes_open=app.wd_long_open=app.wd_sync_panel=app.wd_consent=app.wd_cloud_prompt=false;
    app.time_edit=false;
    thingsos_font_close(&app.preview_font);
    if(target==THINGSOS_UI_WEIDU)weidu_enter();
    else navigate(target);
}
static void capacitive_key_dispatch(thingsos_key_action_t action) {
    if(alarm_active()){if(action!=THINGSOS_KEY_NONE)alarm_finish(action==THINGSOS_KEY_NEXT);return;}
    if(app.asleep)return;
    if(app.pin.required || app.page==THINGSOS_UI_LOCK_PIN) {
        if(app.page==THINGSOS_UI_LOCK){unlock_screen();return;}
        if(app.pin.required && app.page!=THINGSOS_UI_LOCK_PIN)pin_wake();
        if(action==THINGSOS_KEY_PREVIOUS || action==THINGSOS_KEY_MENU)pin_cancel();
        return;
    }
    if(bt_ui_key(action))return;
    if(ima_key(action))return;
    if(cubox_key(action))return;
    if(app.jump_pending && action!=THINGSOS_KEY_NONE) {
        reader_jump_cancel();
        if(action!=THINGSOS_KEY_HOME)return;
    }
    thingsos_board_tilt_active(false);thingsos_tilt_reset(&app.tilt);
    if(app.asleep)return;
    if(online_modal_key(action))return;
    if((action==THINGSOS_KEY_PREVIOUS||action==THINGSOS_KEY_MENU)&&
       (app.controls_open||app_media.preview||app.page==THINGSOS_UI_APPS||app.page==THINGSOS_UI_RECORDER||app.page==THINGSOS_UI_MUSIC||app.page==THINGSOS_UI_ALARM||app.page==THINGSOS_UI_SYSTEM||app.page==THINGSOS_UI_TRANSFER||app.page==THINGSOS_UI_WIFI)&&app_back())return;
    /* Short motor pulse per key press; 50 ms matches the vendor firmware. */
    if(app.haptic_enabled)thingsos_board_haptic(50);
    if(app.page==THINGSOS_UI_FILES && (action==THINGSOS_KEY_MENU || action==THINGSOS_KEY_PREVIOUS) && (app.file_action_open || app.file_more)) {
        app.file_action_open=app.file_more=false;app.dirty=true;return;
    }
    if((app.page==THINGSOS_UI_READING || app.page==THINGSOS_UI_WEIDU) && (app.annotation_open || app.annotation_selecting)) {
        if(action==THINGSOS_KEY_MENU || action==THINGSOS_KEY_PREVIOUS){
            if(app.annotation_delete_confirm){app.annotation_delete_confirm=false;app.dirty=true;}
            else reader_annotations_dismiss();
            return;
        }
    }
    app.reader_background_refresh=false;
    if(app.wd_power_action)return;
    if(app.wd_consent||app.wd_cloud_prompt){
        if(action==THINGSOS_KEY_MENU || action==THINGSOS_KEY_PREVIOUS)wd_tap(100,875);
        else if(action==THINGSOS_KEY_HOME)return_to_menu();
        return;
    }

    if(action==THINGSOS_KEY_PREVIOUS && app.wd_thought_open && app.wd_thought_full){app.wd_thought_full=false;app.dirty=true;return;}
    if(action==THINGSOS_KEY_PREVIOUS && app.wd_notes_open && app.wd_long_open){app.wd_long_open=false;app.dirty=true;return;}
    if(app.wd_thought_open){wd_thought_close();app.dirty=true;if(action!=THINGSOS_KEY_HOME)return;}
    if(app.wd_notes_open && app.wd_notes_reader){app.wd_notes_open=app.wd_long_open=false;thingsos_runtime_overlay(&app.runtime,false,now());app.dirty=true;if(action!=THINGSOS_KEY_HOME)return;}

    ESP_LOGI("ThingsOS_keys", "action=%d page=%d", action, app.page);
    thingsos_runtime_activity(&app.runtime, now());
    app.activity_at = now();
    if (app.image_view.open && (action == THINGSOS_KEY_HOME || app.page == THINGSOS_UI_READING)) {
        thingsos_image_close(&app.image_view, &app.runtime.location);
        app.start = app.runtime.location.byte_offset;
        free(app.image_pixels);
        app.image_pixels = NULL;
        thingsos_runtime_image_overlay(&app.runtime, false, now());
        if(action==THINGSOS_KEY_PREVIOUS){app.dirty=true;return;}
    }
    if (app.footnote_open && (action == THINGSOS_KEY_HOME || action==THINGSOS_KEY_PREVIOUS)) {
        app.footnote_open = false;
        app.dirty = true;
        if(action==THINGSOS_KEY_PREVIOUS)return;
    }
    if (app.page == THINGSOS_UI_LOCK) {
        unlock_screen();
        return;
    }
    if (app.confirm_action) {
        app.confirm_action = 0;
        app.dirty = true;
        return;
    }
    if(action==THINGSOS_KEY_PREVIOUS) {
        if(app.time_edit){app.time_edit=false;app.dirty=true;return;}
        if(app.page==THINGSOS_UI_READING && (app.menu || app.jump)) {
            if(app.jump)app.jump=false;else app.menu=false;
            thingsos_runtime_overlay(&app.runtime,app.menu,now());
            app.dirty=true;return;
        }
        if(app.page!=THINGSOS_UI_READING) {
            if(app.page==THINGSOS_UI_WEIDU) {
                if(app.weidu_logout_confirm)tap(100,930);
                else if(app.wd_notes_open || app.wd_sync_panel || app.wd_long_open)tap(60,150);
                else if(app.weidu_detail || app.mp_mode)tap(100,275);
                else if(app.weidu_query[0])weidu_search_exit();
                return;
            }
            if(app.page==THINGSOS_UI_LIBRARY || app.page==THINGSOS_UI_SETTINGS || app.page==THINGSOS_UI_TRANSFER)return;
            if(app.page==THINGSOS_UI_MANAGE && app.delete_confirm){tap(100,1100);return;}
            if(app.page==THINGSOS_UI_RECENT){navigate(THINGSOS_UI_LIBRARY);return;}
            if(app.page==THINGSOS_UI_STATS && app.stats_from_reader){navigate(THINGSOS_UI_READING);return;}
            tap(60,150);return;
        }
    }
    if (action == THINGSOS_KEY_HOME) {
        if(app.page==THINGSOS_UI_WEIDU && app.weidu.authenticated && !app.mp_mode && !app.weidu_detail &&
           !app.weidu_logout_confirm && app.weidu_query[0] && weidu_search_exit())return;
        return_to_menu();
    } else if(app.page==THINGSOS_UI_READER_MORE && action==THINGSOS_KEY_MENU) {
        reader_more_back();
    } else if(app.page==THINGSOS_UI_FONTS && app.font_from_reader && action==THINGSOS_KEY_MENU) {
        reader_font_back();
    } else if (app.page == THINGSOS_UI_READING) {
        if (action == THINGSOS_KEY_MENU) {
            app.menu = !app.menu;
            app.jump = false;
            thingsos_runtime_overlay(&app.runtime, app.menu || app.annotation_open || app.annotation_selecting, now());
            app.dirty = true;
        } else {
            app.menu = app.jump = false;
            thingsos_runtime_overlay(&app.runtime, false, now());
            next_page(action == THINGSOS_KEY_NEXT);
        }
    }
}
static void capacitive_key(thingsos_key_action_t action) {
    if(action==THINGSOS_KEY_NONE)return;
    bool before=app.menu;
    app.ui_action_at=now();++app.ui_action_id;
    capacitive_key_dispatch(action);
    ESP_LOGI("ThingsOS_keys","Dispatch action=%u page=%u menu=%d->%d dirty=%d asleep=%d",
        (unsigned)action,(unsigned)app.page,before,app.menu,app.dirty,app.asleep);
    if(!app.dirty)app.ui_action_at=0;
}
#include "ble_input_ui.inc"
static void tap_dispatch(int x, int y) {
    if(app.asleep)return;
    if(alarm_ring_tap(x,y))return;
    if(app.pin.required || app.page==THINGSOS_UI_LOCK_PIN) {
        if(app.page==THINGSOS_UI_LOCK){unlock_screen();return;}
        if(app.pin.required && app.page!=THINGSOS_UI_LOCK_PIN)pin_wake();
        pin_tap(x,y);return;
    }
    if(online_modal_tap(x,y))return;
    if(app.jump_pending) {
        reader_jump_pending_tap(x,y);return;
    }
    if(app.wd_power_action)return;
    ESP_LOGI("ThingsOS_touch", "tap x=%d y=%d page=%d", x, y, (int)app.page);
    if(app.record_notice>=RECORD_NOTICE_START_ERROR &&
       x>=(app.canvas.width-456)/2&&x<(app.canvas.width+456)/2&&
       y>=(app.canvas.height-240)/2&&y<(app.canvas.height+240)/2) {
        app.record_notice=RECORD_NOTICE_NONE;app.record_notice_until=0;feedback_redraw();return;
    }
    if(app_tap(x,y))return;
    if(usb_tap(x,y))return;
    bool message_was_visible = app.message[0] != 0;
    app.message[0] = 0;
    if (message_was_visible) app.dirty = true;
    thingsos_runtime_activity(&app.runtime, now());
    app.activity_at = now();
    if(reader_annotations_tap(x,y))return;
    if(wd_tap(x,y))return;
    if(file_picture_tap(x,y))return;
    if(thingsos_weidu_busy() && !app.weidu.background && app.page!=THINGSOS_UI_WEIDU && y<1098) {
        say("微读正在处理，请返回微读取消操作");return;
    }
    if(app.page==THINGSOS_UI_READER_MORE) {
        for(unsigned interval=0;interval<2;++interval) {
            int left=reader_more_refresh_left(interval!=0)+reader_more_row_width()-320,top=reader_more_refresh_top(interval!=0);
            if(x>=left && x<left+312 && y>=top && y<top+64) {
                reader_refresh_update(interval!=0,(unsigned)(x-left)*(interval?4u:3u)/312);return;
            }
        }
        bool wide=reader_landscape_frame();int back=reader_more_back_top();
        int toggle=reader_more_toggle_hit(x,y);
        int note_x=36,note_y=reader_more_note_top(),note_w=reader_more_footer_width();
        int sx=wide?630:36,sy=reader_more_sync_top(),sw=reader_more_row_width();
        if(reader_more_cloud() && x>=sx+sw-192 && x<sx+sw && y>=sy && y<sy+64) {
            bool previous=app.annotation_manual;app.annotation_manual=x<sx+sw-96;
            if(!preferences_save()){app.annotation_manual=previous;say("设置保存失败");}
            app.annotation_auto_after=now()+2500;app.dirty=true;return;
        }
        if(reader_more_cloud() && app.annotation_manual && x>=sx && x<sx+200 && y>=sy+68 && y<sy+102) {
            app.annotation_auto_job=false;
            if(!reader_annotations_queue(true))reader_annotations_refresh_cloud();
            return;
        }
        if(x>=note_x && x<note_x+note_w && y>=note_y && y<note_y+66){navigate(THINGSOS_UI_READING);reader_annotations_enter();}
        else if((x<90 && y>=74 && y<200) || (x>=reader_more_back_left() && x<reader_more_back_left()+reader_more_footer_width() && y>=back && y<back+66))reader_more_back();
        else if(toggle==1) {
            bool old=app.landscape_enabled;app.landscape_enabled=reader_more_choice(x,1);
            if(!preferences_save()){app.landscape_enabled=old;say("设置保存失败，请重试");}
            else if(old!=app.landscape_enabled){
                reader_cache_clear();app.pagination_started=app.pagination_valid=app.pagination_failed=false;
                app.history_count=0;app.page_cache_key=0;
                thingsos_tilt_reset(&app.tilt);
            }
            reader_canvas_geometry();app.dirty=true;
        }
        else if(toggle==3) {
            bool old=app.immersive_enabled;app.immersive_enabled=reader_more_choice(x,3);
            if(!preferences_save()){app.immersive_enabled=old;say("设置保存失败，请重试");}
            else if(old!=app.immersive_enabled) {
                reader_cache_clear();app.pagination_started=app.pagination_valid=app.pagination_failed=false;
                app.history_count=0;app.page_cache_key=0;
                thingsos_tilt_reset(&app.tilt);
            }
            app.dirty=true;
        }
        else if(toggle==4) {
            bool old=app.vertical_enabled;app.vertical_enabled=reader_more_choice(x,4);
            if(!preferences_save()){app.vertical_enabled=old;say("设置保存失败，请重试");}
            else if(old!=app.vertical_enabled) {
                reader_cache_clear();app.pagination_started=app.pagination_valid=app.pagination_failed=false;
                app.history_count=0;app.page_cache_key=0;
                memset(&app.pagination_cursor,0,sizeof(app.pagination_cursor));
                memset(&app.text_hits,0,sizeof(app.text_hits));
                thingsos_tilt_reset(&app.tilt);
            }
            app.dirty=true;
        }
        else if(toggle==2) {
            unsigned old=app.tilt_sensitivity;app.tilt_sensitivity=reader_more_sensitivity_choice(x);
            if(!preferences_save()){app.tilt_sensitivity=old;say("设置保存失败，请重试");}
            thingsos_tilt_reset(&app.tilt);app.dirty=true;
        }
        else if(toggle==0) {
            bool old=app.tilt_enabled;app.tilt_enabled=reader_more_choice(x,0);
            if(!preferences_save()) {app.tilt_enabled=old;say("设置保存失败，请重试");}
            thingsos_tilt_reset(&app.tilt);app.dirty=true;
        }
        return;
    }
    if(app.page==THINGSOS_UI_WALLPAPER){wallpaper_tap(x,y);return;}
    if (!app.confirm_action && !app.time_edit && y >= 1098 && app.page != THINGSOS_UI_READING &&
        app.page != THINGSOS_UI_CHAPTERS && app.page != THINGSOS_UI_BOOKMARKS &&
        app.page != THINGSOS_UI_LOCK && app.page != THINGSOS_UI_PASSWORD &&
        app.page != THINGSOS_UI_MANAGE && app.page != THINGSOS_UI_PASSWORD &&
        !(app.page==THINGSOS_UI_FONTS && app.font_from_reader)) {
        int item = thingsos_ui_nav_hit(x, y);
        if (item >= 0) {
            if ((item == 0 && app.page == THINGSOS_UI_LIBRARY && app.library_valid) ||
                (item == 1 && app.page == THINGSOS_UI_WEIDU && !app.weidu_query[0] && !app.mp_mode && !app.weidu_detail &&
                 !app.weidu_logout_confirm && !app.wd_notes_open && !app.wd_thought_open && !app.wd_sync_panel && !app.wd_long_open && !app.wd_consent) ||
                (item == 2 && app.page == THINGSOS_UI_APPS) ||
                (item == 3 && app.page == THINGSOS_UI_SETTINGS)) {
                if (!app.dirty) app.ui_action_at = 0;
                return;
            }
            ESP_LOGI("ThingsOS_nav","Tab from=%u item=%d stage=prepare",(unsigned)app.page,item);
            thingsos_font_close(&app.preview_font);
            if (item == 0) {
                ESP_LOGI("ThingsOS_nav","Tab stage=library-load");
                library_load();
                navigate(THINGSOS_UI_LIBRARY);
            }
            if (item == 1) {
                if(app.weidu_query[0]){app.weidu_query[0]=0;app.weidu_start=0;app.weidu_shelf_valid=false;}
                weidu_enter();
            }
            if (item == 2) {
                navigate(THINGSOS_UI_APPS);
            }
            if (item == 3) navigate(THINGSOS_UI_SETTINGS);
            return;
        }
    }
    if (!app.confirm_action && !app.time_edit && y >= 74 && y < 200 && x < 90 &&
        (app.page!=THINGSOS_UI_STATS || y<STATS_TABS_Y) &&
        app.page != THINGSOS_UI_LIBRARY && app.page != THINGSOS_UI_RECENT &&
        app.page != THINGSOS_UI_WEIDU && app.page != THINGSOS_UI_CUBOX &&
        app.page != THINGSOS_UI_SETTINGS && app.page != THINGSOS_UI_APPS &&
        app.page != THINGSOS_UI_READING && app.page != THINGSOS_UI_LOCK &&
        app.page != THINGSOS_UI_MANAGE && app.page != THINGSOS_UI_PASSWORD) {
        if (app.page == THINGSOS_UI_DISPLAY && app.light_page) {
            app.light_page=false;app.dirty=true;return;
        }
        if (app.page == THINGSOS_UI_CHAPTERS || app.page == THINGSOS_UI_BOOKMARKS)
            navigate(THINGSOS_UI_READING);
        else if (app.page == THINGSOS_UI_STATS) navigate(THINGSOS_UI_LIBRARY);
        else if (app.page == THINGSOS_UI_FONTS) {
            reader_font_back();
        } else
            navigate(THINGSOS_UI_SETTINGS);
        return;
    }
    if (app.page == THINGSOS_UI_LIBRARY && y >= 90 && y < 200 && x >= 540) {
        navigate(THINGSOS_UI_STATS);
        return;
    }
    if(weidu_tap(x,y)) return;
    if(cubox_tap(x,y)) return;
    if (alignment_tap(x, y)) return;
    if (management_tap(x, y)) return;
    if (feature_tap(x, y)) return;
    if (app.page == THINGSOS_UI_READING) {
        if (app.menu && y >= 8 && y < 72 && x >= 16 && x < 112 && !app.image_view.open) {
            if(app.reader_from_cubox){release_book();app.reader_from_cubox=false;navigate(THINGSOS_UI_CUBOX);}
            else if(app.reader_from_weidu) weidu_enter();else navigate(THINGSOS_UI_LIBRARY);
            return;
        }
        if (app.image_view.open) {
            if (y < 180) {
                thingsos_image_close(&app.image_view, &app.runtime.location);
                app.start = app.runtime.location.byte_offset;
                free(app.image_pixels);
                app.image_pixels = NULL;
                thingsos_runtime_image_overlay(&app.runtime, false, now());
            } else if (y > reader_height()-216) {
                if (x < reader_width()*210/684)
                    thingsos_image_zoom(&app.image_view, -50);
                else if (x < reader_width()*450/684)
                    thingsos_image_zoom(&app.image_view, 50);
                else
                    thingsos_image_zoom(&app.image_view, 100 - (int)app.image_view.zoom_percent);
            }
            app.dirty = true;
            return;
        }
        if (app.menu) {
            if(app.jump) {
                reader_jump_layout_t g=reader_jump_layout();int half=(g.width-28)/2;
                if(reader_control_hit(x,y,36,g.input,g.width)) {feature_editor(4);return;}
                if(reader_control_hit(x,y,36+half+28,g.footer,half)) {
                    reader_jump_request(true,app.jump_percent);
                } else if(y<g.top || reader_control_hit(x,y,36,g.footer,half))app.jump=false;
                else if(reader_jump_track_hit(x,y))reader_jump_track_update(x);
                else if(reader_control_hit(x,y,36,g.step,137))
                    app.jump_percent=app.jump_percent>=5?app.jump_percent-5:0;
                else if(reader_control_hit(x,y,36+g.width-137,g.step,137))
                    app.jump_percent=app.jump_percent<=95?app.jump_percent+5:100;
            } else {
                unsigned choice = 0;
                thingsos_menu_action_t action = app.landscape_enabled?thingsos_reader_menu_hit_landscape(x,y,&choice,app.wd_id[0]!=0):app.wd_id[0]?thingsos_reader_menu_hit_weidu(x,y,&choice):thingsos_reader_menu_hit(x, y, &choice);
                if(action==THINGSOS_MENU_NONE)return;
                switch (action) {
                    case THINGSOS_MENU_CLOSE: app.menu = false; break;
                    case THINGSOS_MENU_SMALLER:
                        app.px = thingsos_reader_size_step(app.px, false);
                        app.history_count = 0;
                        break;
                    case THINGSOS_MENU_LARGER:
                        app.px = thingsos_reader_size_step(app.px, true);
                        app.history_count = 0;
                        break;
                    case THINGSOS_MENU_LINE:
                        app.line_choice = choice;
                        app.history_count = 0;
                        break;
                    case THINGSOS_MENU_MARGIN:
                        app.margin_choice = choice;
                        app.history_count = 0;
                        break;
                    case THINGSOS_MENU_WEIGHT:
                        if(app.weight_choice!=choice) {
                            app.weight_choice=choice;reader_cache_invalidate();
                            ESP_LOGI("ThingsOS_reader","Weight=%u; retain page index=%d pages=%u",
                                app.weight_choice,app.pagination_valid,(unsigned)app.page_count);
                        }
                        break;
                    case THINGSOS_MENU_INDENT:
                        /* An indent change moves every following line start, so
                           the pre-rendered pages and pagination both restart. */
                        app.indent_chars = choice == 1 ? THINGSOS_READER_INDENT_CHARS
                                                       : THINGSOS_READER_INDENT_OFF;
                        app.history_count = 0;
                        reader_cache_clear();
                        break;
                    case THINGSOS_MENU_RIPPLE:
                        app.ripple_enabled=choice==1;
                        break;
                    case THINGSOS_MENU_CHAPTERS: navigate(THINGSOS_UI_CHAPTERS); break;
                    case THINGSOS_MENU_BOOKMARKS: navigate(THINGSOS_UI_BOOKMARKS); break;
                    case THINGSOS_MENU_NOTES:
                        reader_annotations_enter();break;
                    case THINGSOS_MENU_MORE:
                        navigate(THINGSOS_UI_READER_MORE);break;
                    case THINGSOS_MENU_FONTS:
                        app.font_from_reader=true;
                        font_scan();
                        font_preview_current();
                        navigate(THINGSOS_UI_FONTS);
                        break;

                    case THINGSOS_MENU_JUMP:
                        app.jump = true;
                        app.jump_percent = book_progress();
                        break;
                    default: break;
                }
            }
            preferences_save();
            thingsos_runtime_overlay(&app.runtime, app.menu || app.annotation_open || app.annotation_selecting, now());
            app.dirty = true;
            return;
        }
        /* An open footnote window owns the page: inside turns the note, blank
           space anywhere else dismisses it. Checked before the header strip so
           any tap outside the card closes the window. */
        if (app.footnote_open && app.chapter.footnotes &&
            app.footnote_index < app.chapter.footnote_count) {
            bool inside = x >= FOOTNOTE_CARD_LEFT && x < FOOTNOTE_CARD_LEFT + FOOTNOTE_CARD_WIDTH &&
                          y >= FOOTNOTE_CARD_TOP &&
                          y < FOOTNOTE_CARD_TOP + app.footnote_card_height;
            if (inside && app.footnote_next > app.footnote_offset)
                app.footnote_offset = app.footnote_next;
            else
                app.footnote_open = false;
            app.dirty = true;
            return;
        }
        if (!app.immersive_enabled && y < 74) {
            if (x < 180 || x >= reader_width()-64)
                return;
            else {
                app.menu = true;
                thingsos_runtime_overlay(&app.runtime, true, now());
                app.dirty = true;
            }
            return;
        }
        /* The reading footer is display-only. The explicit menu jump action
           remains available, but a footer tap must not open an overlay or turn. */
        if (!app.immersive_enabled && y >= reader_height()-76) return;
        for (size_t hit = 0; hit < app.text_hits.count; hit++) {
            if (app.text_hits_chapter != app.chapter_index || app.text_hits_start != app.start) break;
            const thingsos_text_hit_t* box = app.text_hits.items + hit;
            if (x < box->x || x >= box->x + box->w || y < box->y || y >= box->y + box->h) continue;
            for (size_t i = 0; i < app.chapter.footnote_count; ++i) {
                const thingsos_footnote_t* note = app.chapter.footnotes + i;
                if (note->begin != box->begin) continue;
                if(!note->note_bytes){say(app.epub.weidu_cache?"未找到脚注内容，请重新缓存书籍":"未找到脚注内容，请检查 EPUB 的注释引用");return;}
                app.footnote_open = true;
                app.footnote_index = i;
                app.footnote_offset = app.footnote_next = 0;
                app.footnote_card_height = FOOTNOTE_CARD_MAX;
                app.dirty = true;
                return;
            }
        }
        if(x>=reader_width()/3 && x<=reader_width()*2/3 && app.wd_id[0] && app.text_hits_chapter==app.chapter_index && app.text_hits_start==app.start)
            for(size_t i=0;i<app.text_hits.line_count;++i){
                const thingsos_text_hit_t* h=app.text_hits.lines+i;
                if(x<h->x || x>=h->x+h->w || y<h->y || y>=h->y+h->h)continue;
                if(thingsos_wd_device_line_note(app.wd_id,app.chapter_index,&app.wd_map,&app.chapter,h->begin,h->end,&app.wd_note)) {
                    bool mine=false;for(size_t j=0;j<app.wd_line_count;++j)if(app.wd_lines[j].mine && app.wd_lines[j].begin==h->begin && app.wd_lines[j].end==h->end)mine=true;
                    if(mine)wd_own_thought_enter(app.wd_id,&app.wd_note);else wd_thought_enter(app.wd_id,&app.wd_note);
                }
                else say("划线位置已变化，请刷新划线后重试");
                return;
            }
        /* A short image tap stays on the page. Only the 1 s hold owns zoom. */
        if(reader_image_hit(x,y)!=SIZE_MAX)return;
        if (x < reader_width()/3)
            next_page(false);
        else if (x > reader_width()*2/3)
            next_page(true);
        else if (x < reader_width()*2/3 &&
                 y >= reader_height()/3 && y < reader_height()*2/3) {
            /* Only the central ninth opens settings; the rest of the middle
               column is inert after text/image interactions are handled. */
            app.menu = true;
            app.footnote_open = false;
            thingsos_runtime_overlay(&app.runtime, true, now());
            app.dirty = true;
        }
        return;
    }
    if (app.page == THINGSOS_UI_CHAPTERS) {
        size_t count=reader_toc_count();
        if(x>=644 && y>=350 && y<950 && count>6) {
            app.toc_follow_current=false;
            app.list_start=(size_t)(y-350)*(count-6)/599;
            app.dirty=true;return;
        }
        if (y >= 1000)
            navigate(THINGSOS_UI_READING);
        else if (reader_control_hit(x,y,356,280,292))
            navigate(THINGSOS_UI_BOOKMARKS);
        else if (y >= 350 && y < 950) {
            size_t n = app.list_start + (size_t)(y - 350) / 100;
            if (app.epub_open && n < count) {
                size_t row=n-app.list_start;
                thingsos_toc_entry_t target=row<app.toc_draw_count
                    ?app.toc_draw_entries[row]:reader_toc_entry(n);
                size_t page = thingsos_page_find(app.pages, app.page_count, target.chapter, target.offset);
                if (app.pagination_valid) {
                    if (go_page(page)) navigate(THINGSOS_UI_READING);
                } else if (load_chapter(target.chapter) && (!target.offset || target.offset<app.chapter.bytes)) {
                    app.start=target.offset;
                    app.runtime.location.chapter=app.chapter_index;
                    app.runtime.location.byte_offset=target.offset;
                    navigate(THINGSOS_UI_READING);
                }
            } else if (!app.epub_open && n * 10 < app.page_count && go_page(n * 10))
                navigate(THINGSOS_UI_READING);
        }
        return;
    }
    if (app.page == THINGSOS_UI_FONTS) {
        if (y>=1015 && y<1083 && ((x>=36 && x<328)||(x>=356 && x<648))) {
            if (x >= 356 && app.preview_font.face) {
                thingsos_font_close(font_setting_face());
                *font_setting_face() = app.preview_font;
                memset(&app.preview_font, 0, sizeof(app.preview_font));
                strlcpy(font_setting_path(), app.preview_path, sizeof(app.font_path));
                if(app.font_from_reader){app.history_count=0;app.pagination_valid=false;
                    app.pagination_started=app.pagination_failed=false;app.progress_total=0;reader_cache_clear();}
                preferences_save();
            } else
                thingsos_font_close(&app.preview_font);
            reader_font_back();
            return;
        }
        if (y >= 220 && y < 290 && x > 510) {
            font_scan();
            app.dirty = true;
            return;
        }
        if (y >= 290 && y < 380)
            font_preview(-1);
        else if (y >= 390 && y < 770) {
            size_t n = app.list_start + (size_t)(y - 390) / 95;
            if (n < app.font_count) font_preview((int)n);
        }
        return;
    }
    int nav = thingsos_ui_nav_hit(x, y);
    if (nav >= 0) {
        if(nav==1) {weidu_enter();return;}
        if (nav == 0) {
            library_load();
            navigate(THINGSOS_UI_LIBRARY);
        }
        if (nav == 2) {
            if (thingsos_network_start(false) == ESP_OK)
                navigate(THINGSOS_UI_TRANSFER);
            else
                say("联网启动失败");
        }
        if (nav == 3) navigate(THINGSOS_UI_SETTINGS);
        return;
    }
    if (app.page == THINGSOS_UI_LIBRARY || app.page == THINGSOS_UI_RECENT) {
        bool recent = app.page == THINGSOS_UI_RECENT;
        if(!recent && y>=100 && y<195 && x>=244 && x<444) {
            bool selected=x>=344;
            if(selected!=app.library_weidu){app.library_weidu=selected;app.library_order_valid=false;app.list_start=0;app.dirty=true;}
            return;
        }
        if (!recent && y >= 220 && y < 310) {
            if(app.library_weidu && x>=420 && x<496) {feature_editor(5);return;}
            if (x >= 496 && x < 572) {
                app.library_name_sort = !app.library_name_sort;
            } else if (x >= 572 && x < 648) app.library_cards = !app.library_cards;
            else return;
            app.transition_refresh = true;
            app.list_start = 0;
            preferences_save();
            app.dirty = true;
            return;
        }
        if (!recent) {
            size_t n = library_hit(x, y);
            if (n < app.book_count && !open_book(n)) say("书籍损坏、超限或 SD 不可用");
            return;
        }
        if (recent && y >= 1000 && y < 1098) {
            app.confirm_action = 5;
            app.dirty = true;
            return;
        }
        if (!recent && y >= 330 && y < 600 && app.book_count) {
            size_t hero = library_hero();
            if (!open_book(hero)) say("书籍无法打开");
            return;
        }
        int base = recent ? 300 : 620, rows = recent ? 4 : 3;
        if (y >= base && y < base + rows * 150) {
            size_t n = app.list_start + (size_t)(y - base) / 150;
            if (recent)
                n = recent_book(n);
            else
                n = library_row(n);
            if (n < app.book_count && !open_book(n)) say("书籍损坏、超限或 SD 不可用");
        }
        return;
    }
}
static void tap(int x, int y) {
    if(!app.pin.required && app.page!=THINGSOS_UI_LOCK_PIN)WHERE("tap x=%d y=%d page=%d",x,y,(int)app.page);
    app.reader_background_refresh=false;
    app.ui_action_at=now();++app.ui_action_id;
    tap_dispatch(x,y);
    if (!app.dirty) app.ui_action_at=0;
}
static bool power_pin_press(int x,int y) {
    if(app.asleep || app.page!=THINGSOS_UI_POWER || app.controls_open || app.confirm_action ||
       !power_pin_hit(x,y))return false;
    /* Match other fixed toggles: activate on contact, and let the gesture
       latch consume release so a small slide cannot lose or repeat the tap. */
    tap(x,y);return true;
}
static bool stats_tab_press(int x,int y) {
    if(app.asleep || app.page!=THINGSOS_UI_STATS || app.confirm_action || app.time_edit ||
       stats_tab_hit(x,y)<0)return false;
    tap(x,y);return true;
}
static bool home_tab_press(int x,int y) {
    if(app.asleep || !home_tab(app.page) || app.confirm_action || app.time_edit ||
       app.image_view.open || app.annotation_open || app.annotation_selecting ||
       app.wd_notes_open || app.wd_thought_open || app.wd_cloud_prompt || app.wd_consent ||
       thingsos_ui_nav_hit(x,y)<0)return false;
    /* One action on contact; the gesture latch consumes its release even if
       the finger is lifted while the synchronous panel scan is running. */
    tap(x,y);return true;
}
static bool keyboard_mode_press(int x,int y) {
    if(app.page!=THINGSOS_UI_PASSWORD || !input_uses_qwerty())return false;
    int key=wifi_key_hit(x,y);
    bool candidate=app.input.chinese && (input_hit_page(x,y) || input_hit_candidate(x,y));
    if(!candidate && !(key>=100 || key==40 || key==41 || key==42 || key==43 || key==49 || key==50))return false;
    tap(x,y);return true;
}
static bool reading_menu_press(int x,int y) {
    if(app.page==THINGSOS_UI_READING && app.jump_pending) {
        reader_jump_pending_tap(x,y);return true;
    }
    if(app.page!=THINGSOS_UI_READING || !app.menu || app.jump || app.image_view.open ||
       app.wd_consent || app.wd_cloud_prompt || app.wd_notes_open || app.wd_thought_open)return false;
    unsigned choice=0;
    thingsos_menu_action_t action=app.landscape_enabled?thingsos_reader_menu_hit_landscape(x,y,&choice,app.wd_id[0]!=0):app.wd_id[0]?thingsos_reader_menu_hit_weidu(x,y,&choice):thingsos_reader_menu_hit(x,y,&choice);
    if(action==THINGSOS_MENU_NONE)return false;
    tap(x,y);return true;
}
static bool reader_more_press(int x,int y) {
    if(app.page!=THINGSOS_UI_READER_MORE || app.asleep || app.confirm_action || app.time_edit)return false;
    /* This fixed control page has no drag/long-press action. All switches and
       navigation share one contact latch, including during a blocking scan. */
    tap(x,y);return true;
}
static bool reader_font_press(int x,int y) {
    if(app.asleep || app.page!=THINGSOS_UI_FONTS || !app.font_from_reader)return false;
    /* Keep list rows on release so a swipe remains a swipe. Fixed buttons
       have no long-press/drag action and can respond on contact. */
    if((x<90 && y>=74 && y<200) || (y>=1000 && y<1090) || (x>510 && y>=220 && y<290)) {
        tap(x,y);return true;
    }
    return false;
}
#ifndef THINGSOS_HOST_UI
static void fallback(const char* reason) {
    thingsos_diag_event(DIAG_FAILURE,1,0,0,0,0);
    ESP_LOGE("ThingsOS", "%s; starting independent recovery AP", reason);
    thingsos_network_start(true);
    if (state && app.board_ok && app.canvas.pixels) {
        memset(app.canvas.pixels, 255, 684u * 1216u);
        thingsos_board_ascii(&app.canvas, "ThingsOS recovery", 36, 160);
        thingsos_board_ascii(&app.canvas, reason, 36, 240);
        thingsos_board_ascii(&app.canvas, thingsos_network_ssid(), 36, 350);
        thingsos_board_ascii(&app.canvas, thingsos_network_password(), 36, 420);
        thingsos_board_ascii(&app.canvas, "http://192.168.4.1", 36, 500);
        thingsos_board_ascii(&app.canvas, thingsos_network_token(), 36, 580);
        thingsos_board_present(&app.canvas);
    }
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (running && esp_ota_get_state_partition(running, &state) == ESP_OK &&
        state == ESP_OTA_IMG_PENDING_VERIFY) {
        vTaskDelay(pdMS_TO_TICKS(1500));
        esp_ota_mark_app_invalid_rollback_and_reboot();
    }
    while (true) {
        thingsos_board_idle();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
#ifndef THINGSOS_HOST_UI
static void* json_psram_malloc(size_t size){
    void* p=heap_caps_malloc(size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    return p?p:malloc(size);
}
#endif
void app_main(void) {
    thingsos_diag_start();
    ESP_LOGI(
        "ThingsOS", "Native application starting; PSRAM initialized=%d",
        esp_psram_is_initialized());
    /* Default malloc otherwise prefers scarce internal DMA RAM for every
       allocation below 16 KiB, including stdio, glyph and protocol buffers.
       Keep small control objects internal; larger ordinary objects prefer
       PSRAM. Drivers/tasks still request their required capabilities. */
    if(esp_psram_is_initialized())heap_caps_malloc_extmem_enable(512);
    /* cJSON trees (WeRead read-time, ima, Cubox, release) are hundreds of
       sub-512-byte nodes. Through malloc they landed in main SRAM and left it
       fragmented after a WeRead job (largest block ~13 KiB), which pushed the
       audio stack into slow LP RAM. Nodes go to PSRAM; free() handles both. */
#ifndef THINGSOS_HOST_UI
    if(esp_psram_is_initialized()){cJSON_Hooks hooks={.malloc_fn=json_psram_malloc,.free_fn=free};cJSON_InitHooks(&hooks);}
#endif
    state = heap_caps_calloc(1, sizeof(*state), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    thingsos_path_mapper(cache_path_identity);
    if (!state) {
        fallback("PSRAM state allocation failed");
        return;
    }
    memset(&app, 0, sizeof(app));
    app.ai_key_enabled=true;
    app.canvas = (thingsos_canvas_t){ .width=684, .height=1216 };
    app.px = THINGSOS_READER_SIZE_DEFAULT;
    app.line_choice = app.margin_choice = 1;
    app.indent_chars = THINGSOS_READER_INDENT_OFF;
    app.refresh_pages = 15;
    app.ui_px = 30;
    app.ui_refresh_pages = 15;
    app.auto_lock_minutes = 5;
    app.auto_time = true;
    app.ripple_enabled=false;
    app.hotspot = false;
    app.activity_at = now();
    app.font_candidate = -2;
    thingsos_runtime_init(&app.runtime);
    app.runtime.reading_interval=wd_rt_interval;
    thingsos_records_init(&app.records);
    setenv("TZ", "CST-8", 1);
    tzset();
    app.page = usb_return_marker==USB_RETURN_SETTINGS?THINGSOS_UI_SETTINGS:THINGSOS_UI_LIBRARY;
    usb_return_marker=0;
    thingsos_io_lock = xSemaphoreCreateMutex();
    if (!thingsos_io_lock) return;
    app.canvas.pixels = heap_caps_malloc(684u * 1216u, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!app.canvas.pixels) {
        fallback("PSRAM unavailable");
        return;
    }
    memory_log("boot-pre-board");
    esp_err_t e = thingsos_board_start();
    app.board_ok = e == ESP_OK;
    if (!app.board_ok) {
        ESP_LOGE("ThingsOS", "Board initialization returned %s", esp_err_to_name(e));
        fallback("Board calibration/initialization failed");
        return;
    }
    app.font_ok = thingsos_board_font(&app.font) && thingsos_board_font(&app.ui_font);
    if (!app.font_ok) {
        fallback("Default font partition invalid");
        return;
    }
    app.battery = thingsos_board_battery();
    app.charging = thingsos_board_charging();
    ESP_LOGI("ThingsOS", "Default font verified; initial battery=%u%% charging=%u", app.battery,
             (unsigned)app.charging);
    memory_log("boot-post-board");
    uint32_t unix_sec;
    if (thingsos_board_time(&unix_sec)) {
        struct timeval clock = { .tv_sec = unix_sec };
        settimeofday(&clock, NULL);
    }
    if (media()) {
        feature_mount();
        app.sd_seen = true;
    }
    app.wd_pull_on=app.wd_push_on=app.wd_time_on=true;
    preferences_load();
    pin_boot();
    alarm_load();
    if (app.board_ok) app_light_apply();
    thingsos_audio_volume(app.audio_volume);
    thingsos_audio_reserve();
    thingsos_network_radio(app.wifi_enabled);
    ESP_LOGI("ThingsOS", "Reader config: px=%u refresh=%u mode=%u ripple=%d",
        app.px,app.refresh_pages,app.refresh_mode,app.ripple_enabled);
    thingsos_network_library(app.records.library_path);
    scan();
    memory_log("boot-post-scan");
    thingsos_board_refresh_policy(app.ui_refresh_mode,app.ui_refresh_pages);
    draw();
    if (thingsos_board_present(&app.canvas) != ESP_OK) {
        fallback("Display update failed");
        return;
    }
    app.dirty=false;app.force_refresh=false;app.transition_refresh=false;
    /* Initialize radio after the first complete panel scan, so bring-up and
       association do not overlap the initial full-clear/DMA workload. */
    memory_log("boot-post-network");
    thingsos_network_start(false);
    thingsos_ima_init();
    thingsos_cubox_init();
    thingsos_cubox_set_network_yield(app_audio_yield_for_cubox);
    thingsos_bluetooth_init();
    thingsos_ble_init();
    thingsos_network_service(app.service_on);
    strlcpy(app.wifi_ssid, thingsos_network_saved_name(), sizeof(app.wifi_ssid));
    ESP_LOGI(
        "ThingsOS", "UI ready: %s; initial screen update succeeded; stack margin=%u bytes",
        app.pin.required?"locked PIN":"library",
        (unsigned)uxTaskGetStackHighWaterMark(NULL));
    /* Health requires actual successful BSP, font and display bring-up, then a stable period. */
#ifndef THINGSOS_HOST_UI
    /* On-demand request diagnostics avoid a competing boot-time probe task. */
#endif
    uint64_t started = now();
    memory_log("library-ready");
    bool healthy = false, pressed = false, dragging = false, held = false, touch_ok = false, moved = false;
    bool input_contact=false;
    uint64_t touch_started = 0;
    thingsos_keys_t keys = { 0 };
    int last_x = 0, last_y = 0, initial_x = 0, initial_y = 0;
    uint64_t refresh_at = now();
    while (true) {
        thingsos_ima_pair_poll();
        thingsos_cubox_pair_poll();
        /* Joining the HTTP task must happen before taking the shared I/O mutex. */
        if(thingsos_network_service_poll()!=ESP_OK) {
            app.service_on=false;thingsos_network_service(false);
            say("传书服务启动或关闭失败，请重试");
        }
        uint64_t time = now();
        thingsos_runtime_tick(&app.runtime, time);
        feature_account();
        if (transfer_blocks_ui()) {
            thingsos_board_tilt_active(false);thingsos_tilt_reset(&app.tilt);
            if (!app.runtime.reading_overlay) thingsos_runtime_overlay(&app.runtime, true, time);
            if (thingsos_network_pause_requested() &&
                xSemaphoreTake(thingsos_io_lock, pdMS_TO_TICKS(10)) == pdTRUE) {
                if (app.opened) {
                    release_book();
                    if (app.page == THINGSOS_UI_READING) navigate(THINGSOS_UI_LIBRARY);
                } else
                    feature_save();
                thingsos_network_pause_complete(!app.records_bound || !app.records_dirty);
                xSemaphoreGive(thingsos_io_lock);
            }
            thingsos_transfer_state_t transfer;
            thingsos_network_transfer(&transfer);
            transfer_sync_activity(&transfer,time);
            if (!app.asleep && !app.pin.required && app.page!=THINGSOS_UI_LOCK_PIN && app.page!=THINGSOS_UI_LOCK &&
                transfer_refresh_due(&transfer,time,refresh_at) &&
                xSemaphoreTake(thingsos_io_lock, pdMS_TO_TICKS(10)) == pdTRUE) {
                transfer_draw(&transfer);
                thingsos_board_present(&app.canvas);
                transfer_refresh_record(&transfer);
                refresh_at = time;
                app.dirty = true;
                xSemaphoreGive(thingsos_io_lock);
            }
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        bool overlay =
            alarm_active() || app.asleep || app.wd_power_action || app.controls_open || app.menu || app.page != THINGSOS_UI_READING || app.image_view.open || online_modal_visible();
        if (overlay != app.runtime.reading_overlay ||
            app.image_view.open != app.runtime.image_overlay) {
            if (app.image_view.open)
                thingsos_runtime_image_overlay(&app.runtime, true, time);
            else
                thingsos_runtime_overlay(&app.runtime, overlay, time);
        }
        /* Stay queued while an HTTPS step owns the shared display/SD lock.
           A 50 ms timeout followed by a 100 ms sleep misses its brief yields
           and lets the higher-priority worker starve input for seconds. */
        uint64_t io_wait_at = now();
        xSemaphoreTake(thingsos_io_lock, portMAX_DELAY);
        uint64_t io_wait_ms = now() - io_wait_at;
        if (io_wait_ms >= 100)
            ESP_LOGI("ThingsOS_reader", "UI IO wait=%u ms", (unsigned)io_wait_ms);
        weidu_poll();
        online_poll();
        app_poll();
        ble_input_poll();
        bool was_alarm=alarm_active();
        alarm_poll();
        if(!was_alarm&&alarm_active()){
            pressed=dragging=held=moved=input_contact=false;memset(&keys,0,sizeof(keys));
        }
        reading_power_poll();
        wifi_scan_step();
        unsigned power_key = thingsos_board_power_key();
        if (power_key) {
            power_action(power_key);
            pressed = dragging = held = moved = input_contact = false;
            memset(&keys, 0, sizeof(keys));
        }
        if (app.asleep) {
            xSemaphoreGive(thingsos_io_lock);
            thingsos_board_sleep_wait();
            continue;
        }
        /* Reading keeps volume keys as page turns; every other surface uses
           them for volume, including the control center and recorder/music. */
        if (app.ai_key_enabled && thingsos_board_ai_key() && !app.pin.required && app.page!=THINGSOS_UI_LOCK_PIN) app_ai_record_toggle();
        unsigned side_key = thingsos_board_side_key();
        if (side_key && !app.pin.required && app.page!=THINGSOS_UI_LOCK_PIN) {
            if(app.page==THINGSOS_UI_READING && !app.controls_open)
                capacitive_key(side_key==1?THINGSOS_KEY_PREVIOUS:THINGSOS_KEY_NEXT);
            else
                app_volume_set(side_key==1?(app.audio_volume<=95?app.audio_volume+5:100):(app.audio_volume>=5?app.audio_volume-5:0));
        }
        thingsos_board_touch_buffer(buffered_touch_page(app.page) || app.controls_open ||
            app.page==THINGSOS_UI_APPS || app.page==THINGSOS_UI_SYSTEM || app.page==THINGSOS_UI_RECORDER || app.page==THINGSOS_UI_MUSIC ||
            app.page==THINGSOS_UI_WEIDU || app.page==THINGSOS_UI_CUBOX || (app.page==THINGSOS_UI_LIBRARY && app.library_weidu));
        network_connection_poll();
        upgrade_poll();
        bool present = media();
        if (app.sd_seen && !present) {
            thingsos_ima_quiesce_locked();
            thingsos_cubox_quiesce_locked();
            app.cache_names_migrated=false;app.file_action_open=false;app.file_more=false;
            if(thingsos_weidu_busy()) thingsos_weidu_cancel();
            thingsos_network_card(0);
            bool builtin = app.opened && app.runtime.location.book_id == 1;
            if (!builtin) release_book();
            thingsos_board_font(&app.font);
            thingsos_board_font(&app.ui_font);
            if (!builtin && app.page != THINGSOS_UI_FILES) app.page = app.pin.required?THINGSOS_UI_LOCK_PIN:THINGSOS_UI_LIBRARY;
            memset(&app.browser, 0, sizeof(app.browser));
            scan();
            say("SD 卡已移除");
        }
        if (present && !app.sd_seen) {
            feature_mount();
            scan();
            if (app.page == THINGSOS_UI_FILES && !app.browser.path[0])
                thingsos_browser_scan(&app.browser, "/sdcard", "/sdcard");
            app.dirty = true;
        }
        app.sd_seen = present;
        thingsos_transfer_state_t transfer;
        thingsos_network_transfer(&transfer);
        transfer_sync_activity(&transfer,time);
        if (transfer.active && app.page == THINGSOS_UI_TRANSFER &&
            (transfer.ota ? transfer.percent / 10 != app.transfer_progress / 10 :
             transfer_refresh_due(&transfer,time,refresh_at))) {
            transfer_refresh_record(&transfer); app.dirty = true;
        }
        if (transfer.serial && transfer.serial != app.transfer_serial && !transfer.active) {
            transfer_refresh_record(&transfer);
            app.dirty = true;
            if (media()) {
                scan();
                font_scan();
            }
            say(transfer.message);
        }
        if(!thingsos_network_busy())thingsos_diag_poll(false);
        thingsos_network_maintenance();
        management_network();
        thingsos_touch_t touch = { 0 };
        uint64_t touch_time=time;
        if (thingsos_board_touch(&touch,&touch_time)) {
            touch_ok = true;
            if(touch.touched && !input_contact) {
                app.activity_at=now();thingsos_runtime_activity(&app.runtime,app.activity_at);
            }
            input_contact=touch.touched;
            int x = (int)touch.x,
                y = (int)touch.y; /* Raw-device mapping requires hardware acceptance. */
            bool screen_touch=reader_touch_coordinates(&x,&y);
            if(screen_touch)keyboard_touch_coordinates(&x,&y);
            if (touch.count > 1) keys.cancelled = true;
            unsigned key_before=keys.key;
            uint64_t key_started=keys.started;
            thingsos_key_action_t key_action =
                thingsos_keys_step(&keys, touch.touched, touch.x, touch.y, touch_time);
            if(key_action!=THINGSOS_KEY_NONE || (!touch.touched && key_before))
                ESP_LOGI("ThingsOS_keys","Sample key=%u action=%u held_ms=%u age_ms=%u",
                    key_before,(unsigned)key_action,(unsigned)(touch_time-key_started),(unsigned)(now()-touch_time));
            if (key_action != THINGSOS_KEY_NONE) capacitive_key(key_action);
            if(alarm_active()){
                if(touch.touched&&screen_touch&&!keys.key){
                    if(!pressed){initial_x=x;initial_y=y;pressed=true;moved=false;}
                    else if(abs(x-initial_x)>18||abs(y-initial_y)>18)moved=true;
                }else if(pressed){pressed=false;if(!moved)alarm_ring_tap(initial_x,initial_y);}
            }else if (key_action != THINGSOS_KEY_NONE || keys.key || keys.cancelled ||
                (touch.touched && touch.y >= 1300)) {
                pressed = false;
            } else if (touch.touched && screen_touch) {
                if (pressed && (abs(x-initial_x)>18 || abs(y-initial_y)>18)) moved=true;
                if (!pressed) {
                    last_x = x;
                    last_y = y;
                    initial_x = x;
                    initial_y = y;
                    dragging = held = moved = false;
                    touch_started = touch_time;
                    pressed = true;
                    /* Keyboard input responds to contact, once per gesture, rather
                       than waiting for a release after a blocking e-ink scan. */
                    if(power_pin_press(x,y) || reader_annotations_contact(x,y) || keyboard_mode_press(x,y) || reading_menu_press(x,y) || reader_more_press(x,y) || reader_font_press(x,y) || stats_tab_press(x,y) || home_tab_press(x,y))dragging=held=true;
                } else if(!held&&!moved&&app_hold(initial_x,initial_y,touch_time-touch_started)) {
                    held=dragging=true;
                } else if(app.annotation_dragging && app.annotation_selecting) {
                    reader_annotations_drag(x,y);dragging=held=true;
                } else if (
                    !held && !dragging && !moved && touch_time - touch_started >= reader_hold_delay(initial_x,initial_y) && abs(x - initial_x) < 18 &&
                    abs(y - initial_y) < 18 && !online_modal_visible() && (reader_content_hold(initial_x,initial_y,touch_time-touch_started) || weidu_hold(initial_x,initial_y) || management_hold(initial_x, initial_y))) {
                    held = true;
                    dragging = true;
                } else if (app.jump && reader_jump_track_hit(initial_x,initial_y)) {
                    reader_jump_track_update(x);
                    app.dirty = true;
                    dragging = true;
                } else if (app.page == THINGSOS_UI_CHAPTERS && initial_x >= 644 &&
                           initial_y >= 350 && initial_y < 950 && abs(y-initial_y)>8) {
                    tap(650,y<350?350:y>949?949:y);
                    dragging=true;last_y=y;
                } else if (app.page == THINGSOS_UI_FILES && !file_picture.open && initial_x >= 644 &&
                           initial_y >= 390 && initial_y < 966 && abs(y-initial_y)>8) {
                    tap(650, y < 390 ? 390 : y > 965 ? 965 : y);
                    dragging=true;last_y=y;
                } else if (app.page == THINGSOS_UI_WIFI && initial_x >= 644 && initial_y >= wifi_base() &&
                           initial_y < wifi_base() + wifi_rows() * 114 && abs(y-initial_y)>8) {
                    int base=wifi_base(), bottom=base+wifi_rows()*114-1;
                    tap(650,y<base?base:y>bottom?bottom:y);
                    dragging=true;last_y=y;
                } else if (app.image_view.open && (abs(x - last_x) > 12 || abs(y - last_y) > 12)) {
                    thingsos_size_t fit;
                    thingsos_image_fit_width(app.image_w,app.image_h,reader_width()-72,&fit);
                    thingsos_image_pan(
                        &app.image_view, last_x - x, last_y - y, fit.width, fit.height, reader_width()-72, reader_height()-426);
                    last_x = x;
                    last_y = y;
                    dragging = true;
                    app.dirty = true;
                } else {
                    last_x = x;
                    last_y = y;
                }
            } else if (pressed) {
                pressed = false;
                if(app.annotation_dragging){app.annotation_dragging=false;dragging=held=true;}
                if(now()>touch_time+40)ESP_LOGI("ThingsOS_touch","Buffered release age=%u ms",(unsigned)(now()-touch_time));
                /* A complete one-second hold can be queued while the panel
                   is busy; honor its sampled duration before the short tap. */
                if(!dragging&&!moved&&app_hold(initial_x,initial_y,touch_time-touch_started))held=dragging=true;
                if(!dragging && !moved && touch_time-touch_started>=reader_hold_delay(initial_x,initial_y) &&
                    !online_modal_visible() && (reader_content_hold(initial_x,initial_y,touch_time-touch_started) || weidu_release_hold(initial_x,initial_y,touch_time-touch_started)))held=dragging=true;
                if(!dragging&&app_controls_swipe(initial_x,initial_y,last_x-initial_x,last_y-initial_y))dragging=true;
                if(!dragging&&app.controls_open){app_controls_tap(last_x,last_y);dragging=true;}
                if(!dragging && reader_swipe_page(initial_x,initial_y,last_x-initial_x,last_y-initial_y))dragging=true;
                if (!dragging) {
                    int dy = last_y - initial_y;
                    if (abs(dy) > 40 && abs(dy) > abs(last_x - initial_x) && !(app.page==THINGSOS_UI_FILES && file_picture.open)) {
                        if(app.page==THINGSOS_UI_WEIDU)weidu_scroll(dy);
                        else if(app.page==THINGSOS_UI_CUBOX)cubox_scroll(dy);
                        else {
                        size_t count = 0, step = 5;
                        size_t before=app.list_start;
                        if (app.page == THINGSOS_UI_LIBRARY) {
                            size_t visible=library_visible_count();
                            count = app.library_cards ? visible : visible ? visible - 1 : 0;
                            step = app.library_cards ? 9 : 3;
                        }
                        if (app.page == THINGSOS_UI_RECENT) {
                            count = app.recent_count;
                            step = 4;
                        }
                        if (app.page == THINGSOS_UI_CHAPTERS) {
                            app.toc_follow_current=false;
                            count = reader_toc_count();
                            step = 6;
                        }
                        if (app.page == THINGSOS_UI_FONTS) {
                            count = app.font_count;
                            step = 4;
                        }
                        if (app.page == THINGSOS_UI_MANAGE) {
                            count = app.manage_count;
                            step = 5;
                        }
                        if (app.page == THINGSOS_UI_FILES) {
                            count = thingsos_browser_matches(&app.browser,app.file_query);
                            step = THINGSOS_FILE_ROWS;
                        }
                        if (app.page == THINGSOS_UI_BOOKMARKS) {
                            count = 0;
                            while (mark_index(count) != SIZE_MAX) ++count;
                            step = 4;
                        }
                        if (app.page == THINGSOS_UI_STATS) {
                            count = app.stats_count;
                            step = 2;
                        }
                        if (app.page == THINGSOS_UI_WIFI) {
                            count = app.wifi_count;
                            step = (size_t)wifi_rows();
                        }
                        if (dy < 0 && app.list_start + step < count) app.list_start += step;
                        if (dy > 0)
                            app.list_start = app.list_start > step ? app.list_start - step : 0;
                        if(app.page==THINGSOS_UI_FILES && count && app.list_start>(count-1)/step*step)app.list_start=(count-1)/step*step;
                        if ((app.page == THINGSOS_UI_CHAPTERS || app.page == THINGSOS_UI_WIFI || (app.page == THINGSOS_UI_LIBRARY && !app.library_cards)) && count > step && app.list_start > count - step)
                            app.list_start = count - step;
                        if (app.list_start!=before) {
                            app.dirty = true;
                            if(app.page!=THINGSOS_UI_LIBRARY && app.page!=THINGSOS_UI_FILES && !reader_surface(app.page))app.transition_refresh = true;
                        }
                        }
                    } else if (!moved)
                        tap(initial_x, initial_y);
                }
            }
        }
        /* The gesture has already been consumed above. Do not leave a contact
           action's dragging latch set after release (notably after Home). */
        if(!input_contact)pressed=dragging=held=moved=false;
        if(healthy)auto_sleep_poll(time);
        if(app.asleep) {xSemaphoreGive(thingsos_io_lock);continue;}
        thingsos_board_idle();
        if(app.page==THINGSOS_UI_LOCK && app.lock_snapshot_valid) {
            app.dirty=false;app.force_refresh=false;app.transition_refresh=false;
        }
        if(usb_begin_requested) {thingsos_board_tilt_active(false);thingsos_tilt_reset(&app.tilt);usb_begin_requested=false;usb_mode_run();}
        reader_tilt_input_poll(pressed,keys.key!=0);
        wd_poll(); /* Input and accepted tilt intents precede optional mark/sync work. */
        app_system_poll();
        reader_annotations_poll();
        reader_annotations_auto_poll();
        message_expire_poll();
        if(!input_contact && !keys.key && !pressed && !dragging && !thingsos_board_touch_pending())reader_jump_poll();
        bool awaiting_release=reader_input_awaiting_release(&keys,pressed,held,dragging);
        if(!app.dirty && !app.force_refresh && !app.transition_refresh && !awaiting_release && !online_modal_animate()) {
            app.dirty=app.force_refresh=true;
        }
        if(app.dirty && !awaiting_release && ui_background_frame_yields(thingsos_board_touch_pending())) {
            xSemaphoreGive(thingsos_io_lock);
            vTaskDelay(pdMS_TO_TICKS(1));continue;
        }
        if (app.dirty && !awaiting_release && (app.ui_action_at || now() - refresh_at >= 120)) {
            bool pin_region=pin_input_frame();
            bool system_region=app_system_region();
            bool audio_region=app_audio_region();
            bool cubox_region=app_cubox_region();
            bool bluetooth_region=bt_ui_region();
            bool loading_region=online_modal_visible() && online.painted && !app.force_refresh && !app.transition_refresh && !app.feedback_refresh;
            bool wave_requested = reader_wave_requested();
            uint64_t layout_at=now();
            unsigned cover_hits=app.thumbnails.hits, cover_misses=app.thumbnails.misses;
            if (app.ui_action_at) app.ui_action_ms = (unsigned)(layout_at-app.ui_action_at);
            unsigned loads=app.font.glyph_loads, hits=app.font.bitmap_hits;
            unsigned font_errors=app.font.errors;
            unsigned ui_font_errors=app.ui_font.errors;
            draw();
            /* Retry font failures with a fresh face before presenting. A
               persistent failure shows explicit boxes and retains navigation. */
            if(app.font.errors!=font_errors) {
                ESP_LOGE("ThingsOS_font","Incomplete UI frame page=%u font_errors=%u retry=%u",
                    (unsigned)app.page,app.font.errors-font_errors,app.font_retries);
                if(!font_frame_ready(font_errors)) {
                    xSemaphoreGive(thingsos_io_lock);
                    vTaskDelay(pdMS_TO_TICKS(200));continue;
                }
            } else {
                app.font_retries=0;
            }
            if(!ui_font_frame_ready(ui_font_errors)) {
                xSemaphoreGive(thingsos_io_lock);
                vTaskDelay(pdMS_TO_TICKS(200));continue;
            }
            /* The sampler can enqueue a tap during draw(). Recheck before any
               refresh flags, output buffers or panel scan are committed; the
               next iteration can then paint the tap's destination directly. */
            if(ui_background_frame_yields(thingsos_board_touch_pending())) {
                ESP_LOGI("ThingsOS_touch","Queued input preempts background frame page=%u layout_ms=%u",
                    (unsigned)app.page,(unsigned)(now()-layout_at));
                xSemaphoreGive(thingsos_io_lock);
                vTaskDelay(pdMS_TO_TICKS(1));continue;
            }
            ESP_LOGI("ThingsOS_reader","UI layout page=%u: %u ms, glyph loads=%u bitmap hits=%u",
                (unsigned)app.page,(unsigned)(now()-layout_at),app.font.glyph_loads-loads,
                app.font.bitmap_hits-hits);
            if(app.page==THINGSOS_UI_READING) {
                thingsos_font_io_t io;
                if(thingsos_font_io(&app.font,&io))ESP_LOGI("ThingsOS_font","SD cache=%u reads=%llu requests=%llu hits=%llu",
                    io.cache_bytes,(unsigned long long)io.disk_reads,(unsigned long long)io.requests,(unsigned long long)io.cache_hits);
            }
            ESP_LOGI("ThingsOS_perf", "Covers load=%u ms decode=%u ms scale=%u ms loads=%u hits=%u misses=%u cache=%u bytes",
                app.cover_load_ms,app.cover_decode_ms,app.cover_scale_ms,app.cover_loads,
                app.thumbnails.hits-cover_hits,app.thumbnails.misses-cover_misses,(unsigned)app.thumbnails.bytes);
            thingsos_board_refresh_policy(
                reader_surface(app.page) ? app.refresh_mode : app.ui_refresh_mode,
                reader_surface(app.page) ? app.refresh_pages : app.ui_refresh_pages);
            if (app.force_refresh) {
                thingsos_board_full_refresh();
                app.force_refresh = false;
            }
            if (app.transition_refresh) {
                thingsos_board_transition_refresh();
                app.transition_refresh = false;
            }
            reader_cache_output_begin();
            thingsos_board_static_page(ui_static_panel_page());
            e = app.lock_wave_pending ? present_lock_transition() : pin_region?thingsos_board_present_pin_input(&app.canvas):
                (online_modal_visible() || online.cleanup)?online_modal_present_frame(loading_region):system_region?app_system_present():audio_region?app_audio_present():cubox_region?app_cubox_present():bluetooth_region?bt_ui_present():app.feedback_refresh
                ? (app.page==THINGSOS_UI_READING?thingsos_board_present_reader_background(&app.canvas):thingsos_board_present(&app.canvas))
                : app.controls_open || app.controls_refresh
                ? thingsos_board_present_controls(&app.canvas)
                : app.audio_progress && !app.controls_open
                ? thingsos_board_present_audio(&app.canvas)
                : wave_requested && !app.image_hit_count
                ? thingsos_board_present_reader_stagger(&app.canvas,app.ripple_forward)
                : app.page == THINGSOS_UI_PASSWORD && input_uses_qwerty() && (app.keyboard_refresh || app.input_refresh)
                ? (app.keyboard_refresh ? thingsos_board_present_keyboard(&app.canvas) : thingsos_board_present_input(&app.canvas))
                : app.page == THINGSOS_UI_WEIDU && app.weidu_progress_refresh
                ? thingsos_board_present_progress(&app.canvas)
                : reader_ui_active() ? thingsos_board_present_reader_ui(&app.canvas)
                : app.page == THINGSOS_UI_READING && !app.menu && !app.jump && !app.image_view.open && !app.controls_open
            ? (app.reader_background_refresh && !app.ui_action_at
                ? thingsos_board_present_reader_background(&app.canvas)
                : app.image_hit_count
                ? thingsos_board_present_reader_image(&app.canvas)
                : thingsos_board_present_reader(&app.canvas)) : thingsos_board_present(&app.canvas);
            reader_cache_output_end();
            app_system_presented(e==ESP_OK);
            app_audio_presented(e==ESP_OK);
            app_cubox_presented(e==ESP_OK);
            bt_ui_presented(e==ESP_OK);
            online_modal_presented(e==ESP_OK);
            volume_overlay_presented(e==ESP_OK);
            app.keyboard_refresh = false;
            app.audio_progress=false;
            app.feedback_refresh=false;
            app.controls_refresh=false;
            app.input_refresh = false;
            app.weidu_progress_refresh = false;
            app.dirty = false;
            app.reader_background_refresh=false;
            refresh_at = now();
            reader_cache_after = refresh_at + 80;
            if (app.ui_action_at) {
                ESP_LOGI("ThingsOS_perf", "Action id=%u page=%u before_layout=%u ms total=%u ms",
                    app.ui_action_id,(unsigned)app.page,app.ui_action_ms,(unsigned)(now()-app.ui_action_at));
                app.ui_action_at = 0;
            }
            if (e != ESP_OK) {
                ESP_LOGE("ThingsOS_board","UI refresh failed page=%u: %s (0x%x) retry=%u",
                    (unsigned)app.page,esp_err_to_name(e),(unsigned)e,app.refresh_retries);
                xSemaphoreGive(thingsos_io_lock);
                /* A transient panel/PMU read failure must not strand the device on
                   the recovery screen: redraw and try the frame again first. */
                if (app.refresh_retries < REFRESH_RETRY_LIMIT) {
                    ++app.refresh_retries;
                    app.dirty = true;
                    app.force_refresh = true;
                    vTaskDelay(pdMS_TO_TICKS(200));
                    continue;
                }
                fallback("Display refresh failed");
                return;
            }
            if (app.refresh_retries) {
                ESP_LOGW("ThingsOS_board","UI refresh recovered after %u retry(ies)",
                    app.refresh_retries);
                app.refresh_retries = 0;
            }
            if(app.opened && app.page==THINGSOS_UI_READING && !app.reader_ready_at)app.reader_ready_at=now();
        }
        /* Prepare the immediately useful page before whole-book indexing
           seeks through unrelated chapters of an SD font. */
        bool input_idle=reader_input_can_idle(&keys,input_contact,pressed,thingsos_board_touch_contact_pending());
        thingsos_wd_device_poll(app.wd_push_on && !online_pending() && app.page!=THINGSOS_UI_WEIDU && app.page!=THINGSOS_UI_CUBOX &&
            !(app.page==THINGSOS_UI_PASSWORD && (app.input_return==THINGSOS_UI_WEIDU || app.input_return==THINGSOS_UI_CUBOX)) &&
            !app.dirty && !app.asleep && input_idle && !dragging &&
            !thingsos_board_touch_pending() && now()>=app.activity_at+2000 && !app.wd_power_action);
        thingsos_wd_rt_poll(app.wd_rt_on && app.wd_time_on && app.wd_time_confirmed,
            !online_pending() && !app.dirty && !app.asleep && input_idle && !dragging &&
            !thingsos_board_touch_pending() && now()>=app.activity_at+2000 && !app.wd_power_action);
        if (input_idle && !dragging && !thingsos_board_touch_pending()) reader_cache_idle();
        if(input_idle && !dragging && !thingsos_board_touch_pending())reader_toc_poll();
        if (app.opened && reader_background_work_allowed() && !thingsos_network_busy() && !thingsos_weidu_busy() &&
            input_idle && now() >= app.activity_at + 500 &&
            !thingsos_board_touch_pending() && now() >= reader_cache_after + 500 &&
            !app.pagination_valid && !app.pagination_failed) {
            uint64_t until = now() + 8;
            for (unsigned i = 0; i < 8; ++i) {
                if (!pagination_step_impl(true) || app.pagination_valid || now() >= until) break;
            }
            if(app.pagination_valid || app.pagination_failed)background_redraw();
        }
        if (time - app.saved_at > 30000) {
            save_state();
            app.saved_at = time;
        }
        if (status_refresh_due(time)) {
            if (!app.dirty && !app.force_refresh && !app.transition_refresh && !app.ui_action_at &&
                (app.page==THINGSOS_UI_LIBRARY || app.page==THINGSOS_UI_TRANSFER || app.page==THINGSOS_UI_SETTINGS ||
                 (online_modal_visible() && app.canvas.width==684) ||
                 (!app.controls_open && (app.page==THINGSOS_UI_WEIDU || app.page==THINGSOS_UI_CUBOX ||
                  (app.page==THINGSOS_UI_PASSWORD && (app.input_return==THINGSOS_UI_WEIDU || app.input_return==THINGSOS_UI_CUBOX)))))) {
                app.font.ui_size_percent=(app.ui_px ? app.ui_px : 30)*100/30;
                app.font.ui_weight_override=app.ui_weight+1;
                thingsos_font_weight(&app.font,0);
                draw_status();
                app.font.ui_size_percent=app.font.ui_weight_override=0;
                thingsos_board_static_page(ui_static_panel_page());
                e=online_modal_visible()?online_modal_present_status():thingsos_board_present_status(&app.canvas);
                refresh_at=now();
                if(e!=ESP_OK) {
                    ESP_LOGW("ThingsOS_board","Status refresh failed: %s; retrying with full frame",esp_err_to_name(e));
                    app.dirty=app.force_refresh=true;
                }
            } else feedback_redraw();
            time_t seconds = wall_time();
            if (seconds >= 1577836800 && seconds <= 4102444799)
                thingsos_board_set_time((uint32_t)seconds);
        }
        if (!healthy && time - started >= 30000 && touch_ok) {
            const esp_partition_t* p = esp_ota_get_running_partition();
            esp_ota_img_states_t state;
            if (p && esp_ota_get_state_partition(p, &state) == ESP_OK &&
                state == ESP_OTA_IMG_PENDING_VERIFY)
                if (esp_ota_mark_app_valid_cancel_rollback() != ESP_OK) {
                    xSemaphoreGive(thingsos_io_lock);
                    fallback("Health confirmation failed");
                    return;
                }
            thingsos_board_healthy();
            thingsos_diag_health();thingsos_diag_poll(true);
            healthy = true;startup_healthy=true;thingsos_network_healthy();
            ESP_LOGI(
                "ThingsOS", "Startup health confirmed: display, font, touch; single app, no version rollback");
        }
        if (!healthy && time - started > 60000 && !touch_ok) {
            xSemaphoreGive(thingsos_io_lock);
            fallback("Touch health check failed");
            return;
        }
        xSemaphoreGive(thingsos_io_lock);
        vTaskDelay(pdMS_TO_TICKS(thingsos_board_touch_pending()?1:20));
    }
}
#endif
