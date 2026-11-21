/* SPDX-License-Identifier: Apache-2.0 */
static bool loading_canvas_target(const thingsos_canvas_t* c) {
    return loading_area.x==0 && loading_area.y==0 &&
           loading_area.width==c->width && loading_area.height==c->height;
}
static enum EpdDrawError loading_present(const thingsos_canvas_t* c,bool landscape,
                                         int temperature,enum EpdDrawMode* mode) {
    EpdRect area=landscape?(EpdRect){684-loading_area.y-loading_area.height,
        loading_area.x,loading_area.height,loading_area.width}:loading_area;
    bool spinner=loading_area.width<=128 && loading_area.height<=128;
    bool canvas=loading_canvas_target(c);
    *mode=spinner?MODE_DU:MODE_GL16;
    enum EpdDrawError result;
    if(spinner) {
        epd_hl_waveform(&hl,&E0470_FOLLOW_WAVEFORM);
        result=epd_hl_update_area(&hl,*mode,temperature,area);
    } else if(canvas) {
        /* Dimming/removal can leave optical ink on software-white pixels.
           Differential masks suppress the GL16 equal-white maintenance tick,
           and reader menus defer automatic cleanup. Deliver the same GL16
           table to every pixel in this one scan; no GC16 black/white cycle. */
        result=epd_hl_update_area_full(&hl,*mode,temperature,area);
    } else result=epd_hl_update_area_full(&hl,*mode,temperature,area);
    ESP_LOGI("ThingsOS_board","Loading region x=%d y=%d w=%d h=%d spinner=%d canvas=%d result=0x%x",
        loading_area.x,loading_area.y,loading_area.width,loading_area.height,spinner,canvas,(unsigned)result);
    return result;
}
