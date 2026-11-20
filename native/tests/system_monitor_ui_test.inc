/* SPDX-License-Identifier: Apache-2.0 */
#include "thingsos/display_buffer.h"
static void system_monitor_controller_test(void) {
    bool sd=host_sd;host_sd=false;
    app.pin.enabled=app.pin.required=app.pin.fault=false;app.asleep=false;
    app.controls_open=app_media.preview=false;app.lock_wave_pending=false;
    app.font.ui_size_percent=100;host_touch_contact=false;host_nvs_available=true;
    host_system=(thingsos_system_snapshot_t){.cpu_mhz=320,.cpu_max_mhz=320,
        .sram={.total=384*1024,.free=150*1024,.largest=96*1024,.valid=true},
        .psram={.total=16ULL<<20,.free=10300000,.largest=8ULL<<20,.valid=true},
        .display={.pclk_mhz=20,.last_refresh_ms=312,.refresh_valid=true}};
    CHECK(thingsos_system_flash_layout(16ULL<<20,0xae0000,6905840,&host_system.flash));
    unsigned reads=host_system_reads,resets=host_system_resets;
    navigate(THINGSOS_UI_APPS);snapshot("apps-system");
    tap(130,680);CHECK(app.page==THINGSOS_UI_SYSTEM&&app.app_return==THINGSOS_UI_APPS);
    CHECK(host_system_reads==reads+1&&host_system_resets==resets+1);
    snapshot("system-warmup");CHECK(!system_ui.state.cpu_valid&&!app_system_region());
    app_system_presented(true);app.dirty=false;app.transition_refresh=app.force_refresh=false;
    app.controls_refresh=app.feedback_refresh=false;app.message[0]=0;
    uint64_t activity=app.activity_at;
    host_system.cpu_used_bp=2400;host_system.cpu_valid=true;host_clock_us+=2100000;
    app_system_poll();CHECK(host_system_reads==reads+2&&app.dirty&&app_system_region());
    CHECK(app.activity_at==activity);snapshot("system-live");
    CHECK(app.canvas.pixels[520*684+40]==0&&app.canvas.pixels[520*684+600]==232);
    unsigned metric_frames=host_metrics_presents,loading_frames=host_loading_presents;
    CHECK(app_system_present()==ESP_OK&&host_metrics_presents==metric_frames+1);
    CHECK(host_loading_presents==loading_frames);
    /* Actual portrait packing: unchanged gray tracks must remain gray, a CPU
       change must touch only its rows, and an identical frame must skip work. */
    unsigned char* packed=malloc(THINGSOS_PANEL_BYTES);CHECK(packed);
    bool inverted=app.canvas.inverted;app.canvas.inverted=false;
    CHECK(thingsos_panel_pack(&app.canvas,packed,THINGSOS_PANEL_BYTES,false));
    host_system.cpu_used_bp=6200;app_system_take();++reads;draw();
    unsigned first=0,rows=0;
    CHECK(thingsos_panel_pack_diff(&app.canvas,packed,THINGSOS_PANEL_BYTES,false,&first,&rows));
    CHECK(rows&&first>=440&&first+rows<=568);
    CHECK((packed[(683-600)*608+520/2]&15)==14);
    CHECK(thingsos_panel_pack_diff(&app.canvas,packed,THINGSOS_PANEL_BYTES,false,&first,&rows)&&!rows);
    tap(500,250);CHECK(system_ui.details);draw();canvas_snapshot("system-more-bars");
    CHECK(thingsos_panel_pack(&app.canvas,packed,THINGSOS_PANEL_BYTES,false));
    host_system.display.pclk_mhz=10;host_system.display.last_refresh_ms=900;
    system_ui.state=host_system;draw();
    CHECK(thingsos_panel_pack_diff(&app.canvas,packed,THINGSOS_PANEL_BYTES,false,&first,&rows));
    CHECK(rows&&first>=300&&first+rows<900);
    CHECK(app.canvas.pixels[375*684+600]==232);
    CHECK(app.canvas.pixels[520*684+40]==0&&app.canvas.pixels[520*684+600]==232);
    canvas_snapshot("system-display-fallback");
    host_system.display.underrun_count=3;host_system.display.underrun_last_seconds=918;
    host_system.display.underrun_during_clear=true;host_system.display.last_present_failed=true;
    system_ui.state=host_system;draw();canvas_snapshot("system-underrun-failed");
    CHECK(system_ui.state.display.underrun_count==3);
    host_system.display.last_present_failed=false;app_system_take();++reads;draw();canvas_snapshot("system-underrun-recovered");
    CHECK(system_ui.state.display.underrun_count==3);
    tap(100,250);CHECK(!system_ui.details);draw();
    free(packed);app.canvas.inverted=inverted;
    host_metrics_result=ESP_FAIL;CHECK(app_system_present()==ESP_FAIL);host_metrics_result=ESP_OK;
    app.force_refresh=true;CHECK(!app_system_region());app.force_refresh=false;
    app_system_presented(false);CHECK(!system_ui.painted&&!system_ui.regional);
    app.dirty=false;host_clock_us+=2100000;host_touch_contact=true;app_system_poll();
    CHECK(host_system_reads==reads+2);host_touch_contact=false;
    app.controls_open=true;app_system_poll();CHECK(host_system_reads==reads+2);
    app.controls_open=false;app_system_poll();CHECK(host_system_reads==reads+3&&!app_system_region());
    app_system_presented(true);app.dirty=false;
    tap(600,150);CHECK(host_system_reads==reads+4&&app.page==THINGSOS_UI_SYSTEM);
    tap(60,150);CHECK(app.page==THINGSOS_UI_APPS&&!system_ui.visible);
    host_clock_us+=3000000;app_system_poll();CHECK(host_system_reads==reads+4);
    tap(130,680);CHECK(app.page==THINGSOS_UI_SYSTEM);
    host_system=(thingsos_system_snapshot_t){.cpu_error=true};app_system_take();snapshot("system-unavailable");
    capacitive_key(THINGSOS_KEY_PREVIOUS);CHECK(app.page==THINGSOS_UI_APPS);
    navigate(THINGSOS_UI_SYSTEM);app.asleep=true;app_system_poll();CHECK(!system_ui.visible);
    app.asleep=false;navigate(THINGSOS_UI_LIBRARY);memset(&host_system,0,sizeof(host_system));
    host_sd=sd;
    puts("PASS: system app grayscale differences, unchanged frame skip, no loading redraw, input priority, inactivity, return and sleep lifecycle");
}
