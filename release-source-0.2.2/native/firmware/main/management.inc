// SPDX-License-Identifier: Apache-2.0
static size_t management_selected(void) {
    size_t n = 0;
    for (size_t i = 0; i < app.manage_count; ++i)
        if (app.manage_selected[i]) ++n;
    return n;
}
static void management_sync(void) {
    app.runtime.bookmark_count = app.records.mark_count;
    memcpy(
        app.runtime.bookmarks, app.records.marks,
        app.records.mark_count * sizeof(app.records.marks[0]));
    app.records_dirty = false;
    app.progress_dirty = true;
    progress_save();
    feature_recent();
    scan();
    if (app.browser.path[0] && media())
        thingsos_browser_scan(&app.browser, "/sdcard", app.browser.path);
    app.dirty = true;
}
static const char* management_error(thingsos_manage_result_t r) {
    return r == THINGSOS_MANAGE_EXISTS    ? "同名文件或记录已存在，请换一个名称"
           : r == THINGSOS_MANAGE_INVALID ? "名称无效，请保留 EPUB/TXT 扩展名"
           : r == THINGSOS_MANAGE_RESTORE_FAILED ? "恢复失败，请检查 SD 卡中的隐藏暂存文件"
           : r == THINGSOS_MANAGE_STORE ? "记录保存失败，文件操作已撤销"
                                        : "文件操作失败，请检查 SD 卡";
}
static bool management_audio_prepare(const char* path) {
    if(!app_audio_path(path))return true;
    if(thingsos_ima_active())return false;
    thingsos_audio_status(&app.audio);
    /* A queued start has not published its new path yet. Do not race it. */
    if(app.audio.state==THINGSOS_AUDIO_STARTING || app.audio.state==THINGSOS_AUDIO_STOPPING)return false;
    if(strcasecmp(app.audio.path,path) || !thingsos_audio_active())return true;
    if(app.audio.state==THINGSOS_AUDIO_RECORDING || app.audio.state==THINGSOS_AUDIO_RECORD_PAUSED)return false;
    if(!thingsos_audio_quiesce_locked())return false;
    thingsos_audio_status(&app.audio);
    app_media.playlist=false;app_media.preview=false;
    return true;
}
static void management_audio_changed(const char* path) {
    if(!app_audio_path(path))return;
    bool music_loaded=app_media.loaded_music,records_loaded=app_media.loaded_records;
    app_media.loaded_music=app_media.loaded_records=false;
    if(music_loaded)app_audio_scan(false);
    if(records_loaded)app_audio_scan(true);
    if(app_media.selected>=app_media.music_count)app_media.selected=0;
    for(size_t i=0;i<app_media.music_count;++i)
        if(!strcasecmp(app_media.music[i].name,app_basename(app.audio.path))){app_media.selected=i;break;}
    if(app_media.music_start>=app_media.music_count)app_media.music_start=0;
    if(app_media.record_start>=app_media.record_count)app_media.record_start=0;
}
static bool management_rename(void) {
    if (app.manage_target >= app.manage_count || !media() || !app.records_bound ||
        app.records_blocked) {
        say("SD 或记录不可写");
        return false;
    }
    if (!thingsos_book_name(app.input.text)) {
        say("名称无效，扩展名须为 .epub、.txt 或 .md");
        return false;
    }
    const char* old = app.manage_paths[app.manage_target];
    if (app.opened && app.runtime.location.book_id == identity(old))
        release_book();
    else
        feature_save();
    if (app.records_dirty) {
        say("请先完成阅读记录保存");
        return false;
    }
    thingsos_manage_result_t r = thingsos_book_manage(&app.records, "/sdcard", old, app.input.text);
    if (r != THINGSOS_MANAGE_OK) {
        say(management_error(r));
        return false;
    }
    char* name = strrchr(app.manage_paths[app.manage_target], '/') + 1;
    strlcpy(
        name, app.input.text,
        THINGSOS_RECORD_PATH_BYTES - (size_t)(name - app.manage_paths[app.manage_target]));
    management_sync();
    say("已重命名，阅读位置和书签保留");
    return true;
}
static const char* management_delete_reason(thingsos_manage_result_t r) {
    return r == THINGSOS_MANAGE_INVALID          ? "名称或位置不受支持"
           : r == THINGSOS_MANAGE_EXISTS         ? "SD 卡上已有同名暂存文件"
           : r == THINGSOS_MANAGE_RESTORE_FAILED ? "恢复失败，请检查 SD 卡的隐藏暂存文件"
           : r == THINGSOS_MANAGE_STORE          ? "记录无法保存，操作已撤销"
                                                 : "SD 卡写入失败";
}
static bool management_audio_only(void) {
    for (size_t i = 0; i < app.manage_count; ++i)
        if (app.manage_selected[i] && !app_audio_path(app.manage_paths[i])) return false;
    return true;
}
static void management_delete(void) {
    bool audio_only=management_audio_only();
    bool audio_delete=app.manage_single_delete && app.manage_count==1 && app_audio_path(app.manage_paths[0]);
    if(thingsos_weidu_busy() || thingsos_network_busy()){say("后台任务进行中，请稍后删除");return;}
    /* Audio files carry no book record, so deleting them must not depend on
       the records store being bound and writable. */
    if (!media() || (!audio_only && (!app.records_bound || app.records_blocked))) {
        say("SD 或记录不可写");
        return;
    }
    feature_save();
    if (!audio_only && app.records_dirty) {
        say("请先完成阅读记录保存");
        return;
    }
    ESP_LOGI(
        "ThingsOS_manage", "delete audio=%d count=%u bound=%d blocked=%d dirty=%d media=%d",
        (int)audio_only, (unsigned)app.manage_count, (int)app.records_bound,
        (int)app.records_blocked, (int)app.records_dirty, (int)media());
    size_t done = 0, failed = 0;
    const char* failure = NULL;
    for (size_t i = 0; i < app.manage_count; ++i)
        if (app.manage_selected[i]) {
            if(!management_audio_prepare(app.manage_paths[i])){
                ESP_LOGW(
                    "ThingsOS_manage", "delete audio busy state=%u ima=%d",
                    (unsigned)app.audio.state, (int)thingsos_ima_active());
                ++failed;if(!failure)failure="音频占用中，请先停止播放";continue;
            }
            if(thingsos_weidu_busy() || thingsos_network_busy()){++failed;if(!failure)failure="后台任务进行中";continue;}
            if (app.opened && app.runtime.location.book_id == identity(app.manage_paths[i]))
                release_book();
            thingsos_manage_result_t r =
                thingsos_book_manage(&app.records, "/sdcard", app.manage_paths[i], NULL);
            if (r == THINGSOS_MANAGE_OK) {
                management_audio_changed(app.manage_paths[i]);
                char cached_id[64];
                if(thingsos_weidu_cache_id(app.manage_paths[i],cached_id,sizeof(cached_id))) {
                    char backup[640];snprintf(backup,sizeof(backup),"%s.bak",app.manage_paths[i]);remove(backup);
                }
                app.manage_selected[i] = false;
                app.manage_paths[i][0] = 0;
                ++done;
            } else {
                ESP_LOGW(
                    "ThingsOS_manage", "delete result=%d audio=%d errno=%d", (int)r,
                    (int)app_audio_path(app.manage_paths[i]), errno);
                ++failed;if(!failure)failure=management_delete_reason(r);
            }
        }
    bool unsaved = app.records_dirty && (!app.records_bound || app.records_blocked);
    size_t n = 0;
    for (size_t i = 0; i < app.manage_count; ++i)
        if (app.manage_paths[i][0]) {
            if (n != i) {
                strcpy(app.manage_paths[n], app.manage_paths[i]);
                app.manage_selected[n] = app.manage_selected[i];
            }
            ++n;
        }
    app.manage_count = n;
    app.list_start = 0;
    management_sync();
    /* Nothing can reach the card while the records store is blocked. */
    if (unsaved) app.records_dirty = true;
    char message[128];
    if (failed && !done && failure)
        snprintf(message, sizeof(message), "删除失败：%s", failure);
    else
        snprintf(
            message, sizeof(message), audio_delete?"已删除 %u 个音频文件 · 失败 %u 个":"已删除 %u 本 · 失败 %u 本 · 历史时长保留", (unsigned)done,
            (unsigned)failed);
    ESP_LOGI("ThingsOS_manage", "delete done=%u failed=%u", (unsigned)done, (unsigned)failed);
    say(message);
}
static bool management_hold(int x, int y) {
    app.manage_single_delete=false;
    size_t index = SIZE_MAX;
    const char* chosen = NULL;
    size_t n = 0;
    if (app.page == THINGSOS_UI_LIBRARY || app.page == THINGSOS_UI_RECENT) {
        bool recent = app.page == THINGSOS_UI_RECENT;
        if (!recent) index = library_hit(x, y);
        else if (y >= 300 && y < 900) index = recent_book(app.list_start + (size_t)(y - 300) / 150);
        if (index >= app.book_count) return false;
        chosen = app.books[index].path;
        for (size_t i = 0; i < app.book_count; ++i) {
            if(!recent && library_cached_book(&app.books[i])!=app.library_weidu)continue;
            strlcpy(app.manage_paths[n++], app.books[i].path, THINGSOS_RECORD_PATH_BYTES);
        }
    } else if (app.page == THINGSOS_UI_FILES && x < 560 && y >= 390 && y < 966 && media()) {
        index = thingsos_browser_match(&app.browser,app.file_query,app.list_start+(size_t)(y-390)/72);
        char path[THINGSOS_RECORD_PATH_BYTES];
        if (index >= app.browser.count || app.browser.entries[index].directory ||
            !thingsos_browser_path(&app.browser, index, path) || !thingsos_book_path(path))
            return false;
        strlcpy(app.rename_name, app.browser.entries[index].name, sizeof(app.rename_name));
        strlcpy(app.manage_paths[0], path, THINGSOS_RECORD_PATH_BYTES);
        n = 1;
        app.manage_target = 0;
        for (size_t i = 0; i < app.browser.count && n < BOOK_LIMIT; ++i)
            if (i != index && !app.browser.entries[i].directory &&
                thingsos_browser_path(&app.browser, i, app.manage_paths[n]) &&
                thingsos_book_path(app.manage_paths[n])) {
                /* The held file is already at index zero. */
                ++n;
            }
    } else
        return false;
    (void)x;
    app.manage_count = n;
    memset(app.manage_selected, 0, sizeof(app.manage_selected));
    if (chosen) {
        for (size_t i = 0; i < n; ++i)
            if (!strcmp(chosen, app.manage_paths[i])) app.manage_target = i;
    }
    app.manage_selected[app.manage_target] = true;
    app.manage_return = app.page;
    app.delete_confirm = false;
    navigate(THINGSOS_UI_MANAGE);
    return true;
}
static void file_delete_request(size_t index) {
    if(index>=app.browser.count || app.browser.entries[index].directory)return;
    char path[THINGSOS_RECORD_PATH_BYTES];
    if(!thingsos_browser_path(&app.browser,index,path) || (!thingsos_book_path(path) && !app_audio_path(path)))return;
    if(!thingsos_managed_path(path)){say("此文件不可删除");return;}
    memset(app.manage_selected,0,sizeof(app.manage_selected));
    strlcpy(app.manage_paths[0],path,sizeof(app.manage_paths[0]));
    app.manage_count=1;app.manage_target=0;app.manage_selected[0]=true;app.manage_return=THINGSOS_UI_FILES;
    app.file_return_start=app.list_start;app.manage_single_delete=app.delete_confirm=true;
    navigate(THINGSOS_UI_MANAGE);
}
static bool management_draw(void) {
    if (app.page != THINGSOS_UI_MANAGE) return false;
    char line[320];
    bool audio_delete=app.manage_single_delete && app.manage_count==1 && app_audio_path(app.manage_paths[0]);
    header(audio_delete?"音频文件":"书籍管理");
    snprintf(
        line, sizeof(line), audio_delete?"已选择 %u / %u 个音频文件":"当前批次 %u / %u 本 · 点击选择，滑动翻页",
        (unsigned)management_selected(), (unsigned)app.manage_count);
    label(line, 36, 265, 24, 612);
    centered_label("全选 / 清空", 36, 330, 26, 300);
    centered_label("取消管理", 410, 330, 26, 240);
    for (size_t row = 0; row < 5 && row + app.list_start < app.manage_count; ++row) {
        size_t i = row + app.list_start;
        const char* name = strrchr(app.manage_paths[i], '/');
        int y = 380 + (int)row * 110;
        button("", y);
        thingsos_rect(&app.canvas, 36, y + 20, 30, 2, 0);
        thingsos_rect(&app.canvas, 36, y + 48, 30, 2, 0);
        thingsos_rect(&app.canvas, 36, y + 20, 2, 30, 0);
        thingsos_rect(&app.canvas, 64, y + 20, 2, 30, 0);
        if (app.manage_selected[i]) {
            for (int k = 0; k < 8; ++k) thingsos_rect(&app.canvas, 41 + k, y + 34 + k, 3, 3, 0);
            for (int k = 0; k < 14; ++k) thingsos_rect(&app.canvas, 48 + k, y + 41 - k, 3, 3, 0);
        }
        label(name ? name + 1 : app.manage_paths[i], 88, y + 54, 28, 560);
    }
    if (!app.manage_count) label("没有可管理的用户书籍", 36, 450, 28, 612);
    centered_label("重命名（单选）", 36, 1060, 26, 310);
    centered_label("删除所选", 400, 1060, 26, 250);
    if (app.delete_confirm) {
        thingsos_rect(&app.canvas, 0, 638, 684, 578, 255);
        snprintf(line, sizeof(line), audio_delete?"删除选中的 %u 个音频文件？":"删除选中的 %u 本书籍？", (unsigned)management_selected());
        label(line, 36, 740, 32, 612);
        label(audio_delete?"文件将从 SD 卡删除。":"文件将删除，历史阅读时长保留。", 36, 830, 26, 612);
        centered_label("取消", 36, 1100, 30, 260);
        centered_label("确认删除", 440, 1100, 30, 208);
    }
    return true;
}
static bool management_tap(int x, int y) {
    if (app.page != THINGSOS_UI_MANAGE) return false;
    if (app.delete_confirm) {
        if (y >= 1020) {
            app.delete_confirm = false;
            if (x > 342) management_delete();
            if(app.manage_single_delete) {
                app.manage_single_delete=false;navigate(THINGSOS_UI_FILES);app.list_start=app.file_return_start;
                if(x>342 && app.message[0])app.message_until=now()+2200;
            }
        }
        app.dirty = true;
        return true;
    }
    if (y < 200 && x < 150)
        navigate(app.manage_return);
    else if (y >= 290 && y < 370) {
        if (x > 342)
            navigate(app.manage_return);
        else {
            bool on = management_selected() != app.manage_count;
            for (size_t i = 0; i < app.manage_count; ++i) app.manage_selected[i] = on;
        }
    } else if (y >= 380 && y < 930) {
        size_t i = app.list_start + (size_t)(y - 380) / 110;
        if (i < app.manage_count) app.manage_selected[i] = !app.manage_selected[i];
    } else if (y >= 1000) {
        size_t count = management_selected();
        if (!count)
            say("请先选择书籍");
        else if (x > 342)
            app.delete_confirm = true;
        else if (count != 1)
            say("重命名时请选择一本书");
        else {
            for (size_t i = 0; i < app.manage_count; ++i)
                if (app.manage_selected[i]) app.manage_target = i;
            const char* name = strrchr(app.manage_paths[app.manage_target], '/') + 1;
            if (strlen(name) > 96)
                say("文件名太长，请通过电脑重命名");
            else {
                strlcpy(app.rename_name, name, sizeof(app.rename_name));
                feature_editor(3);
            }
        }
    }
    app.dirty = true;
    return true;
}

static void management_network(void) {
    if (!thingsos_network_job(&app.network_job)) return;
    unsigned done = 0, failed = 0;
    feature_save();
    if (app.records_dirty || !media() || !app.records_bound || app.records_blocked ||
        app.network_job.card_id != app.records.card_id)
        failed = (unsigned)app.network_job.count;
    else
        for (size_t i = 0; i < app.network_job.count; ++i) {
            const char* path = app.network_job.paths[i];
            if(!management_audio_prepare(path)){++failed;continue;}
            if (app.opened && app.runtime.location.book_id == identity(path)) release_book();
            if (thingsos_book_manage(
                    &app.records, "/sdcard", path,
                    app.network_job.new_name[0] ? app.network_job.new_name : NULL) ==
                THINGSOS_MANAGE_OK) {
                management_audio_changed(path);++done;
            }
            else
                ++failed;
        }
    management_sync();
    thingsos_network_job_done(app.network_job.job, done, failed);
    say(failed ? "电脑文件管理存在失败项，请查看结果" : "电脑文件管理已完成");
    if (!app.opened && app.page == THINGSOS_UI_READING) navigate(THINGSOS_UI_LIBRARY);
}
static bool transfer_blocks_ui(void) {
    return thingsos_network_busy() && !thingsos_audio_active() && !thingsos_ima_active() && !thingsos_cubox_active() && !thingsos_flomo_active() && !thingsos_weidu_busy() && !thingsos_network_release_checking();
}
static void transfer_activity(uint64_t time){
    /* File transfer is activity even without a touch. Restart the existing
       idle interval so completion can be acknowledged before WiFi sleeps. */
    if(time>app.activity_at){app.activity_at=time;thingsos_runtime_activity(&app.runtime,time);}
}
static void transfer_sync_activity(const thingsos_transfer_state_t* s,uint64_t time){
    /* Re-reading an old completion must not extend the idle deadline. A later
       physical interaction remains newer than the published finish time. */
    if(s->active)transfer_activity(time);
    else if(s->finished_ms)transfer_activity(s->finished_ms);
}
static bool transfer_refresh_due(const thingsos_transfer_state_t* s,uint64_t time,uint64_t refresh_at){
    if(!s->serial||s->serial==app.transfer_serial)return false;
    const thingsos_transfer_state_t* last=&app.transfer_displayed;
    /* State changes bypass progress throttling. A reset also handles a new
       upload of the same name without relying on observing its idle gap. */
    if(!last->serial||s->active!=last->active||s->failed!=last->failed||s->ota!=last->ota||
       strcmp(s->name,last->name)||strcmp(s->message,last->message)||s->percent<last->percent)return true;
    /* Leave a gap after the panel's 3 s ordinary-UI power reuse window. */
    if(s->ota)return time-refresh_at>4000;
    return s->percent/20!=app.transfer_progress/20;
}
static void transfer_refresh_record(const thingsos_transfer_state_t* s){
    app.transfer_displayed=*s;app.transfer_serial=s->serial;app.transfer_progress=s->percent;
}
static void transfer_draw(const thingsos_transfer_state_t* s) {
    if (app.page == THINGSOS_UI_TRANSFER && !s->ota) { draw(); return; }
    memset(app.canvas.pixels, 255, 684u * 1216u);
    header(s->ota ? "固件升级" : "文件传输");
    label(s->name, 36, 340, 30, 612);
    label(s->message, 36, 450, 26, 612);
    char text[32];
    snprintf(text, sizeof(text), "%u%%", s->percent);
    label(text, 36, 590, 42, 612);
    thingsos_rect(&app.canvas, 36, 640, 612, 10, 220);
    thingsos_rect(&app.canvas, 36, 640, (int)(612 * s->percent / 100), 10, 0);
    label(s->active ? "请保持连接和供电" : "完成后可继续操作", 36, 780, 26, 612);
}
