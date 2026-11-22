/* SPDX-License-Identifier: Apache-2.0
 * Main-task-only interval capture. No network or disk access in the runtime
 * callback; accepted spans are split into historical hour buckets.
 */
static thingsos_rt_capture_t *wd_rt_capture;
static uint64_t wd_rt_last_mono, wd_rt_last_wall, wd_rt_flush_at;
static bool wd_rt_problem, wd_rt_clock_problem;
static uint32_t wd_rt_risk;
static uint32_t wd_rt_floor;
static bool wd_rt_ready;
static uint32_t wd_rt_ui_day;
static uint32_t wd_rt_display_day(void) {
    uint64_t stamp=wall_time();return stamp>=WD_SYNC_MIN_TIME?(uint32_t)((stamp+28800)/86400):0;
}
static void wd_rt_day_poll(void) {
    if(app.page==THINGSOS_UI_WEIDU&&(app.wd_sync_panel||app.wd_rt_panel)&&
       !app.weidu.busy&&!app.wd_consent&&!app.dirty&&wd_rt_ui_day!=wd_rt_display_day())background_redraw();
}
static void wd_rt_interval(void *context, uint64_t start, uint32_t ms) {
    (void)context;
    if (!app.wd_active || !app.wd_rt_session || !app.wd_rt_on || !app.wd_time_on ||
        !app.wd_time_confirmed || !app.wd_id[0] || !app.wd_owner[0] || !wd_rt_capture ||
        !wd_rt_ready || !media())
        return;
    uint64_t mono = now(), wall;
#ifdef THINGSOS_HOST_UI
    wall = (uint64_t)wall_time() * 1000;
#else
    struct timeval stamp;
    gettimeofday(&stamp, NULL);
    wall = (uint64_t)stamp.tv_sec * 1000 + (uint32_t)stamp.tv_usec / 1000;
#endif
    if (wall / 1000 < WD_SYNC_MIN_TIME || mono < start || mono - start > 5000 ||
        wall - (mono - start) < (uint64_t)wd_rt_floor * 1000) {
        wd_rt_clock_problem = true;
        return;
    }
    if (wd_rt_last_mono && (wall < wd_rt_last_wall ||
                            ((wall - wd_rt_last_wall > mono - wd_rt_last_mono
                                  ? wall - wd_rt_last_wall - (mono - wd_rt_last_mono)
                                  : mono - wd_rt_last_mono - (wall - wd_rt_last_wall)) > 10000))) {
        if (wall < wd_rt_last_wall) {
            wd_rt_risk |= 1;
            wd_rt_clock_problem = true;
            return;
        }
        wd_rt_last_mono = mono;
        wd_rt_last_wall = wall;
        wd_rt_clock_problem = true;
        return;
    }
    wd_rt_last_mono = mono;
    wd_rt_last_wall = wall;
    wd_rt_clock_problem = false;
    if (!thingsos_rt_capture(wd_rt_capture, app.wd_owner, app.wd_id, wall - (mono - start), ms, 480,
                             wd_rt_risk))
        wd_rt_problem = true;
}
static void wd_rt_begin(void) {
    wd_rt_ready = false;
    app.wd_rt_session = false;
    if(app.wd_rt_on && app.wd_time_on && app.wd_time_confirmed) {
        thingsos_rt_device_status_t status;
        thingsos_wd_rt_status(&status);
        app.wd_rt_session=status.authorized && !status.expired;
    }
    if (!app.wd_rt_session)
        return;
    if (!wd_rt_capture) {
        wd_rt_capture = thingsos_psram_calloc(1, sizeof(*wd_rt_capture));
        if (wd_rt_capture) {
#ifdef THINGSOS_HOST_UI
            wd_rt_capture->nonce = 0x52455431;
#else
            wd_rt_capture->nonce = esp_random();
            if (!wd_rt_capture->nonce)
                wd_rt_capture->nonce = 1;
#endif
        }
    }
    if (!wd_rt_capture)
        wd_rt_problem = true;
    wd_rt_flush_at = now() + 30000;
}
static void wd_rt_checkpoint(bool force) {
    if (!wd_rt_ready && app.wd_rt_session && wd_rt_capture && !thingsos_weidu_busy() && media()) {
        bool ok = thingsos_wd_rt_flush(wd_rt_capture);
        thingsos_rt_device_status_t status;
        thingsos_wd_rt_status(&status);
        wd_rt_floor = status.capture_floor;
        wd_rt_ready = ok && status.error != WD_STORAGE;
        wd_rt_problem = !wd_rt_ready;
    }
    if (!wd_rt_capture || !wd_rt_capture->count || thingsos_weidu_busy() || !media() ||
        (!force && now() < wd_rt_flush_at))
        return;
    wd_rt_flush_at = now() + 30000;
    wd_rt_problem = !thingsos_wd_rt_flush(wd_rt_capture);
}
static const char* wd_rt_hint(const thingsos_rt_device_status_t* s) {
    return wd_rt_problem         ? "本地保存失败或缓存已满，请保留 SD 卡"
        : wd_rt_clock_problem ? "时间未校准或发生回拨，暂停新时长采集"
        : s->queue.damaged     ? "队列损坏，原始记录保留"
        : s->queue.full        ? "待发队列已满，请先同步"
        : s->queue.quarantined ? "小时超量记录已隔离保留，正常记录继续同步"
        : s->blocked           ? "云端位置变化，请先在微信读书核对"
        /* A failed first authorization is still unauthorized. Show its error
           before the generic invitation, otherwise the failure disappears. */
        : s->error==WD_AUTH    ? (s->authorized?"时长通道授权失效，请重新扫码":"时长通道授权失败，请重新扫码")
        : s->error==WD_CANCEL&&app.weidu_action==WD_DEVICE_RT_AUTH ? "二维码已过期或授权已取消，请重试"
        : s->error != WD_OK    ? thingsos_weidu_error_text(s->error)
        : s->expired           ? "授权失效，待发时长保留"
        : !s->authorized       ? "在下方授权离线补传，已有授权自动复用"
                              : "联网后自动补传，历史时长按原小时保存";
}
static void wd_rt_auth_finished(const thingsos_weidu_status_t* completed) {
    /* Authorization updates channel availability, never the parent's switch. */
    (void)completed;app.wd_rt_auth_pending=false;app.dirty=true;
}
static void wd_rt_authorize(void) {
    if(!app.weidu.authenticated) {say("请先登录微读，再授权离线补传");return;}
    app.wd_rt_auth_pending=true;
    if(!weidu_begin(WD_DEVICE_RT_AUTH,NULL))app.wd_rt_auth_pending=false;
}
static void rt_fade(int x, int y, int w, int h, unsigned keep) {
    if (app.ui_refresh_mode == 1 || !app.canvas.pixels) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > app.canvas.width) w = app.canvas.width - x;
    if (y + h > app.canvas.height) h = app.canvas.height - y;
    for (int j = y; j < y + h; ++j)
        for (int i = x; i < x + w; ++i) {
            unsigned char* p = app.canvas.pixels + (size_t)j * app.canvas.width + i;
            *p = (unsigned char)(255 - ((255 - *p) * keep) / 256);
        }
}
static void rt_card(int x, int y, int w, int h, int radius, int border, bool shadow, bool mono) {
    if (shadow) {
        thingsos_round_rect(&app.canvas, x + 5, y + 6, w, h, radius, mono ? 0 : 170);
        if (mono)
            for (int py = y + 6; py < y + 6 + h && py < app.canvas.height; ++py)
                for (int px = x + 5; px < x + 5 + w && px < app.canvas.width; ++px)
                    if (py >= 0 && ((px + py) & 1))
                        app.canvas.pixels[(size_t)py * app.canvas.width + px] = 255;
    }
    thingsos_round_rect(&app.canvas, x, y, w, h, radius, 0);
    thingsos_round_rect(&app.canvas, x + border, y + border, w - 2 * border, h - 2 * border,
                        radius > border ? radius - border : 0, 255);
}
static int rt_pill(int x, int y, int h, const char* text, int px) {
    const int pad = 16, max_w = 240;
    label(text, x + pad, y + h - 8, px, max_w - 2 * pad);
    int ink_right = x + pad;
    for (int py = y; py < y + h && py < app.canvas.height; ++py) {
        if (py < 0) continue;
        for (int pxx = x + pad; pxx < x + max_w && pxx < app.canvas.width; ++pxx)
            if (app.canvas.pixels[(size_t)py * app.canvas.width + pxx] < 160 && pxx + 1 > ink_right)
                ink_right = pxx + 1;
    }
    int pill_w = ink_right - x + pad;
    if (pill_w < h + 16) pill_w = h + 16;
    if (pill_w > max_w) pill_w = max_w;
    canvas_invert_pill(x, y, pill_w, h);
    return pill_w;
}
/* Right-aligned variant: keeps the pill inside a card's right padding. The
   left-anchored pills at x=524/474 ran past the 612 px cards (also in the Pico
   0.4.3 source render). */
static int rt_pill_right(int right, int y, int h, const char* text, int px) {
    enum { PAD = 16, MAX_W = 240, MAX_H = 48 };
    /* Measure the text alone in an off-screen strip (PSRAM, not the main
       stack): ink already on the page (e.g. a step subtitle) must not widen it. */
    static EXT_RAM_BSS_ATTR unsigned char strip[MAX_W * MAX_H];
    if (h > MAX_H) h = MAX_H;
    thingsos_canvas_t measure = { strip, MAX_W, h, false, false };
    memset(strip, 255, sizeof(strip));
    thingsos_font_size(ui_font(), px);
    thingsos_label_right(ui_font(), &measure, text, PAD, h - 8, MAX_W - 2 * PAD);
    int ink_left = MAX_W - PAD;
    for (int py = 0; py < h; ++py)
        for (int pxx = PAD; pxx < ink_left; ++pxx)
            if (strip[(size_t)py * MAX_W + pxx] < 160) { ink_left = pxx; break; }
    int pill_w = MAX_W - ink_left + PAD;
    if (pill_w < h + 16) pill_w = h + 16;
    if (pill_w > MAX_W) pill_w = MAX_W;
    /* Same placement on the page, then invert into a pill. */
    thingsos_rect(&app.canvas, right - pill_w, y, pill_w, h, 255);
    thingsos_font_size(ui_font(), px);
    thingsos_label_right(ui_font(), &app.canvas, text, right - MAX_W + PAD, y + h - 8, MAX_W - 2 * PAD);
    canvas_invert_pill(right - pill_w, y, pill_w, h);
    return pill_w;
}
static void rt_step_badge(int x, int y, int d, unsigned step, bool done, bool active, bool mono) {
    int r = d / 2;
    if (done) {
        thingsos_round_rect(&app.canvas, x, y, d, d, r, 0);
        /* Crisp pixel checkmark inside the solid badge. */
        int mx = x + d * 42 / 100, my = y + d * 64 / 100;
        for (int i = 0; i <= d / 7; ++i)
            thingsos_rect(&app.canvas, mx - i, my - i, 3, 3, 255);
        for (int i = 1; i <= d * 28 / 100; ++i)
            thingsos_rect(&app.canvas, mx + i, my - i, 3, 3, 255);
    } else {
        thingsos_round_rect(&app.canvas, x, y, d, d, r, active || mono ? 0 : 140);
        thingsos_round_rect(&app.canvas, x + 2, y + 2, d - 4, d - 4, r - 2, 255);
        char num[4];
        snprintf(num, sizeof(num), "%u", step);
        centered_label(num, x + 2, y + d - 9, 20, d - 4);
        if (!active) rt_fade(x, y, d, d, 160);
    }
}
static void rt_invert_round_box(int x, int y, int w, int h, int r) {
    if (!app.canvas.pixels) return;
    if (r > h / 2) r = h / 2;
    if (r > w / 2) r = w / 2;
    for (int j = 0; j < h; ++j) {
        int row = j < r ? j : (j >= h - r ? h - 1 - j : r), inset = 0;
        if (row < r) {
            int dy2 = (2 * (r - row) - 1) * (2 * (r - row) - 1);
            while (inset < r && (2 * (r - inset) - 1) * (2 * (r - inset) - 1) + dy2 > 4 * r * r) ++inset;
        }
        int py = y + j;
        if (py < 0 || py >= app.canvas.height) continue;
        for (int i = x + inset; i < x + w - inset; ++i) {
            if (i < 0 || i >= app.canvas.width) continue;
            unsigned char* p = app.canvas.pixels + (size_t)py * app.canvas.width + i;
            *p = (unsigned char)(255 - *p);
        }
    }
}
static void rt_primary_button(const char* text, int x, int y, int w, int h) {
    thingsos_rect(&app.canvas, x, y, w, h, 255);
    centered_label(text, x + 16, y + (h + 24) / 2 - 2, 26, w - 32);
    rt_invert_round_box(x, y, w, h, 14);
}
static void rt_outline_button(const char* text, int x, int y, int w, int h) {
    thingsos_round_rect(&app.canvas, x, y, w, h, 14, 0);
    thingsos_round_rect(&app.canvas, x + 2, y + 2, w - 4, h - 4, 12, 255);
    centered_label(text, x + 16, y + (h + 24) / 2 - 2, 26, w - 32);
}
static bool wd_rt_draw(void) {
    if (!app.wd_rt_panel || app.page != THINGSOS_UI_WEIDU)
        return false;
    if (app.weidu.busy && !app.weidu.background)
        return false;
    wd_rt_ui_day = wd_rt_display_day();
    thingsos_rt_device_status_t s;
    thingsos_wd_rt_status(&s);
    const bool mono = app.ui_refresh_mode == 1;
    header(app.wd_rt_details ? "待确认时长" : "离线阅读时长补传");
    label(app.wd_rt_details ? "待确认时长" : "离线阅读时长补传", 36, 154, 32, 480);
    label("返回", 550, 154, 25, 100);
    unsigned saved_scale = app.font.ui_size_percent;
    if (app.font.ui_size_percent > 115) app.font.ui_size_percent = 115;
    char text[160];
    if (app.wd_rt_details) {
        rt_card(36, 240, 612, 156, 18, 3, true, mono);
        label("待确认记录合计", 64, 290, 21, 320);
        rt_fade(64, 262, 320, 40, 160);
        rt_pill_right(620, 262, 34, "不自动重传", 19);
        snprintf(text, sizeof(text), "%u 分 %02u 秒", (unsigned)(s.queue.unknown / 60),
                 (unsigned)(s.queue.unknown % 60));
        label(text, 64, 362, 34, 480);

        rt_card(36, 424, 612, 416, 18, 2, false, mono);
        static const char* const notes[4] = {
            "这些记录已发出请求，但未收到明确回执。",
            "请在微信读书查看对应日期的阅读统计。",
            "为避免重复计时，设备不会自动重传。",
            "关闭开关或重新扫码授权均会保留记录。"
        };
        for (int i = 0; i < 4; ++i) {
            int ry = 424 + i * 104;
            if (i) thingsos_rect(&app.canvas, 60, ry, 564, 1, mono ? 0 : 190);
            rt_step_badge(64, ry + 34, 34, (unsigned)(i + 1), false, true, mono);
            label(notes[i], 112, ry + 59, 23, 508);
        }
        rt_outline_button("返回离线阅读时长补传", 36, 890, 612, 72);
        app.font.ui_size_percent = saved_scale;
        return true;
    }

    const bool switch_on = app.wd_time_on && app.wd_time_confirmed && app.wd_rt_on;
    if (!switch_on || !s.authorized) {
        rt_card(36, 238, 612, 178, 18, 3, true, mono);
        const char* pill_text = !switch_on ? (s.authorized ? "未开启" : "未配置") : "待授权";
        const char* hero_title = !switch_on
            ? (s.authorized ? "通道已授权 · 等待开启" : "需要开启并扫码授权")
            : "请扫码授权离线补传通道";
        int pill_w = rt_pill(64, 262, 34, pill_text, 19);
        label(hero_title, 64 + pill_w + 14, 288, 25, 620 - (64 + pill_w + 14));
        if (!switch_on) {
            label("离线阅读微读缓存书籍时，按原小时保存在本机，", 64, 344, 21, 556);
            label("开启并授权后，联网空闲自动补传至微信读书。", 64, 378, 21, 556);
        } else {
            label("「上传阅读时长」已开启。完成微信扫码授权后，", 64, 344, 21, 556);
            label("本机待发记录与后续离线时长会自动补传。", 64, 378, 21, 556);
        }
        rt_fade(64, 314, 556, 80, 170);

        rt_card(36, 440, 612, 324, 18, 2, false, mono);
        thingsos_rect(&app.canvas, 60, 548, 564, 1, mono ? 0 : 190);
        thingsos_rect(&app.canvas, 60, 656, 564, 1, mono ? 0 : 190);

        /* Step 1: upload reading duration switch */
        rt_step_badge(64, 440 + 36, 36, 1, switch_on, !switch_on, mono);
        label("上传阅读时长", 116, 440 + 48, 24, 340);
        label("同步页共用开关 · 重开书籍生效", 116, 440 + 84, 19, 360);
        rt_fade(116, 440 + 58, 360, 36, 160);
        thingsos_font_size(ui_font(), 20);
        thingsos_label_right(ui_font(), &app.canvas,
            switch_on ? "已开启" : (app.wd_time_on ? "待确认" : "未开启"), 476, 440 + 62, 144);
        if (!switch_on) rt_fade(476, 440 + 36, 144, 38, 170);

        /* Step 2: channel authorization */
        rt_step_badge(64, 548 + 36, 36, 2, s.authorized, switch_on && !s.authorized, mono);
        label("时长通道授权", 116, 548 + 48, 24, 340);
        label("微信扫码一次 · 已有授权自动复用", 116, 548 + 84, 19, 360);
        rt_fade(116, 548 + 58, 360, 36, 160);
        if (!s.authorized && switch_on) {
            rt_pill_right(620, 548 + 37, 34, "待扫码", 19);
        } else {
            thingsos_font_size(ui_font(), 20);
            thingsos_label_right(ui_font(), &app.canvas,
                s.authorized ? "已授权" : "未授权", 476, 548 + 62, 144);
        }

        /* Step 3: background upload queue */
        rt_step_badge(64, 656 + 36, 36, 3, false, false, mono);
        label("联网自动补传", 116, 656 + 48, 24, 340);
        snprintf(text, sizeof(text), "待上传队列：%u 分 %02u 秒",
                 (unsigned)(s.queue.pending / 60), (unsigned)(s.queue.pending % 60));
        label(text, 116, 656 + 84, 19, 360);
        rt_fade(116, 656 + 58, 360, 36, 160);
        thingsos_font_size(ui_font(), 20);
        thingsos_label_right(ui_font(), &app.canvas, "就绪后启用", 476, 656 + 62, 144);
        rt_fade(476, 656 + 36, 144, 38, 150);

        if (s.error != WD_OK || wd_rt_problem || wd_rt_clock_problem ||
            s.queue.damaged || s.queue.full) {
            centered_label(wd_rt_hint(&s), 36, 826, 22, 612);
        }
        const char* cta = !switch_on
            ? (s.authorized ? "开启上传阅读时长" : "开启并扫码授权")
            : "扫码授权离线补传";
        rt_primary_button(cta, 36, 890, 612, 72);
        centered_label("仅对启用后的微读缓存书生效 · 不补传旧总统计", 36, 1030, 20, 612);
        rt_fade(36, 1002, 612, 42, 150);
        app.font.ui_size_percent = saved_scale;
        return true;
    }

    const bool need_reauth = s.expired || s.error == WD_AUTH;
    rt_card(36, 238, 612, 166, 18, 3, true, mono);
    const char* badge = need_reauth ? "授权失效" : (s.blocked ? "已暂停" : "已就绪");
    const char* title = need_reauth ? "时长通道授权已过期"
        : (s.blocked ? "云端位置变化 · 等待核对" : "离线补传正常运行中");
    int pill_w = rt_pill(64, 262, 34, badge, 19);
    label(title, 64 + pill_w + 14, 288, 25, 620 - (64 + pill_w + 14));
    label(wd_rt_hint(&s), 64, 352, 22, 556);
    rt_fade(64, 322, 556, 44, 165);

    /* 2x2 E-Ink metrics grid */
    rt_card(36, 428, 612, 284, 18, 2, false, mono);
    thingsos_rect(&app.canvas, 342, 428, 1, 284, mono ? 0 : 185);
    thingsos_rect(&app.canvas, 36, 570, 612, 1, mono ? 0 : 185);

    /* Top-left: today uploaded */
    label("今日已上传", 62, 468, 20, 258);
    rt_fade(62, 442, 258, 34, 155);
    if (!s.today_ready || s.today_online > UINT64_MAX - s.today_native) {
        label("暂不可读取", 62, 516, 26, 258);
    } else {
        uint64_t total = s.today_online + s.today_native;
        snprintf(text, sizeof(text), "%llu 时 %02u 分",
                 (unsigned long long)(total / 3600), (unsigned)(total / 60 % 60));
        label(text, 62, 516, 28, 258);
        snprintf(text, sizeof(text), "%02u 秒 · 在线+离线", (unsigned)(total % 60));
        label(text, 62, 550, 18, 258);
        rt_fade(62, 526, 258, 32, 150);
    }

    /* Top-right: cumulative offline uploaded */
    label("补传累计已上传", 368, 468, 20, 258);
    rt_fade(368, 442, 258, 34, 155);
    if (!s.uploaded_ready) {
        label("暂不可读取", 368, 516, 26, 258);
    } else {
        snprintf(text, sizeof(text), "%llu 时 %02u 分",
                 (unsigned long long)(s.uploaded / 3600), (unsigned)(s.uploaded / 60 % 60));
        label(text, 368, 516, 28, 258);
        snprintf(text, sizeof(text), "%02u 秒 · 历史补传", (unsigned)(s.uploaded % 60));
        label(text, 368, 550, 18, 258);
        rt_fade(368, 526, 258, 32, 150);
    }

    /* Bottom-left: pending queue */
    label("待上传队列", 62, 610, 20, 258);
    rt_fade(62, 584, 258, 34, 155);
    snprintf(text, sizeof(text), "%u 分 %02u 秒", (unsigned)(s.queue.pending / 60),
             (unsigned)(s.queue.pending % 60));
    label(text, 62, 658, 28, 258);
    label(need_reauth ? "重新授权后发送" : "联网空闲自动发送", 62, 692, 18, 258);
    rt_fade(62, 668, 258, 32, 150);

    /* Bottom-right: unknown / unconfirmed */
    label("待确认记录", 368, 610, 20, 258);
    rt_fade(368, 584, 258, 34, 155);
    snprintf(text, sizeof(text), "%u 分 %02u 秒", (unsigned)(s.queue.unknown / 60),
             (unsigned)(s.queue.unknown % 60));
    label(text, 368, 658, 28, 258);
    label(s.queue.unknown ? "点此查看说明 ›" : "无待确认记录", 368, 692, 18, 258);
    rt_fade(368, 668, 258, 32, s.queue.unknown ? 180 : 145);

    label("• 关闭开关会暂停采集和补传，队列保留。", 40, 760, 21, 604);
    label("• 修改「上传阅读时长」开关后，重开微读书籍生效。", 40, 798, 21, 604);
    rt_fade(40, 732, 604, 82, 165);

    if (need_reauth) {
        rt_primary_button("重新扫码授权", 36, 890, 612, 72);
    } else if (s.blocked) {
        rt_primary_button("已核对云端位置，继续补传", 36, 890, 612, 72);
    } else if (s.queue.unknown) {
        rt_outline_button("查看待确认时长说明", 36, 890, 612, 72);
    }

    centered_label("仅对启用后的微读缓存书生效 · 不补传旧总统计", 36, 1030, 20, 612);
    rt_fade(36, 1002, 612, 42, 150);
    app.font.ui_size_percent = saved_scale;
    return true;
}
static bool wd_rt_tap(int x, int y) {
    if (!app.wd_rt_panel || app.page != THINGSOS_UI_WEIDU ||
        (app.weidu.busy && !app.weidu.background))
        return false;
    if (y >= 100 && y < 195 && (x < 112 || x >= 548)) {
        if (app.wd_rt_details) app.wd_rt_details = false;
        else {
            app.wd_rt_panel = false;
            app.transition_refresh = true;
        }
        app.dirty = true;
        return true;
    }
    if (app.wd_rt_details) {
        if (y >= 890 && y < 962 && x >= 36 && x < 648) {
            app.wd_rt_details = false;
            app.dirty = true;
        }
        return true;
    }
    thingsos_rt_device_status_t s;
    thingsos_wd_rt_status(&s);
    const bool switch_on = app.wd_time_on && app.wd_time_confirmed && app.wd_rt_on;
    if (switch_on && s.authorized && !s.expired && s.error != WD_AUTH &&
        s.queue.unknown && x >= 342 && x < 648 && y >= 570 && y < 712) {
        app.wd_rt_details = true;
        app.dirty = true;
        return true;
    }
    if (y >= 890 && y < 962 && x >= 36 && x < 648) {
        if (!switch_on) {
            bool prev_confirmed = app.wd_time_confirmed;
            app.wd_time_confirmed = true;
            if (!wd_rt_set(true)) {
                app.wd_time_confirmed = prev_confirmed;
            } else if (!s.authorized || s.expired || s.error == WD_AUTH) {
                wd_rt_authorize();
            }
            return true;
        }
        if (!s.authorized || s.expired || s.error == WD_AUTH) {
            wd_rt_authorize();
        } else if (s.blocked) {
            wd_rt_checkpoint(true);
            if (thingsos_wd_rt_resume()) say("已确认，联网空闲后自动继续补传");
            else say("微读正在处理，请稍后重试");
            app.dirty = true;
        } else if (s.queue.unknown) {
            app.wd_rt_details = true;
            app.dirty = true;
        }
    }
    return true;
}
