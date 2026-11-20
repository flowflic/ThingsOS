// SPDX-License-Identifier: Apache-2.0
static unsigned upgrade_notes_page,upgrade_notes_pages=1;
static void upgrade_poll(void) {
    if(app.page!=THINGSOS_UI_UPGRADE)return;
    static unsigned serial;
    thingsos_release_state_t release;thingsos_network_release_state(&release);
    if(serial!=release.serial){serial=release.serial;app.dirty=true;}
}
/* Fixed readable lines, paged without truncating the release notes. UTF-8
   characters stay intact; ASCII occupies half a CJK cell. */
static void upgrade_notes(const char* notes) {
    unsigned row=0,units=0;size_t n=0;char line[192];
    const unsigned char* p=(const unsigned char*)notes;
    for(;;) {
        unsigned bytes=*p<128?1:(*p&0xe0)==0xc0?2:(*p&0xf0)==0xe0?3:4;
        unsigned width=*p<128?1:2;
        if(!*p || *p=='\n' || units+width>44 || n+bytes>=sizeof(line)) {
            line[n]=0;
            if(row/6==upgrade_notes_page)label(line,48,526+(int)(row%6)*29,24,588);
            ++row;n=units=0;
            if(!*p)break;
            if(*p=='\n'){++p;continue;}
        }
        for(unsigned i=0;i<bytes && *p;++i)line[n++]=(char)*p++;
        units+=width;
    }
    upgrade_notes_pages=(row+5)/6;
    if(upgrade_notes_pages>1) {
        char text[64];snprintf(text,sizeof(text),"上一页     %u / %u     下一页",upgrade_notes_page+1,upgrade_notes_pages);
        centered_label(text,36,718,23,612);
    }
}
static void upgrade_draw(void) {
    thingsos_release_state_t release;thingsos_network_release_state(&release);
    char text[96];header("固件升级");
    snprintf(text,sizeof(text),"当前版本 %s",esp_app_get_description()->version);
    label(text,36,248,28,612);label("thingsos.scholarcn.com",36,286,23,612);
    network_button(release.active?"检查中…":"检查新固件",36,315,298,false);
    network_button("WiFi 设置",350,315,298,false);
    label(release.message[0]?release.message:"连接可上网的 WiFi 后检查更新",36,432,25,612);
    if(release.checked) {
        snprintf(text,sizeof(text),"官网版本 %s%s",release.release.version,release.available?" · 可更新":"");
        label(text,36,480,27,612);
        upgrade_notes(release.release.notes);
    }
    if(release.available)network_button("下载并安装",36,745,612,false);
    else label("校验固件后升级，启动异常自动回退",36,790,24,612);
    network_button("浏览器 OTA",36,835,298,false);
    network_button("USB 线刷",350,835,298,false);
    network_button("回退固件",36,925,298,false);
    network_button("恢复与救援",350,925,298,false);
    network_button("返回设置",36,1010,612,false);
    nav();
}
static bool upgrade_tap(int x,int y) {
    thingsos_release_state_t release;thingsos_network_release_state(&release);
    if(y>=1010)navigate(THINGSOS_UI_SETTINGS);
    else if(y>=315 && y<383) {
        if(x>=342)navigate(THINGSOS_UI_WIFI);
        else {
            upgrade_notes_page=0;
            if(thingsos_network_release_start(false)!=ESP_OK)say("请连接可上网的 WiFi，或等待当前任务结束");
            else app.message[0]=0;
        }
    } else if(y>=680 && y<735 && release.checked && upgrade_notes_pages>1) {
        if(x<342 && upgrade_notes_page) --upgrade_notes_page;
        if(x>=342 && upgrade_notes_page+1<upgrade_notes_pages)++upgrade_notes_page;
    } else if(y>=745 && y<813 && release.available)app.confirm_action=8;
    else if(y>=835 && y<903) {
        if(x>=342)app.confirm_action=4;
        else {thingsos_network_start(false);app.service_on=true;thingsos_network_service(true);navigate(THINGSOS_UI_TRANSFER);}
    } else if(y>=925 && y<993) {
        if(x<342)app.confirm_action=6;else navigate(THINGSOS_UI_RECOVERY);
    }
    app.dirty=true;return true;
}
