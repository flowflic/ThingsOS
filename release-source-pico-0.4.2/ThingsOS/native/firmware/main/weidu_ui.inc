/* SPDX-License-Identifier: Apache-2.0 */
static void wd_notes_enter(const char*,bool);
static bool weidu_begin(thingsos_weidu_action_t,const thingsos_weidu_book_t*);
static bool mp_draw(void);
static bool mp_tap(int,int);
static bool mp_open(const thingsos_weidu_book_t*);
static bool mp_request(thingsos_weidu_action_t,const thingsos_weidu_book_t*);
static void weidu_shelf_load(void);
/* Query-owned state survives unrelated cover/progress jobs. An empty local
   shelf match is not a completed online search. */
static struct {bool pending,running,failed;char query[97];} weidu_search;
static unsigned weidu_cover_serial;
static void weidu_search_poll(void) {
    if(!weidu_search.pending)return;
    if(strcmp(weidu_search.query,app.weidu_query) || !app.weidu_query[0]) {
        weidu_search.pending=weidu_search.running=false;return;
    }
    if(thingsos_weidu_busy())return;
    if(weidu_search.running) {
        thingsos_weidu_status_t state;thingsos_weidu_state(&state);
        weidu_search.failed=state.phase!=WD_READY;
        weidu_search.pending=weidu_search.running=false;
    } else {
        app.weidu_action=WD_DEVICE_SEARCH;
        bool ok=thingsos_weidu_device_search_online(weidu_search.query);
        thingsos_weidu_state(&app.weidu);
        if(ok && thingsos_weidu_busy()){weidu_search.running=true;return;}
        weidu_search.pending=false;weidu_search.failed=!ok;
    }
    app.weidu_shelf_valid=false;
    if(app.page==THINGSOS_UI_WEIDU && !app.weidu_detail){weidu_shelf_load();app.dirty=true;}
}
static void weidu_search_submit(void) {
    if(weidu_search.running)thingsos_weidu_cancel();
    memset(&weidu_search,0,sizeof(weidu_search));
    strlcpy(weidu_search.query,app.weidu_query,sizeof(weidu_search.query));
    weidu_search.pending=app.weidu_query[0] && app.weidu.authenticated;
    app.weidu_banner[0]=0;app.weidu_start=0;
    weidu_shelf_load();weidu_search_poll();
}
static void weidu_button(const char* text, int x, int y, int w) {
    thingsos_rect(&app.canvas, x, y, w, 2, 0);
    thingsos_rect(&app.canvas, x, y + 66, w, 2, 0);
    thingsos_rect(&app.canvas, x, y, 2, 68, 0);
    thingsos_rect(&app.canvas, x + w - 2, y, 2, 68, 0);
    centered_label(text, x + 8, y + 45, 26, w - 16);
}
static void weidu_shelf_load(void) {
    size_t requested_start=app.weidu_start;
    bool reuse_hero=app.weidu_shelf_valid && !app.mp_mode && !app.weidu_cards && app.weidu_start && app.weidu_count;
    app.weidu_shelf_valid=false;
    if(app.mp_mode){
        size_t count=0,total=0;
        if(thingsos_mp_device_page(app.mp_mode==2?app.mp_account.id:NULL,app.weidu_start,
            app.weidu_books,5,&count,&total)){
            app.weidu_count=count;app.weidu_total=total;
            if(app.weidu_start>=total && app.weidu_start){app.weidu_start=0;weidu_shelf_load();}
        }else app.weidu_count=app.weidu_total=0;
        return;
    }
    uint64_t started=now();unsigned passes=1;
    size_t n=0,total=0,first=app.weidu_cards?app.weidu_start:0;
    size_t capacity=app.weidu_cards?9:app.weidu_start?1:4;
    if(reuse_hero) {
        if(!thingsos_weidu_device_search(app.weidu_start+1,app.weidu_books+1,3,&n,&total,app.weidu_query))return;
    } else if(!thingsos_weidu_device_search(first,app.weidu_books,capacity,&n,&total,app.weidu_query))return;
    size_t available=app.weidu_cards?total:total?total-1:0,step=app.weidu_cards?9:3;
    if(app.weidu_start>=available)app.weidu_start=available?(available-1)/step*step:0;
    if(app.weidu_cards && first!=app.weidu_start) {
        ++passes;
        if(!thingsos_weidu_device_search(app.weidu_start,app.weidu_books,9,&n,&total,app.weidu_query))return;
    } else if(!app.weidu_cards && first==0 && capacity==1 && !app.weidu_start) {
        ++passes;
        if(!thingsos_weidu_device_search(0,app.weidu_books,4,&n,&total,app.weidu_query))return;
    }
    if(reuse_hero && requested_start!=app.weidu_start){weidu_shelf_load();return;}
    app.weidu_count=n+(reuse_hero && total?1:0);app.weidu_total=total;
    if(!reuse_hero && !app.weidu_cards && app.weidu_start && total) {
        ++passes;
        if(!thingsos_weidu_device_search(app.weidu_start+1,app.weidu_books+1,3,&n,&total,app.weidu_query))return;
        app.weidu_count+=n;
    }
    app.weidu_shelf_valid=true;
    memset(app.weidu_shelf_member,0,sizeof(app.weidu_shelf_member));
    thingsos_weidu_device_membership(app.weidu_books,app.weidu_count,app.weidu_shelf_member);
    ESP_LOGI("ThingsOS_perf","WeRead view: scans=%u load=%u ms rows=%u",passes,(unsigned)(now()-started),(unsigned)app.weidu_count);
    app.weidu_covers_pending=true;
}
static bool weidu_search_exit(void) {
    if(!app.weidu_query[0])return false;
    if(weidu_search.running)thingsos_weidu_cancel();
    memset(&weidu_search,0,sizeof(weidu_search));
    app.weidu_query[0]=0;app.weidu_start=0;app.weidu_shelf_valid=false;
    weidu_shelf_load();app.dirty=true;app.transition_refresh=true;return true;
}
static void weidu_scroll(int dy) {
    if(online_modal_visible())return;
    if(!app.weidu.authenticated || (app.weidu.busy && !app.weidu.background) || app.weidu_detail || app.weidu_logout_confirm)return;
    if(app.mp_mode){
        if(dy<0 && app.weidu_start+5<app.weidu_total)app.weidu_start+=5;
        else if(dy>0 && app.weidu_start)app.weidu_start-=5;
        else return;
        weidu_shelf_load();app.dirty=true;return;
    }
    size_t count=app.weidu_cards?app.weidu_total:app.weidu_total?app.weidu_total-1:0;
    size_t step=app.weidu_cards?9:3,before=app.weidu_start;
    if(dy<0 && before+step<count)app.weidu_start+=step;
    if(dy>0)app.weidu_start=before>step?before-step:0;
    if(before!=app.weidu_start){weidu_shelf_load();app.dirty=true;}
}
static bool weidu_book_model(const thingsos_weidu_book_t* source,book_t* book) {
    memset(book,0,sizeof(*book));
    bool ready=thingsos_weidu_device_cached(source->id,book->path,sizeof(book->path));
    if(!ready) {
        char online_path[640];thingsos_weidu_book_t saved;
        if(thingsos_weidu_stream_path("/sdcard/.thingsos/weidu",source->id,online_path,sizeof(online_path)) &&
           thingsos_weidu_stream_book(online_path,&saved))strlcpy(book->path,online_path,sizeof(book->path));
        else snprintf(book->path,sizeof(book->path),"weidu:%s",source->id);
    }
    book->id=identity(book->path);book->epub=ready;
    strlcpy(book->name,source->title,sizeof(book->name));strlcpy(book->author,source->author,sizeof(book->author));
    return ready;
}
static void weidu_cover(const thingsos_weidu_book_t* source,book_t* model,int x,int y,int w,int h) {
    /* Retain the painted footprint, including covers scrolled off earlier rows. */
    if(!app.shelf_cover_w) {
        app.shelf_cover_x=x;app.shelf_cover_y=y;app.shelf_cover_w=w;app.shelf_cover_h=h;
    } else {
        int right=app.shelf_cover_x+app.shelf_cover_w,bottom=app.shelf_cover_y+app.shelf_cover_h;
        if(x<app.shelf_cover_x)app.shelf_cover_x=x;
        if(y<app.shelf_cover_y)app.shelf_cover_y=y;
        if(x+w>right)right=x+w;
        if(y+h>bottom)bottom=y+h;
        app.shelf_cover_w=right-app.shelf_cover_x;app.shelf_cover_h=bottom-app.shelf_cover_y;
    }
    char path[640];snprintf(path,sizeof(path),"/sdcard/.thingsos/weidu/covers/%s.jpg",source->id);
    cover_from(model,x,y,w,h,path);
    if(model->epub)cached_mark(x,y,w,h);
}
static void weidu_enter(void) {
    bool article_return=app.page==THINGSOS_UI_READING && app.reader_from_weidu && app.mp_mode==2;
    if (app.opened) release_book();
    thingsos_weidu_device_init();
    thingsos_weidu_status_t next;thingsos_weidu_state(&next);
    bool reuse=app.weidu_shelf_valid && !app.mp_mode && !app.weidu_detail && !app.weidu_start &&
        next.serial==app.weidu.serial && next.authenticated==app.weidu.authenticated;
    app.weidu=next;
    app.weidu_detail = app.weidu_logout_confirm = false;
    app.weidu_detail_return=THINGSOS_UI_WEIDU;
    if(!article_return){app.mp_mode=0;app.weidu_start=0;}
    if(!reuse)weidu_shelf_load();
    navigate(THINGSOS_UI_WEIDU);
}
static bool weidu_banner_visible(bool mp) {
    return app.weidu_banner[0] && app.weidu_banner_mp == mp &&
        now() - app.weidu_banner_at < 6000;
}
static void weidu_banner_take(const thingsos_weidu_status_t* s) {
    strlcpy(app.weidu_banner, s->message, sizeof(app.weidu_banner));
    app.weidu_banner_at = now();
    app.weidu_banner_mp = app.weidu_action >= WD_DEVICE_MP_ACCOUNTS &&
        app.weidu_action <= WD_DEVICE_MP_CONTENT;
}
#ifndef THINGSOS_HOST_UI
static void wd_rt_auth_finished(const thingsos_weidu_status_t*);
#endif
static void weidu_poll(void) {
    thingsos_weidu_status_t next;
    weidu_search_poll();
    if(app.weidu_covers_redraw && app.page==THINGSOS_UI_WEIDU && !app.weidu_detail &&
       !thingsos_board_touch_contact_pending() && now()>=app.activity_at+700) {
        app.weidu_covers_redraw=false;app.dirty=true;
    }
    if(app.weidu_covers_retry_at && now()>=app.weidu_covers_retry_at &&
       app.page==THINGSOS_UI_WEIDU && !app.weidu_detail && !thingsos_weidu_busy()) {
        app.weidu_covers_retry_at=0;app.weidu_covers_pending=true;
    }
    if(app.weidu_covers_pending && app.page==THINGSOS_UI_WEIDU && !app.dirty && !app.weidu_detail &&
       !app.mp_mode && !weidu_foreground_active() && thingsos_weidu_background_ready() &&
       !thingsos_weidu_busy() && !thingsos_board_touch_contact_pending() && now()>=app.activity_at+700) {
        app.weidu_covers_pending=false;
        thingsos_weidu_device_covers(app.weidu_books,app.weidu_count);
    }
    /* Status messages belong to the request that produced them. The shelf used
       to paint a public-account failure above every book; keep the banner on the
       page that raised it and let it disappear on its own. */
    if (app.weidu_banner[0] && now() - app.weidu_banner_at >= 6000) {
        app.weidu_banner[0] = 0;
        if (app.page == THINGSOS_UI_WEIDU) app.dirty = true;
    }
    thingsos_weidu_state(&next);
    if (next.serial == app.weidu.serial) return;
#ifndef THINGSOS_HOST_UI
    if(app.wd_rt_auth_pending && !next.busy && app.weidu_action==WD_DEVICE_RT_AUTH)
        wd_rt_auth_finished(&next);
#endif
    bool new_error = !next.busy && next.message[0] &&
        (next.phase == WD_ERROR || next.phase == WD_CANCELLED);
    if(!next.busy && app.weidu_deferred) {
        app.weidu=next;app.weidu_deferred=false;
        weidu_begin(app.weidu_deferred_action,&app.weidu_deferred_book);
        return;
    }
    if (app.weidu.busy && !next.busy) {
        app.activity_at = now();
        thingsos_runtime_activity(&app.runtime, app.activity_at);
    }
    if (app.page != THINGSOS_UI_WEIDU) {
        app.weidu = next;
        if(new_error)weidu_banner_take(&next);
        if(!next.busy)app.mp_open_pending=false;
        if (!next.busy && !next.background) {app.library_valid = false;app.weidu_shelf_valid=false;}
        return;
    }
    unsigned previous =
        app.weidu.total ? (unsigned)((uint64_t)app.weidu.completed * 20 / app.weidu.total) : 0;
    unsigned progress = next.total ? (unsigned)((uint64_t)next.completed * 20 / next.total) : 0;
    /* Replace an initial preparation frame with a normal grayscale UI frame.
       Treating the first text change out of it as a DU progress update can
       leave the previous antialiased label visible behind the new stage. The
       table-of-contents stage uses the same row, so it needs the same clean
       transition before chapter titles replace it. Later chapter progress
       keeps the bounded DU path. */
    bool initial_stage_finished=strcmp(next.message,app.weidu.message)&&
        (!strcmp(app.weidu.message,"正在准备微读服务")||
         !strcmp(app.weidu.message,"正在读取章节目录"));
    bool redraw = next.phase != app.weidu.phase || next.busy != app.weidu.busy ||
                  next.authenticated != app.weidu.authenticated || next.total != app.weidu.total ||
                  progress != previous || initial_stage_finished || strcmp(next.qr, app.weidu.qr) ||
                  (next.completed==next.total && strcmp(next.message,app.weidu.message));
    bool progress_only=redraw && !initial_stage_finished && next.busy && app.weidu.busy && next.phase==app.weidu.phase &&
        next.phase!=WD_QR && !app.dirty && !app.transition_refresh && !app.force_refresh;
    app.weidu = next;
    if(new_error)weidu_banner_take(&next);
    if (!next.busy) {
        if(app.weidu_detail && app.weidu_action==WD_DEVICE_SHELF_ADD)
            thingsos_weidu_device_membership(app.weidu_books,app.weidu_count,app.weidu_shelf_member);
        if(app.mp_open_pending){
            app.mp_open_pending=false;
            if(next.phase==WD_READY && next.path[0] && app.mp_mode==2 && app.weidu_selected<app.weidu_count && mp_open(&app.weidu_books[app.weidu_selected]))return;
        }
        thingsos_wd_device_status_t completed;thingsos_wd_device_status(&completed);
        /* Cover downloads only create missing files. Successful thumbnails
           remain valid; missing external covers are deliberately retryable. */
        if(!next.background)thingsos_thumbnail_clear(&app.thumbnails);
        if(!app.weidu_detail && (!next.background || (!app.weidu_count && next.authenticated && !app.weidu_query[0]))) {
            app.weidu_shelf_valid=false;weidu_shelf_load();
        }
        if(!next.background)app.library_valid = false;
        if(next.background) {
            if(completed.action==WD_DEVICE_COVERS){
                unsigned serial=thingsos_weidu_device_cover_serial();
                if(serial!=weidu_cover_serial){weidu_cover_serial=serial;app.weidu_covers_redraw=true;}
                /* 连接失败是暂时现象：30 秒后重试，最多三次，成功后重绘。
                   / A transient connect failure must not strand a cover for
                   the session: retry after 30 s, at most three attempts. */
                app.weidu_covers_retry_at=now()+30000;
            }
            /* Search owns its completion in weidu_search_poll. A stale SEARCH
               action must not make later cover/reading jobs repaint this page. */
            redraw=new_error;
        } else redraw=true;
    }
    if(next.background && next.busy)redraw=false;
    if (redraw) {app.weidu_progress_refresh=progress_only;app.dirty = true;}
}
static bool weidu_draw(void) {
    if (app.page != THINGSOS_UI_WEIDU) return false;
    header("微读");
    if (app.weidu.busy && !app.weidu.background) {
        label(app.weidu.phase==WD_QR?
              (app.weidu_action==WD_DEVICE_RT_AUTH?"微信扫码授权时长":"微信扫码登录"):
              "正在处理，请稍候",36,280,32,612);
        label(app.weidu.message, 36, 355, 26, 612);
        if (app.weidu.phase == WD_QR && app.weidu.qr[0]) {
            qr_address(app.weidu.qr);
            centered_label("用微信扫一扫，并在手机确认", 36, 765, 26, 612);
        } else {
            label(app.weidu.title, 36, 465, 30, 612);
            if (app.weidu.total) {
                char info[96];
                snprintf(info, sizeof(info), "%u / %u 章%s", app.weidu.completed, app.weidu.total,app.weidu.completed==app.weidu.total?" · 正在保存":"");
                label(info, 36, 575, 28, 612);
                thingsos_rect(&app.canvas, 36, 630, 612, 8, 220);
                thingsos_rect(
                    &app.canvas, 36, 630,
                    (int)((uint64_t)612 * app.weidu.completed / ((uint64_t)app.weidu.total+1)), 8, 0);
            }
        }
        weidu_button("取消操作", 36, 980, 612);
        nav();
        return true;
    }
    if (app.weidu_logout_confirm) {
        label("退出微读登录？", 36, 320, 34, 612);
        label("设备登录信息将删除。", 36, 410, 28, 612);
        label("已缓存 EPUB 和阅读记录保留。", 36, 465, 26, 612);
        weidu_button("取消", 36, 900, 292);
        weidu_button("确认退出", 356, 900, 292);
        nav();
        return true;
    }
    if (!app.weidu.authenticated) {
        label("把微信读书带到墨水屏", 36, 300, 36, 612);
        label("扫码登录 · 个人书架 · 离线阅读", 36, 370, 27, 612);
        label("非官方客户端，接口可能随服务调整。", 36, 460, 25, 612);
        label("只缓存账号可访问的正文与资源。", 36, 515, 25, 612);
        label(
            app.weidu.message[0] ? app.weidu.message
            : !media()           ? "请先插入 SD 卡"
                                 : "登录信息保存于本设备",
            36, 650, 25, 612);
        weidu_button("同意并扫码登录", 36, 800, 612);
        weidu_button("WiFi 设置", 36, 980, 612);
        nav();
        return true;
    }
    if(app.mp_mode)return mp_draw();
    if (app.weidu_detail && app.weidu_selected < app.weidu_count) {
        thingsos_weidu_book_t* book = &app.weidu_books[app.weidu_selected];
        weidu_button(app.weidu_detail_return==THINGSOS_UI_LIBRARY?"返回书库":app.weidu_detail_return==THINGSOS_UI_RECENT?"返回最近":app.weidu_query[0]?"返回搜索":"返回书架", 36, 245, 612);
        label(book->title, 36, 405, 36, 612);
        label(book->author, 36, 470, 27, 612);
        char cached[512];
        bool ready = thingsos_weidu_device_cached(book->id, cached, sizeof(cached));
        label(ready ? "已缓存，可离线阅读" : online_pending()?"正在准备阅读缓冲":"支持按章在线阅读", 36, 575, 28, 612);
        label("在线阅读按章加载，已加载内容离线可读。", 36, 640, 25, 612);
        if(app.weidu_shelf_member[app.weidu_selected])centered_label("已在书架",36,725,26,612);
        else weidu_button("加入书架",36,680,612);
        if (weidu_banner_visible(false)) label(app.weidu_banner, 36, 773, 22, 612);
        weidu_button("划线书评",36,785,612);
        weidu_button(ready ? "重新整本缓存" : thingsos_weidu_device_partial(book->id)?"生成完整离线书":"整本缓存", 36, 880, 612);
        weidu_button(ready?"开始阅读":"在线阅读", 36, 980, 612);
        nav();
        return true;
    }
    /* Measured spacing: 同步 333..379, WiFi 438..494 (59 px), 账号 549..597 (55 px).
       公众号 is 73 px wide, so x=200 keeps the same 59 px gap in front of 同步. */
    label("公众号",200,154,25,150);label("同步",330,154,25,85);label("WiFi",438,154,25,80);label("账号",548,154,25,100);
    char info[96];
    snprintf(info,sizeof(info),"共 %u 本 · 长按 1 秒管理",(unsigned)app.weidu_total);
    if(app.weidu_query[0]) {
        label("< 书架",36,276,24,130);
        snprintf(info,sizeof(info),weidu_search.pending?"搜索中 · 已找到 %u 本":weidu_search.failed?"线上未完成 · %u 本":"搜索结果 · %u 本",(unsigned)app.weidu_total);
        label(info,180,276,22,300);
    } else label(info,36,276,24,380);
    search_control(96);library_view_control(app.weidu_cards);
    for (size_t i = 0; i < app.weidu_count; ++i) {
        thingsos_weidu_book_t* book = &app.weidu_books[i];
        book_t model;bool ready=weidu_book_model(book,&model);
        thingsos_book_record_t* record=thingsos_records_book(&app.records,model.id,NULL,NULL);
        if(app.weidu_cards) {
            int x=36+(int)(i%3)*208,y=330+(int)(i/3)*250;
            weidu_cover(book,&model,x+24,y,148,188);centered_label(book->title,x+4,y+227,26,188);
        } else if(i==0) {
            weidu_cover(book,&model,36,344,124,174);label(book->title,188,389,32,460);label(book->author,188,440,24,460);
            snprintf(info,sizeof(info),"%s · %s · %u%%",app.weidu_shelf_member[i]?"书架":"在线",ready?"离线可读":"按章阅读",record?(unsigned)record->percent:0);
            label(info,188,486,24,460);thingsos_rect(&app.canvas,188,508,460,5,220);
            thingsos_rect(&app.canvas,188,508,record?(int)(460*record->percent/100):0,5,0);
        } else {
            int y=620+(int)(i-1)*150;
            weidu_cover(book,&model,36,y,94,130);label(book->title,154,y+42,30,490);label(book->author,154,y+80,22,490);
            snprintf(info,sizeof(info),"%s · %s · %u%%",app.weidu_shelf_member[i]?"书架":"在线",ready?"离线可读":"按章阅读",record?(unsigned)record->percent:0);
            label(info,154,y+115,22,490);thingsos_rect(&app.canvas,36,y+143,612,1,200);
        }
    }
    if (!app.weidu_count)
        label(
            app.weidu_query[0] ? (weidu_search.pending?"正在搜索线上书籍…":
                weidu_search.failed?"线上搜索未完成，可点击搜索重试":"未找到匹配书籍，可点击搜索修改") :
                app.weidu.message[0] ? app.weidu.message : "尚无本地书架，请刷新同步", 36, 535, 27,
            612);
    if (app.weidu_count && weidu_banner_visible(false))
        label(app.weidu_banner, 36, 306, 21, 612);
    nav();
    return true;
}
static bool weidu_sync_running(void) {
    return app.weidu_action==WD_DEVICE_SYNC && app.weidu.busy && !app.weidu.background && thingsos_weidu_busy();
}
static bool weidu_begin(thingsos_weidu_action_t action, const thingsos_weidu_book_t* book) {
    if(action==WD_DEVICE_SYNC && weidu_sync_running())return true;
    if(app.weidu.busy && app.weidu.background) {
        app.weidu_deferred=true;app.weidu_deferred_action=action;
        if(book)app.weidu_deferred_book=*book;
        thingsos_weidu_cancel();
        return true;
    }
    if (!media()) {
        say("请插入 SD 卡后使用微读");
        return false;
    }
    if (action != WD_DEVICE_LOGOUT && action!=WD_DEVICE_RESTORE && !thingsos_network_connected()) {
        say("请先在 WiFi 设置连接网络");
        return false;
    }
    bool started=action>=WD_DEVICE_MP_ACCOUNTS && action<=WD_DEVICE_MP_CONTENT?
        thingsos_mp_device_request(action,action==WD_DEVICE_MP_ACCOUNTS?NULL:&app.mp_account,action==WD_DEVICE_MP_CONTENT?book:NULL):
        thingsos_weidu_start(action,book);
    app.weidu_action=action;
    app.weidu_banner[0]=0;
    if (!started) {
        say("微读启动失败，请确认网络、内存及传输状态");
        return false;
    }
    thingsos_weidu_state(&app.weidu);
    app.dirty = true;
    return true;
}
static bool mp_request(thingsos_weidu_action_t action,const thingsos_weidu_book_t* article) {
    bool ok=weidu_begin(action,action==WD_DEVICE_MP_ARTICLES?&app.mp_account:article);
    if(ok && action==WD_DEVICE_MP_CONTENT)app.mp_open_pending=true;
    return ok;
}
static bool mp_open(const thingsos_weidu_book_t* article) {
    char path[512];if(!thingsos_mp_device_cached(&app.mp_account,article,path,sizeof(path)))return false;
    book_t saved=app.books[0];size_t count=app.book_count;
    memset(&app.books[0],0,sizeof(app.books[0]));
    strlcpy(app.books[0].path,path,sizeof(app.books[0].path));strlcpy(app.books[0].name,article->title,sizeof(app.books[0].name));
    strlcpy(app.books[0].author,app.mp_account.title,sizeof(app.books[0].author));app.books[0].id=identity(path);
    if(!count)app.book_count=1;
    bool ok=open_book(0);app.books[0]=saved;app.book_count=count;app.library_valid=false;
    if(ok){
        app.reader_from_weidu=true;
        /* Generic Markdown already hides local image syntax. Discard its
           image anchors for old MP caches too, retaining text/styles/offsets. */
        if(app.chapter.image_count) {
            app.chapter.image_count=0;
            memset(app.chapter.images,0,sizeof(app.chapter.images));
            reader_cache_clear();app.pagination_started=false;pagination_prepare();
        }

    }
    return ok;
}
static bool mp_draw(void) {
    label(app.mp_mode==1?"已关注的公众号":app.mp_account.title,36,154,28,612);
    weidu_button(app.mp_mode==1?"返回书架":"返回公众号",36,245,292);
    weidu_button(app.mp_mode==1?"刷新公众号":"刷新文章",356,245,292);
    char info[96];snprintf(info,sizeof(info),app.mp_mode==1?"共 %u 个公众号":"最近 %u 篇 · 正文存入 articles 目录",(unsigned)app.weidu_total);
    label(info,36,355,23,612);
    for(size_t i=0;i<app.weidu_count;++i){
        thingsos_weidu_book_t* b=&app.weidu_books[i];int y=390+(int)i*120;
        label(b->title,36,y+32,28,612);
        if(app.mp_mode==2){
            char path[512],date[48];time_t stamp=(time_t)b->updated;struct tm t;
            bool valid=b->updated && localtime_r(&stamp,&t);
            if(valid)snprintf(date,sizeof(date),"%04d-%02d-%02d",t.tm_year+1900,t.tm_mon+1,t.tm_mday);
            else strlcpy(date,"文章",sizeof(date));
            snprintf(info,sizeof(info),"%s · %s",date,thingsos_mp_device_cached(&app.mp_account,b,path,sizeof(path))?"已缓存":"点击阅读");
            label(info,36,y+76,23,612);
        }else label("点击查看最近 10 篇文章",36,y+76,23,612);
        thingsos_rect(&app.canvas,36,y+103,612,1,210);
    }
    if(!app.weidu_count)label(app.mp_mode==1?"暂无公众号，请点击刷新":"暂无文章，请点击刷新",36,535,27,612);
    if(weidu_banner_visible(true))label(app.weidu_banner,36,1000,21,612);
    weidu_button("上一页",36,1015,292);weidu_button("下一页",356,1015,292);nav();return true;
}
static bool mp_tap(int x,int y) {
    if(y>=245 && y<313){
        if(x>=36 && x<328){
            if(app.mp_mode==2){app.mp_mode=1;app.weidu_start=0;}
            else{app.mp_mode=0;app.weidu_start=0;}
            weidu_shelf_load();app.dirty=true;app.transition_refresh=true;
        }else if(x>=356 && x<648)mp_request(app.mp_mode==1?WD_DEVICE_MP_ACCOUNTS:WD_DEVICE_MP_ARTICLES,NULL);
    }else if(y>=390 && y<990 && x>=36 && x<648){
        size_t row=(size_t)(y-390)/120;if(row>=app.weidu_count)return true;
        if((y-390)%120>=104)return true;
        if(app.mp_mode==1){
            app.mp_account=app.weidu_books[row];app.mp_mode=2;app.weidu_start=0;weidu_shelf_load();app.dirty=true;app.transition_refresh=true;
            if(!app.weidu_total)mp_request(WD_DEVICE_MP_ARTICLES,NULL);
        }else{
            app.weidu_selected=row;
            if(!mp_open(&app.weidu_books[row]))mp_request(WD_DEVICE_MP_CONTENT,&app.weidu_books[row]);
        }
    }else if(y>=1015 && y<1083){
        if(x>=36 && x<328)weidu_scroll(1);
        else if(x>=356 && x<648)weidu_scroll(-1);
    }
    return true;
}
static bool weidu_open_path(const thingsos_weidu_book_t* book,const char* cached) {
    book_t saved = app.books[0];
    size_t count = app.book_count;
    memset(&app.books[0], 0, sizeof(app.books[0]));
    strlcpy(app.books[0].path, cached, sizeof(app.books[0].path));
    strlcpy(app.books[0].name, book->title, sizeof(app.books[0].name));
    strlcpy(app.books[0].author, book->author, sizeof(app.books[0].author));
    app.books[0].epub = true;
    app.books[0].id = identity(cached);
    if (!count) app.book_count = 1;
    bool ok = open_book(0);
    app.books[0] = saved;
    app.book_count = count;
    app.library_valid = false;
    if (ok) {
        app.reader_from_weidu = true;
        app.weidu_detail = false;
    }
    return ok;
}
static bool weidu_open(const thingsos_weidu_book_t* book) {
    char cached[512];
    if(thingsos_weidu_device_cached(book->id,cached,sizeof(cached))) {
        bool ok=weidu_open_path(book,cached);if(!ok)say("离线图书无法打开，请重新缓存");return ok;
    }
    return online_open(book);
}
static size_t weidu_hit(int x,int y) {
    if(x<36 || x>=648 || y<330 || y>=1070)return SIZE_MAX;
    if(app.weidu_cards)return (x-36)%208<196 && (y-330)%250<240?
        (size_t)(y-330)/250*3+(size_t)(x-36)/208:SIZE_MAX;
    return y<600?0:y>=620?1+(size_t)(y-620)/150:SIZE_MAX;
}
static unsigned weidu_hold_delay(int x,int y) {
    if(app.page==THINGSOS_UI_WEIDU)return 1000;
    size_t i=app.page==THINGSOS_UI_LIBRARY?library_hit(x,y):
        app.page==THINGSOS_UI_RECENT && y>=300 && y<900?recent_book(app.list_start+(size_t)(y-300)/150):app.book_count;
    return i<app.book_count && library_cached_book(&app.books[i])?1000:650;
}
static bool weidu_library_manage(size_t i) {
    thingsos_weidu_book_t selected={0};
    if(i>=app.book_count || !thingsos_weidu_cache_id(app.books[i].path,selected.id,sizeof(selected.id)))return false;
    strlcpy(selected.title,app.books[i].name,sizeof(selected.title));strlcpy(selected.author,app.books[i].author,sizeof(selected.author));
    thingsos_ui_page_t return_page=app.page;size_t return_start=app.list_start;
    if(app.opened)release_book();
    if(!thingsos_weidu_device_init()){say("微读服务暂不可用，请稍后重试");return true;}
    thingsos_weidu_state(&app.weidu);
    app.weidu_logout_confirm=false;
    app.weidu_shelf_valid=false;
    app.weidu_books[0]=selected;app.weidu_selected=0;app.weidu_count=1;
    thingsos_weidu_device_membership(app.weidu_books,1,app.weidu_shelf_member);
    app.weidu_detail=true;app.weidu_detail_return=return_page;app.weidu_detail_return_start=return_start;
    navigate(THINGSOS_UI_WEIDU);return true;
}
static bool weidu_hold(int x,int y) {
    if(app.page==THINGSOS_UI_WEIDU && app.mp_mode)return false;
    if(app.wd_notes_open || app.wd_sync_panel ||
       (app.page==THINGSOS_UI_WEIDU && (app.weidu_detail || app.weidu_logout_confirm)) ||
       (app.weidu.busy && !app.weidu.background))return false;
    if(app.page==THINGSOS_UI_WEIDU) {
        size_t row=weidu_hit(x,y);
        if(!app.weidu.authenticated || row>=app.weidu_count)return false;
        online_close();
        app.weidu_selected=row;app.weidu_detail=true;app.weidu_detail_return=THINGSOS_UI_WEIDU;
        app.dirty=true;app.transition_refresh=true;return true;
    }
    if(app.page!=THINGSOS_UI_LIBRARY && app.page!=THINGSOS_UI_RECENT)return false;
    size_t i=app.page==THINGSOS_UI_LIBRARY?library_hit(x,y):
        y>=300 && y<900?recent_book(app.list_start+(size_t)(y-300)/150):app.book_count;
    return weidu_library_manage(i);
}

static bool weidu_release_hold(int x,int y,uint64_t elapsed) {
    return elapsed>=weidu_hold_delay(x,y) && weidu_hold(x,y);
}
static bool weidu_tap(int x, int y) {
    if (app.page != THINGSOS_UI_WEIDU) return false;
    if (app.weidu.busy && !app.weidu.background) {
        if (y >= 980 && y < 1048) {
            thingsos_weidu_cancel();
            say("正在取消，请稍候");
        }
        return true;
    }
    if (app.weidu_logout_confirm) {
        if (y >= 900 && y < 968) {
            app.weidu_logout_confirm = false;
            if (x >= 356) weidu_begin(WD_DEVICE_LOGOUT, NULL);
            app.dirty = true;
        }
        return true;
    }
    if (!app.weidu.authenticated) {
        if (y >= 800 && y < 868) weidu_begin(WD_DEVICE_LOGIN, NULL);
        if (y >= 980 && y < 1048) navigate(THINGSOS_UI_WIFI);
        return true;
    }
    if(app.mp_mode)return mp_tap(x,y);
    if (app.weidu_detail && app.weidu_selected < app.weidu_count) {
        if (y >= 245 && y < 313) {
            online_close();
            app.weidu_detail = false;
            if(app.weidu_detail_return!=THINGSOS_UI_WEIDU) {
                thingsos_ui_page_t target=app.weidu_detail_return;size_t start=app.weidu_detail_return_start;
                navigate(target);app.list_start=start;return true;
            }
            weidu_shelf_load();
            app.dirty = true;
        }
        if(y>=680 && y<748 && !app.weidu_shelf_member[app.weidu_selected]) {
            online_close();weidu_begin(WD_DEVICE_SHELF_ADD,&app.weidu_books[app.weidu_selected]);return true;
        }
        if(y>=785 && y<853){wd_notes_enter(app.weidu_books[app.weidu_selected].id,false);return true;}
        if (y >= 880 && y < 948) {
            thingsos_weidu_book_t* book=&app.weidu_books[app.weidu_selected];char cached[512];
            bool restore=!thingsos_weidu_device_cached(book->id,cached,sizeof(cached)) && thingsos_weidu_device_partial(book->id);
            weidu_begin(restore?WD_DEVICE_RESTORE:WD_DEVICE_DOWNLOAD,book);
        }
        if (y >= 980 && y < 1048) {
            if(weidu_open(&app.weidu_books[app.weidu_selected]))app.reader_from_weidu=app.weidu_detail_return==THINGSOS_UI_WEIDU;
        }
        return true;
    }
    if (y >= 100 && y < 195) {
        if(x>=176 && x<316){
            app.mp_mode=1;app.weidu_start=0;app.weidu_detail=false;weidu_shelf_load();app.dirty=true;app.transition_refresh=true;
            if(!app.weidu_total)mp_request(WD_DEVICE_MP_ACCOUNTS,NULL);
        }else if (x >= 320 && x < 425) {
            app.wd_sync_panel=true;app.dirty=true;
        } else if (x >= 425 && x < 530)
            navigate(THINGSOS_UI_WIFI);
        else if (x >= 530 && x < 648) {
            app.weidu_logout_confirm = true;
            app.dirty = true;
        }
    } else if(y>=220 && y<310 && x>=36 && x<168 && app.weidu_query[0]) {
        weidu_search_exit();
    } else if(y>=220 && y<310 && x>=496 && x<572) {
        feature_editor(5);
    } else if(y>=220 && y<310 && x>=572 && x<648) {
        app.weidu_cards=!app.weidu_cards;app.weidu_start=0;weidu_shelf_load();preferences_save();app.dirty=true;app.transition_refresh=true;
    } else if (y >= 330 && y < 1070) {
        size_t row=weidu_hit(x,y);
        if (row < app.weidu_count) {
            char cached[512];
            if(app.weidu_query[0] && !app.weidu_shelf_member[row]) {
                app.weidu_selected=row;app.weidu_detail=true;
                app.weidu_detail_return=THINGSOS_UI_WEIDU;app.dirty=true;
            } else if(thingsos_weidu_device_cached(app.weidu_books[row].id,cached,sizeof(cached))) {
                weidu_open(&app.weidu_books[row]);
            }else {
                app.weidu_selected = row;app.weidu_detail = true;
                app.weidu_detail_return=THINGSOS_UI_WEIDU;app.dirty = true;
                online_open(&app.weidu_books[row]);
            }
        }
    }
    return true;
}
