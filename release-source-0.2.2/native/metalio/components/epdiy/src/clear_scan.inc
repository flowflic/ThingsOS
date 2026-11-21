/* SPDX-License-Identifier: LGPL-3.0-or-later */
static enum EpdDrawError clear_area_cycles(EpdRect area, int cycles, int cycle_time) {
    /* Metalio needs more white-going than dark-going phases to erase fully. */
    static const int phases[] = {10, 13, 3};
    for (int cycle = 0; cycle < cycles; ++cycle) {
        for (int color = 0; color < 3; ++color) {
            for (int phase = 0; phase < phases[color]; ++phase) {
                epd_push_pixels(area, cycle_time, color);
                enum EpdDrawError error = render_context.error;
                if (error != EPD_DRAW_SUCCESS) {
                    ESP_LOGE("epdiy", "Physical clear failed cycle=%d color=%d phase=%d error=0x%x",
                             cycle, color, phase, (unsigned)error);
                    return error;
                }
            }
        }
    }
    return EPD_DRAW_SUCCESS;
}
