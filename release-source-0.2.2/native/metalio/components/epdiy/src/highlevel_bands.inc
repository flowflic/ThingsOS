/* SPDX-License-Identifier: LGPL-3.0-or-later */
/* Full-width logical row bands on a portrait display become native column
   bands. Calculate their differences together, then apply exact column masks
   in one scan. The vector diff's 32-pixel alignment must not widen the drive
   or advance the back buffer in the gap between the bands. */
enum EpdDrawError epd_hl_update_bands_full(
    EpdiyHighlevelState* state, enum EpdDrawMode mode, int temperature,
    const EpdRect* bands, size_t count
) {
    if (!state || !bands || !count || count > 2) return EPD_DRAW_INVALID_CROP;
    EpdRect physical[2];
    int width = epd_width(), height = epd_height();
    int left = width, right = 0;
    for (size_t i = 0; i < count; ++i) {
        if (bands[i].width <= 0 || bands[i].height <= 0) return EPD_DRAW_INVALID_CROP;
        physical[i] = _inverse_rotated_area(bands[i].x, bands[i].y, bands[i].width, bands[i].height);
        EpdRect area = physical[i];
        if (area.y != 0 || area.height != height || area.x < 0 || area.width > width ||
            area.x > width - area.width) return EPD_DRAW_INVALID_CROP;
        if (area.x < left) left = area.x;
        if (area.x + area.width > right) right = area.x + area.width;
    }
    uint32_t started = esp_timer_get_time() / 1000;
    epd_difference_image_cropped(state->front_fb, state->back_fb,
        (EpdRect){left, 0, right - left, height}, state->difference_fb,
        state->dirty_lines, state->dirty_columns);
    memset(state->dirty_lines, 1, (size_t)height * sizeof(bool));
    memset(state->dirty_columns, 0, (size_t)width / 2);
    for (size_t i = 0; i < count; ++i) {
        EpdRect area = physical[i];
        for (int x = area.x; x < area.x + area.width; ++x)
            state->dirty_columns[x / 2] |= (uint8_t)(15u << (4 * (x & 1)));
    }
    uint32_t prepared = esp_timer_get_time() / 1000;
    EpdRect full = epd_full_screen();
    enum EpdDrawError error = epd_draw_base(full, state->difference_fb, full,
        MODE_PACKING_1PPB_DIFFERENCE | mode, temperature,
        state->dirty_lines, state->dirty_columns, state->waveform);
    uint32_t drawn = esp_timer_get_time() / 1000;
    if (error == EPD_DRAW_SUCCESS) {
        for (size_t i = 0; i < count; ++i) {
            EpdRect area = physical[i];
            for (int y = 0; y < height; ++y) {
                const uint8_t* front = state->front_fb + (size_t)y * width / 2;
                uint8_t* back = state->back_fb + (size_t)y * width / 2;
                int begin = area.x, end = area.x + area.width;
                if (begin & 1) {
                    back[begin / 2] = (back[begin / 2] & 15) | (front[begin / 2] & 240);
                    ++begin;
                }
                if (end & 1) {
                    back[end / 2] = (back[end / 2] & 240) | (front[end / 2] & 15);
                    --end;
                }
                if (end > begin) memcpy(back + begin / 2, front + begin / 2, (size_t)(end - begin) / 2);
            }
        }
    }
    uint32_t finished = esp_timer_get_time() / 1000;
    hl_record_timing(prepared - started, drawn - prepared, finished - drawn);
    ESP_LOGI("epdiy", "band diff: %ums, draw: %ums, buffer update: %ums, scans=1, bands=%u",
        (unsigned)(prepared - started), (unsigned)(drawn - prepared),
        (unsigned)(finished - drawn), (unsigned)count);
    return error;
}
