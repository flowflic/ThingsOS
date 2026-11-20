/* SPDX-License-Identifier: Apache-2.0
 * Portrait GC16 scheduler with a bounded current-phase SRAM working set.
 * Difference is computed once; each physical row selects its original phase.
 */
#include "thingsos/stagger.h"
#ifndef THINGSOS_STAGGER_HOST
#include "esp_rom_sys.h"
#endif
#include "panel_waveform.h"
void thingsos_scan_row_tables(const uint8_t* const* tables);
static const uint8_t* turn_rows[684];
static enum EpdDrawError stagger_turn(int temperature,bool forward,bool clean,bool* attempted) {
    *attempted=false;
    if(epd_get_rotation()!=EPD_ROT_INVERTED_PORTRAIT || epd_width()!=1216 || epd_height()!=684)
        return EPD_DRAW_SUCCESS;
    enum EpdDrawMode drive_mode=MODE_GC16;
    const EpdWaveform* panel=thingsos_panel_waveform();
    const EpdWaveformPhases* original=e0470_waveform_phases(panel,drive_mode);
    if(!original || original->phases<=0 || original->phases>THINGSOS_PANEL_GC16_PHASES ||
       !original->luts || original->phase_times)
        return EPD_DRAW_SUCCESS;
    /* LUT lookup is a hard scan deadline. Keep only the eight CURRENT phase
       tables in internal SRAM, with a 16 KiB reserve; never stream random LUT
       lookups from PSRAM while the feeders consume the PSRAM image. */
    size_t bytes=THINGSOS_TURN_BANDS*1024;
    if(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)<bytes+16*1024)return EPD_DRAW_SUCCESS;
    uint8_t* tables=heap_caps_aligned_alloc(16,bytes,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    if(!tables)return EPD_DRAW_SUCCESS;
    uint8_t text_data[THINGSOS_PANEL_GC16_PHASES*64];
    EpdWaveformPhases view=*original;
    if(!clean) {
        if(!thingsos_panel_reader_ripple_wave(original->luts,original->phases,text_data,sizeof(text_data))) {
            heap_caps_free(tables);return EPD_DRAW_SUCCESS;
        }
        view.luts=text_data;
    }
    EpdWaveformPhases single={.phases=1,.luts=view.luts,.phase_times=NULL};
    const EpdWaveformPhases* ranges[]={&single};
    EpdWaveformMode mode={.type=drive_mode,.temp_ranges=1,.range_data=ranges};
    const EpdWaveformMode* modes[]={&mode};
    EpdWaveform apply={.num_modes=1,.num_temp_ranges=1,
        .temp_intervals=panel->temp_intervals,.mode_data=modes};
    EpdRect area=epd_full_screen();
    epd_difference_image_cropped(hl.front_fb,hl.back_fb,area,
        hl.difference_fb,hl.dirty_lines,hl.dirty_columns);
    /* Row/column diff masks would discard the source white reset sequence.
       This is one full-screen scan stream, with overlapping phase offsets. */
    memset(hl.dirty_columns,0xff,epd_width()/2);
    *attempted=true;
    enum EpdDrawError result=EPD_DRAW_SUCCESS;
    {
        const unsigned gap=clean?THINGSOS_CLEAN_BAND_GAP:1;
        const unsigned ticks=(unsigned)original->phases+(THINGSOS_TURN_BANDS-1)*gap;
        int64_t started=esp_timer_get_time();
        /* Keep calibrated FULL scan timing and the established prefill margin. */
        for(unsigned tick=0;tick<ticks;++tick) {
            int64_t before=esp_timer_get_time();
            for(unsigned row=0;row<684;++row) {
                int phase=thingsos_turn_phase(row,684,forward,tick,(unsigned)original->phases,gap);
                unsigned band=row*THINGSOS_TURN_BANDS/684;
                if(phase>=0 && (!row || band!=(row-1)*THINGSOS_TURN_BANDS/684))
                    epd_build_1ppB_lut_1k(tables+(size_t)band*1024,&view,phase);
                turn_rows[row]=phase<0?NULL:tables+(size_t)band*1024;
                hl.dirty_lines[row]=phase>=0;
            }
            thingsos_scan_row_tables(turn_rows);
            result=epd_draw_base(area,hl.difference_fb,area,
                MODE_PACKING_1PPB_DIFFERENCE|drive_mode,temperature,
                hl.dirty_lines,hl.dirty_columns,&apply);
            thingsos_scan_row_tables(NULL);
            if(result!=EPD_DRAW_SUCCESS)break;
            int64_t elapsed=esp_timer_get_time()-before;
            if(elapsed<THINGSOS_TURN_TICK_US)
                esp_rom_delay_us((uint32_t)(THINGSOS_TURN_TICK_US-elapsed));
        }
        ESP_LOGI("ThingsOS_wave","Stagger full_pixels=1 clean=%d bands=8 gap=%u phases=%d ticks=%u forward=%d wall=%lld ms result=0x%x lut=SRAM/8192 drive=GC16 black_corrected=%d",
            clean,gap,original->phases,ticks,forward,(long long)((esp_timer_get_time()-started)/1000),(unsigned)result,!clean);
    }
    epd_leading_skip_discard();
    /* A partial scan never commits an imaginary completed frame. */
    if(result==EPD_DRAW_SUCCESS)memcpy(hl.back_fb,hl.front_fb,THINGSOS_PANEL_BYTES);
    heap_caps_free(tables);
    return result;
}
