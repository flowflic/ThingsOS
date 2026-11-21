// SPDX-License-Identifier: Apache-2.0
#ifndef THINGSOS_UI_H
#define THINGSOS_UI_H
#include "thingsos/runtime.h"
/* Device pages from v3. Browser panels belong to the future HTTP client. */
typedef enum {
    THINGSOS_UI_LIBRARY,
    THINGSOS_UI_READING,
    THINGSOS_UI_CHAPTERS,
    THINGSOS_UI_BOOKMARKS,
    THINGSOS_UI_RECENT,
    THINGSOS_UI_STATS,
    THINGSOS_UI_FILES,
    THINGSOS_UI_FONTS,
    THINGSOS_UI_SETTINGS,
    THINGSOS_UI_DISPLAY,
    THINGSOS_UI_TIME,
    THINGSOS_UI_POWER,
    THINGSOS_UI_LOCK,
    THINGSOS_UI_TRANSFER,
    THINGSOS_UI_WIFI,
    THINGSOS_UI_PASSWORD,
    THINGSOS_UI_UPGRADE,
    THINGSOS_UI_RECOVERY,
    THINGSOS_UI_MANAGE,
    THINGSOS_UI_WEIDU,
    THINGSOS_UI_WALLPAPER,
    THINGSOS_UI_USB,
    THINGSOS_UI_READER_MORE,
    THINGSOS_UI_CONTROL_CENTER,
    THINGSOS_UI_BLE,
    THINGSOS_UI_CUBOX,
    THINGSOS_UI_LOCK_PIN,
    THINGSOS_UI_SYSTEM,
    THINGSOS_UI_APPS,
    THINGSOS_UI_LOGS,
    THINGSOS_UI_FLOMO,
    THINGSOS_UI_COUNT
} thingsos_ui_page_t;
typedef struct {
    int x, y, width, height;
} thingsos_rect_t;
typedef struct {
    thingsos_rect_t status, header, body, footer;
    thingsos_rect_t navigation[4];
} thingsos_ui_layout_t;
typedef struct {
    bool open;
    unsigned zoom_percent;
    int pan_x, pan_y;
    thingsos_location_t return_location;
} thingsos_image_view_t;
void thingsos_ui_layout(thingsos_ui_layout_t* layout);
int thingsos_ui_nav_hit(int x, int y);
void thingsos_image_open(thingsos_image_view_t* view, thingsos_location_t location);
void thingsos_image_zoom(thingsos_image_view_t* view, int delta);
void thingsos_image_pan(
    thingsos_image_view_t* view, int dx, int dy, unsigned image_width, unsigned image_height,
    unsigned viewport_width, unsigned viewport_height);
void thingsos_image_close(thingsos_image_view_t* view, thingsos_location_t* location);
#endif
