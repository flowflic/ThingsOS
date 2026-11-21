// SPDX-License-Identifier: Apache-2.0
/* Real controller: touch hit boxes, NVS transactions, boot/wake and key guards. */
static void pin_type_test(const char* digits) {
    for(const char* p=digits;*p;++p) {
        unsigned digit=(unsigned)(*p-'0');CHECK(digit<10);
        unsigned row=digit?(digit-1)/3:3,column=digit?(digit-1)%3:1;
        tap(138+(int)column*204,602+(int)row*128);
    }
}
static void pin_submit_test(void){tap(342,1128);}
static void pin_background_priority_test(void) {
    thingsos_touch_queue_t queue={0};thingsos_touch_sample_t sample;
    app.ui_action_at=0;app.force_refresh=app.transition_refresh=app.lock_wave_pending=false;
    app.dirty=true;CHECK(app.page==THINGSOS_UI_POWER);
    CHECK(!ui_background_frame_yields(queue.count!=0));
    draw(); /* Background layout began before the captured contact. */
    /* Actual field ordering: DOWN 361905, UP 361936, layout ends 361953.
       Physical contact has ended, but the complete gesture still awaits UI. */
    thingsos_touch_queue_push(&queue,(thingsos_touch_sample_t){361905,631,638,1,true});
    thingsos_touch_queue_push(&queue,(thingsos_touch_sample_t){361936,0,0,0,false});
    CHECK(!queue.latest.down&&queue.count==2);
    app.force_refresh=app.transition_refresh=true;
    CHECK(ui_background_frame_yields(queue.count!=0));
    CHECK(app.dirty&&app.force_refresh&&app.transition_refresh&&queue.count==2);
    app.force_refresh=app.transition_refresh=false;
    CHECK(thingsos_touch_queue_pop(&queue,&sample)&&sample.down);
    CHECK(power_pin_press(sample.x,sample.y));
    CHECK(app.page==THINGSOS_UI_LOCK_PIN&&!app.pin.length);
    /* The accepted contact paints immediately even with its UP still queued;
       waiting for an empty queue here would regress contact activation. */
    CHECK(!ui_background_frame_yields(queue.count!=0));draw();
    CHECK(thingsos_touch_queue_pop(&queue,&sample)&&!sample.down);
    CHECK(!app.pin.length&&!power_pin_press(631,638));
    pin_cancel();app.ui_action_at=0;
    CHECK(app.page==THINGSOS_UI_POWER&&!ui_background_frame_yields(queue.count!=0));
}
static void pin_input_pixels_test(void) {
    unsigned char* previous=malloc(READER_CACHE_BYTES);CHECK(previous);
    memcpy(previous,app.canvas.pixels,READER_CACHE_BYTES);
    CHECK(app.input_refresh&&pin_input_frame());draw();
    CHECK(memcmp(previous,app.canvas.pixels,READER_CACHE_BYTES));
    for(int y=0;y<1216;++y)if(!(y>=320&&y<478)&&!(y>=1090&&y<1166))
        CHECK(!memcmp(previous+(size_t)y*684,app.canvas.pixels+(size_t)y*684,684));
    /* Narrow repaint must produce the same pixels as a fresh complete frame. */
    memcpy(previous,app.canvas.pixels,READER_CACHE_BYTES);
    app.input_refresh=false;draw();CHECK(!memcmp(previous,app.canvas.pixels,READER_CACHE_BYTES));
    app.dirty=false;free(previous);
}
static void pin_input_refresh_test(void) {
    draw();app.force_refresh=app.transition_refresh=app.lock_wave_pending=false;
    app.input_refresh=app.keyboard_refresh=app.dirty=false;
    pin_type_test("1");
    app.force_refresh=true;CHECK(!pin_input_frame());app.force_refresh=false;
    app.transition_refresh=true;CHECK(!pin_input_frame());app.transition_refresh=false;
    app.lock_wave_pending=true;CHECK(!pin_input_frame());app.lock_wave_pending=false;
    pin_input_pixels_test();
    for(unsigned i=0;i<5;++i){pin_type_test("2");pin_input_pixels_test();}
    CHECK(app.pin.length==6);
    pin_type_test("3");CHECK(!app.dirty&&!app.input_refresh&&app.pin.length==6);
    tap(546,986);CHECK(app.pin.length==5);pin_input_pixels_test();
    pin_error("密码错误，请重新输入");CHECK(!app.pin.length);pin_input_pixels_test();
    pin_type_test("123456");pin_submit_test();
    CHECK(app.pin.stage==PIN_CONFIRM&&!app.input_refresh&&!pin_input_frame());
    pin_cancel();pin_open(PIN_SETUP);CHECK(!app.input_refresh&&!app.pin.length);
}
static void lock_pin_controller_test(void) {
    release_book();app.wd_active=false;app.asleep=false;app.wd_power_action=0;
    app.controls_open=false;app.record_notice=RECORD_NOTICE_NONE;
    host_audio=(thingsos_audio_status_t){0};app.audio=host_audio;
    host_nvs_available=host_nvs_read=true;host_nvs_commit_fail=host_pin_set_fail=false;
    host_pin_blob_size=host_pin_pending_size=0;pin_boot();
    CHECK(!app.pin.enabled&&!app.pin.required&&!app.pin.fault);
    /* Independent PBKDF2-SHA256 known value (Node crypto / OpenSSL). */
    uint8_t salt[16],digest[32];char hex[65];
    for(unsigned i=0;i<16;++i)salt[i]=(uint8_t)i;
    CHECK(pin_derive("123456",salt,digest));
    for(unsigned i=0;i<32;++i)snprintf(hex+i*2,3,"%02x",digest[i]);
    CHECK(!strcmp(hex,"a9660861d611d46a191971eccf0cc895ee7cd58091c1973ee6d60a5c4f304219"));
    CHECK(!pin_derive("12345",salt,digest)&&!pin_derive("12345x",salt,digest));
    navigate(THINGSOS_UI_POWER);snapshot("pin-settings-off");
    pin_background_priority_test();
    /* Wake -> settings toggle must enter on the first contact, before release. */
    power_action(1);CHECK(app.asleep);power_action(1);CHECK(!app.asleep&&app.page==THINGSOS_UI_POWER);
    CHECK(!power_pin_press(35,600)&&!power_pin_press(648,600));
    CHECK(!power_pin_press(588,550)&&!power_pin_press(588,685));
    app.confirm_action=1;CHECK(!power_pin_press(588,600));app.confirm_action=0;
    CHECK(power_pin_press(588,600));
    CHECK(app.page==THINGSOS_UI_LOCK_PIN&&app.pin.stage==PIN_NEW&&host_private_input&&!app.pin.length);
    CHECK(!power_pin_press(588,600)&&!app.pin.length);
    pin_input_refresh_test();
    pin_type_test("12");CHECK(app.input_refresh);
    tap(138,986);CHECK(app.page==THINGSOS_UI_POWER&&!app.pin.enabled&&!app.pin.entry[0]&&!app.pin.draft[0]);
    tap(588,600);pin_type_test("123456");pin_submit_test();CHECK(app.pin.stage==PIN_CONFIRM);
    power_action(1);CHECK(app.asleep&&!app.pin.draft[0]);power_action(1);
    CHECK(!app.asleep&&app.page==THINGSOS_UI_POWER&&!app.pin.enabled&&!app.pin.required);
    tap(588,600);pin_type_test("12345");pin_submit_test();CHECK(app.pin.stage==PIN_NEW);
    pin_type_test("67");CHECK(app.pin.length==6&&!strcmp(app.pin.entry,"123456"));
    tap(546,986);CHECK(app.pin.length==5);pin_type_test("6");pin_submit_test();
    CHECK(app.pin.stage==PIN_CONFIRM&&!app.pin.entry[0]&&!app.pin.enabled&&!host_pin_blob_size);
    snapshot("pin-confirm");pin_type_test("000000");pin_submit_test();
    CHECK(app.pin.stage==PIN_CONFIRM&&!app.pin.length&&strstr(app.pin.notice,"不一致")&&!app.pin.enabled);
    pin_type_test("123456");host_pin_random_fail=true;pin_submit_test();host_pin_random_fail=false;
    CHECK(!app.pin.enabled&&!host_pin_blob_size&&strstr(app.pin.notice,"保存失败"));
    pin_type_test("123456");host_nvs_commit_fail=true;pin_submit_test();host_nvs_commit_fail=false;
    CHECK(!app.pin.enabled&&!host_pin_blob_size&&!host_pin_pending_size);
    pin_type_test("123456");pin_submit_test();
    CHECK(app.pin.enabled&&!app.pin.required&&app.page==THINGSOS_UI_POWER&&!host_private_input);
    CHECK(host_pin_blob_size==sizeof(lock_pin_record_t)&&!app.pin.entry[0]&&!app.pin.draft[0]);
    lock_pin_record_t initial=app.pin.record;
    CHECK(pin_record_valid(&initial)&&initial.enabled);
    /* No plaintext survives anywhere in the stored record. */
    for(size_t i=0;i+6<=host_pin_blob_size;++i)CHECK(memcmp(host_pin_blob+i,"123456",6));
    snapshot("pin-settings-on");
    CHECK(power_pin_press(48,607)&&app.pin.purpose==PIN_DISABLE&&app.pin.stage==PIN_VERIFY);
    pin_cancel();CHECK(app.page==THINGSOS_UI_POWER&&app.pin.enabled);
    tap(138,819);CHECK(app.asleep&&app.pin.required);power_action(1);
    CHECK(!app.asleep&&app.page==THINGSOS_UI_LOCK_PIN&&app.pin.purpose==PIN_UNLOCK);
    CHECK(app.lock_wave_pending&&!app.lock_wave_forward);
    snapshot("pin-unlock");CHECK(!region_ink(1175,1216));
    CHECK(!app_controls_swipe(100,20,0,200));app_controls_set(true);CHECK(!app.controls_open);
    CHECK(!app_ai_record_toggle());
    capacitive_key(THINGSOS_KEY_HOME);CHECK(app.pin.required&&app.page==THINGSOS_UI_LOCK_PIN);
    tap(48,150);tap(588,1180);CHECK(app.pin.required&&app.page==THINGSOS_UI_LOCK_PIN);
    navigate(THINGSOS_UI_FILES);CHECK(app.page==THINGSOS_UI_LOCK_PIN);
    pin_type_test("000000");pin_submit_test();CHECK(app.pin.required&&!app.pin.length&&strstr(app.pin.notice,"错误"));
    snapshot("pin-wrong");
    pin_type_test("123456");host_pin_crypto_fail=true;pin_submit_test();host_pin_crypto_fail=false;
    CHECK(app.pin.required&&!app.pin.length);
    pin_type_test("12");tap(138,986);CHECK(app.asleep&&!app.pin.entry[0]);power_action(1);
    pin_type_test("123456");pin_submit_test();
    CHECK(!app.pin.required&&app.page==THINGSOS_UI_LIBRARY&&app.lock_wave_pending&&!app.lock_wave_forward);
    /* Main boot ignores the USB-return marker when password protection is on. */
    app.page=THINGSOS_UI_SETTINGS;pin_boot();
    CHECK(app.pin.required&&app.page==THINGSOS_UI_LOCK_PIN);
    /* Alarm stop does not authenticate or restore an underlying home page. */
    alarm_ringing=0;capacitive_key(THINGSOS_KEY_HOME);
    CHECK(!alarm_active()&&app.pin.required&&app.page==THINGSOS_UI_LOCK_PIN);
    app.page=THINGSOS_UI_LIBRARY;draw();CHECK(app.page==THINGSOS_UI_LOCK_PIN);
    pin_type_test("123456");pin_submit_test();CHECK(!app.pin.required);
    navigate(THINGSOS_UI_POWER);tap(138,737);CHECK(app.pin.purpose==PIN_CHANGE&&app.pin.stage==PIN_VERIFY);
    pin_type_test("123456");pin_submit_test();CHECK(app.pin.verified&&app.pin.stage==PIN_NEW);
    pin_type_test("654321");pin_submit_test();pin_type_test("654321");
    host_pin_set_fail=true;pin_submit_test();host_pin_set_fail=false;
    CHECK(!memcmp(&app.pin.record,&initial,sizeof(initial))&&app.pin.enabled);
    pin_type_test("654321");pin_submit_test();CHECK(app.page==THINGSOS_UI_POWER&&app.pin.enabled);
    CHECK(memcmp(initial.salt,app.pin.record.salt,16)&&memcmp(initial.verifier,app.pin.record.verifier,32));
    tap(588,600);CHECK(app.pin.purpose==PIN_DISABLE&&app.pin.stage==PIN_VERIFY);
    pin_type_test("123456");pin_submit_test();CHECK(app.pin.enabled&&app.pin.stage==PIN_VERIFY&&strstr(app.pin.notice,"错误"));
    pin_type_test("654321");host_nvs_commit_fail=true;pin_submit_test();host_nvs_commit_fail=false;
    CHECK(app.pin.enabled&&app.page==THINGSOS_UI_LOCK_PIN&&strstr(app.pin.notice,"仍开启"));
    pin_type_test("654321");pin_submit_test();CHECK(!app.pin.enabled&&app.page==THINGSOS_UI_POWER);
    /* Disabling must not leave PIN_DISABLE/verification state owning the next
       contact. Re-enable immediately, and repeat after cancelling the editor. */
    for(unsigned retry=0;retry<3;++retry) {
        const int contacts[][2]={{571,600},{48,607},{588,670}};
        CHECK(power_pin_press(contacts[retry][0],contacts[retry][1]));
        CHECK(app.page==THINGSOS_UI_LOCK_PIN&&app.pin.purpose==PIN_SETUP&&app.pin.stage==PIN_NEW);
        CHECK(!app.pin.required&&!app.pin.verified&&!app.pin.length&&!app.pin.draft[0]);
        pin_cancel();CHECK(app.page==THINGSOS_UI_POWER&&!app.pin.enabled);
    }
    tap(48,607);CHECK(app.page==THINGSOS_UI_LOCK_PIN&&app.pin.purpose==PIN_SETUP);
    pin_cancel();
    pin_boot();CHECK(!app.pin.enabled&&!app.pin.required&&!app.pin.fault);
    tap(138,737);CHECK(app.asleep&&!app.pin.required);power_action(1);CHECK(app.page==THINGSOS_UI_POWER&&!app.pin.required);
    /* Enabled state and verifier are indivisible; malformed records fail closed. */
    memcpy(host_pin_blob,&initial,sizeof(initial));host_pin_blob_size=sizeof(initial)-1;
    pin_boot();CHECK(app.pin.fault&&app.pin.required&&app.page==THINGSOS_UI_LOCK_PIN);
    capacitive_key(THINGSOS_KEY_HOME);navigate(THINGSOS_UI_LIBRARY);CHECK(app.page==THINGSOS_UI_LOCK_PIN&&app.pin.required);
    pin_type_test("123456");pin_submit_test();CHECK(app.pin.required);
    lock_pin_record_t invalid=initial;invalid.iterations=0;
    host_pin_blob_size=sizeof(invalid);memcpy(host_pin_blob,&invalid,sizeof(invalid));
    pin_boot();CHECK(app.pin.fault&&app.pin.required);
    host_pin_blob_size=0;pin_boot();CHECK(!app.pin.required&&!app.pin.enabled);
    host_nvs_available=false;pin_boot();CHECK(app.pin.fault&&app.pin.required);
    host_nvs_available=true;pin_boot();CHECK(!app.pin.required&&!app.pin.fault);
    pin_wipe(&initial,sizeof(initial));host_nvs_read=host_nvs_available=false;
    navigate(THINGSOS_UI_LIBRARY);app.lock_wave_pending=false;
}
