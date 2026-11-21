/* SPDX-License-Identifier: Apache-2.0
 * No previous canvas: the board retains the physical 4bpp history.
 */
static bool reader_wave_requested(void) {
    bool pending=app.ripple_pending;
    app.ripple_pending=false;
    return pending && app.ripple_enabled && app.page==THINGSOS_UI_READING &&
        !app.menu && !app.jump && !app.image_view.open && !app.image_hit_count &&
        !app.force_refresh && !app.transition_refresh && !thingsos_network_busy() &&
        app.refresh_mode==0;
}
