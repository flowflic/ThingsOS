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
static const char* wd_rt_action(const thingsos_rt_device_status_t* s) {
    if(app.wd_rt_on && (!s->authorized || s->expired || s->error==WD_AUTH))
        return s->authorized?"重新扫码授权":"授权离线补传";
    if(s->blocked)return "已核对云端位置，继续补传";
    if(s->queue.unknown)return "查看待确认时长说明";
    return NULL;
}
static bool wd_rt_draw(void) {
    if (!app.wd_rt_panel || app.page != THINGSOS_UI_WEIDU)
        return false;
    if (app.weidu.busy && !app.weidu.background)
        return false;
    wd_rt_ui_day=wd_rt_display_day();
    thingsos_rt_device_status_t s;
    thingsos_wd_rt_status(&s);
    char text[160];
    header("阅读时长明细");
    label(app.wd_rt_details?"待确认时长":"阅读时长明细",36,154,32,480);
    label("返回", 550, 154, 25, 100);
    if(app.wd_rt_details) {
        snprintf(text,sizeof(text),"待确认 %u 分 %02u 秒",(unsigned)(s.queue.unknown/60),
                 (unsigned)(s.queue.unknown%60));
        label(text,36,285,28,612);
        label("这些记录已发送，但未收到明确回执。",36,400,24,612);
        label("请在微信读书查看对应日期的阅读统计。",36,460,24,612);
        label("为避免重复计时，设备不会自动重传。",36,550,24,612);
        label("关闭开关或重新授权均会保留这些记录。",36,610,24,612);
        return true;
    }
    snprintf(text,sizeof(text),"上传阅读时长（含离线）：%s",app.wd_time_on?(app.wd_time_confirmed?"开":"待确认"):"关");
    label(text,36,285,28,612);
    label("在线和离线时长共用同步页开关",36,370,23,612);
    snprintf(text, sizeof(text), "待上传 %u 分 %02u 秒", (unsigned)(s.queue.pending / 60),
             (unsigned)(s.queue.pending % 60));
    label(text, 36, 480, 27, 612);
    snprintf(text, sizeof(text), "待确认 %u 分 %02u 秒", (unsigned)(s.queue.unknown / 60),
             (unsigned)(s.queue.unknown % 60));
    label(text, 36, 538, 27, 612);
    if(!s.uploaded_ready)strlcpy(text,"补传累计 暂不可读取",sizeof(text));
    else snprintf(text,sizeof(text),"补传累计已上传 %llu 小时 %02u 分 %02u 秒",
        (unsigned long long)(s.uploaded/3600),(unsigned)(s.uploaded/60%60),(unsigned)(s.uploaded%60));
    label(text,36,596,27,612);
    wd_rt_uploaded_text(text,sizeof(text),&s);label(text,36,639,21,612);
    const char *hint = wd_rt_hint(&s);
    label(hint, 36, 680, 24, 612);
    label("关闭开关会暂停采集和补传，队列保留。",36,735,22,612);
    label("修改后请重新打开微读书籍，使设置生效。", 36, 790, 23, 612);
    const char* action=wd_rt_action(&s);
    if(action)weidu_button(action,36,830,612);
    label("仅对启用后的微读书籍生效，不补传旧总统计。", 36, 990, 22, 612);
    return true;
}
static bool wd_rt_tap(int x, int y) {
    if (!app.wd_rt_panel || app.page != THINGSOS_UI_WEIDU ||
        (app.weidu.busy && !app.weidu.background))
        return false;
    if (y >= 100 && y < 195 && (x < 112 || x >= 548)) {
        if(app.wd_rt_details)app.wd_rt_details=false;
        else app.wd_rt_panel = false;
        app.dirty = true;
        return true;
    }
    if(app.wd_rt_details)return true;
    if (y >= 830 && y < 898) {
        thingsos_rt_device_status_t s;
        thingsos_wd_rt_status(&s);
        if(app.wd_rt_on && (!s.authorized || s.expired || s.error==WD_AUTH))
            wd_rt_authorize();
        else if(s.blocked) {
            if(!app.wd_rt_on || !app.wd_time_on || !app.wd_time_confirmed) {
                say("请先启用阅读时长与离线补传");
                return true;
            }
            wd_rt_checkpoint(true);
            if(thingsos_wd_rt_resume())say("已确认，联网空闲后自动继续补传");
            else say("微读正在处理，请稍后重试");
            app.dirty=true;
        } else if(s.queue.unknown) {app.wd_rt_details=true;app.dirty=true;}
    }
    return true;
}
