/* SPDX-License-Identifier: Apache-2.0 */
/* Input uses one fast DU scan; layout changes use one complete GL16 scan.
   Exact masks hold the submit/cancel gap after vector-difference alignment. */
static void keyboard_regions(bool layout,bool pin,EpdRect bands[2]) {
    bands[0]=pin?(EpdRect){0,320,684,158}:(EpdRect){0,300,684,254};
    bands[1]=pin?(EpdRect){0,1090,684,76}:(EpdRect){0,638,684,layout?440:116};
}
static enum EpdDrawError keyboard_present(bool layout, int temperature, bool dark,bool pin) {
    EpdRect bands[2];keyboard_regions(layout,pin,bands);
    enum EpdDrawMode mode = dark ? MODE_GC16 : layout ? MODE_GL16 : MODE_DU;
    epd_hl_waveform(&hl, dark ? dark_refresh_wave : layout ? panel_wave : thingsos_panel_fast_waveform());
    return epd_hl_update_bands_full(&hl, mode, temperature, bands, 2);
}
