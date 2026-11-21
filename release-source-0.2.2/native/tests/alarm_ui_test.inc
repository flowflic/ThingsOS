// SPDX-License-Identifier: Apache-2.0
static void alarm_controller_test(void){
    time_t old_wall=host_wall_time;int64_t old_clock=host_clock_us;
    char old_root[512];strlcpy(old_root,host_media_root,sizeof(old_root));
    snprintf(host_media_root,sizeof(host_media_root),"%s/alarm-media",fixture_root);mkdir(host_media_root,0775);
    char path[800];app_storage_path(path,sizeof(path),"music");mkdir(path,0775);
    size_t end=strlen(path);snprintf(path+end,sizeof(path)-end,"/birds.wav");
    FILE* f=fopen(path,"wb");CHECK(f);CHECK(fwrite("fixture",1,7,f)==7);fclose(f);
    memset(alarms,0,sizeof(alarms));memset(alarm_pending,0,sizeof(alarm_pending));memset(alarm_snooze,0,sizeof(alarm_snooze));
    alarm_edit=alarm_ringing=-1;alarm_checked=0;alarm_picking=alarm_schedule_open=false;alarm_date_field=0;
    host_alarm_blob_size=host_alarm_v1_blob_size=0;
    host_audio=(thingsos_audio_status_t){0};app.audio=host_audio;
    host_audio_deferred=host_audio_stop_reject=false;host_network_busy=false;host_sd=true;
    host_weidu.busy=false;host_ima.busy=false;
    app.record_start_pending=app.record_save_pending=false;app.record_notice=RECORD_NOTICE_NONE;
    app.wd_power_action=0;app.wd_active=false;app.controls_open=false;app.asleep=false;
    host_nvs_available=true;host_nvs_read=false;host_nvs_commit_fail=false;
    struct tm date={0};date.tm_year=126;date.tm_mon=9;date.tm_mday=26;date.tm_hour=6;date.tm_min=59;date.tm_sec=58;date.tm_isdst=-1;
    host_wall_time=mktime(&date);host_clock_us+=10000000;
    navigate(THINGSOS_UI_APPS);tap(552,420);CHECK(app.page==THINGSOS_UI_ALARM);
    snapshot("alarm-empty");tap(340,940);CHECK(alarm_edit==0&&alarm_draft.hour==7&&alarm_draft.days==127);
    tap(440,270);CHECK(alarm_draft.minute==1);tap(440,470);CHECK(!alarm_draft.minute);
    tap(220,470);CHECK(alarm_draft.hour==6);tap(220,270);CHECK(alarm_draft.hour==7);
    tap(300,680);CHECK(alarm_schedule_open);tap(300,290);CHECK(alarm_draft.days==62);
    tap(600,390);CHECK(alarm_draft.days==63);tap(600,390);CHECK(alarm_draft.days==62);
    snapshot("alarm-repeat-schedule");tap(300,1035);CHECK(!alarm_schedule_open);
    snapshot("alarm-editor");tap(220,880);CHECK(alarm_picking);
    snapshot("alarm-music-picker");tap(100,300);CHECK(!alarm_picking&&!strcmp(alarm_draft.music,"birds.wav"));
    alarm_draft.days=0;tap(480,1035);CHECK(alarm_edit==0&&!alarms[0].days);alarm_draft.days=62;
    host_nvs_commit_fail=true;tap(480,1035);CHECK(alarm_edit==0&&!alarms[0].days);
    host_nvs_commit_fail=false;tap(480,1035);CHECK(alarm_edit==-1&&alarms[0].enabled&&alarms[0].days==62);
    CHECK(host_alarm_blob_size>0);snapshot("alarm-list");
    tap(580,295);CHECK(!alarms[0].enabled);host_nvs_commit_fail=true;tap(580,295);CHECK(!alarms[0].enabled);
    host_nvs_commit_fail=false;tap(580,295);CHECK(alarms[0].enabled);
    tap(150,290);CHECK(alarm_edit==0);tap(220,270);CHECK(alarm_draft.hour==8);capacitive_key(THINGSOS_KEY_PREVIOUS);CHECK(alarm_edit==-1&&alarms[0].hour==7);
    host_nvs_read=true;memset(alarms,0,sizeof(alarms));alarm_load();CHECK(alarms[0].hour==7&&alarms[0].days==62);host_nvs_read=false;
    alarm_poll();CHECK(!alarm_active());
    app.audio_volume=35;thingsos_audio_volume(35);power_action(1);CHECK(app.asleep);
    host_wall_time+=3;host_clock_us+=3000000;alarm_poll();
    CHECK(alarm_active()&&!app.asleep&&host_audio.state==THINGSOS_AUDIO_PLAYING&&host_audio.volume==60);
    CHECK(alarms[0].last_day==20261026&&!app_media.playlist);snapshot("alarm-ringing");
    CHECK(!app_ai_record_toggle());app_controls_set(true);CHECK(!app.controls_open);
    /* Natural end loops the chosen song, never the music playlist. */
    host_audio.state=THINGSOS_AUDIO_IDLE;host_audio.ended=true;++host_audio.serial;app_poll();
    host_clock_us+=3000000;alarm_poll();CHECK(host_audio.state==THINGSOS_AUDIO_PLAYING&&strstr(host_audio.path,"birds.wav"));
    int top=(app.canvas.height-450)/2;tap(480,top+360);CHECK(!alarm_active()&&alarm_snooze[0]&&host_audio.volume==35);
    host_clock_us+=299000000;host_wall_time+=299;alarm_poll();CHECK(!alarm_active());
    host_clock_us+=1000000;host_wall_time++;alarm_poll();CHECK(alarm_active());
    power_action(1);CHECK(!alarm_active()&&!app.asleep&&host_audio.state==THINGSOS_AUDIO_IDLE);
    host_wall_time=mktime(&date)+3;alarm_checked=0;host_nvs_read=true;alarm_load();host_nvs_read=false;
    alarm_poll();CHECK(!alarm_active()); /* persisted occurrence survives reboot/clock rollback */
    /* Missing SD is visible; inserting it retries the selected music. */
    host_wall_time+=86400;host_sd=false;app.page=THINGSOS_UI_READING;app.landscape_enabled=true;
    alarm_poll();CHECK(alarm_active()&&!alarm_audio_owned&&strstr(alarm_message,"SD"));
    snapshot("alarm-landscape-missing-sd");CHECK(app.canvas.width==1216&&app.canvas.height==684);
    host_sd=true;host_clock_us+=3000000;alarm_poll();CHECK(alarm_audio_owned);alarm_finish(false);
    app.landscape_enabled=false;app.page=THINGSOS_UI_ALARM;reader_canvas_geometry();
    /* Existing recording is never discarded or stopped by the alarm. */
    host_wall_time+=86400;host_audio.state=THINGSOS_AUDIO_RECORDING;alarm_poll();
    CHECK(alarm_active()&&!alarm_audio_owned&&host_audio.state==THINGSOS_AUDIO_RECORDING);
    snapshot("alarm-wait-recording");alarm_finish(false);CHECK(host_audio.state==THINGSOS_AUDIO_RECORDING);
    host_audio.state=THINGSOS_AUDIO_IDLE;
    /* Foreground transfer defers output, and a short gap can catch the occurrence. */
    host_wall_time+=86400;app.page=THINGSOS_UI_USB;alarm_poll();CHECK(!alarm_active()&&alarm_pending[0]);
    app.page=THINGSOS_UI_ALARM;app.wd_power_action=1;alarm_poll();CHECK(!alarm_active()&&alarm_pending[0]);
    app.wd_power_action=0;alarm_poll();CHECK(alarm_active());
    host_clock_us+=180000000;alarm_poll();CHECK(!alarm_active());
    tap(100,295);CHECK(alarm_edit==0);tap(100,1035);CHECK(!alarms[0].days&&alarm_edit==-1);
    host_nvs_read=true;alarm_load();host_nvs_read=false;CHECK(!alarms[0].days);
    host_alarm_blob[0]^=1;host_nvs_read=true;alarm_load();host_nvs_read=false;CHECK(!alarms[0].days);
    /* One-shot today/tomorrow, e-ink switch geometry, snooze and automatic off. */
    memset(alarms,0,sizeof(alarms));host_alarm_blob_size=host_alarm_v1_blob_size=0;
    memset(alarm_pending,0,sizeof(alarm_pending));memset(alarm_snooze,0,sizeof(alarm_snooze));
    alarm_edit=alarm_ringing=-1;alarm_checked=0;alarm_schedule_open=false;alarm_date_field=0;
    date.tm_mday=31;date.tm_hour=6;date.tm_min=59;date.tm_sec=58;host_wall_time=mktime(&date);
    app.page=THINGSOS_UI_ALARM;app.asleep=false;host_sd=true;host_audio.state=THINGSOS_AUDIO_IDLE;
    tap(340,940);tap(160,580);CHECK(alarm_draft.mode==THINGSOS_ALARM_ONCE&&alarm_draft.days==0&&alarm_draft.start_day==20261031);
    tap(500,680);CHECK(alarm_draft.start_day==20261101&&alarm_draft.end_day==20261101);
    tap(100,680);CHECK(alarm_draft.start_day==20261031);snapshot("alarm-once-editor");
    tap(480,1035);CHECK(alarm_edit==-1&&thingsos_alarm_used(alarms)&&alarms[0].enabled);
    snapshot("alarm-switch-on");CHECK(app.canvas.pixels[300u*684+560]==0&&app.canvas.pixels[300u*684+618]==255);
    tap(580,295);CHECK(!alarms[0].enabled);snapshot("alarm-switch-off");
    CHECK(app.canvas.pixels[300u*684+560]==255&&app.canvas.pixels[300u*684+618]>128);
    CHECK(app.canvas.pixels[300u*684+530]==0&&app.canvas.pixels[270u*684+589]==0);
    tap(580,295);CHECK(alarms[0].enabled);
    alarm_poll();CHECK(!alarm_active());host_wall_time+=3;host_clock_us+=3000000;alarm_poll();
    CHECK(alarm_active()&&alarms[0].last_day==20261031);
    alarm_finish(true);CHECK(alarms[0].enabled&&alarm_snooze[0]);
    host_wall_time+=300;host_clock_us+=300000000;alarm_poll();CHECK(alarm_active());
    alarm_finish(false);CHECK(!alarms[0].enabled&&thingsos_alarm_used(alarms));
    host_nvs_read=true;alarm_load();host_nvs_read=false;CHECK(!alarms[0].enabled&&alarms[0].mode==THINGSOS_ALARM_ONCE);
    tap(580,295);CHECK(!alarms[0].enabled); /* elapsed one-shot cannot simply be switched on */
    tap(100,295);CHECK(alarm_edit==0);tap(100,680);tap(480,1035);CHECK(alarm_edit==0); /* today 07:00 has elapsed */
    tap(60,150);CHECK(alarm_edit==-1&&!alarms[0].enabled);
    /* Inclusive periodic range editor, invalid ordering, cancel and persistence. */
    tap(340,940);CHECK(alarm_edit==1);tap(300,680);CHECK(alarm_schedule_open);
    tap(450,500);CHECK(alarm_date_field==1&&alarm_date_draft==20261031);
    snapshot("alarm-range-date-picker");tap(550,340);CHECK(alarm_date_draft==20261101);
    tap(480,1035);CHECK(alarm_draft.start_day==20261101&&!alarm_date_field);
    tap(450,600);CHECK(alarm_date_field==2);tap(480,1035);
    CHECK(alarm_date_field==2&&!alarm_draft.end_day); /* end before start is rejected */
    tap(500,680);tap(480,1035);CHECK(!alarm_date_field&&alarm_draft.end_day==20261101);
    tap(500,290);CHECK(alarm_draft.days==65);tap(500,390);CHECK(alarm_draft.days==1);
    snapshot("alarm-bounded-schedule");
    tap(450,500);alarm_date_draft=20280131;alarm_date_adjust(1,1);CHECK(alarm_date_draft==20280229);
    alarm_date_adjust(0,1);CHECK(alarm_date_draft==20290228);alarm_date_adjust(2,1);CHECK(alarm_date_draft==20290301);
    alarm_date_draft=20991231;alarm_date_adjust(2,1);CHECK(alarm_date_draft==20991231);
    tap(60,150);CHECK(!alarm_date_field&&alarm_draft.start_day==20261101);
    tap(300,1035);CHECK(!alarm_schedule_open);snapshot("alarm-bounded-editor");
    tap(480,1035);CHECK(alarm_edit==-1&&alarms[1].days==1&&alarms[1].start_day==20261101&&alarms[1].end_day==20261101);
    host_nvs_read=true;alarm_load();host_nvs_read=false;CHECK(alarms[1].end_day==20261101);
    snapshot("alarm-once-and-recurring-list");
    date.tm_mon=10;date.tm_mday=1;host_wall_time=mktime(&date);alarm_checked=0;
    alarm_poll();host_wall_time+=3;host_clock_us+=3000000;alarm_poll();CHECK(alarm_ringing==1);
    alarm_finish(false);CHECK(alarms[1].enabled);
    date.tm_mday=2;date.tm_hour=0;date.tm_min=date.tm_sec=0;host_wall_time=mktime(&date);
    alarm_poll();CHECK(!alarms[1].enabled&&thingsos_alarm_used(alarms+1));
    date.tm_mon=11;date.tm_mday=31;date.tm_hour=12;host_wall_time=mktime(&date);
    CHECK(alarm_relative_day(1)==20270101);
    /* v1 reads migrate in memory; a successful write creates v2. Corrupt v2
       never resurrects a deleted/old alarm by falling back to the v1 key. */
    struct {uint32_t magic;thingsos_alarm_v1_t items[THINGSOS_ALARM_LIMIT];} old={.magic=0x414c0001};
    old.items[2]=(thingsos_alarm_v1_t){.enabled=1,.hour=8,.minute=15,.days=62,.volume=55,.last_day=20261030};
    strcpy(old.items[2].music,"birds.wav");memcpy(host_alarm_v1_blob,&old,sizeof(old));host_alarm_v1_blob_size=sizeof(old);
    host_alarm_blob_size=0;host_nvs_read=true;alarm_load();host_nvs_read=false;
    CHECK(alarms[2].mode==THINGSOS_ALARM_REPEAT&&alarms[2].days==62&&!alarms[2].start_day&&!alarms[2].end_day&&alarms[2].last_day==20261030);
    thingsos_alarm_t revised=alarms[2];revised.start_day=20270101;
    host_nvs_commit_fail=true;CHECK(!alarm_replace(2,&revised)&&!alarms[2].start_day&&!host_alarm_blob_size);
    host_nvs_commit_fail=false;CHECK(alarm_replace(2,&revised)&&host_alarm_blob_size==4+4*sizeof(thingsos_alarm_t));
    CHECK(!memcmp(host_alarm_v1_blob,&old,sizeof(old)));
    host_nvs_read=true;alarm_load();host_nvs_read=false;CHECK(alarms[2].start_day==20270101);
    host_alarm_blob[0]^=1;host_nvs_read=true;alarm_load();host_nvs_read=false;CHECK(!thingsos_alarm_used(alarms+2));
    host_alarm_blob_size=host_alarm_v1_blob_size=0;
    alarm_checked=0;host_wall_time=old_wall;host_clock_us=old_clock;
    strlcpy(host_media_root,old_root,sizeof(host_media_root));host_nvs_available=false;
    app.message[0]=0;host_audio=(thingsos_audio_status_t){0};app.audio=host_audio;
    puts("PASS: one-shot/date range UI, pill switches, v1/v2 migration, alarm lifecycle and audio/USB ownership");
}
