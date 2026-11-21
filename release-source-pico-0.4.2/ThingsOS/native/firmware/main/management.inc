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
static void management_delete(void) {
    if(thingsos_weidu_busy() || thingsos_network_busy()){say("后台任务进行中，请稍后删除");return;}
    if (!media() || !app.records_bound || app.records_blocked) {
        say("SD 或记录不可写");
        return;
    }
    feature_save();
    if (app.records_dirty) {
        say("请先完成阅读记录保存");
        return;
    }
    size_t done = 0, failed = 0;
    for (size_t i = 0; i < app.manage_count; ++i)
        if (app.manage_selected[i]) {
            if (app.opened && app.runtime.location.book_id == identity(app.manage_paths[i]))
                release_book();
            thingsos_manage_result_t r =
                thingsos_book_manage(&app.records, "/sdcard", app.manage_paths[i], NULL);
            if (r == THINGSOS_MANAGE_OK) {
                char cached_id[64];
                if(thingsos_weidu_cache_id(app.manage_paths[i],cached_id,sizeof(cached_id))) {
                    char backup[640];snprintf(backup,sizeof(backup),"%s.bak",app.manage_paths[i]);remove(backup);
                }
                app.manage_selected[i] = false;
                app.manage_paths[i][0] = 0;
                ++done;
            } else
                ++failed;
        }
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
    char message[128];
    snprintf(
        message, sizeof(message), "已删除 %u 本 · 失败 %u 本 · 历史时长保留", (unsigned)done,
        (unsigned)failed);
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
    if(!thingsos_browser_path(&app.browser,index,path) || !thingsos_book_path(path))return;
    memset(app.manage_selected,0,sizeof(app.manage_selected));
    strlcpy(app.manage_paths[0],path,sizeof(app.manage_paths[0]));
    app.manage_count=1;app.manage_target=0;app.manage_selected[0]=true;app.manage_return=THINGSOS_UI_FILES;
    app.file_return_start=app.list_start;app.manage_single_delete=app.delete_confirm=true;
    navigate(THINGSOS_UI_MANAGE);
}
static bool management_draw(void) {
    if (app.page != THINGSOS_UI_MANAGE) return false;
    char line[320];
    header("书籍管理");
    snprintf(
        line, sizeof(line), "当前批次 %u / %u 本 · 点击选择，滑动翻页",
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
        snprintf(line, sizeof(line), "删除选中的 %u 本书籍？", (unsigned)management_selected());
        label(line, 36, 740, 32, 612);
        label("文件将删除，历史阅读时长保留。", 36, 830, 26, 612);
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
            if (app.opened && app.runtime.location.book_id == identity(path)) release_book();
            if (thingsos_book_manage(
                    &app.records, "/sdcard", path,
                    app.network_job.new_name[0] ? app.network_job.new_name : NULL) ==
                THINGSOS_MANAGE_OK)
                ++done;
            else
                ++failed;
        }
    management_sync();
    thingsos_network_job_done(app.network_job.job, done, failed);
    say(failed ? "电脑文件管理存在失败项，请查看结果" : "电脑文件管理已完成");
    if (!app.opened && app.page == THINGSOS_UI_READING) navigate(THINGSOS_UI_LIBRARY);
}
static bool transfer_blocks_ui(void) {
    return thingsos_network_busy() &&
#ifndef THINGSOS_HOST_UI
           !thingsos_cubox_active() && !thingsos_flomo_active() &&
#endif
           !thingsos_weidu_busy() && !thingsos_network_release_checking();
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
