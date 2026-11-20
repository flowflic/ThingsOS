// SPDX-License-Identifier: Apache-2.0
/* Cubox e-ink UI controller. Runs strictly on the main UI task under thingsos_io_lock. */
typedef enum {
    CUBOX_VIEW_LIST = 0,
    CUBOX_VIEW_DETAIL = 1,
    CUBOX_VIEW_FOLDERS = 2,
    CUBOX_VIEW_PAIR = 3,
    CUBOX_VIEW_SETTINGS = 4
} cubox_view_t;

typedef struct {
    cubox_view_t view;
    cubox_view_t pair_return;
    thingsos_cubox_status_t status;
    thingsos_cubox_detail_t detail;
    thingsos_cubox_card_t scratch_card;
    thingsos_cubox_folder_t scratch_folder;
    bool detail_valid;
    bool open_after_download;
    bool loaded_once;
    bool painted;
    bool painted_controls;
    thingsos_ui_page_t painted_page;
    unsigned list_start;
    unsigned folder_start;
    unsigned last_serial;
    unsigned pair_generation;
    uint64_t poll_tick;
    char active_id[THINGSOS_CUBOX_ID_LEN];
} cubox_ui_t;

static cubox_ui_t* cubox_ui;

static bool cubox_ready(void) {
    if (!cubox_ui) cubox_ui = thingsos_psram_calloc(1, sizeof(*cubox_ui));
    return cubox_ui != NULL;
}

static bool app_cubox_region(void) {
    return app.page == THINGSOS_UI_CUBOX && cubox_ready() &&
        cubox_ui->painted && cubox_ui->painted_page == app.page &&
        !app.controls_open && !cubox_ui->painted_controls &&
        !app.controls_refresh && !app.feedback_refresh &&
        !app.force_refresh && !app.transition_refresh &&
        !app.lock_wave_pending && !app.message[0];
}

static void app_cubox_presented(bool ok) {
    if (!cubox_ui) return;
    if (app.page == THINGSOS_UI_CUBOX && !app.controls_open) {
        cubox_ui->painted = ok;
        cubox_ui->painted_page = ok ? app.page : (thingsos_ui_page_t)0;
    } else if (!ok || app.page != THINGSOS_UI_CUBOX) {
        cubox_ui->painted = false;
        cubox_ui->painted_page = (thingsos_ui_page_t)0;
    }
    cubox_ui->painted_controls = app.controls_open;
}

static esp_err_t app_cubox_present(void) {
    return thingsos_board_present_metrics(&app.canvas);
}

static void cubox_pair_link(char* link, size_t cap) {
    char address[96];
    thingsos_network_address(false, address, sizeof(address));
    const char* host = strstr(address, "://");
    if (strncmp(address, "http://", 7)) {
        link[0] = 0;
        return;
    }
    snprintf(link, cap, "http://%s:8082/", host ? host + 3 : address);
}

static bool cubox_open_article(const char* card_id, const char* fallback_title, const char* fallback_author) {
    if (!media()) {
        say("请先插入 SD 卡");
        return false;
    }
    char path[192];
    if (!thingsos_cubox_article_path(card_id, path, sizeof(path))) {
        say("本地尚未缓存正文");
        return false;
    }

    thingsos_cubox_detail_t* d = thingsos_psram_calloc(1, sizeof(*d));
    const char* title = fallback_title && fallback_title[0] ? fallback_title : "Cubox 文章";
    const char* author = fallback_author ? fallback_author : "";
    if (d && thingsos_cubox_detail_copy(card_id, d)) {
        if (d->card.title[0]) title = d->card.title;
        else if (d->card.article_title[0]) title = d->card.article_title;
        if (d->author[0]) author = d->author;
        else if (d->card.domain[0]) author = d->card.domain;
    }

    book_t saved = app.books[0];
    size_t count = app.book_count;
    memset(&app.books[0], 0, sizeof(app.books[0]));
    strlcpy(app.books[0].path, path, sizeof(app.books[0].path));
    strlcpy(app.books[0].name, title, sizeof(app.books[0].name));
    strlcpy(app.books[0].author, author, sizeof(app.books[0].author));
    app.books[0].id = identity(path);
    if (!count) app.book_count = 1;

    bool ok = open_book(0);
    app.books[0] = saved;
    app.book_count = count;
    app.library_valid = false;
    free(d);
    if (ok) {
        app.reader_from_cubox = true;
        if (cubox_ui) {
            cubox_ui->painted = false;
            cubox_ui->painted_page = (thingsos_ui_page_t)0;
        }
    } else {
        say("打开文章失败");
    }
    return ok;
}

static void cubox_enter(void) {
    if (!cubox_ready()) return;
    thingsos_cubox_status(&cubox_ui->status);
    cubox_ui->last_serial = cubox_ui->status.serial;
    cubox_ui->open_after_download = false;

    if (!cubox_ui->loaded_once) {
        thingsos_cubox_load_offline();
        thingsos_cubox_status(&cubox_ui->status);
        cubox_ui->loaded_once = true;
        if (cubox_ui->status.configured && thingsos_network_connected() && !cubox_ui->status.busy) {
            if (!cubox_ui->status.folder_count) thingsos_cubox_refresh_folders();
            else if (!cubox_ui->status.card_count) thingsos_cubox_refresh_list(false);
        }
    }

    if (cubox_ui->view == CUBOX_VIEW_DETAIL && cubox_ui->active_id[0]) {
        cubox_ui->detail_valid = thingsos_cubox_detail_copy(cubox_ui->active_id, &cubox_ui->detail);
    }
    if (app.page != THINGSOS_UI_CUBOX) {
        cubox_ui->painted = false;
        cubox_ui->painted_page = (thingsos_ui_page_t)0;
    }
    navigate(THINGSOS_UI_CUBOX);
}

static void cubox_open_detail(const thingsos_cubox_card_t* card) {
    if (!cubox_ready() || !card) return;
    strlcpy(cubox_ui->active_id, card->id, sizeof(cubox_ui->active_id));
    cubox_ui->open_after_download = false;
    cubox_ui->detail_valid = thingsos_cubox_detail_copy(card->id, &cubox_ui->detail);
    if (!cubox_ui->detail_valid) {
        memset(&cubox_ui->detail, 0, sizeof(cubox_ui->detail));
        cubox_ui->detail.card = *card;
        cubox_ui->detail_valid = true;
    }
    cubox_ui->view = CUBOX_VIEW_DETAIL;
    app.dirty = true;

    if (thingsos_network_connected() && cubox_ui->status.configured && !cubox_ui->status.busy &&
        !cubox_ui->detail.has_insight && !cubox_ui->detail.annotation_count) {
        thingsos_cubox_fetch_detail(card->id);
    }
}

static void cubox_search_submit(void) {
    if (!cubox_ready()) return;
    cubox_ui->list_start = 0;
    cubox_ui->view = CUBOX_VIEW_LIST;
    thingsos_cubox_set_filter(
        cubox_ui->status.filter,
        cubox_ui->status.folder_id,
        cubox_ui->status.folder_name,
        app.cubox_query,
        true);
    app.dirty = true;
}

static void cubox_start_pair(cubox_view_t ret) {
    if (!cubox_ready()) return;
    thingsos_cubox_status(&cubox_ui->status);
    cubox_ui->pair_return = ret;
    cubox_ui->pair_generation = cubox_ui->status.connection_serial;
    cubox_ui->view = CUBOX_VIEW_PAIR;
    thingsos_cubox_pair(true);
    thingsos_cubox_status(&cubox_ui->status);
    app.dirty = true;
}

static void cubox_poll(void) {
    if (app.page != THINGSOS_UI_CUBOX || !cubox_ready()) return;

    uint64_t t = now();
    thingsos_cubox_status_t prev = cubox_ui->status;
    thingsos_cubox_status(&cubox_ui->status);
    bool changed = cubox_ui->status.serial != cubox_ui->last_serial;

    if (cubox_ui->view == CUBOX_VIEW_PAIR && cubox_ui->status.pairing && t - cubox_ui->poll_tick >= 1000) {
        cubox_ui->poll_tick = t;
        app.dirty = true;
    }
    if (!changed) return;

    cubox_ui->last_serial = cubox_ui->status.serial;
    if (cubox_ui->view == CUBOX_VIEW_PAIR &&
        cubox_ui->status.config_phase == THINGSOS_CUBOX_CONFIG_READY &&
        cubox_ui->status.connection_serial != cubox_ui->pair_generation) {
        cubox_ui->view = CUBOX_VIEW_LIST;
        cubox_ui->list_start = 0;
    }

    if (cubox_ui->view == CUBOX_VIEW_DETAIL && cubox_ui->active_id[0]) {
        bool was_dl = prev.busy && prev.op == THINGSOS_CUBOX_OP_DOWNLOAD;
        bool now_idle = !cubox_ui->status.busy;
        cubox_ui->detail_valid = thingsos_cubox_detail_copy(cubox_ui->active_id, &cubox_ui->detail);
        if (was_dl && now_idle && cubox_ui->open_after_download) {
            cubox_ui->open_after_download = false;
            if (cubox_ui->detail.card.cached) {
                cubox_open_article(
                    cubox_ui->active_id,
                    cubox_ui->detail.card.title,
                    cubox_ui->detail.author[0] ? cubox_ui->detail.author : cubox_ui->detail.card.domain);
                return;
            }
        }
        if (prev.busy && prev.op == THINGSOS_CUBOX_OP_ARCHIVE && now_idle && cubox_ui->detail.card.archived) {
            cubox_ui->view = CUBOX_VIEW_LIST;
        }
    }

    app.dirty = true;
}

static bool cubox_draw_list(const thingsos_cubox_status_t* s) {
    header("Cubox");
    if (s->configured) {
        reader_control(s->busy ? "同步中" : "刷新", 460, 124, 90, false);
        reader_control("设置", 558, 124, 90, false);
    }

    if (!s->configured && !s->card_count) {
        centered_label("连接你的 Cubox", 36, 330, 36, 612);
        centered_label("浏览未读、全部、星标收藏与文件夹，离线图文阅读", 36, 405, 24, 612);
        reader_control("手机扫码配对", 36, 520, 612, true);
        centered_label("支持 cubox.pro 与 cubox.cc API 扩展链接或 Token", 36, 650, 22, 612);
        if (s->message[0]) centered_label(s->message, 36, 730, 22, 612);
        nav();
        return true;
    }

    reader_control("未读", 36, 226, 134, s->filter == THINGSOS_CUBOX_FILTER_UNREAD);
    reader_control("全部", 180, 226, 134, s->filter == THINGSOS_CUBOX_FILTER_ALL);
    reader_control("星标", 324, 226, 134, s->filter == THINGSOS_CUBOX_FILTER_STARRED);
    reader_control(app.cubox_query[0] ? "搜索*" : "搜索", 472, 226, 176, app.cubox_query[0] != 0);

    char folder_bar[300];
    if (app.cubox_query[0]) {
        snprintf(folder_bar, sizeof(folder_bar), "目录：%s · 词：%s", s->folder_name, app.cubox_query);
    } else {
        snprintf(folder_bar, sizeof(folder_bar), "目录：%s >", s->folder_name[0] ? s->folder_name : "全部文件夹");
    }
    label(folder_bar, 36, 322, 23, 450);
    label(
        s->busy ? "同步中…" : (!thingsos_network_connected() || s->offline_mode) ? "离线缓存" : "在线",
        500, 322, 22, 148);
    thingsos_rect(&app.canvas, 36, 342, 612, 1, 180);

    if (cubox_ui->list_start >= s->card_count) cubox_ui->list_start = 0;

    if (!s->card_count) {
        centered_label(
            s->busy ? "正在获取 Cubox 收藏…"
                    : (s->message[0] ? s->message : "当前筛选下没有收藏"),
            36, 540, 27, 612);
        if (app.cubox_query[0]) {
            reader_control("清空搜索关键词", 162, 620, 360, false);
        }
    } else {
        for (unsigned row = 0; row < 4 && cubox_ui->list_start + row < s->card_count; ++row) {
            thingsos_cubox_card_t* card = &cubox_ui->scratch_card;
            if (!thingsos_cubox_card(cubox_ui->list_start + row, card)) continue;
            int y = 352 + (int)row * 160;
            char title_line[320];
            const char* raw_title = card->title[0] ? card->title
                                  : card->article_title[0] ? card->article_title
                                  : "未命名收藏";
            snprintf(
                title_line, sizeof(title_line), "%s%s%s",
                card->starred ? "★ " : "",
                card->read ? "" : "● ",
                raw_title);
            thingsos_font_size(ui_font(), 26);
            thingsos_text_string_page(ui_font(), &app.canvas, title_line, strlen(title_line), 0, 36, y + 8, 612, 68, 36);

            char date10[12] = "";
            if (card->create_time[0]) {
                strlcpy(date10, card->create_time, sizeof(date10));
            }
            char meta[320];
            snprintf(
                meta, sizeof(meta), "%s%s%s%s%s%s",
                card->domain[0] ? card->domain : "网页",
                card->folder_name[0] ? " · " : "",
                card->folder_name,
                card->cached ? " · [已离线]" : "",
                date10[0] ? " · " : "",
                date10);
            label(meta, 36, y + 106, 21, 612);

            if (card->description[0]) {
                label(card->description, 36, y + 138, 20, 612);
            } else if (card->tag_summary[0]) {
                label(card->tag_summary, 36, y + 138, 20, 612);
            }
            thingsos_rect(&app.canvas, 36, y + 154, 612, 1, 200);
        }
    }

    unsigned total_pages = s->card_count ? (s->card_count + 3u) / 4u : 1u;
    unsigned cur_page = cubox_ui->list_start / 4u + 1u;
    bool at_last = cubox_ui->list_start + 4u >= s->card_count;
    char pstr[48];
    snprintf(pstr, sizeof(pstr), "%u / %u", cur_page, total_pages);
    reader_control("上一页", 36, 1006, 190, false);
    centered_label(pstr, 236, 1046, 24, 212);
    reader_control(
        (at_last && s->has_more && thingsos_network_connected()) ? "加载更多" : "下一页",
        458, 1006, 190, false);

    if (s->message[0]) label(s->message, 36, 1095, 20, 612);
    nav();
    return true;
}

static void cubox_draw_detail(const thingsos_cubox_status_t* s) {
    header("文章详情");
    const thingsos_cubox_detail_t* d = &cubox_ui->detail;
    const thingsos_cubox_card_t* c = &d->card;
    const char* title = c->title[0] ? c->title : c->article_title[0] ? c->article_title : "未命名收藏";

    thingsos_font_size(ui_font(), 30);
    thingsos_text_string_page(ui_font(), &app.canvas, title, strlen(title), 0, 36, 224, 612, 108, 42);

    char meta1[256];
    snprintf(
        meta1, sizeof(meta1), "来源：%s%s%s",
        c->domain[0] ? c->domain : "未知",
        d->author[0] ? " · 作者：" : "",
        d->author);
    label(meta1, 36, 360, 22, 612);

    char meta2[256];
    snprintf(
        meta2, sizeof(meta2), "目录：%s · %s · %s",
        c->folder_name[0] ? c->folder_name : "未分类",
        c->read ? "已读" : "未读",
        c->starred ? "★ 已星标" : "未星标");
    label(meta2, 36, 396, 22, 612);

    char cache_line[160];
    if (c->cached) {
        snprintf(
            cache_line, sizeof(cache_line), "本地已缓存：%u KB · 图片 %u 张",
            (unsigned)((c->cached_bytes + 1023u) / 1024u),
            (unsigned)c->cached_images);
    } else {
        strlcpy(cache_line, "正文未缓存到本地 SD 卡", sizeof(cache_line));
    }
    label(cache_line, 36, 432, 22, 612);
    thingsos_rect(&app.canvas, 36, 452, 612, 1, 180);

    const char* preview = d->summary[0] ? d->summary
                        : c->description[0] ? c->description
                        : "点击下方按钮下载图文正文并在阅读器中打开。";
    const char* ptitle = d->summary[0] ? "AI 摘要" : "内容简介";
    label(ptitle, 36, 488, 24, 612);
    thingsos_font_size(ui_font(), 24);
    thingsos_text_string_page(ui_font(), &app.canvas, preview, strlen(preview), 0, 36, 504, 612, 210, 34);

    if (d->annotation_count > 0) {
        char ann_info[64];
        snprintf(ann_info, sizeof(ann_info), "包含 %u 条标注与笔记", d->annotation_count);
        label(ann_info, 36, 742, 22, 612);
    }

    bool downloading_this = s->busy && s->op == THINGSOS_CUBOX_OP_DOWNLOAD && !strcmp(s->active_card_id, c->id);
    char pri[64];
    if (downloading_this) {
        if (s->dl_total > 0) snprintf(pri, sizeof(pri), "下载图片 %u/%u (点击取消)", s->dl_done, s->dl_total);
        else strlcpy(pri, "正在下载正文… (点击取消)", sizeof(pri));
    } else if (c->cached) {
        strlcpy(pri, "开始阅读", sizeof(pri));
    } else {
        strlcpy(pri, "下载正文与图片并阅读", sizeof(pri));
    }
    reader_control(pri, 36, 770, 612, true);

    reader_control(c->read ? "标为未读" : "标为已读", 36, 868, 196, false);
    reader_control(c->starred ? "取消星标" : "加星标", 244, 868, 196, false);
    reader_control("归档", 452, 868, 196, false);

    reader_control(c->cached ? "更新缓存" : "刷新详情", 36, 966, 298, false);
    reader_control(c->cached ? "删除缓存" : "返回列表", 350, 966, 298, false);

    if (s->message[0]) label(s->message, 36, 1080, 21, 612);
    nav();
}

static void cubox_draw_folders(const thingsos_cubox_status_t* s) {
    header("选择文件夹");
    reader_control(s->busy ? "同步中…" : "同步文件夹列表", 36, 226, 612, false);

    unsigned total_items = 1u + s->folder_count;
    if (cubox_ui->folder_start >= total_items) cubox_ui->folder_start = 0;

    for (unsigned row = 0; row < 5 && cubox_ui->folder_start + row < total_items; ++row) {
        unsigned idx = cubox_ui->folder_start + row;
        int y = 312 + (int)row * 130;
        char title[200];
        bool selected = false;
        if (idx == 0) {
            strlcpy(title, "全部文件夹", sizeof(title));
            selected = s->folder_id[0] == 0;
        } else {
            thingsos_cubox_folder_t* f = &cubox_ui->scratch_folder;
            if (thingsos_cubox_folder(idx - 1u, f)) {
                const char* fn = f->nested_name[0] ? f->nested_name : f->name;
                snprintf(title, sizeof(title), "%s%s", fn[0] ? fn : "未命名", f->uncategorized ? " (未分类)" : "");
                selected = !strcmp(s->folder_id, f->id);
            } else {
                title[0] = 0;
            }
        }
        if (selected) thingsos_rect(&app.canvas, 36, y + 6, 6, 92, 0);
        label(title, 54, y + 62, 28, 550);
        label(">", 615, y + 62, 26, 30);
        thingsos_rect(&app.canvas, 36, y + 116, 612, 1, 190);
    }

    reader_control("上一页", 36, 1006, 292, false);
    reader_control("下一页", 356, 1006, 292, false);
    if (s->message[0]) label(s->message, 36, 1095, 20, 612);
    nav();
}

static void cubox_draw_pair(const thingsos_cubox_status_t* s) {
    header("连接 Cubox");
    char line[256];
    char link[128];
    cubox_pair_link(link, sizeof(link));

    centered_label("手机与设备连接同一 WiFi", 36, 265, 26, 612);
    if (s->config_phase == THINGSOS_CUBOX_CONFIG_VERIFYING || s->config_phase == THINGSOS_CUBOX_CONFIG_FAILED) {
        bool verifying = s->config_phase == THINGSOS_CUBOX_CONFIG_VERIFYING;
        centered_label(verifying ? "正在验证 Cubox…" : "Cubox 连接未完成", 36, 370, 34, 612);
        thingsos_font_size(ui_font(), 26);
        thingsos_text_string_page(ui_font(), &app.canvas, s->message, strlen(s->message), 0, 48, 430, 588, 200, 38);
        if (!verifying) reader_control("返回配对二维码", 36, 868, 612, false);
        reader_control("关闭手机配对", 36, 974, 612, false);
    } else if (!s->pairing || !link[0]) {
        centered_label("手机配对尚未就绪", 36, 380, 30, 612);
        label(link[0] ? "服务启动后将显示配对二维码" : "请先连接 WiFi", 36, 440, 24, 612);
        label(s->message, 36, 520, 23, 612);
        reader_control("重新打开手机配对", 36, 868, 612, false);
        reader_control("关闭手机配对", 36, 974, 612, false);
    } else {
        qr_address_box(link, 300, 300);
        centered_label(link, 36, 640, 26, 612);
        centered_label("扫码配置 Cubox API 链接，无需解锁密码", 36, 690, 24, 612);
        snprintf(line, sizeof(line), "配对码：%s", s->code);
        centered_label(line, 36, 765, 38, 612);
        snprintf(line, sizeof(line), "剩余 %u 秒", s->pair_seconds);
        centered_label(line, 36, 810, 24, 612);
        reader_control("刷新配对码", 36, 868, 612, false);
        reader_control("关闭手机配对", 36, 974, 612, false);
    }
    nav();
}

static void cubox_draw_settings(const thingsos_cubox_status_t* s) {
    header("Cubox 设置");
    setting_row("连接状态", s->configured ? "已连接" : "未连接", 260);
    setting_row("服务器", s->server[0] ? s->server : "cubox.pro", 365);
    char cnt[48];
    snprintf(cnt, sizeof(cnt), "%u 篇收藏 · %u 个文件夹", s->card_count, s->folder_count);
    setting_row("同步数据", cnt, 470);
    setting_row("本地缓存", "/sdcard/.thingsos/cubox", 575);

    reader_control(s->configured ? "更新连接 / 手机配对" : "手机扫码配对", 36, 745, 612, true);
    if (s->configured) reader_control("断开 Cubox 连接", 36, 850, 612, false);
    reader_control("返回列表", 36, 955, 612, false);
    if (s->message[0]) label(s->message, 36, 1080, 22, 612);
    nav();
}

static bool cubox_draw(void) {
    if (app.page != THINGSOS_UI_CUBOX) return false;
    if (!cubox_ready()) return false;
    thingsos_cubox_status(&cubox_ui->status);
    cubox_ui->last_serial = cubox_ui->status.serial;
    const thingsos_cubox_status_t* s = &cubox_ui->status;

    if (cubox_ui->view == CUBOX_VIEW_DETAIL) cubox_draw_detail(s);
    else if (cubox_ui->view == CUBOX_VIEW_FOLDERS) cubox_draw_folders(s);
    else if (cubox_ui->view == CUBOX_VIEW_PAIR) cubox_draw_pair(s);
    else if (cubox_ui->view == CUBOX_VIEW_SETTINGS) cubox_draw_settings(s);
    else cubox_draw_list(s);
    return true;
}

static bool cubox_back(void) {
    if (!cubox_ready()) return false;
    if (cubox_ui->view == CUBOX_VIEW_PAIR) {
        thingsos_cubox_pair(false);
        cubox_ui->view = cubox_ui->pair_return;
    } else if (cubox_ui->view == CUBOX_VIEW_DETAIL || cubox_ui->view == CUBOX_VIEW_FOLDERS ||
               cubox_ui->view == CUBOX_VIEW_SETTINGS) {
        cubox_ui->open_after_download = false;
        cubox_ui->view = CUBOX_VIEW_LIST;
    } else {
        navigate(THINGSOS_UI_APPS);
        return true;
    }
    app.dirty = true;
    return true;
}

static bool cubox_tap(int x, int y) {
    if (app.page != THINGSOS_UI_CUBOX || !cubox_ready()) return false;
    if (x < 120 && y >= 74 && y < 200) {
        return cubox_back();
    }

    const thingsos_cubox_status_t* s = &cubox_ui->status;

    if (cubox_ui->view == CUBOX_VIEW_PAIR) {
        if (y >= 868 && y < 940) {
            if (s->config_phase != THINGSOS_CUBOX_CONFIG_VERIFYING) cubox_start_pair(cubox_ui->pair_return);
        } else if (y >= 974 && y < 1046) {
            thingsos_cubox_pair(false);
            cubox_ui->view = cubox_ui->pair_return;
            app.dirty = true;
        }
        return true;
    }

    if (cubox_ui->view == CUBOX_VIEW_SETTINGS) {
        if (y >= 745 && y < 817) {
            cubox_start_pair(CUBOX_VIEW_SETTINGS);
        } else if (s->configured && y >= 850 && y < 922) {
            if (!thingsos_cubox_disconnect()) say("请等待当前网络操作完成");
            else {
                say("已断开 Cubox 连接");
                cubox_ui->view = CUBOX_VIEW_LIST;
            }
            app.dirty = true;
        } else if (y >= 955 && y < 1027) {
            cubox_ui->view = CUBOX_VIEW_LIST;
            app.dirty = true;
        }
        return true;
    }

    if (cubox_ui->view == CUBOX_VIEW_FOLDERS) {
        if (y >= 226 && y < 294) {
            if (!thingsos_cubox_refresh_folders()) {
                thingsos_cubox_status(&cubox_ui->status);
                say(cubox_ui->status.message[0] ? cubox_ui->status.message : "无法刷新文件夹");
            }
            app.dirty = true;
            return true;
        }
        if (y >= 312 && y < 962) {
            unsigned row = (unsigned)(y - 312) / 130u;
            unsigned idx = cubox_ui->folder_start + row;
            if (idx == 0) {
                cubox_ui->list_start = 0;
                cubox_ui->view = CUBOX_VIEW_LIST;
                thingsos_cubox_set_filter(s->filter, "", "全部文件夹", app.cubox_query, true);
                app.dirty = true;
            } else {
                thingsos_cubox_folder_t* f = &cubox_ui->scratch_folder;
                if (thingsos_cubox_folder(idx - 1u, f)) {
                    const char* fname = f->nested_name[0] ? f->nested_name : f->name;
                    cubox_ui->list_start = 0;
                    cubox_ui->view = CUBOX_VIEW_LIST;
                    thingsos_cubox_set_filter(s->filter, f->id, fname, app.cubox_query, true);
                    app.dirty = true;
                }
            }
            return true;
        }
        if (y >= 1006 && y < 1078) {
            unsigned total = 1u + s->folder_count;
            if (x < 342) {
                cubox_ui->folder_start = cubox_ui->folder_start >= 5u ? cubox_ui->folder_start - 5u : 0u;
            } else if (cubox_ui->folder_start + 5u < total) {
                cubox_ui->folder_start += 5u;
            }
            app.dirty = true;
        }
        return true;
    }

    if (cubox_ui->view == CUBOX_VIEW_DETAIL) {
        const thingsos_cubox_card_t* c = &cubox_ui->detail.card;
        if (y >= 770 && y < 842) {
            bool downloading_this = s->busy && s->op == THINGSOS_CUBOX_OP_DOWNLOAD &&
                                    !strcmp(s->active_card_id, c->id);
            if (downloading_this) {
                cubox_ui->open_after_download = false;
                thingsos_cubox_cancel();
                say("正在取消下载");
            } else if (c->cached) {
                cubox_open_article(
                    c->id,
                    c->title,
                    cubox_ui->detail.author[0] ? cubox_ui->detail.author : c->domain);
            } else {
                cubox_ui->open_after_download = true;
                if (!thingsos_cubox_download(c->id)) {
                    cubox_ui->open_after_download = false;
                    thingsos_cubox_status(&cubox_ui->status);
                    say(cubox_ui->status.message[0] ? cubox_ui->status.message : "无法下载正文");
                } else {
                    thingsos_cubox_status(&cubox_ui->status);
                }
            }
            app.dirty = true;
        } else if (y >= 868 && y < 940) {
            if (x >= 36 && x < 232) {
                if (!thingsos_cubox_mark(c->id, c->read ? 0 : 1, -1)) {
                    thingsos_cubox_status(&cubox_ui->status);
                    say(cubox_ui->status.message[0] ? cubox_ui->status.message : "更新已读状态失败");
                }
            } else if (x >= 244 && x < 440) {
                if (!thingsos_cubox_mark(c->id, -1, c->starred ? 0 : 1)) {
                    thingsos_cubox_status(&cubox_ui->status);
                    say(cubox_ui->status.message[0] ? cubox_ui->status.message : "更新星标状态失败");
                }
            } else if (x >= 452 && x < 648) {
                if (!thingsos_cubox_archive(c->id)) {
                    thingsos_cubox_status(&cubox_ui->status);
                    say(cubox_ui->status.message[0] ? cubox_ui->status.message : "归档失败");
                }
            }
            app.dirty = true;
        } else if (y >= 966 && y < 1038) {
            if (x < 342) {
                if (c->cached) {
                    cubox_ui->open_after_download = false;
                    if (!thingsos_cubox_download(c->id)) {
                        thingsos_cubox_status(&cubox_ui->status);
                        say(cubox_ui->status.message[0] ? cubox_ui->status.message : "更新缓存失败");
                    }
                } else {
                    if (!thingsos_cubox_fetch_detail(c->id)) {
                        thingsos_cubox_status(&cubox_ui->status);
                        say(cubox_ui->status.message[0] ? cubox_ui->status.message : "刷新详情失败");
                    }
                }
            } else {
                if (c->cached) {
                    if (thingsos_cubox_delete_cache(c->id)) {
                        cubox_ui->detail_valid = thingsos_cubox_detail_copy(c->id, &cubox_ui->detail);
                        say("已清除本地缓存");
                    }
                } else {
                    cubox_ui->view = CUBOX_VIEW_LIST;
                }
            }
            app.dirty = true;
        }
        return true;
    }

    /* CUBOX_VIEW_LIST */
    if (s->configured && y >= 120 && y < 195) {
        if (x >= 460 && x < 554) {
            if (!thingsos_cubox_refresh_list(false)) {
                thingsos_cubox_status(&cubox_ui->status);
                say(cubox_ui->status.message[0] ? cubox_ui->status.message : "刷新失败");
            }
            app.dirty = true;
            return true;
        }
        if (x >= 556 && x < 652) {
            cubox_ui->view = CUBOX_VIEW_SETTINGS;
            app.dirty = true;
            return true;
        }
    }

    if (!s->configured && !s->card_count) {
        if (y >= 520 && y < 600) cubox_start_pair(CUBOX_VIEW_LIST);
        return true;
    }

    if (y >= 226 && y < 290) {
        if (x >= 36 && x < 464) {
            thingsos_cubox_filter_t nf =
                x < 175 ? THINGSOS_CUBOX_FILTER_UNREAD
                : x < 319 ? THINGSOS_CUBOX_FILTER_ALL
                          : THINGSOS_CUBOX_FILTER_STARRED;
            cubox_ui->list_start = 0;
            thingsos_cubox_set_filter(nf, s->folder_id, s->folder_name, app.cubox_query, true);
            app.dirty = true;
        } else if (x >= 472 && x < 648) {
            feature_editor(5);
        }
        return true;
    }

    if (y >= 292 && y < 346 && x < 500) {
        cubox_ui->folder_start = 0;
        cubox_ui->view = CUBOX_VIEW_FOLDERS;
        if (!s->folder_count && thingsos_network_connected() && s->configured && !s->busy) {
            thingsos_cubox_refresh_folders();
        }
        app.dirty = true;
        return true;
    }

    if (!s->card_count && app.cubox_query[0] && y >= 620 && y < 692) {
        app.cubox_query[0] = 0;
        cubox_search_submit();
        return true;
    }

    if (y >= 352 && y < 992 && s->card_count > 0) {
        unsigned row = (unsigned)(y - 352) / 160u;
        unsigned idx = cubox_ui->list_start + row;
        if (thingsos_cubox_card(idx, &cubox_ui->scratch_card)) {
            cubox_open_detail(&cubox_ui->scratch_card);
        }
        return true;
    }

    if (y >= 1006 && y < 1078) {
        if (x < 240) {
            cubox_ui->list_start = cubox_ui->list_start >= 4u ? cubox_ui->list_start - 4u : 0u;
            app.dirty = true;
        } else if (x >= 440) {
            if (cubox_ui->list_start + 4u < s->card_count) {
                cubox_ui->list_start += 4u;
                app.dirty = true;
            } else if (s->has_more && thingsos_network_connected() && !s->busy) {
                thingsos_cubox_refresh_list(true);
                app.dirty = true;
            }
        }
        return true;
    }

    return true;
}

static bool cubox_scroll(int dy) {
    if (app.page != THINGSOS_UI_CUBOX || !cubox_ready()) return false;
    const thingsos_cubox_status_t* s = &cubox_ui->status;
    if (cubox_ui->view == CUBOX_VIEW_LIST) {
        if (dy > 0) {
            if (cubox_ui->list_start + 4u < s->card_count) {
                cubox_ui->list_start += 4u;
                app.dirty = true;
            } else if (s->has_more && thingsos_network_connected() && !s->busy) {
                thingsos_cubox_refresh_list(true);
                app.dirty = true;
            }
        } else if (cubox_ui->list_start > 0) {
            cubox_ui->list_start = cubox_ui->list_start >= 4u ? cubox_ui->list_start - 4u : 0u;
            app.dirty = true;
        }
        return true;
    }
    if (cubox_ui->view == CUBOX_VIEW_FOLDERS) {
        unsigned total = 1u + s->folder_count;
        if (dy > 0 && cubox_ui->folder_start + 5u < total) {
            cubox_ui->folder_start += 5u;
            app.dirty = true;
        } else if (dy < 0 && cubox_ui->folder_start > 0) {
            cubox_ui->folder_start = cubox_ui->folder_start >= 5u ? cubox_ui->folder_start - 5u : 0u;
            app.dirty = true;
        }
        return true;
    }
    return false;
}

static bool cubox_key(thingsos_key_action_t action) {
    if (app.page != THINGSOS_UI_CUBOX || !cubox_ready()) return false;
    if (action == THINGSOS_KEY_MENU) {
        return cubox_back();
    }
    if (cubox_ui->view == CUBOX_VIEW_LIST || cubox_ui->view == CUBOX_VIEW_FOLDERS) {
        if (action == THINGSOS_KEY_NEXT) return cubox_scroll(1);
        if (action == THINGSOS_KEY_PREVIOUS) return cubox_scroll(-1);
    }
    return true;
}
