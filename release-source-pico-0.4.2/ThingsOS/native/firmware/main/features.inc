// SPDX-License-Identifier: Apache-2.0
/* Main-task controller; storage/input/file models are independently host tested. */
static time_t wall_time(void) {
    return time(NULL);
}
static uint32_t feature_date(void) {
    time_t t = wall_time();
    struct tm v;
    if (t < 1577836800 || !localtime_r(&t, &v) || v.tm_year > 199) return 0;
    return (uint32_t)(v.tm_year + 1900) * 10000 + (v.tm_mon + 1) * 100 + v.tm_mday;
}
static void feature_account(void) {
    uint64_t n=app.runtime.reading_ms;
    if(n>app.accounted_ms && app.opened) {
        uint64_t delta=n-app.accounted_ms;
        int64_t end=(int64_t)wall_time()*1000;
#ifndef THINGSOS_HOST_UI
        struct timeval realtime;
        if(gettimeofday(&realtime,NULL)==0)end=(int64_t)realtime.tv_sec*1000+realtime.tv_usec/1000;
#endif
        if(end<1577836800000LL || delta>(uint64_t)end) {
            if(thingsos_records_time(&app.records,app.runtime.location.book_id,0,delta))app.records_dirty=true;
        } else {
            int64_t at=end-(int64_t)delta;
            while(at<end) {
                time_t seconds=(time_t)(at/1000);struct tm local;
                if(!localtime_r(&seconds,&local) || local.tm_year>199)break;
                uint32_t date=(uint32_t)(local.tm_year+1900)*10000+(local.tm_mon+1)*100+local.tm_mday;
                unsigned hour=(unsigned)local.tm_hour;
                struct tm next=local;next.tm_hour++;next.tm_min=next.tm_sec=0;next.tm_isdst=-1;
                int64_t boundary=(int64_t)mktime(&next)*1000;
                if(boundary<=at)break;
                int64_t until=boundary<end?boundary:end;
                uint64_t portion=(uint64_t)(until-at);
                if(thingsos_records_time(&app.records,app.runtime.location.book_id,date,portion)) {
                    thingsos_records_hour(&app.records,date,hour,portion);app.records_dirty=true;
                }
                at=until;
            }
        }
    }
    app.accounted_ms=n;
}
static void feature_save(void) {
    thingsos_runtime_tick(&app.runtime, now());
    feature_account();
    if (app.opened) {
        thingsos_book_record_t* b =
            thingsos_records_book(&app.records, app.runtime.location.book_id, NULL, NULL);
        if(b && app.epub.weidu_online) {
            unsigned percent=book_progress();
            if(b->percent!=percent || b->page || b->pages) {
                b->percent=percent;b->page=b->pages=0;app.records_dirty=true;
            }
        }
        if (b && app.pagination_valid && app.page_count) {
            unsigned percent = book_progress();
            if (b->page != current_page() + 1 || b->pages != app.page_count ||
                b->percent != percent)
                app.progress_dirty = true;
            b->page = (uint32_t)current_page() + 1;
            b->pages = (uint32_t)app.page_count;
            b->percent = percent;
        }
        unsigned source=app.epub_open?app.epub.chapter_sources[app.chapter_index]:0;
        size_t offset=app.start+(app.epub_open?app.chapter.source_start:0);
        if (b && (b->chapter != source || b->offset != offset)) {
            b->chapter = source;
            b->offset = (uint32_t)offset;
            app.records_dirty = true;
        }
    }
    if (app.progress_dirty && media() && app.records_bound && !app.records_blocked) progress_save();
    if (!app.records_dirty || !media() || !app.records_bound || app.records_blocked) return;
    if (thingsos_store_save_compatible(&app.records, "/sdcard"))
        app.records_dirty = false;
    else
        say("记录待保存，请检查 SD 卡");
}
static void feature_recent(void) {
    app.recent_count = 0;
    for (size_t i = 0; i < app.records.book_count; i++) {
        thingsos_book_record_t* b = &app.records.books[i];
        if (!b->order || b->id == 1) continue;
        size_t at = 0;
        while (at < app.recent_count) {
            thingsos_book_record_t* p =
                thingsos_records_book(&app.records, app.recent_ids[at], NULL, NULL);
            if (p->order < b->order) break;
            at++;
        }
        if (at >= BOOK_LIMIT) continue;
        size_t end = app.recent_count < BOOK_LIMIT ? app.recent_count++ : BOOK_LIMIT - 1;
        for (size_t j = end; j > at; j--) app.recent_ids[j] = app.recent_ids[j - 1];
        app.recent_ids[at] = b->id;
    }
}
static void feature_mount(void) {
    thingsos_records_t* loaded = malloc(sizeof(*loaded));
    if (!loaded) {
        say("记录加载内存不足");
        return;
    }
    thingsos_store_result_t result = thingsos_store_load(loaded, "/sdcard");
    if (result == THINGSOS_STORE_CORRUPT || result == THINGSOS_STORE_IO_ERROR) {
        app.records_blocked = true;
        app.records_bound = false;
        free(loaded);
        say("记录无法校验，保留原文件并暂停保存");
        return;
    }
    if (result != THINGSOS_STORE_MISSING && !thingsos_manage_recover(loaded, "/sdcard")) {
        free(loaded);
        app.records_blocked = true;
        app.records_bound = false;
        say("文件管理恢复失败，请检查 SD 卡");
        return;
    }
    if (app.records_dirty && app.records.card_id &&
        (loaded->card_id != app.records.card_id || loaded->generation != app.records.generation)) {
        app.records_blocked = true;
        app.records_bound = false;
        free(loaded);
        say("有未保存记录，请插回原卡或重启加载新卡");
        return;
    }
    if (!app.records_dirty || !app.records.card_id) {
        if (app.records_dirty && !app.records.card_id) {
            /* Before first mount only the built-in example can accumulate time. */
            thingsos_book_record_t* sample =
                thingsos_records_book(loaded, 1, "builtin:example", "Example.txt");
            bool merged = sample != NULL;
            uint64_t dated = 0;
            for (size_t i = 0; merged && i < app.records.day_count; i++) {
                dated += app.records.days[i].milliseconds;
                merged = thingsos_records_time(
                    loaded, 1, app.records.days[i].date, app.records.days[i].milliseconds);
            }
            if (merged) merged = thingsos_records_time(loaded, 1, 0, app.records.total_ms - dated);
            if (!merged) {
                free(loaded);
                app.records_blocked = true;
                say("示例统计合并失败，保留未保存记录");
                return;
            }
            thingsos_book_record_t* old = thingsos_records_book(&app.records, 1, NULL, NULL);
            if (old) {
                sample->chapter = old->chapter;
                sample->offset = old->offset;
            }
            for (size_t i = 0; i < app.records.mark_count; i++) {
                thingsos_location_t m = app.records.marks[i];
                bool duplicate = false;
                for (size_t j = 0; j < loaded->mark_count; j++)
                    if (loaded->marks[j].book_id == m.book_id &&
                        loaded->marks[j].chapter == m.chapter &&
                        loaded->marks[j].byte_offset == m.byte_offset)
                        duplicate = true;
                if (!duplicate && loaded->mark_count < THINGSOS_BOOKMARK_LIMIT)
                    loaded->marks[loaded->mark_count++] = m;
            }
            if(app.records.hour_date && (!loaded->hour_date || loaded->hour_date==app.records.hour_date))
                for(unsigned h=0;h<24;++h)thingsos_records_hour(loaded,app.records.hour_date,h,app.records.hour_ms[h]);
            app.records_dirty = true;
        }
        app.records = *loaded;
    }
    free(loaded);
    if (!app.records.card_id) {
        app.records.card_id = ((uint64_t)esp_random() << 32) | esp_random();
        if (!app.records.card_id) app.records.card_id = 1;
        app.records_dirty = true;
    }
    app.records_bound = true;
    app.records_blocked = false;
    thingsos_network_card(app.records.card_id);
    app.runtime.bookmark_count = app.records.mark_count;
    memcpy(
        app.runtime.bookmarks, app.records.marks,
        app.records.mark_count * sizeof(app.records.marks[0]));
    feature_recent();
    progress_load();
    thingsos_network_library(app.records.library_path);
    if (!thingsos_network_recover_files()) say("上传覆盖恢复失败，旧内容保留在隐藏备份中");
    feature_save();
    if (result == THINGSOS_STORE_RECOVERED) say("已从有效备份恢复阅读记录");
}
static void feature_opened(void) {
    thingsos_book_record_t* b =
        thingsos_records_book(&app.records, app.runtime.location.book_id, NULL, NULL);
    if (b) {
        strlcpy(b->path,app.read_path,sizeof(b->path));strlcpy(b->title,app.read_title,sizeof(b->title));
        reader_restore_source(b->chapter,b->offset);
    } else
        b = thingsos_records_book(
            &app.records, app.runtime.location.book_id, app.read_path, app.read_title);
    if (b && app.runtime.location.book_id == 1) strlcpy(b->title, "Example.txt", sizeof(b->title));
    if (!b) {
        say("阅读记录已满，本书无法保存记录");
        return;
    }
    if (app.records.order < UINT64_MAX) b->order = ++app.records.order;
    app.records_dirty = true;
}
static size_t feature_path(const char* path, const char* title) {
    if (!thingsos_book_path(path)) return SIZE_MAX;
    for (size_t i = 0; i < app.book_count; i++)
        if (!strcmp(app.books[i].path, path)) return i;
    size_t i = app.book_count < BOOK_LIMIT ? app.book_count++ : BOOK_LIMIT - 1;
    book_t* b = &app.books[i];
    strlcpy(b->path, path, sizeof(b->path));
    strlcpy(b->name, title, sizeof(b->name));
    b->id = identity(path);
    const char* ext = strrchr(path, '.');
    b->epub = ext && !strcasecmp(ext, ".epub");
    return i;
}
static size_t feature_book(uint64_t id) {
    if (id == 1) return app.book_count;
    for (size_t i = 0; i < app.book_count; i++)
        if (app.books[i].id == id) return i;
    thingsos_book_record_t* b = thingsos_records_book(&app.records, id, NULL, NULL);
    if(!b)return SIZE_MAX;
    char cache_id[64],current[512];
    if(thingsos_weidu_cache_id(b->path,cache_id,sizeof(cache_id)) && thingsos_weidu_cached("/sdcard/books",cache_id,current,sizeof(current)))
        return feature_path(current,b->title);
    return feature_path(b->path,b->title);
}
static void feature_marks_save(void) {
    app.records.mark_count = app.runtime.bookmark_count;
    memcpy(
        app.records.marks, app.runtime.bookmarks,
        app.records.mark_count * sizeof(app.records.marks[0]));
    app.records_dirty = true;
    feature_save();
}
static void feature_toggle_mark(void) {
    if (!app.opened) return;
    if (!thingsos_records_book(&app.records, app.runtime.location.book_id, NULL, NULL)) {
        say("本书没有可保存的记录，无法设置书签");
        return;
    }
    app.runtime.location.chapter = app.epub_open?app.epub.chapter_sources[app.chapter_index]:0;
    app.runtime.location.byte_offset = (uint32_t)(app.start+(app.epub_open?app.chapter.source_start:0));
    thingsos_mark_result_t r = thingsos_runtime_toggle_mark(&app.runtime);
    if (r == THINGSOS_MARK_FULL) {
        say("书签已满（32 个）");
        return;
    }
    if (r == THINGSOS_MARK_INVALID) {
        say("当前书籍无法设置书签");
        return;
    }
    feature_marks_save();
    say(app.records_dirty          ? "书签已更新，等待 SD 保存"
        : r == THINGSOS_MARK_ADDED ? "书签已保存"
                                   : "书签已取消并保存");
}
static void feature_duration(uint64_t ms, char* out, size_t cap) {
    uint64_t s = ms / 1000;
    snprintf(
        out, cap, "%llu 时 %02u 分 %02u 秒", (unsigned long long)(s / 3600),
        (unsigned)(s / 60 % 60), (unsigned)(s % 60));
}
/* One layout supplies both glyphs and caret hit positions, including masked fields. */
static void feature_input_layout(bool paint,int hit_x,int hit_y) {
    thingsos_font_size(ui_font(),26);
    bool compact=input_uses_qwerty();
    int left=compact?48:36,right=compact?636:648,spacing=compact?34:44;
    unsigned first_row=0;
    int best=INT_MAX;size_t nearest=0;
    /* The shared password-sized field shows three lines. Follow the caret for
       long search names, without painting over clear/cancel/search controls. */
    for(unsigned pass=0;pass<(compact?2u:1u);++pass) {
        size_t byte=0;unsigned row=0;int x=left;
        int target_row=(hit_y<312?0:(hit_y-(compact?312:305))/spacing)+(int)first_row;
        while(true) {
            uint32_t cp=0;size_t bytes=0;
            if(byte<app.input.length) {
                unsigned char c=(unsigned char)app.input.text[byte];
                bytes=c<128?1:c<224?2:c<240?3:4;
                cp=c&(bytes==1?127:bytes==2?31:bytes==3?15:7);
                for(size_t k=1;k<bytes;++k)cp=(cp<<6)|((unsigned char)app.input.text[byte+k]&63);
            }
            int advance=bytes?thingsos_glyph(ui_font(),NULL,app.input.secret && !app.input.reveal?'*':cp,0,0):0;
            if(x+advance>right && bytes){++row;x=left;}
            if(compact && pass==0) {
                if(byte==app.input.cursor)first_row=row>2?row-2:0;
            } else if(paint) {
                int baseline=(compact?338:330)+((int)row-(int)first_row)*spacing;
                if(!compact || (row>=first_row && row<first_row+3)) {
                    if(byte==app.input.cursor)thingsos_rect(&app.canvas,x,baseline-25,2,28,0);
                    if(bytes)thingsos_glyph(ui_font(),&app.canvas,app.input.secret && !app.input.reveal?'*':cp,x,baseline);
                }
            } else {
                int distance=abs((int)row-target_row)*1000+abs(x-hit_x);
                if(distance<best){best=distance;nearest=byte;}
            }
            if(!bytes)break;
            byte+=bytes;x+=advance;
        }
    }
    if(!paint)app.input.cursor=nearest;
}
static void feature_editor(unsigned which) {
    if(which==6 && app.annotation_edit.sync==THINGSOS_NOTE_SENDING){say("这条笔记正在同步，请完成后再编辑");return;}
    app.editing = which;
    app.wifi_symbols_more = false;
    app.input_return = app.page;
    char stamp[32] = "";
    time_t t = wall_time();
    struct tm v;
    if (which == 2 && feature_date() && localtime_r(&t, &v))
        strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M", &v);
    thingsos_input_open(
        &app.input,
        which == 6   ? app.annotation_edit.thought
        : which == 0   ? app.wifi_ssid
        : which == 1 ? app.wifi_password
        : which == 3 ? app.rename_name
        : which == 5 ? (app.page==THINGSOS_UI_CUBOX?app.cubox_query:
            app.page==THINGSOS_UI_FLOMO?app.flomo_query:
            app.page==THINGSOS_UI_WEIDU?app.weidu_query:app.page==THINGSOS_UI_FILES?app.file_query:app.library_query)
        : which == 4 ? ""
                     : stamp,
        which == 6   ? THINGSOS_ANNOTATION_THOUGHT
        : which == 0   ? 32
        : which == 1 ? 63
        : which == 3 ? 96
        : which == 5 ? 96
        : which == 4 ? 5
                     : 16,
        which == 1);
    navigate(THINGSOS_UI_PASSWORD);
}
static bool feature_draw(void) {
    char line[640];
    switch (app.page) {
        case THINGSOS_UI_PASSWORD: {
            if (input_uses_qwerty()) { wifi_password_draw(); return true; }
            header(
                app.editing == 0   ? "WiFi 名称"
                : app.editing == 1 ? "WiFi 密码"
                : app.editing == 3 ? "重命名书籍"
                : app.editing == 5 ? "搜索微读与线上书籍"
                : app.editing == 4 ? "跳转页码"
                                   : "设置时间");
            label(
                app.editing == 5 ? "输入书名搜索书架和线上书城；清空显示全部" : app.editing == 2 ? "格式：2026-10-11 12:30" : "点击键盘输入，箭头移动光标", 36, 270,
                26, 612);
            feature_input_layout(true, 0, 0);
            if (app.editing == 4) {
                label(app.pagination_valid?"输入 1 至全书总页数":"输入目标页码，确认后完成全书分页", 36, 490, 24, 612);
                for (unsigned n = 1; n <= 9; ++n) {
                    int x = 120 + (int)((n - 1) % 3) * 148, y = 600 + (int)((n - 1) / 3) * 90;
                    snprintf(line, sizeof(line), "%u", n);
                    thingsos_rect(&app.canvas, x, y, 140, 1, 180);
                    centered_label(line, x, y + 55, 32, 140);
                }
                centered_label("清空", 120, 920, 26, 140);
                centered_label("0", 268, 920, 32, 140);
                centered_label("删除", 416, 920, 26, 140);
                centered_label("取消", 36, 1050, 30, 260);
                centered_label("确认跳转", 390, 1050, 30, 258);
                return true;
            }
            snprintf(
                line, sizeof(line), "%u / %u 字节", (unsigned)app.input.length,
                (unsigned)app.input.limit);
            label(line, 36, 490, 22, 340);
            label(app.input.chinese ? "拼音 / EN" : "EN / 拼音", 360, 490, 22, 185);
            label("清空", 565, 490, 22, 85);
            if (app.input.chinese) {
                snprintf(line, sizeof(line), "拼音 %s", app.input.composition);
                label(line, 36, 535, 24, 380);
                label("‹      ›", 530, 535, 24, 118);
                for (size_t k = 0; k < THINGSOS_IME_PAGE_SIZE; ++k) {
                    char candidate[THINGSOS_IME_CANDIDATE_BYTES];
                    if (thingsos_ime_candidate(
                            &app.input, app.input.candidate_page * THINGSOS_IME_PAGE_SIZE + k, candidate))
                        centered_label(candidate, 36 + (int)k * 204, 580, strlen(candidate)>18?22:28, 196);
                }
            }
            for (unsigned i = 0; i < 49; i++) {
                int x = 36 + (int)(i % 10) * 61, y = 600 + (int)(i / 10) * 74;
                thingsos_rect(&app.canvas, x, y, 55, 1, 160);
                thingsos_rect(&app.canvas, x, y + 67, 55, 1, 160);
                centered_label(thingsos_input_key(&app.input, i), x + 3, y + 44, i < 40 ? 27 : 17, 50);
            }
            return true;
        }
        case THINGSOS_UI_FILES:
            if(file_picture_draw())return true;
            file_browser_draw();return true;
        case THINGSOS_UI_BOOKMARKS:
            header("书签");
            if (!app.runtime.bookmark_count) label("暂无书签", 36, 300, 28, 612);
            for (size_t i = 0; i < 5 && i + app.list_start < app.runtime.bookmark_count; i++) {
                thingsos_location_t m = app.runtime.bookmarks[i + app.list_start];
                thingsos_book_record_t* b =
                    thingsos_records_book(&app.records, m.book_id, NULL, NULL);
                int y = 270 + (int)i * 140;
                label(b ? b->title : "书籍记录不可用", 36, y + 40, 28, 480);
                snprintf(
                    line, sizeof(line), "第 %u 章 · 位置 %u", (unsigned)m.chapter + 1,
                    (unsigned)m.byte_offset);
                label(line, 36, y + 85, 23, 480);
                label("删除", 552, y + 55, 25, 96);
            }
            button(app.opened ? "返回阅读" : "返回设置", 1000);
            return true;
        case THINGSOS_UI_STATS: {
            header("阅读统计");
            feature_duration(app.records.total_ms, line, sizeof(line));
            label("累计阅读", 36, 270, 25, 612);
            label(line, 36, 325, 32, 612);
            uint64_t today = 0;
            for (size_t i = 0; i < app.records.day_count; i++)
                if (app.records.days[i].date == feature_date())
                    today = app.records.days[i].milliseconds;
            feature_duration(today, line, sizeof(line));
            label("今日", 36, 385, 24, 100);
            label(line, 145, 385, 25, 500);
            feature_duration(app.records.undated_ms, line, sizeof(line));
            label("未校时", 36, 440, 22, 125);
            label(line, 170, 440, 22, 470);
            label(
                app.stats_by_day ? "按书籍      [按日期]" : "[按书籍]      按日期", 36, 545, 28,
                612);
            size_t count = app.stats_by_day ? app.records.day_count : app.records.book_count;
            for (size_t i = 0; i < count; i++) app.stats_indices[i] = i;
            for (size_t i = 0; i < count; i++)
                for (size_t j = i + 1; j < count; j++) {
                    size_t a = app.stats_indices[i], b = app.stats_indices[j];
                    bool swap =
                        app.stats_by_day
                            ? app.records.days[b].date > app.records.days[a].date
                            : app.records.books[b].milliseconds > app.records.books[a].milliseconds;
                    if (swap) {
                        app.stats_indices[i] = b;
                        app.stats_indices[j] = a;
                    }
                }
            for (size_t i = 0; i < 4 && i + app.list_start < count; i++) {
                size_t n = app.stats_indices[i + app.list_start];
                int y = 615 + (int)i * 90;
                uint64_t duration;
                if (app.stats_by_day) {
                    uint32_t d = app.records.days[n].date;
                    snprintf(
                        line, sizeof(line), "%04u-%02u-%02u", (unsigned)(d / 10000),
                        (unsigned)(d / 100 % 100), (unsigned)(d % 100));
                    duration = app.records.days[n].milliseconds;
                } else {
                    strlcpy(line, app.records.books[n].title, sizeof(line));
                    duration = app.records.books[n].milliseconds;
                }
                label(line, 36, y, 24, 612);
                feature_duration(duration, line, sizeof(line));
                label(line, 36, y + 34, 22, 612);
            }

            return true;
        }
        case THINGSOS_UI_WIFI:
            wifi_draw();
            return true;
        case THINGSOS_UI_TIME: {
            header("日期与时间");
            time_t t = wall_time();
            struct tm v;
            if (feature_date() && localtime_r(&t, &v))
                strftime(line, sizeof(line), "%Y-%m-%d %H:%M", &v);
            else
                strlcpy(line, "尚未校时", sizeof(line));
            label(line, 36, 315, 36, 612);
            button("手动设置时间", 400);
            label("时区：北京时间 UTC+8", 36, 565, 26, 612);
            label("联网后自动校时；未校时统计单独保存", 36, 635, 24, 612);
            button("返回设置", 1000);
            return true;
        }
        default: return false;
    }
}
static bool feature_tap(int x, int y) {
    app.keyboard_refresh = false;
    app.input_refresh = false;
    if (app.page == THINGSOS_UI_PASSWORD) {
        if (y >= 74 && y < 200 && x < 90) {
            app.wifi_join_after = false;
            memset(&app.input, 0, sizeof(app.input));
            navigate(app.input_return);
            return true;
        }
        int key = -1;
        if (input_uses_qwerty()) {
            if (y >= 300 && y < 420) {
                feature_input_layout(false,x,y);app.input_refresh=true;app.dirty=true;return true;
            }
            if(y>=440 && y<508) {
                if(x>=350)thingsos_input_clear(&app.input);
                else if(app.editing==1)app.input.reveal=!app.input.reveal;
                else thingsos_input_move(&app.input,x<184?-1:1);
                app.input_refresh=true;app.dirty=true;return true;
            }
            if (app.input.chinese && y >= 666 && y < 704 && ((x >= 510 && x < 576) || (x >= 584 && x < 650))) {
                thingsos_ime_page(&app.input, x < 580 ? -1 : 1);
                app.input_refresh = true; app.dirty = true; return true;
            }
            if (app.input.chinese && y >= 704 && y < 752 && x >= 36 && x < 636) {
                if (!thingsos_ime_commit(&app.input, app.input.candidate_page * THINGSOS_IME_PAGE_SIZE + (size_t)(x - 36) / 204)) say("候选不可用或字节已达上限");
                app.input_refresh = true; app.dirty = true; return true;
            }
            key = y >= 560 && y < 628 ? (x < 342 ? 47 : 48) : wifi_key_hit(x, y);
        } else {
        if (app.editing != 4 && y >= 460 && y < 510) {
            if (x >= 550)
                thingsos_input_clear(&app.input);
            else if (x >= 350) {
                app.input.chinese = !app.input.chinese;
                app.input.mode = THINGSOS_KEYS_LOWER;
                app.input.composition[0] = 0;
            }
            app.dirty = true;
            return true;
        }
        if (app.input.chinese && y >= 510 && y < 550) {
            thingsos_ime_page(&app.input, x < 590 ? -1 : 1);
            app.dirty = true;
            return true;
        }
        if (app.input.chinese && y >= 550 && y < 600) {
            if (!thingsos_ime_commit(
                    &app.input, app.input.candidate_page * THINGSOS_IME_PAGE_SIZE + (size_t)(x < 36 ? 0 : x - 36) / 204))
                say("候选不可用或字节已达上限");
            app.dirty = true;
            return true;
        }
        if (y >= 300 && y < 460) {
            feature_input_layout(false, x, y);
            app.dirty = true;
            return true;
        }
        key = thingsos_input_key_hit(x, y);
        }
        if (app.editing == 4) {
            key = -1;
            if (y >= 600 && y < 870 && x >= 120 && x < 564 && (x - 120) % 148 < 140 &&
                (y - 600) % 90 < 80) {
                unsigned digit = (unsigned)(y - 600) / 90 * 3 + (unsigned)(x - 120) / 148 + 1;
                char number[2] = { (char)('0' + digit), 0 };
                if (!thingsos_input_insert(&app.input, number)) say("页码已达输入上限");
                app.dirty = true;
                return true;
            }
            if (y >= 870 && y < 950) {
                if (x < 260)
                    thingsos_input_clear(&app.input);
                else if (x < 410) {
                    if (!thingsos_input_insert(&app.input, "0")) say("页码已达输入上限");
                } else
                    thingsos_input_backspace(&app.input);
                app.dirty = true;
                return true;
            }
            if (y >= 1000 && y < 1090) key = x < 342 ? 47 : 48;
        }
        if (key < 0) return true;
        if(input_uses_qwerty() && key!=47 && key!=48)app.input_refresh=true;
        if (input_uses_qwerty() && (key == 40 || key == 41 || key == 49)) app.keyboard_refresh = true;
        if (key < 40 || key >= 100) {
            char character[2] = { (char)(key - 100), 0 };
            const char* value = key >= 100 ? character : thingsos_input_key(&app.input, (unsigned)key);
            if (!(app.input.chinese && ((value[0] >= 'a' && value[0] <= 'z') || (value[0]=='\'' && app.input.composition[0]))
                      ? thingsos_ime_letter(&app.input, value[0])
                      : thingsos_input_insert(&app.input, value)))
                say("输入已达到字节上限");
        } else if (key == 40 && input_uses_qwerty() && app.input.mode == THINGSOS_KEYS_SYMBOLS)
            app.wifi_symbols_more = !app.wifi_symbols_more;
        else if (key == 40)
            app.input.mode =
                app.input.mode == THINGSOS_KEYS_UPPER ? THINGSOS_KEYS_LOWER : THINGSOS_KEYS_UPPER;
        else if (key == 41)
            app.input.mode = app.input.mode == THINGSOS_KEYS_SYMBOLS ? THINGSOS_KEYS_LOWER
                                                                     : THINGSOS_KEYS_SYMBOLS;
        else if (key == 42) {
            if (app.input.composition[0]) {
                if (!thingsos_ime_commit(&app.input, app.input.candidate_page * THINGSOS_IME_PAGE_SIZE))
                    say("请选择有效拼音候选");
            } else if (!thingsos_input_insert(&app.input, " "))
                say("输入已达到字节上限");
        } else if (key == 43)
            thingsos_ime_backspace(&app.input);
        else if (key == 44 || key == 45)
            thingsos_input_move(&app.input, key == 44 ? -1 : 1);
        else if(key==49) {
            if(app.input.composition[0])say("请先选择拼音候选，再切换语言");
            else {app.input.chinese=!app.input.chinese;app.input.mode=THINGSOS_KEYS_LOWER;app.wifi_symbols_more=false;}
        } else if(key==50) {
            if(app.input.composition[0])say("请先选择拼音候选");
            else if(!thingsos_input_insert(&app.input,app.input.chinese?"，":","))say("输入已达到字节上限");
        }
        else if (key == 46)
            app.input.reveal = !app.input.reveal;
        else if (key == 47 || key == 48) {
            if (key == 47) app.wifi_join_after = false;
            if (key == 48) {
                if (app.editing == 0 && !app.input.length) { say("网络名称不能为空"); return true; }
                if (app.editing == 1 && app.wifi_join_after && (app.wifi_requires_password || app.input.length) && app.input.length < 8) { say("加密网络密码至少 8 字节"); return true; }
                if (app.input.composition[0]) {
                    say("请先选择拼音候选，再确认");
                    return true;
                }
                if(app.editing==6) {
                    strlcpy(app.annotation_edit.thought,app.input.text,sizeof(app.annotation_edit.thought));
                    if(!reader_annotations_save())return true;
                }
                if(app.editing==5) {
#ifndef THINGSOS_HOST_UI
                    if(app.input_return==THINGSOS_UI_CUBOX) {
                        strlcpy(app.cubox_query,app.input.text,sizeof(app.cubox_query));
                        cubox_search_submit();
                    } else if(app.input_return==THINGSOS_UI_FLOMO) {
                        strlcpy(app.flomo_query,app.input.text,sizeof(app.flomo_query));
                        flomo_search_submit();
                    } else
#endif
                    if(app.input_return==THINGSOS_UI_WEIDU) {
                        strlcpy(app.weidu_query,app.input.text,sizeof(app.weidu_query));
                        weidu_search_submit();
                    } else if(app.input_return==THINGSOS_UI_FILES) {
                        strlcpy(app.file_query,app.input.text,sizeof(app.file_query));app.list_start=0;
                    } else {
                        strlcpy(app.library_query,app.input.text,sizeof(app.library_query));
                        app.list_start=0;app.library_order_valid=false;
                    }
                }
                if (app.editing == 3 && !management_rename()) return true;
                if (app.editing == 4) {
                    char* end;
                    unsigned long value = strtoul(app.input.text, &end, 10);
                    if (!app.input.text[0] || *end || value < 1 || value>65536 ||
                        (app.pagination_valid && value > app.page_count)) {
                        say("页码超出全书范围");
                        return true;
                    }
                    /* Leave the editor before creating the job: navigation
                       cancels jobs that belong to an older screen. */
                    memset(&app.input,0,sizeof(app.input));
                    navigate(THINGSOS_UI_READING);
                    reader_jump_request(false,(size_t)value-1);
                    return true;
                }
                if (app.editing == 2) {
                    int year, month, day, hour, minute;
                    char extra;
                    struct tm v = { 0 };
                    if (strlen(app.input.text) != 16 ||
                        sscanf(
                            app.input.text, "%d-%d-%d %d:%d%c", &year, &month, &day, &hour, &minute,
                            &extra) != 5 ||
                        year < 2020 || year > 2099 || month < 1 || month > 12 || day < 1 ||
                        day > 31 || hour < 0 || hour > 23 || minute < 0 || minute > 59 ||
                        !thingsos_date_valid((uint32_t)(year * 10000 + month * 100 + day))) {
                        say("时间格式无效");
                        return true;
                    }
                    v.tm_year = year - 1900;
                    v.tm_mon = month - 1;
                    v.tm_mday = day;
                    v.tm_hour = hour;
                    v.tm_min = minute;
                    time_t t = mktime(&v);
                    struct timeval clock = { .tv_sec = t };
                    settimeofday(&clock, NULL);
                    if (thingsos_board_set_time((uint32_t)t) != ESP_OK)
                        say("系统时间已设置，硬件时钟同步失败");
                } else if (app.editing == 0)
                    strlcpy(app.wifi_ssid, app.input.text, sizeof(app.wifi_ssid));
                else if (app.editing == 1)
                    strlcpy(app.wifi_password, app.input.text, sizeof(app.wifi_password));
            }
            if(key==47 && app.editing==6 && !app.annotation_edit.id)app.annotation_open=false;
            memset(&app.input, 0, sizeof(app.input));
            navigate(app.input_return);
            if (key == 48 && app.editing == 0) {
                app.wifi_join_after = true;
                feature_editor(1);
                return true;
            }
            if (key == 48 && app.editing == 1 && app.wifi_join_after) {
                app.wifi_join_after = false;
                if (thingsos_network_mode(false) == ESP_OK && thingsos_network_connect(app.wifi_ssid, app.wifi_password) == ESP_OK) {
                    app.wifi_requested = now();
                    app.hotspot = false;
                } else
                    say("连接失败，请检查密码");
                memset(app.wifi_password, 0, sizeof(app.wifi_password));
            }
        }
        app.dirty = true;
        return true;
    }
    if (app.page == THINGSOS_UI_FILES)return file_browser_tap(x,y);
    if (app.page == THINGSOS_UI_BOOKMARKS) {
        if (y >= 1000) {
            navigate(app.opened ? THINGSOS_UI_READING : THINGSOS_UI_SETTINGS);
            return true;
        }
        if (y >= 270 && y < 970) {
            size_t i = app.list_start + (size_t)(y - 270) / 140;
            if (i < app.runtime.bookmark_count) {
                if (x >= 540) {
                    memmove(
                        app.runtime.bookmarks + i, app.runtime.bookmarks + i + 1,
                        (app.runtime.bookmark_count - i - 1) * sizeof(app.runtime.bookmarks[0]));
                    app.runtime.bookmark_count--;
                    feature_marks_save();
                    say(app.records_dirty ? "删除待保存" : "书签已删除");
                } else {
                    thingsos_location_t mark = app.runtime.bookmarks[i];
                    if (!app.opened || app.runtime.location.book_id != mark.book_id) {
                        size_t b = feature_book(mark.book_id);
                        if (b == SIZE_MAX || !open_book(b)) {
                            say("书籍不在当前 SD 卡或无法打开");
                            return true;
                        }
                    }
                    if(!reader_restore_source(mark.chapter,mark.byte_offset)) {
                        say("书签位置失效");return true;
                    }
                    app.runtime.location = mark;
                    app.history_count = 0;
                    navigate(THINGSOS_UI_READING);
                    feature_save();
                }
            }
        }
        app.dirty = true;
        return true;
    }
    if (app.page == THINGSOS_UI_STATS) {
        if (y >= 495 && y < 585) {
            app.stats_by_day = x > 342;
            app.list_start = 0;
            app.dirty = true;
        }
        return true;
    }
    if (app.page == THINGSOS_UI_TIME) {
        if (y >= 1000)
            navigate(THINGSOS_UI_SETTINGS);
        else if (y >= 400 && y < 500)
            feature_editor(2);
        return true;
    }
    if (app.page == THINGSOS_UI_WIFI && y < 1098) {
        int base = wifi_base(), rows = wifi_rows();
        if (x >= 644 && y >= base && y < base + rows * 114 && app.wifi_count > (size_t)rows) {
            app.list_start = (size_t)(y - base) * (app.wifi_count - rows) / (rows * 114 - 1);
        } else if (wifi_has_card() && x >= 432 && y >= 352 && y < 420) {
            app.confirm_action = 7;
        } else if (wifi_has_card() && x>=60 && x<300 && y>=352 && y<420) {
            if(thingsos_network_busy())say("正在传输，请完成后调整网络");
            else if(thingsos_network_connected()) {
                if(thingsos_network_disconnect()!=ESP_OK)say("断开失败，请稍后重试");
                else {app.wifi_requested=0;app.wifi_connected=false;say("已断开，密码已保留，点击“再次连接”恢复");}
            }
            else if(thingsos_network_mode(false)!=ESP_OK || thingsos_network_reconnect(thingsos_network_saved_name())!=ESP_OK)say("连接失败，请重新选择网络");
            else {app.hotspot=false;app.wifi_requested=now();}
        } else if (y >= 856 && y < 924) {
            if (thingsos_network_busy()) { say("正在传输，请完成后调整网络"); return true; }
            if (x < 342) wifi_scan(true);
            else navigate(THINGSOS_UI_TRANSFER);
        } else if (y >= 990 && y < 1058) {
            app.wifi_join_after = app.wifi_requires_password = false; feature_editor(0);
        } else if (y >= base && y < base + rows * 114) {
            size_t i = app.list_start + (size_t)(y - base) / 114;
            if (i < app.wifi_count) {
                if (thingsos_network_busy()) { say("正在传输，请完成后调整网络"); return true; }
                strlcpy(app.wifi_ssid, app.wifi_networks[i].ssid, sizeof(app.wifi_ssid));
                memset(app.wifi_password, 0, sizeof(app.wifi_password));
                app.wifi_requires_password = !app.wifi_networks[i].open;
                if(thingsos_network_saved(app.wifi_ssid)) {
                    if(thingsos_network_mode(false)!=ESP_OK || thingsos_network_reconnect(app.wifi_ssid)!=ESP_OK)say("连接请求失败");
                    else {app.hotspot=false;app.wifi_requested=now();}
                } else if (!app.wifi_networks[i].open) {
                    app.wifi_join_after = true; feature_editor(1);
                } else if (thingsos_network_mode(false) != ESP_OK || thingsos_network_connect(app.wifi_ssid, "") != ESP_OK) say("连接请求失败");
                else { app.hotspot = false; app.wifi_requested = now(); }
            }
        }
        app.dirty = true; return true;
    }
    return false;
}
