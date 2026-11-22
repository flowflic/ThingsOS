/* SPDX-License-Identifier: Apache-2.0 */
static void readtime_auth_result(thingsos_weidu_phase_t phase) {
    host_weidu.busy=false;host_weidu.phase=phase;++host_weidu.serial;weidu_poll();
}
static void readtime_default_test(void) {
    bool available=host_nvs_available,read=host_nvs_read,fail=host_nvs_commit_fail;
    bool present=host_offline_present,valid=host_saved_wd_valid;uint8_t offline=host_saved_offline;
    uint8_t saved[4];memcpy(saved,host_saved_wd,sizeof(saved));unsigned requests=host_weidu_calls;
    host_nvs_available=false;app.wd_time_on=app.wd_rt_on=false;preferences_load();CHECK(app.wd_time_on&&app.wd_rt_on);
    host_nvs_available=host_nvs_read=true;host_nvs_commit_fail=false;host_saved_wd_valid=false;
    host_offline_present=true;host_saved_offline=0;preferences_load();CHECK(app.wd_time_on&&app.wd_rt_on);
    host_saved_wd_valid=true;host_saved_wd[2]=0;preferences_load();CHECK(!app.wd_time_on&&!app.wd_rt_on);
    host_saved_wd[2]=1;preferences_load();CHECK(app.wd_time_on&&app.wd_rt_on);
    host_saved_wd[2]=99;preferences_load();CHECK(app.wd_time_on&&app.wd_rt_on);
    saved_preferences_valid=false;CHECK(wd_rt_set(false));
    CHECK(!app.wd_time_on&&!app.wd_rt_on&&host_saved_wd[2]==0);
    app.wd_time_on=app.wd_rt_on=true;preferences_load();CHECK(!app.wd_time_on&&!app.wd_rt_on);
    CHECK(host_weidu_calls==requests);
    host_nvs_available=available;host_nvs_read=read;host_nvs_commit_fail=fail;
    host_offline_present=present;host_saved_offline=offline;host_saved_wd_valid=valid;
    memcpy(host_saved_wd,saved,sizeof(saved));saved_preferences_valid=false;
    puts("PASS: unified default-on duration setting, legacy offline key retired, saved master off survives reload");
}
static void readtime_controller_test(void) {
    readtime_default_test();bool network_before=host_network_connected;
    host_sd=host_network_connected=host_nvs_available=true;host_nvs_commit_fail=false;
    host_weidu.busy=false;host_weidu.authenticated=true;app.weidu=host_weidu;
    app.page=THINGSOS_UI_WEIDU;app.wd_sync_panel=true;app.wd_rt_panel=false;
    app.wd_time_on=app.wd_rt_on=app.wd_time_confirmed=false;
    memset(&host_rt_status,0,sizeof(host_rt_status));
    host_rt_status.today_ready=host_rt_status.uploaded_ready=true;
    host_rt_status.uploaded=7384;
    host_rt_status.today_online=120;host_rt_status.today_native=3600;
    unsigned starts=host_weidu_calls;
    CHECK(wd_tap(100,845)&&app.wd_consent);CHECK(wd_tap(500,875));
    CHECK(app.wd_time_on&&app.wd_rt_on&&app.wd_time_confirmed&&!app.wd_consent);
    CHECK(host_weidu_calls==starts);
    char text[160];wd_rt_uploaded_text(text,sizeof(text),&host_rt_status);
    CHECK(strstr(text,"1 小时 02 分 00 秒"));snapshot("weread-unified-sync");
    host_nvs_commit_fail=true;CHECK(wd_tap(100,845)&&app.wd_time_on&&app.wd_rt_on);
    uint64_t reading=app.runtime.reading_ms;app.wd_active=true;app.runtime.reading_ms=10000;
    host_nvs_commit_fail=false;CHECK(wd_tap(100,845)&&!app.wd_time_on&&!app.wd_rt_on);
    CHECK(app.wd_accounted==10000);app.runtime.reading_ms+=5000;
    snapshot("weread-unified-off");CHECK(wd_tap(100,845)&&app.wd_time_on&&app.wd_rt_on);
    CHECK(app.wd_accounted==15000);app.runtime.reading_ms=reading;app.wd_active=false;
    host_wall_time=1794002400;wd_rt_ui_day=wd_rt_display_day();app.dirty=false;
    uint64_t activity=app.activity_at;host_wall_time+=86400;wd_rt_day_poll();
    CHECK(app.dirty&&app.activity_at==activity);host_wall_time=0;
    /* Pico 0.4.3 offline-readtime page: entering requests a transition refresh;
       the status card is not a control; the single action button (y 890..962)
       asks for QR authorization while the channel is unauthorized. */
    app.transition_refresh=false;
    CHECK(wd_tap(100,1025)&&app.wd_rt_panel&&app.transition_refresh);starts=host_weidu_calls;
    snapshot("weread-rt-need-auth");
    CHECK(wd_rt_tap(100,270)&&app.wd_time_on&&app.wd_rt_on&&host_weidu_calls==starts);
    CHECK(wd_rt_tap(100,926)&&host_weidu_action==WD_DEVICE_RT_AUTH);
    CHECK(!wd_draw());readtime_auth_result(WD_CANCELLED);
    CHECK(app.wd_time_on&&app.wd_rt_on&&!app.wd_rt_auth_pending);
    CHECK(wd_rt_tap(100,926));readtime_auth_result(WD_ERROR);CHECK(app.wd_time_on&&app.wd_rt_on);
    CHECK(weidu_begin(WD_DEVICE_COVERS,NULL));
    CHECK(wd_rt_tap(100,926)&&app.weidu_deferred);
    weidu_poll();CHECK(!app.weidu_deferred&&host_weidu_action==WD_DEVICE_RT_AUTH);
    host_rt_status.authorized=true;host_rt_status.expired=false;host_rt_status.error=WD_OK;
    readtime_auth_result(WD_READY);CHECK(app.wd_time_on&&app.wd_rt_on);
    app.weidu_covers_pending=false;
    host_rt_status.queue.pending=3690;host_rt_status.queue.unknown=60;
    snapshot("weread-unified-details");
    {unsigned px=app.ui_px;app.ui_px=50;snapshot("weread-rt-ready-large");app.ui_px=px;}
    CHECK(wd_rt_tap(100,926)&&app.wd_rt_details);snapshot("weidu-readtime-unknown-help");
    capacitive_key(THINGSOS_KEY_PREVIOUS);CHECK(!app.wd_rt_details&&app.wd_rt_panel);
    /* The "待确认记录" metrics cell also opens the explanation; its own button returns. */
    CHECK(wd_rt_tap(500,640)&&app.wd_rt_details);
    CHECK(wd_rt_tap(340,926)&&!app.wd_rt_details&&app.wd_rt_panel);
    host_rt_status.blocked=true;unsigned resumes=host_rt_resumes;
    CHECK(wd_rt_tap(100,926)&&!host_rt_status.blocked&&host_rt_resumes==resumes+1);
    app.transition_refresh=false;
    CHECK(wd_rt_tap(590,150)&&!app.wd_rt_panel&&app.transition_refresh);
    CHECK(wd_tap(100,845)&&!app.wd_time_on&&!app.wd_rt_on);
    CHECK(host_rt_status.queue.pending==3690&&host_rt_status.queue.unknown==60);
    CHECK(wd_tap(100,845)&&app.wd_time_on&&app.wd_rt_on);
    CHECK(wd_tap(100,1025)&&app.wd_rt_panel);
    host_rt_status.today_ready=false;wd_rt_uploaded_text(text,sizeof(text),&host_rt_status);CHECK(strstr(text,"暂不可读取"));
    host_rt_status.today_ready=true;host_rt_status.today_online=UINT64_MAX;host_rt_status.today_native=1;
    wd_rt_uploaded_text(text,sizeof(text),&host_rt_status);CHECK(strstr(text,"暂不可读取"));
    host_rt_status.today_online=120;host_rt_status.today_native=3600;
    app.weidu.busy=false;
    app.wd_active = true;
    app.wd_rt_session = true;
    wd_rt_begin();
    CHECK(wd_rt_capture);
    wd_rt_capture->count = 0;
    wd_rt_checkpoint(true);
    CHECK(wd_rt_ready);
    strcpy(app.wd_owner, "0123456789abcdef0123456789abcdef");
    strcpy(app.wd_id, "99");
    wd_rt_last_mono = wd_rt_last_wall = 0;
    wd_rt_floor = 0;
    host_wall_time = 1794002400;
    host_clock_us = 2000000;
    wd_rt_interval(NULL, 1000, 1000);
    CHECK(wd_rt_capture->count == 1 && wd_rt_capture->rows[0].milliseconds == 1000);
    /* A Web session does not gain E-Ink ownership by toggling the setting. */
    app.wd_rt_session = false;
    wd_rt_interval(NULL, 1000, 1000);
    CHECK(wd_rt_capture->rows[0].milliseconds == 1000);
    app.wd_rt_session = true;
    app.wd_rt_on = false;
    wd_rt_interval(NULL, 1000, 1000);
    CHECK(wd_rt_capture->rows[0].milliseconds == 1000);
    app.wd_rt_on = true;
    wd_rt_floor = (uint32_t)host_wall_time + 10;
    wd_rt_interval(NULL, 1000, 1000);
    CHECK(wd_rt_clock_problem && wd_rt_capture->rows[0].milliseconds == 1000);
    free(wd_rt_capture);
    wd_rt_capture = NULL;
    wd_rt_ready = false;
    wd_rt_last_mono = wd_rt_last_wall = 0;
    wd_rt_floor = 0;
    wd_rt_problem = wd_rt_clock_problem = false;
    host_wall_time = 0;
    app.wd_active = app.wd_rt_session = app.wd_rt_on = app.wd_time_on = app.wd_rt_panel = app.wd_sync_panel = false;
    app.wd_id[0] = 0;
    memset(&host_rt_status, 0, sizeof(host_rt_status));
    host_network_connected=network_before;
    puts("PASS: one parent switch, read-only details, authorization isolation, daily totals, preserved queues, exclusive clock ownership and rollback "
         "floor");
}
