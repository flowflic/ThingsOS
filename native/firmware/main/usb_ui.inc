// SPDX-License-Identifier: Apache-2.0
static bool usb_draw(void) {
    if(app.page!=THINGSOS_UI_USB)return false;
    thingsos_usb_status_t status={0};thingsos_board_usb_status(&status);
    header("USB 直连模式");
    centered_label(status.active?(status.ejected?"磁盘已安全弹出":status.connected?"已连接电脑":"等待电脑连接"):"已关闭",36,280,34,612);
    label("通过 USB 数据线连接电脑，直接管理 SD 卡文件",36,365,24,612);
    setting_row("书籍", "books / EPUB、TXT",430);
    setting_row("字体", "fonts / TTF、OTF",525);
    setting_row("壁纸", "wallpapers / 图片",620);
    label(status.active?"直连期间暂停阅读和无线传书":"开启后，电脑将识别 SD 卡为可移动磁盘",36,765,24,612);
    label("复制完成后，建议先在电脑安全弹出",36,815,24,612);
    button(status.active?"关闭 USB 直连":"开启 USB 直连",865);
    if(status.error)label("SD 卡读写失败，请安全弹出后重新连接",36,1000,23,612);
    centered_label(status.active?"可在设备直接关闭 USB 直连":"返回设置",36,1100,27,612);
    return true;
}
static bool usb_tap(int x,int y) {
    if(app.page!=THINGSOS_UI_USB)return false;
    thingsos_usb_status_t status={0};thingsos_board_usb_status(&status);
    if(y>=865 && y<965) {
        if(status.active) {
            usb_end_requested=true;
        } else usb_begin_requested=true;
    } else if(!status.active && ((y>=1040) || (x<120 && y>=100 && y<195)))navigate(THINGSOS_UI_SETTINGS);
    return true;
}

#ifndef THINGSOS_HOST_UI
static RTC_NOINIT_ATTR uint32_t usb_return_marker;
#define USB_RETURN_SETTINGS 0x55534253u
/* Remain on the UI task while holding thingsos_io_lock: network requests and
   background readers cannot acquire SD ownership during the entire MSC session. */
static void usb_mode_run(void) {
    if(!thingsos_ima_quiesce_locked()){say("上传尚未结束，请稍后重试");return;}
    if(!thingsos_cubox_quiesce_locked()){say("Cubox 下载尚未结束，请稍后重试");return;}
    if(!thingsos_audio_quiesce_locked()){say("音频保存失败，请检查 SD 卡");return;}
    if(!startup_healthy){say("设备正在初始化，请稍后开启直连");return;}
    if(!media()){say("请插入可正常读取的 SD 卡");return;}
    if(thingsos_network_busy() || app.weidu.busy){say("传输处理中，请完成后再开启直连");return;}
    const char* dirs[]={"/sdcard/books","/sdcard/fonts","/sdcard/wallpapers"};
    for(unsigned i=0;i<3;++i) {
        struct stat info;
        if(mkdir(dirs[i],0775) && errno!=EEXIST){say("无法准备 SD 卡目录");return;}
        if(stat(dirs[i],&info) || !S_ISDIR(info.st_mode)){say("SD 卡目录被同名文件占用");return;}
    }
    release_book();
    if(app.records_dirty || app.progress_dirty){
        ESP_LOGW("ThingsOS_store","USB save blocked: records=%d progress=%d bound=%d blocked=%d",
            app.records_dirty,app.progress_dirty,app.records_bound,app.records_blocked);
        say(app.records_dirty?"SD 阅读记录尚未保存，请重试":"系统进度缓存尚未保存，请重试");return;
    }
    thingsos_network_service(false);
    thingsos_font_close(&app.preview_font);
    thingsos_font_close(&app.font);
    thingsos_font_close(&app.ui_font);
    if(!thingsos_board_font(&app.font)||!thingsos_board_font(&app.ui_font)){usb_return_marker=USB_RETURN_SETTINGS;esp_restart();}
    /* All SD handles are closed before official FAT unmount and raw SD init. */
    esp_err_t result=thingsos_board_sd_sync();
    if(result==ESP_OK)result=thingsos_board_usb_start();
    if(result!=ESP_OK) {
        /* A clean restart releases any partially initialized raw driver and
           restores the normal console, mount, fonts and service configuration. */
        usb_return_marker=USB_RETURN_SETTINGS;esp_restart();
    }
    thingsos_board_touch_buffer(false);
    bool pressed=false;int initial_x=0,initial_y=0;
    thingsos_usb_status_t previous={0};
    app.dirty=true;app.message[0]=0;usb_end_requested=false;
    for(;;) {
        thingsos_usb_status_t status;thingsos_board_usb_status(&status);
        if(status.connected!=previous.connected || status.ejected!=previous.ejected || status.error!=previous.error)app.dirty=true;
        if(status.ejected && !previous.ejected)app.message[0]=0;
        previous=status;
        thingsos_touch_t touch={0};uint64_t sampled=now();
        if(thingsos_board_touch(&touch,&sampled)) {
            if(touch.touched && !pressed){pressed=true;initial_x=touch.x;initial_y=touch.y;}
            else if(!touch.touched && pressed){pressed=false;usb_tap(initial_x,initial_y);}
        }
        if(thingsos_board_power_key()) {
            say("请点击关闭 USB 直连退出");
        }
        if(usb_end_requested) {
            usb_end_requested=false;
            say("正在关闭直连，重新加载书库…");
            draw();thingsos_board_present(&app.canvas);
            esp_err_t stopped=thingsos_board_usb_stop();
            if(stopped==ESP_OK || stopped==ESP_ERR_TIMEOUT) {
                usb_return_marker=USB_RETURN_SETTINGS;
                esp_restart();
            } else say("关闭失败，请重新连接 USB 后重试");
        }
        if(app.dirty) {
            draw();thingsos_board_present(&app.canvas);app.dirty=false;
        }
        thingsos_board_idle();vTaskDelay(pdMS_TO_TICKS(20));
    }
}
#else
static void usb_mode_run(void){} /* Hardware loop is validated on the device. */
#endif
