// SPDX-License-Identifier: Apache-2.0
/* Native device controllers for the approved v3 pages. User overrides take precedence. */

#include "upgrade_ui.inc"

typedef struct {
    uint64_t card_id;
    uint32_t count;
    struct {
        uint64_t id;
        uint32_t page, pages, percent;
    } books[THINGSOS_RECORD_BOOKS];
} progress_cache_t;
static void progress_save(void) {
    nvs_handle_t h;
    if (nvs_open("thingsos-ui", NVS_READWRITE, &h) != ESP_OK) return;
    progress_cache_t* cache = calloc(1, sizeof(*cache));
    if (cache) {
        cache->card_id = app.records.card_id;
        cache->count = (uint32_t)app.records.book_count;
        for (size_t i = 0; i < app.records.book_count; ++i) {
            thingsos_book_record_t* b = &app.records.books[i];
            cache->books[i].id = b->id;
            cache->books[i].page = b->page;
            cache->books[i].pages = b->pages;
            cache->books[i].percent = b->percent;
        }
        if (nvs_set_blob(h, "progress-v1", cache, sizeof(*cache)) == ESP_OK &&
            nvs_commit(h) == ESP_OK)
            app.progress_dirty = false;
        free(cache);
    }
    nvs_close(h);
}
static void progress_load(void) {
    nvs_handle_t h;
    if (nvs_open("thingsos-ui", NVS_READONLY, &h) != ESP_OK) return;
    progress_cache_t* cache = malloc(sizeof(*cache));
    size_t bytes = sizeof(*cache);
    if (cache && nvs_get_blob(h, "progress-v1", cache, &bytes) == ESP_OK &&
        bytes == sizeof(*cache) && cache->card_id == app.records.card_id &&
        cache->count <= THINGSOS_RECORD_BOOKS) {
        for (size_t i = 0; i < cache->count; ++i) {
            thingsos_book_record_t* b =
                thingsos_records_book(&app.records, cache->books[i].id, NULL, NULL);
            if (b && cache->books[i].percent <= 100 &&
                cache->books[i].page <= cache->books[i].pages) {
                b->page = cache->books[i].page;
                b->pages = cache->books[i].pages;
                b->percent = cache->books[i].percent;
            }
        }
    }
    free(cache);
    nvs_close(h);
}
static void preferences_snapshot(preferences_snapshot_t* p) {
    memset(p, 0, sizeof(*p));
    p->wd_pull=app.wd_pull_on;p->wd_push=app.wd_push_on;p->wd_time=app.wd_time_on;p->wd_confirmed=app.wd_time_confirmed;
    p->wd_rt=app.wd_rt_on;
    p->px=app.px; p->line=app.line_choice; p->margin=app.margin_choice;
    p->weight=app.weight_choice;p->ripple=app.ripple_enabled;p->tilt=app.tilt_enabled;p->tilt_sensitivity=app.tilt_sensitivity;p->landscape=app.landscape_enabled;
    p->indent=app.indent_chars;
    p->refresh=app.refresh_pages; p->mode=app.refresh_mode; p->lock=app.auto_lock_minutes;
    p->auto_time=app.auto_time; p->book_sort=app.library_name_sort; p->book_view=app.library_cards;
    p->weidu_view=app.weidu_cards;
    p->note_manual=app.annotation_manual;p->immersive=app.immersive_enabled;
    p->ble_enabled=app.ble_enabled;
    p->vertical=app.vertical_enabled;
    p->wallpaper=app.lock_wallpaper;p->ui_px=app.ui_px;p->ui_weight=app.ui_weight;p->ui_refresh=app.ui_refresh_pages;p->ui_mode=app.ui_refresh_mode;
    strlcpy(p->font, app.font_path, sizeof(p->font));
    strlcpy(p->ui_font, app.ui_font_path, sizeof(p->ui_font));
    strlcpy(p->wallpaper_book,app.wallpaper_book.path,sizeof(p->wallpaper_book));
    strlcpy(p->wallpaper_image,app.wallpaper_image,sizeof(p->wallpaper_image));
    strlcpy(p->wallpaper_title,app.wallpaper_book.name,sizeof(p->wallpaper_title));
}
static bool preferences_save(void) {
    preferences_snapshot(&app.scratch_preferences);
    if (app.saved_preferences_valid && !memcmp(&app.scratch_preferences, &app.saved_preferences, sizeof(app.scratch_preferences))) return true;
    uint64_t started = now();
    nvs_handle_t h;
    if (nvs_open("thingsos-ui", NVS_READWRITE, &h) != ESP_OK) return false;
    bool ok = nvs_set_u8(h,"wd-pull",app.wd_pull_on)==ESP_OK &&
        nvs_set_u8(h,"wd-push",app.wd_push_on)==ESP_OK &&
        nvs_set_u8(h,"wd-time",app.wd_time_on)==ESP_OK &&
        nvs_set_u8(h,"wd-consent",app.wd_time_confirmed)==ESP_OK &&
        nvs_set_u8(h,"wd-offline",app.wd_rt_on)==ESP_OK &&
        nvs_set_u32(h, "px", app.px) == ESP_OK &&
        nvs_set_u8(h,"px-default",1)==ESP_OK &&
        nvs_set_u32(h, "ui-px", app.ui_px) == ESP_OK &&
        nvs_set_u32(h, "wallpaper", app.lock_wallpaper) == ESP_OK &&
        nvs_set_str(h,"wall-book",app.wallpaper_book.path)==ESP_OK &&
        nvs_set_str(h,"wall-title",app.wallpaper_book.name)==ESP_OK &&
        nvs_set_str(h,"wall-image",app.wallpaper_image)==ESP_OK &&
        nvs_set_u32(h, "ui-weight", app.ui_weight) == ESP_OK &&
        nvs_set_u32(h, "ui-refresh", app.ui_refresh_pages) == ESP_OK &&
        nvs_set_u32(h, "ui-mode", app.ui_refresh_mode) == ESP_OK &&
        nvs_set_u32(h, "line", app.line_choice) == ESP_OK &&
        nvs_set_u32(h, "margin", app.margin_choice) == ESP_OK &&
        nvs_set_u32(h, "weight", app.weight_choice) == ESP_OK &&
        nvs_set_u32(h, "indent", app.indent_chars) == ESP_OK &&
        nvs_set_u8(h, "ripple", app.ripple_enabled) == ESP_OK &&
        nvs_set_u8(h, "tilt", app.tilt_enabled) == ESP_OK &&
        nvs_set_u8(h,"tilt-sens",app.tilt_sensitivity)==ESP_OK &&
        nvs_set_u8(h,"landscape",app.landscape_enabled)==ESP_OK &&
        nvs_set_u8(h,"immersive",app.immersive_enabled)==ESP_OK &&
        nvs_set_u8(h,"note-manual",app.annotation_manual)==ESP_OK &&
        nvs_set_u8(h,"ble",app.ble_enabled)==ESP_OK &&
        nvs_set_u8(h,"vertical",app.vertical_enabled)==ESP_OK &&
        nvs_set_u32(h, "refresh", app.refresh_pages) == ESP_OK &&
        nvs_set_u32(h, "mode", app.refresh_mode) == ESP_OK &&
        nvs_set_u32(h, "lock", app.auto_lock_minutes) == ESP_OK &&
        nvs_set_u8(h, "auto-time", app.auto_time) == ESP_OK &&
        nvs_set_u8(h, "book-sort-icon", app.library_name_sort) == ESP_OK &&
        nvs_set_u8(h, "book-view", app.library_cards) == ESP_OK &&
        nvs_set_u8(h, "weidu-view", app.weidu_cards) == ESP_OK &&
        nvs_set_str(h, "font", app.font_path) == ESP_OK &&
        nvs_set_str(h, "ui-font", app.ui_font_path) == ESP_OK && nvs_commit(h) == ESP_OK;
    nvs_close(h);
    if (ok) { app.saved_preferences=app.scratch_preferences; app.saved_preferences_valid=true; }
    ESP_LOGI("ThingsOS_perf", "Preferences save: %u ms success=%d", (unsigned)(now()-started), ok);
    return ok;
}
static void preferences_load(void) {
    app.wd_rt_on=app.wd_time_on;
    app.tilt_sensitivity=1;
    app.landscape_enabled=false;app.immersive_enabled=false;
    app.tilt_enabled=false; /* Missing or invalid keys default off. */
    /* Never erase existing NVS to recover an initialization failure. */
    if (nvs_flash_init() != ESP_OK) return;
    nvs_handle_t h;
    if (nvs_open("thingsos-ui", NVS_READONLY, &h) != ESP_OK) return;
    uint32_t v;
    uint8_t b;
    if(nvs_get_u8(h,"wd-pull",&b)==ESP_OK && b<2)app.wd_pull_on=b;
    if(nvs_get_u8(h,"wd-push",&b)==ESP_OK && b<2)app.wd_push_on=b;
    if(nvs_get_u8(h,"wd-time",&b)==ESP_OK && b<2)app.wd_time_on=b;
    if(nvs_get_u8(h,"wd-consent",&b)==ESP_OK && b<2)app.wd_time_confirmed=b;
    app.wd_rt_on=app.wd_time_on;
    bool size_migration=nvs_get_u8(h,"px-default",&b)!=ESP_OK || b!=1;
    if(nvs_get_u32(h,"wallpaper",&v)==ESP_OK && v<3)app.lock_wallpaper=v;
    char path[THINGSOS_RECORD_PATH_BYTES]={0},title[192]={0};
    size_t length=sizeof(path);
    if(nvs_get_str(h,"wall-book",path,&length)==ESP_OK && thingsos_book_path(path)) {
        length=sizeof(title);nvs_get_str(h,"wall-title",title,&length);
        wallpaper_book_set(&app.wallpaper_book,path,title);
    }
    length=sizeof(app.wallpaper_image);
    if(nvs_get_str(h,"wall-image",app.wallpaper_image,&length)!=ESP_OK ||
       !wallpaper_image_path(app.wallpaper_image))app.wallpaper_image[0]=0;
    /* Old 'recent cover' has no explicit source. Migrate to statistics. */
    if((app.lock_wallpaper==1 && !app.wallpaper_book.path[0]) ||
       (app.lock_wallpaper==2 && !app.wallpaper_image[0]))app.lock_wallpaper=0;
    if (nvs_get_u32(h,"ui-px",&v)==ESP_OK && v>=26 && v<=50 && v%2==0) app.ui_px=v;
    if (nvs_get_u32(h,"ui-weight",&v)==ESP_OK && v<3) app.ui_weight=v;
    app.ui_refresh_pages=15;app.refresh_pages=15;
    if (nvs_get_u32(h,"ui-refresh",&v)==ESP_OK && (v==5 || v==10 || v==15 || v==20)) app.ui_refresh_pages=v;
    if (nvs_get_u32(h,"ui-mode",&v)==ESP_OK && v<3) app.ui_refresh_mode=v;
    if (nvs_get_u32(h, "px", &v) == ESP_OK && v >= 30 && v <= 70 && (v - 30) % 4 == 0) app.px = v;
    if(size_migration && app.px==THINGSOS_READER_SIZE_MIN)app.px=THINGSOS_READER_SIZE_DEFAULT;
    if (nvs_get_u32(h, "line", &v) == ESP_OK && v < 3) app.line_choice = v;
    if (nvs_get_u32(h, "margin", &v) == ESP_OK && v < 3) app.margin_choice = v;
    if (nvs_get_u32(h, "weight", &v) == ESP_OK && v < 3) app.weight_choice = v;
    if (nvs_get_u32(h, "indent", &v) == ESP_OK && v <= THINGSOS_READER_INDENT_CHARS)
        app.indent_chars = v;
    if(nvs_get_u8(h,"tilt-sens",&b)==ESP_OK && b<3)app.tilt_sensitivity=b;
    app.annotation_manual=false;
    if(nvs_get_u8(h,"note-manual",&b)==ESP_OK && b<2)app.annotation_manual=b;
    if(nvs_get_u8(h,"ble",&b)==ESP_OK && b<2)app.ble_reconnect_due=app.ble_enabled=b;
    if(nvs_get_u8(h,"vertical",&b)==ESP_OK && b<2)app.vertical_enabled=b;
    if(nvs_get_u8(h,"landscape",&b)==ESP_OK && b<=1)app.landscape_enabled=b;
    if(nvs_get_u8(h,"immersive",&b)==ESP_OK && b<=1)app.immersive_enabled=b;
    if (nvs_get_u8(h, "tilt", &b)==ESP_OK && b<=1) app.tilt_enabled=b;
    if (nvs_get_u8(h, "ripple", &b) == ESP_OK && b <= 1) app.ripple_enabled = b;
    if (nvs_get_u32(h, "refresh", &v) == ESP_OK && (v == 5 || v == 10 || v == 15 || v == 20))
        app.refresh_pages = v;
    if (nvs_get_u32(h, "mode", &v) == ESP_OK && v < 3) app.refresh_mode = v;
    if (nvs_get_u32(h, "lock", &v) == ESP_OK && (v == 1 || v == 5 || v == 15))
        app.auto_lock_minutes = v;
    if (nvs_get_u8(h, "auto-time", &b) == ESP_OK && b <= 1) app.auto_time = b;
    if (nvs_get_u8(h, "book-sort-icon", &b) == ESP_OK && b <= 1) app.library_name_sort = b;
    if (nvs_get_u8(h, "book-view", &b) == ESP_OK && b <= 1) app.library_cards = b;
    app.weidu_cards=app.library_cards;
    if (nvs_get_u8(h, "weidu-view", &b) == ESP_OK && b <= 1) app.weidu_cards=b;
    size_t bytes = sizeof(app.font_path);
    if (nvs_get_str(h, "font", app.font_path, &bytes) == ESP_OK && media() &&
        thingsos_font_allowed(app.font_path))
        if (!thingsos_font_file(&app.font, app.font_path)) app.font_path[0] = 0;
    bytes=sizeof(app.ui_font_path);
    if(nvs_get_str(h,"ui-font",app.ui_font_path,&bytes)==ESP_OK && app.ui_font_path[0] &&
       (!media() || !thingsos_font_allowed(app.ui_font_path) ||
        !thingsos_font_file(&app.ui_font,app.ui_font_path)))app.ui_font_path[0]=0;
    nvs_close(h);
    preferences_snapshot(&app.saved_preferences);
    app.saved_preferences_valid = true;
    if(size_migration) {app.saved_preferences_valid=false;preferences_save();}
    progress_load();
    thingsos_network_auto_time(app.auto_time);
}
static void enter_files(void) {
    memset(&app.browser, 0, sizeof(app.browser));
    if (media()) thingsos_browser_scan(&app.browser, "/sdcard", "/sdcard");
    navigate(THINGSOS_UI_FILES);
}
static size_t mark_index(size_t row) {
    size_t indices[THINGSOS_BOOKMARK_LIMIT], count = 0;
    for (size_t i = 0; i < app.runtime.bookmark_count; ++i)
        if (!app.opened || app.runtime.bookmarks[i].book_id == app.runtime.location.book_id)
            indices[count++] = i;
    for (size_t i = 0; i < count; ++i)
        for (size_t j = i + 1; j < count; ++j) {
            thingsos_location_t a = app.runtime.bookmarks[indices[i]],
                                b = app.runtime.bookmarks[indices[j]];
            if (b.chapter < a.chapter ||
                (b.chapter == a.chapter && b.byte_offset < a.byte_offset)) {
                size_t t = indices[i];
                indices[i] = indices[j];
                indices[j] = t;
            }
        }
    return row < count ? indices[row] : SIZE_MAX;
}
static void qr_address_box(const char* address,int box_top,int edge) {
    uint8_t buffer[qrcodegen_BUFFER_LEN_FOR_VERSION(10)], qr[qrcodegen_BUFFER_LEN_FOR_VERSION(10)];
    if (!qrcodegen_encodeText(
            address, buffer, qr, qrcodegen_Ecc_MEDIUM, 1, 10, qrcodegen_Mask_AUTO, true))
        return;
    int size=qrcodegen_getSize(qr),scale=edge/(size+8);
    int left=342-(size+8)*scale/2,top=box_top+(edge-(size+8)*scale)/2;
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
            if (qrcodegen_getModule(qr, x, y))
                thingsos_rect(
                    &app.canvas, left + (x + 4) * scale, top + (y + 4) * scale, scale, scale, 0);
}
static void qr_address(const char* address){qr_address_box(address,450,240);}
static const char* confirmation_text(void) {
    switch (app.confirm_action) {
        case 1: return "确认重启设备？阅读记录将先保存。";
        case 2: return "确认关机？再次按电源键开机。";
        case 3: return "确认进入独立救援？SD 书籍保留。";
        case 4: return "进入 ROM USB 下载模式？需要 USB 线刷。";
        case 5: return "清空最近阅读？书籍和阅读进度保留。";
        case 6: return "回退到上一个有效固件并重启？";
        case 7: return "忘记已保存的 WiFi？下次需要重新输入密码。";
        case 8: return "确认下载并安装所选新固件？";
        default: return "";
    }
}
static void alignment_confirm_draw(void) {
    if (!app.confirm_action) return;
    thingsos_rect(&app.canvas, 0, 638, 684, 578, 255);
    thingsos_rect(&app.canvas, 0, 638, 684, 3, 0);
    centered_label("请确认", 36, 715, 34, 612);
    centered_label(confirmation_text(), 36, 830, 26, 612);
    centered_label("取消", 36, 1100, 30, 260);
    centered_label("确认", 440, 1100, 30, 208);
}
/* E-ink retains this frame after panel power is removed. Keep the lock and
   power-key paths on the same renderer without changing the return page. */
static void stats_draw(bool wallpaper) {
    char line[640];
    centered_label("阅读统计",96,112,40,492);
    if(!wallpaper)label("<",36,110,36,60);
    thingsos_rect(&app.canvas,36,136,612,1,170);
    for(unsigned i=0;i<3;++i) {
        int x=STATS_TABS_X+(int)i*STATS_TAB_WIDTH;
        centered_label(stats_period_name(i),x,186,28,STATS_TAB_WIDTH);
        if(app.stats_period==i)thingsos_rect(&app.canvas,x+52,203,100,3,0);
    }
    time_t t=wall_time();stats_period_t data;
    stats_period_build(t,app.stats_period,&data);
    app.stats_first=data.first;app.stats_last=data.last;
    feature_duration(data.total,line,sizeof(line));label(line,36,264,38,612);
    size_t count=0;
    for(size_t i=0;i<app.records.book_count;++i)
        if(data.book_ms[i])app.stats_indices[count++]=i;
    app.stats_count=count;
    /* Compute each book's period total once, then sort the cached numbers. */
    for(size_t i=1;i<count;++i) {
        size_t key=app.stats_indices[i],j=i;
        while(j && data.book_ms[key]>data.book_ms[app.stats_indices[j-1]]) {
            app.stats_indices[j]=app.stats_indices[j-1];--j;
        }
        app.stats_indices[j]=key;
    }
    snprintf(line,sizeof(line),"%s已读 %u 本 · 最近打开 %u 本",stats_period_name(app.stats_period),(unsigned)count,(unsigned)app.recent_count);
    label(line,36,306,24,612);
    stats_heatmap(t);stats_chart(&data,app.stats_period);
    if(app.list_start>=count)app.list_start=0;
    thingsos_rect(&app.canvas,36,950,612,1,210);
    for(size_t row=0;row<2 && row+app.list_start<count;++row) {
        size_t index=app.stats_indices[row+app.list_start];
        int y=990+(int)row*40;
        label(app.records.books[index].title,36,y,24,420);
        snprintf(line,sizeof(line),"%llu 分钟",(unsigned long long)(data.book_ms[index]/60000));
        label(line,470,y,24,178);
    }
    if(!wallpaper)nav();
}
#include "wallpaper.inc"
static void lock_draw(unsigned power_action) {
    (void)power_action;
    memset(app.canvas.pixels,255,684u*1216u);
    app.font.ui_size_percent=(app.ui_px ? app.ui_px : 30)*100/30;
    app.font.ui_weight_override=app.ui_weight+1;
    thingsos_font_weight(ui_font(),0);
    struct stat info;
    bool shown=false;
    if(app.lock_wallpaper==1 && app.wallpaper_book.path[0] && media() &&
       !stat(app.wallpaper_book.path,&info) && S_ISREG(info.st_mode)) {
        shown=cover_wallpaper(&app.wallpaper_book);
        if(shown)centered_label(app.wallpaper_book.name,36,1020,32,612);
    } else if(app.lock_wallpaper==2 && media())shown=wallpaper_lock_picture(app.wallpaper_image);
    if(!shown) {
        app.canvas.width=684;app.canvas.height=1216;
        memset(app.canvas.pixels,255,684u*1216u);
        unsigned period=app.stats_period;size_t start=app.list_start;
        uint32_t first=app.stats_first,last=app.stats_last;
        app.stats_period=1;app.list_start=0;stats_draw(true);
        app.stats_period=period;app.list_start=start;app.stats_first=first;app.stats_last=last;
        if(app.lock_wallpaper)centered_label("所选壁纸不可用，暂用阅读统计",36,1080,22,612);
    }
    app.font.ui_size_percent=app.font.ui_weight_override=0;
}
static bool alignment_draw(void) {
#ifndef THINGSOS_HOST_UI
    if(pico_ble_draw())return true;
#endif
    if(pico_system_draw())return true;
    char line[640];
    switch (app.page) {
        case THINGSOS_UI_WALLPAPER: wallpaper_draw();return true;
        case THINGSOS_UI_DISPLAY:
            header("显示");
            label("界面显示与阅读全刷周期",36,276,24,612);
            thingsos_rect(&app.canvas,36,300,612,1,170);
            label("界面字号",48,354,23,168);
            thingsos_rect(&app.canvas,258,331,24,3,0);
            snprintf(line,sizeof(line),"%u",app.ui_px ? app.ui_px : 30);
            centered_label(line,342,354,30,170);
            thingsos_rect(&app.canvas,560,331,24,3,0);
            thingsos_rect(&app.canvas,571,321,3,24,0);
            static const char* const modes[]={"均衡","快速","清晰"};
            static const char* const weights[]={"常规","中粗","加粗"};
            refresh_options("首页全刷",app.ui_refresh_pages,430);
            setting_options("首页刷新",modes,app.ui_refresh_mode,560);
            setting_options("字体粗细",weights,app.ui_weight,690);
            label("阅读刷新单独设置",36,862,24,612);
            label("正文菜单 · 更多 · 阅读刷新",36,906,22,612);
            button("立即全刷",950);
            nav();
            return true;
        case THINGSOS_UI_POWER:
            header("电源与锁屏");
            label("自动锁屏", 48, 280, 28, 588);
            thingsos_rect(&app.canvas,36,310,612,1,170);
            static const unsigned lock_minutes[]={1,5,15};
            for(unsigned i=0;i<3;++i) {
                int x=36+(int)i*204;
                snprintf(line,sizeof(line),"%u 分钟",lock_minutes[i]);
                centered_label(line,x,364,28,204);
                if(app.auto_lock_minutes==lock_minutes[i])thingsos_rect(&app.canvas,x+52,382,100,3,0);
            }
            thingsos_rect(&app.canvas,POWER_PIN_LEFT,POWER_PIN_TOP,POWER_PIN_WIDTH,1,170);
            label("解锁密码",48,607,28,400);
            app_circle(571,600,21,app.pin.enabled?21:2);
            app_circle(605,600,21,app.pin.enabled?21:2);
            thingsos_rect(&app.canvas,571,579,34,42,app.pin.enabled?0:255);
            thingsos_rect(&app.canvas,571,579,34,2,0);thingsos_rect(&app.canvas,571,619,34,2,0);
            /* Draw a white thumb on the enabled track, a black one when off. */
            for(int dy=-15;dy<=15;++dy)for(int dx=-15;dx<=15;++dx)
                if(dx*dx+dy*dy<=225)thingsos_rect(&app.canvas,(app.pin.enabled?605:571)+dx,600+dy,1,1,app.pin.enabled?255:0);
            label(app.pin.enabled?"唤醒后需输入 6 位数字密码":"开启后，唤醒需输入密码",48,651,22,588);
            static const char* power_actions[]={"修改密码","立即锁屏","重启设备","关机"};
            unsigned first=app.pin.enabled?0:1;
            for(unsigned i=first;i<4;++i) {
                int y=685+(int)(i-first)*82;
                thingsos_rect(&app.canvas,36,y,612,1,170);
                label(power_actions[i],48,y+52,28,548);
                label(">",612,y+52,28,24);
            }
            snprintf(line, sizeof(line), "电量 %u%% · %s", app.battery, thingsos_board_charge());
            thingsos_rect(&app.canvas,36,1013,612,1,170);
            label(line,48,1060,24,588);
            nav();
            return true;
        case THINGSOS_UI_LOCK: {
            lock_draw(0);
            return true;
        }
        case THINGSOS_UI_TIME: {
            if (app.time_edit) {
                header("设置日期与时间");
                static const char* names[] = { "年", "月", "日", "时", "分" };
                int values[] = { app.time_preview.tm_year + 1900, app.time_preview.tm_mon + 1,
                                 app.time_preview.tm_mday, app.time_preview.tm_hour,
                                 app.time_preview.tm_min };
                for (unsigned row = 0; row < 5; ++row) {
                    int y = 300 + (int)row * 100;
                    button(names[row], y);
                    thingsos_rect(&app.canvas, 285, y + 30, 24, 3, 0);
                    snprintf(line, sizeof(line), "%02d", values[row]);
                    label(line, 380, y + 54, 32, 140);
                    thingsos_rect(&app.canvas, 570, y + 30, 24, 3, 0);
                    thingsos_rect(&app.canvas, 581, y + 20, 3, 24, 0);
                }
                centered_label("取消", 36, 1080, 30, 280);
                centered_label("应用", 440, 1080, 30, 208);
                return true;
            }
            header("日期与时间");
            time_t t = wall_time();
            struct tm v;
            if (feature_date() && localtime_r(&t, &v))
                strftime(line, sizeof(line), "%Y-%m-%d %H:%M", &v);
            else
                strlcpy(line, "尚未校时", sizeof(line));
            label(line, 36, 330, 36, 612);
            button(app.auto_time ? "自动校时    已开启" : "自动校时    已关闭", 420);
            button(app.auto_time ? "手动设置 · 先关闭自动校时" : "手动设置日期与时间", 560);
            label("北京时间 UTC+8", 36, 760, 26, 612);
            button("返回设置", 1000);
            nav();
            return true;
        }
        case THINGSOS_UI_TRANSFER: {
            header("传输");
            transfer_wifi_status();
            label(app.hotspot ? "连接设备热点后打开浏览器" : "设备与浏览器连接同一 WiFi", 36, 195, 24, 612);
            /* The hotspot password is only actionable while the station link is
               down; once WiFi is connected the card drops that line and shrinks. */
            bool connected = thingsos_network_connected();
            network_outline(36, 245, 612, (app.hotspot || connected) ? 176 : 200);
            label(app.hotspot ? "设备热点" : "WiFi", 60, 288, 24, 564);
            label(app.hotspot ? thingsos_network_ssid() : thingsos_network_saved_name()[0] ? thingsos_network_saved_name() : "尚未选择网络", 60, 336, 32, 564);
            if (app.hotspot) {
                if (!connected) {
                    snprintf(line, sizeof(line), "密码：%s", thingsos_network_password());
                    label(line, 60, 392, 30, 564);
                }
            } else {
                snprintf(line, sizeof(line), "连接状态：%s", app.wifi_requested ? "正在连接" : connected ? "已连接" : "未连接");
                label(line, 60, 382, 24, 564);
                if (!connected) {
                    snprintf(line, sizeof(line), "热点密码：%s", thingsos_network_password());
                    label(line, 60, 426, 30, 564);
                }
            }
            thingsos_network_address(app.hotspot, line, sizeof(line));
            if (app.service_on && (app.hotspot || thingsos_network_connected()) && !strncmp(line,"http://",7)) qr_address(line);
            else centered_label(app.service_on ? "连接网络后显示传输二维码" : "传输服务已结束", 36, 565, 26, 612);
            centered_label(line, 36, 736, 28, 612);
            network_button("WiFi 设置", 36, 758, 298, false);
            network_button(app.hotspot ? "热点已开启" : connected ? "WiFi 已连接" : "等待网络", 350, 758, 298, false);
            label(app.service_on ? "传输服务已开启 · 扫码或打开上方地址" : "传输服务已结束", 36, 861, 23, 612);
            thingsos_transfer_state_t transfer; thingsos_network_transfer(&transfer);
            if (transfer.active) {
                snprintf(line, sizeof(line), "接收 %u%% · %s", transfer.percent, transfer.name);
                label(line, 36, 943, 23, 612);
                thingsos_rect(&app.canvas, 36, 952, 612, 6, 215);
                thingsos_rect(&app.canvas, 36, 952, (int)(612 * transfer.percent / 100), 6, 0);
            } else if (app.message[0]) label(app.message, 36, 943, 23, 612);
            network_button(app.service_on ? "结束传输" : "开启传输", 36, 970, 612, false);
            label(media() ? "书籍 /books · 字体 /fonts · 图片 /wallpapers" : "SD 卡未挂载，无法接收书籍", 36, 1075, 22, 612);
            nav(); return true;
        }
        case THINGSOS_UI_UPGRADE:
            upgrade_draw();return true;
        case THINGSOS_UI_LOGS:
            header("诊断日志");
            thingsos_diag_summary(line,sizeof(line));
            label(line[0]?line:"暂无故障摘要",36,300,23,612);
            label("记录启动、待机、任务阶段与错误码",36,380,24,612);
            label("有卡保存近期事件",36,435,23,612);
            label("无卡保留故障摘要",36,487,23,612);
            button("打开传输网页导出诊断包",560);
            label("打开设备网页，选择「下载诊断包」",36,715,23,612);
            label("独立救援模式也可从网页导出",36,770,23,612);
            nav();
            return true;
        case THINGSOS_UI_RECOVERY:
            header("恢复");
            label("启动异常会自动回退或进入独立救援", 36, 280, 24, 612);
            button("进入独立救援", 350);
            button("进入 ROM USB 下载模式", 490);
            button("重新挂载 SD 卡", 630);
            button("回退到上一个有效固件", 770);
            button("诊断日志", 885);
            button("返回设置", 1000);
            nav();
            return true;
        case THINGSOS_UI_BOOKMARKS: {
            header("书签");
            label(app.read_title, 36, 255, 24, 612);
            reader_control("章节",36,280,292,false);
            reader_control("书签",356,280,292,true);
            size_t mark_count = 0;
            while (mark_index(mark_count) != SIZE_MAX) ++mark_count;
            snprintf(line,sizeof(line),"当前位置 %u 页 · 本书 %u · 总书签 %u / %u",
                (unsigned)current_page()+1,(unsigned)mark_count,
                (unsigned)app.runtime.bookmark_count,THINGSOS_BOOKMARK_LIMIT);
            label(line,36,378,22,612);
            reader_control("设置 / 取消当前位置书签",36,398,612,false);
            bool any = false;
            for (size_t row = 0; row < 4; ++row) {
                size_t n = mark_index(app.list_start + row);
                if (n == SIZE_MAX) break;
                any = true;
                thingsos_location_t m = app.runtime.bookmarks[n];
                int y = 490 + (int)row * 112;
                if (app.epub_open && m.chapter < app.epub.spine_count)
                    label(app.epub.chapter_names[m.chapter],36,y+34,24,472);
                else label("正文",36,y+34,24,472);
                size_t page=thingsos_page_find(app.pages,app.page_count,m.chapter,m.byte_offset);
                snprintf(line,sizeof(line),"第 %u 页 · %u%%",(unsigned)page+1,
                    thingsos_book_percent(page,app.page_count));
                label(line,36,y+76,22,472);
                reader_control("删除",536,y+18,112,false);
                thingsos_rect(&app.canvas,36,y+102,612,1,204);
            }
            if (!any) centered_label("本书暂无书签",36,600,24,612);
            else {
                snprintf(line,sizeof(line),"%u / %u · 上下滑动翻页",
                    (unsigned)(app.list_start/4)+1,(unsigned)((mark_count+3)/4));
                centered_label(line,36,972,22,612);
            }
            reader_control("返回阅读",36,1000,612,false);
            return true;
        }
        case THINGSOS_UI_STATS: stats_draw(false);return true;
        default: return false;
    }
}
static bool alignment_tap(int x, int y) {
#ifndef THINGSOS_HOST_UI
    if(pico_ble_tap(x,y))return true;
#endif
    if(pico_system_tap(x,y))return true;
    if (app.confirm_action) {
        if (y < 1020) return true;
        unsigned action = app.confirm_action;
        app.confirm_action = 0;
        app.dirty = true;
        if (x < 342) return true;
        save_state();
        preferences_save();
        if (action == 1) {
            uint8_t request[2] = { 0 };
            read_pico_pmu_cmd(PMU_CMD_HOST_REQUEST_RESET, request, 2);
            esp_restart();
        }
        if (action == 2) power_action(2);
        if (action == 3 && thingsos_enter_rescue() != ESP_OK) say("救援镜像不可用");
        if (action == 4) thingsos_board_rom_download();
        if (action == 5) {
            for (size_t i = 0; i < app.records.book_count; ++i) app.records.books[i].order = 0;
            app.recent_count = 0;
            app.records_dirty = true;
            feature_save();
        }
        if (action == 7) {
            bool forgot = thingsos_network_forget() == ESP_OK;
            memset(app.wifi_ssid, 0, sizeof(app.wifi_ssid));
            memset(app.wifi_password, 0, sizeof(app.wifi_password));
            say(forgot ? "已忘记网络" : "忘记网络保存失败，请检查设备存储");
        }
        if (action == 8 && thingsos_network_release_start(true)!=ESP_OK)say("请检查 WiFi 连接，重新检查更新后重试");
        if (action == 6) {
            const esp_partition_t* last = esp_ota_get_last_invalid_partition();
            (void)last;
            const esp_partition_t* running = esp_ota_get_running_partition();
            const esp_partition_t* previous = esp_ota_get_next_update_partition(running);
            esp_ota_img_states_t status;
            if (!previous || esp_ota_get_state_partition(previous, &status) != ESP_OK ||
                status != ESP_OTA_IMG_VALID || esp_ota_set_boot_partition(previous) != ESP_OK)
                say("没有可回退的有效固件");
            else
                esp_restart();
        }
        return true;
    }
    if (app.page == THINGSOS_UI_LOCK) {
        unlock_screen();
        return true;
    }
    if (app.page == THINGSOS_UI_SETTINGS && y < 1098) {
        if (y < 170 || y >= 1070) return true;
        unsigned row = (unsigned)(y-170)/100;
        static const thingsos_ui_page_t pages[] = { THINGSOS_UI_FONTS,   THINGSOS_UI_DISPLAY,
                                                    THINGSOS_UI_WIFI,    THINGSOS_UI_TIME,
                                                    THINGSOS_UI_POWER,   THINGSOS_UI_FILES,
                                                    THINGSOS_UI_USB, THINGSOS_UI_UPGRADE, THINGSOS_UI_RECOVERY };
        if (row >= 9) return true;
        if (row == 2) {
            if (thingsos_network_start(false) != ESP_OK) {
                say("联网启动失败");
                return true;
            }
        }
        if (row == 5) {
            enter_files();
            return true;
        }
        navigate(pages[row]);
        if(row==0){font_scan();font_preview_current();}
        return true;
    }
    if (app.page == THINGSOS_UI_DISPLAY) {
        if (y >= 950 && y < 1035) app.force_refresh=true;
        else if (y >= 300 && y < 400) {
            unsigned px=app.ui_px ? app.ui_px : 30;
            if (x>=228 && x<320 && px>26) app.ui_px=px-2;
            else if (x>=530 && px<50) app.ui_px=px+2;
        } else if(x>=228 && x<648 && y>=430 && y<530) {
            unsigned interval=5+5*((unsigned)(x-228)/105);
            app.ui_refresh_pages=interval;
        } else if (x>=228 && y>=560 && y<790) {
            unsigned choice=x<368?0:x<508?1:2;
            if(y>=560 && y<660) {app.ui_refresh_mode=choice;app.force_refresh=true;}
            else if(y>=690) app.ui_weight=choice;
        }
        preferences_save();app.dirty=true;
        return true;
    }
    if (app.page == THINGSOS_UI_POWER) {
        if (y >= 310 && y < 400) {
            app.auto_lock_minutes = x < 240 ? 1 : x < 444 ? 5 : 15;
            preferences_save();
        } else if(power_pin_hit(x,y)) {
            pin_open(app.pin.enabled?PIN_DISABLE:PIN_SETUP);return true;
        } else if(y>=685 && y<1013 && x>=36 && x<648) {
            unsigned row=(unsigned)(y-685)/82+(app.pin.enabled?0:1);
            if(row==0){pin_open(PIN_CHANGE);return true;}
            if(row==1){power_action(1);return true;}
            if(row==2)app.confirm_action=1;
            if(row==3)app.confirm_action=2;
        }
        app.dirty = true;
        return true;
    }
    if (app.page == THINGSOS_UI_TIME) {
        if (app.time_edit) {
            if (y >= 1020) {
                if (x > 342) {
                    app.time_preview.tm_sec = 0;
                    app.time_preview.tm_isdst = -1;
                    time_t seconds = mktime(&app.time_preview);
                    if (seconds >= 1577836800 && seconds <= 4102444799) {
                        struct timeval clock = { .tv_sec = seconds };
                        settimeofday(&clock, NULL);
                        if (thingsos_board_set_time((uint32_t)seconds) != ESP_OK)
                            say("系统时间已设置，RTC 同步失败");
                    }
                }
                app.time_edit = false;
            } else if (y >= 300 && y < 800 && (x < 350 || x > 520)) {
                unsigned row = (unsigned)(y - 300) / 100;
                int delta = x < 350 ? -1 : 1;
                int* values[] = { &app.time_preview.tm_year, &app.time_preview.tm_mon,
                                  &app.time_preview.tm_mday, &app.time_preview.tm_hour,
                                  &app.time_preview.tm_min };
                const int mins[] = { 120, 0, 1, 0, 0 }, maxs[] = { 199, 11, 31, 23, 59 };
                int value = *values[row] + delta;
                if (value >= mins[row] && value <= maxs[row]) *values[row] = value;
                while (app.time_preview.tm_mday > 1 &&
                       !thingsos_date_valid(
                           (uint32_t)(app.time_preview.tm_year + 1900) * 10000 +
                           (app.time_preview.tm_mon + 1) * 100 + app.time_preview.tm_mday))
                    --app.time_preview.tm_mday;
            }
            app.dirty = true;
            return true;
        }
        if (y >= 1000)
            navigate(THINGSOS_UI_SETTINGS);
        else if (y >= 420 && y < 520) {
            app.auto_time = !app.auto_time;
            thingsos_network_auto_time(app.auto_time);
            preferences_save();
        } else if (y >= 560 && y < 660 && !app.auto_time) {
            time_t t = wall_time();
            if (!feature_date() || !localtime_r(&t, &app.time_preview))
                app.time_preview =
                    (struct tm){ .tm_year = 126, .tm_mon = 0, .tm_mday = 1, .tm_hour = 12 };
            app.time_edit = true;
        }
        app.dirty = true;
        return true;
    }
    if (app.page == THINGSOS_UI_TRANSFER && y < 1098) {
        if (y >= 758 && y < 826) {
            if (thingsos_network_busy()) { say("正在传输，请完成后调整网络"); return true; }
            if (x < 342) navigate(THINGSOS_UI_WIFI);
            else if(thingsos_network_connected())say("已连接 WiFi，无需开启热点");
            else network_hotspot_policy();
        } else if (y >= 970 && y < 1038) {
            if (app.service_on && thingsos_network_busy()) { say("正在传输，请完成后结束传输"); return true; }
            app.service_on = !app.service_on;
            /* An explicit start keeps serving after leaving this page; only this
               button (or sleep) ends it, so the browser page can stay open while
               reading. Merely visiting the page still stops on the way out. */
            app.service_pinned = app.service_on;
            thingsos_network_service(app.service_on);
            if (!app.service_on && media()) { scan(); font_scan(); }
        }
        app.dirty = true; return true;
    }
    if (app.page == THINGSOS_UI_UPGRADE)return upgrade_tap(x,y);
    if (app.page == THINGSOS_UI_LOGS) {
        if(x>=36 && x<648 && y>=560 && y<660) {
            pico_transfer_open();
            if(app.page==THINGSOS_UI_TRANSFER)say("在传输网页选择「下载诊断包」");
        }
        return true;
    }
    if (app.page == THINGSOS_UI_RECOVERY) {
        if (y >= 1000)
            navigate(THINGSOS_UI_SETTINGS);
        else if (y >= 350 && y < 450)
            app.confirm_action = 3;
        else if (y >= 490 && y < 590)
            app.confirm_action = 4;
        else if (y >= 770 && y < 870)
            app.confirm_action = 6;
        else if (x >= 36 && x < 648 && y >= 885 && y < 957) {
            navigate(THINGSOS_UI_LOGS);
        }
        else if (y >= 630 && y < 730) {
            release_book();
            thingsos_board_font(&app.font);
            thingsos_board_font(&app.ui_font);
            if (read_pico_sd_remount() != ESP_OK)
                say("SD 重新挂载失败");
            else {
                feature_mount();
                scan();
                enter_files();
            }
        }
        app.dirty = true;
        return true;
    }
    if (app.page == THINGSOS_UI_BOOKMARKS) {
        if (reader_control_hit(x,y,36,1000,612))
            navigate(THINGSOS_UI_READING);
        else if (reader_control_hit(x,y,36,280,292))
            navigate(THINGSOS_UI_CHAPTERS);
        else if (reader_control_hit(x,y,36,398,612))
            feature_toggle_mark();
        else if (x>=36 && x<648 && y>=490 && y<938) {
            size_t row=(size_t)(y-490)/112;
            size_t n = mark_index(app.list_start + row);
            if (n != SIZE_MAX) {
                thingsos_location_t m = app.runtime.bookmarks[n];
                if (reader_control_hit(x,y,536,490+(int)row*112+18,112)) {
                    memmove(
                        &app.runtime.bookmarks[n], &app.runtime.bookmarks[n + 1],
                        (app.runtime.bookmark_count - n - 1) * sizeof(m));
                    --app.runtime.bookmark_count;
                    feature_marks_save();
                } else if (x<520 && y<490+(int)row*112+102 && go_page(thingsos_page_find(
                               app.pages, app.page_count, m.chapter, m.byte_offset))) {
                    navigate(THINGSOS_UI_READING);
                    feature_save();
                }
            }
        }
        app.dirty = true;
        return true;
    }
    if (app.page == THINGSOS_UI_STATS) {
        int period=stats_tab_hit(x,y);
        if(period>=0 && app.stats_period!=(unsigned)period) {
            app.stats_period=(unsigned)period;app.list_start=0;
            app.dirty=true;
        }
        return true;
    }

    return false;
}
