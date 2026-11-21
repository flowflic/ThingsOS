// SPDX-License-Identifier: Apache-2.0
#include "thingsos/ui.h"
#include <limits.h>
#include <string.h>
void thingsos_ui_layout(thingsos_ui_layout_t* l) {
    if (!l) return;
    l->status = (thingsos_rect_t){ 36, 0, 612, 74 };
    l->header = (thingsos_rect_t){ 36, 74, 612, 146 };
    l->body = (thingsos_rect_t){ 36, 220, 612, 878 };
    l->footer = (thingsos_rect_t){ 0, 1098, 684, 118 };
    memset(l->navigation, 0, sizeof(l->navigation));
    for (int i = 0; i < 4; i++) l->navigation[i] = (thingsos_rect_t){ i * 171, 1098, 171, 118 };
}
int thingsos_ui_nav_hit(int x, int y) {
    return x >= 0 && x < 684 && y >= 1098 && y < 1216 ? x / 171 : -1;
}
void thingsos_image_open(thingsos_image_view_t* v, thingsos_location_t loc) {
    if (!v) return;
    memset(v, 0, sizeof(*v));
    v->open = true;
    v->zoom_percent = 100;
    v->return_location = loc;
}
void thingsos_image_zoom(thingsos_image_view_t* v, int delta) {
    if (!v || !v->open) return;
    int64_t z = (int64_t)v->zoom_percent + delta;
    v->zoom_percent = (unsigned)(z < 100 ? 100 : z > 300 ? 300 : z);
    v->pan_x = 0;
    v->pan_y = 0;
}
void thingsos_image_close(thingsos_image_view_t* v, thingsos_location_t* loc) {
    if (!v || !v->open) return;
    if (loc) *loc = v->return_location;
    v->open = false;
}

static int pan_axis(int current, int delta, unsigned image, unsigned viewport, unsigned zoom) {
    int64_t extent = (int64_t)image * zoom / 100 - viewport;
    if (extent < 0) extent = 0;
    if (extent > INT_MAX) extent = INT_MAX;
    int64_t next = (int64_t)current + delta;
    return (int)(next < 0 ? 0 : next > extent ? extent : next);
}
void thingsos_image_pan(
    thingsos_image_view_t* v, int dx, int dy, unsigned w, unsigned h, unsigned vw, unsigned vh) {
    if (!v || !v->open || !w || !h || !vw || !vh) return;
    v->pan_x = pan_axis(v->pan_x, dx, w, vw, v->zoom_percent);
    v->pan_y = pan_axis(v->pan_y, dy, h, vh, v->zoom_percent);
}
