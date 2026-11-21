// SPDX-License-Identifier: Apache-2.0
/* Cubox e-ink UI controller. Runs strictly on the main UI task under thingsos_io_lock. */
typedef enum {
    CUBOX_VIEW_LIST = 0,
    CUBOX_VIEW_DETAIL = 1,
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
    unsigned tab;          /* 0 = 最近文章, 1 = 收藏夹 (equals the backend list slot) */
    bool in_folder;        /* 收藏夹 tab shows one folder's articles */
    unsigned tab_start[2]; /* list position remembered per tab */
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
        !app.controls_refresh &&
        !app.force_refresh && !app.transition_refresh &&
        !app.message[0];
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
        navigate(THINGSOS_UI_READING);
        if (cubox_ui) {
            cubox_ui->painted = false;
            cubox_ui->painted_page = (thingsos_ui_page_t)0;
        }
    } else {
        say("打开文章失败");
    }
    return ok;
}

static void cubox_reclaim_psram(void) {
#ifndef THINGSOS_HOST_UI
    if (heap_caps_get_free_size(MALLOC_CAP_SPIRAM) < 1536u * 1024u ||
        heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) < 512u * 1024u) {
        reader_cache_drop_frames();
    }
    if (heap_caps_get_free_size(MALLOC_CAP_SPIRAM) < 1536u * 1024u) {
        thingsos_thumbnail_clear(&app.thumbnails);
    }
#endif
}

static void cubox_enter(void) {
    if (!cubox_ready()){say("内存不足，无法打开 Cubox");return;}
    cubox_reclaim_psram();
    thingsos_cubox_init();
    thingsos_cubox_status(&cubox_ui->status);
    cubox_ui->last_serial = cubox_ui->status.serial;
    cubox_ui->open_after_download = false;

    if (!cubox_ui->loaded_once) {
        thingsos_cubox_load_offline();
        thingsos_cubox_status(&cubox_ui->status);
        cubox_ui->loaded_once = true;
        cubox_ui->tab = cubox_ui->status.slot ? 1u : 0u; /* offline cache lands in the recent slot */
        cubox_ui->in_folder = cubox_ui->tab == 1u && cubox_ui->status.folder_id[0];
        if (cubox_ui->status.configured && thingsos_network_connected() && !cubox_ui->status.busy) {
            if (!cubox_ui->status.card_count) thingsos_cubox_refresh_list(false);
            else if (!cubox_ui->status.folder_count) thingsos_cubox_refresh_folders();
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
    cubox_reclaim_psram();
    thingsos_cubox_status(&cubox_ui->status);
    cubox_ui->pair_return = ret;
    /* A successful pairing loads the recent list; give it the recent slot. */
    if (thingsos_cubox_select_slot(0)) {
        cubox_ui->tab = 0;
        cubox_ui->in_folder = false;
        cubox_ui->list_start = 0;
        strlcpy(app.cubox_query, "", sizeof(app.cubox_query));
    }
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

/* Two tabs: 最近文章 (list slot 0, all folders) and 收藏夹 (folder list, then the chosen
   folder's articles in list slot 1). Each slot keeps its own filter, keyword and cards. */
#define CUBOX_TAB_TOP 210
#define CUBOX_TAB_BOTTOM 296
#define CUBOX_BAR_TOP 306
#define CUBOX_BAR_HIT_TOP 300
#define CUBOX_BAR_HIT_BOTTOM 362
#define CUBOX_LIST_TOP 378
#define CUBOX_LIST_STEP 156
#define CUBOX_LIST_ROWS 4u
#define CUBOX_FOLDER_TOP 370
#define CUBOX_FOLDER_STEP 104
#define CUBOX_FOLDER_ROWS 6u
#define CUBOX_EMPTY_BUTTON 620

static void cubox_dim(int x, int y, int w, int h) {
    for (int yy = y; yy < y + h; ++yy) {
        for (int xx = x; xx < x + w; ++xx) {
            unsigned char* p = &app.canvas.pixels[(size_t)yy * app.canvas.width + xx];
            *p = (unsigned char)(255 - (255 - *p) * 9 / 20);
        }
    }
}

/* Inset of a pill end cap of radius r on a row dy (1..r) away from the centre line. */
static int cubox_cap_inset(int r, int dy) {
    int i = 0;
    while (i < r && (2 * r - 2 * i - 1) * (2 * r - 2 * i - 1) + (2 * dy - 1) * (2 * dy - 1) > 4 * r * r) ++i;
    return i;
}

static void cubox_chip(const char* text, int x, int y, int w, unsigned size, bool on) {
    const int h = 46, r = 23;
    for (int row = 0; row < h; ++row) {
        int dy = row < r ? r - row : row - r + 1;
        int outer = cubox_cap_inset(r, dy);
        thingsos_rect(&app.canvas, x + outer, y + row, w - 2 * outer, 1, 0);
        if (dy <= r - 2) {
            int inner = 2 + cubox_cap_inset(r - 2, dy);
            thingsos_rect(&app.canvas, x + inner, y + row, w - 2 * inner, 1, 255);
        }
    }
    centered_label(text, x + 4, y + 32, size, w - 8);
    if (!on) return;
    for (int row = 0; row < h; ++row) {
        int dy = row < r ? r - row : row - r + 1;
        int outer = cubox_cap_inset(r, dy);
        unsigned char* line = &app.canvas.pixels[(size_t)(y + row) * app.canvas.width];
        for (int xx = x + outer; xx < x + w - outer; ++xx) line[xx] = (unsigned char)(255 - line[xx]);
    }
}

static void cubox_folder_icon(int x, int y) {
    thingsos_rect(&app.canvas, x, y - 6, 14, 2, 0);
    thingsos_rect(&app.canvas, x, y - 6, 2, 6, 0);
    thingsos_rect(&app.canvas, x + 12, y - 6, 2, 6, 0);
    thingsos_rect(&app.canvas, x, y, 34, 26, 0);
    thingsos_rect(&app.canvas, x + 2, y + 2, 30, 22, 255);
}

static void cubox_draw_tabs(void) {
    static const char* const names[2] = {"最近文章", "收藏夹"};
    for (unsigned t = 0; t < 2u; ++t) {
        int x = 36 + (int)t * 306;
        centered_label(names[t], x, 268, 30, 306);
        if (t == cubox_ui->tab) {
            centered_label(names[t], x + 1, 268, 30, 306); /* faux bold */
            thingsos_rect(&app.canvas, x + 73, 283, 160, 6, 0);
        } else {
            cubox_dim(x, 224, 306, 56);
        }
    }
    thingsos_rect(&app.canvas, 341, 236, 1, 34, 170);
    thingsos_rect(&app.canvas, 36, 289, 612, 1, 170);
}

static void cubox_draw_pager(unsigned cur, unsigned total, const char* next) {
    char pstr[48];
    snprintf(pstr, sizeof(pstr), "%u / %u", cur, total);
    reader_control("上一页", 36, 1006, 190, false);
    centered_label(pstr, 236, 1046, 24, 212);
    reader_control(next, 458, 1006, 190, false);
}

static void cubox_draw_folder_list(const thingsos_cubox_status_t* s) {
    char info[64] = "";
    if (s->folder_count) snprintf(info, sizeof(info), "共 %u 个收藏夹", s->folder_count);
    label(info, 36, 338, 22, 420);
    bool syncing = s->busy && s->op == THINGSOS_CUBOX_OP_FOLDERS;
    cubox_chip(syncing ? "同步中…" : "同步列表", 488, CUBOX_BAR_TOP, 160, 22, false);
    thingsos_rect(&app.canvas, 36, 366, 612, 1, 170);

    if (cubox_ui->folder_start >= s->folder_count) cubox_ui->folder_start = 0;
    if (!s->folder_count) {
        centered_label(
            syncing ? "正在同步收藏夹…" : (s->message[0] ? s->message : "还没有收藏夹，点击“同步列表”"),
            36, 560, 27, 612);
    }
    for (unsigned row = 0; row < CUBOX_FOLDER_ROWS; ++row) {
        thingsos_cubox_folder_t* f = &cubox_ui->scratch_folder;
        if (!thingsos_cubox_folder(cubox_ui->folder_start + row, f)) break;
        int y = CUBOX_FOLDER_TOP + (int)row * CUBOX_FOLDER_STEP;
        int ix = f->parent_id[0] ? 80 : 36; /* sub-folders indent one level */
        const char* name = f->name[0] ? f->name : f->nested_name[0] ? f->nested_name : "未命名";
        char title[200];
        snprintf(title, sizeof(title), "%s%s", name, f->uncategorized ? " (未分类)" : "");
        cubox_folder_icon(ix, y + 40);
        label(title, ix + 56, y + 64, 28, 600 - ix - 56);
        label(">", 615, y + 64, 26, 30);
        thingsos_rect(&app.canvas, 36, y + CUBOX_FOLDER_STEP - 1, 612, 1, 200);
    }
    unsigned pages = s->folder_count ? (s->folder_count + CUBOX_FOLDER_ROWS - 1u) / CUBOX_FOLDER_ROWS : 1u;
    cubox_draw_pager(cubox_ui->folder_start / CUBOX_FOLDER_ROWS + 1u, pages, "下一页");
}

static void cubox_draw_cards(const thingsos_cubox_status_t* s, bool in_folder) {
    for (unsigned row = 0; row < CUBOX_LIST_ROWS && cubox_ui->list_start + row < s->card_count; ++row) {
        thingsos_cubox_card_t* card = &cubox_ui->scratch_card;
        if (!thingsos_cubox_card(cubox_ui->list_start + row, card)) continue;
        int y = CUBOX_LIST_TOP + (int)row * CUBOX_LIST_STEP;
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
        thingsos_text_string_page(ui_font(), &app.canvas, title_line, strlen(title_line), 0, 36, y + 6, 612, 68, 36);

        char date10[12] = "";
        if (card->create_time[0]) strlcpy(date10, card->create_time, sizeof(date10));
        bool show_folder = !in_folder && card->folder_name[0];
        char meta[320];
        snprintf(
            meta, sizeof(meta), "%s%s%s%s%s%s",
            card->domain[0] ? card->domain : "网页",
            show_folder ? " · " : "",
            show_folder ? card->folder_name : "",
            date10[0] ? " · " : "",
            date10,
            card->cached ? " · [已离线]" : "");
        label(meta, 36, y + 102, 21, 612);

        if (card->description[0]) label(card->description, 36, y + 132, 20, 612);
        else if (card->tag_summary[0]) label(card->tag_summary, 36, y + 132, 20, 612);
        thingsos_rect(&app.canvas, 36, y + 150, 612, 1, 200);
    }
}

static bool cubox_draw_list(const thingsos_cubox_status_t* s) {
    header("Cubox");
    if (s->configured) {
        reader_control(s->busy ? "同步中" : "刷新", 460, 124, 90, false);
        reader_control("设置", 558, 124, 90, false);
    }

    if (!s->configured && !s->card_count) {
        centered_label("连接你的 Cubox", 36, 330, 36, 612);
        centered_label("浏览最近文章与收藏夹，离线图文阅读", 36, 405, 24, 612);
        reader_control("手机扫码配对", 36, 520, 612, true);
        centered_label("支持 cubox.pro 与 cubox.cc API 扩展链接或 Token", 36, 650, 22, 612);
        if (s->message[0]) centered_label(s->message, 36, 730, 22, 612);
        nav();
        return true;
    }

    cubox_draw_tabs();
    if (cubox_ui->tab == 1u && !cubox_ui->in_folder) {
        cubox_draw_folder_list(s);
        if (s->message[0] && s->folder_count) label(s->message, 36, 1095, 20, 612);
        nav();
        return true;
    }

    bool in_folder = cubox_ui->tab == 1u;
    bool query = app.cubox_query[0] != 0;
    if (!in_folder) {
        cubox_chip("全部", 36, CUBOX_BAR_TOP, 90, 22, s->filter == THINGSOS_CUBOX_FILTER_ALL);
        cubox_chip("未读", 136, CUBOX_BAR_TOP, 90, 22, s->filter == THINGSOS_CUBOX_FILTER_UNREAD);
        cubox_chip("星标", 236, CUBOX_BAR_TOP, 90, 22, s->filter == THINGSOS_CUBOX_FILTER_STARRED);
        char line_state[128];
        if (query) snprintf(line_state, sizeof(line_state), "词：%s", app.cubox_query);
        else strlcpy(line_state, s->busy ? "同步中…" : (!thingsos_network_connected() || s->offline_mode) ? "离线缓存" : "在线", sizeof(line_state));
        label(line_state, 342, 338, 20, 156);
        cubox_chip(query ? "搜索*" : "搜索", 508, CUBOX_BAR_TOP, 140, 22, query);
    } else {
        char crumb[220];
        snprintf(crumb, sizeof(crumb), "< 收藏夹 / %s", s->folder_name);
        label(crumb, 36, 338, 24, 296);
        cubox_chip("全部", 340, CUBOX_BAR_TOP, 72, 20, s->filter == THINGSOS_CUBOX_FILTER_ALL);
        cubox_chip("未读", 418, CUBOX_BAR_TOP, 72, 20, s->filter == THINGSOS_CUBOX_FILTER_UNREAD);
        cubox_chip("星标", 496, CUBOX_BAR_TOP, 72, 20, s->filter == THINGSOS_CUBOX_FILTER_STARRED);
        cubox_chip("搜", 574, CUBOX_BAR_TOP, 74, 22, query);
    }
    thingsos_rect(&app.canvas, 36, 366, 612, 1, 170);

    if (cubox_ui->list_start >= s->card_count) cubox_ui->list_start = 0;
    if (!s->card_count) {
        const char* empty = s->busy ? "正在获取 Cubox 收藏…"
                          : s->message[0] ? s->message
                          : (in_folder && (!thingsos_network_connected() || s->offline_mode)) ? "离线状态下该收藏夹没有缓存文章"
                          : in_folder ? "该收藏夹下没有文章"
                          : "当前筛选下没有收藏";
        centered_label(empty, 36, 540, 27, 612);
        if (query) reader_control("清空搜索关键词", 162, CUBOX_EMPTY_BUTTON, 360, false);
        else if (in_folder) reader_control("返回收藏夹列表", 162, CUBOX_EMPTY_BUTTON, 360, false);
    } else {
        cubox_draw_cards(s, in_folder);
    }

    unsigned total_pages = s->card_count ? (s->card_count + CUBOX_LIST_ROWS - 1u) / CUBOX_LIST_ROWS : 1u;
    bool at_last = cubox_ui->list_start + CUBOX_LIST_ROWS >= s->card_count;
    cubox_draw_pager(
        cubox_ui->list_start / CUBOX_LIST_ROWS + 1u, total_pages,
        (at_last && s->has_more && thingsos_network_connected()) ? "加载更多" : "下一页");

    if (s->message[0] && s->card_count) label(s->message, 36, 1095, 20, 612);
    nav();
    return true;
}

static void cubox_switch_tab(unsigned tab) {
    if (tab == cubox_ui->tab) {
        if (tab == 1u && cubox_ui->in_folder) {
            cubox_ui->in_folder = false;
            app.dirty = true;
        }
        return;
    }
    if (!thingsos_cubox_select_slot(tab)) {
        say("请等待列表同步完成");
        return;
    }
    cubox_ui->tab_start[cubox_ui->tab] = cubox_ui->list_start;
    cubox_ui->tab = tab;
    cubox_ui->list_start = cubox_ui->tab_start[tab];
    thingsos_cubox_status(&cubox_ui->status);
    strlcpy(app.cubox_query, cubox_ui->status.query, sizeof(app.cubox_query));
    const thingsos_cubox_status_t* s = &cubox_ui->status;
    if(tab==0u && !s->card_count && !s->busy && !thingsos_network_connected()) {
        thingsos_cubox_load_offline();
        thingsos_cubox_status(&cubox_ui->status);
    }
    /* Each tab shows what it already holds; only an empty tab fetches on its own. */
    if (s->configured && thingsos_network_connected() && !s->busy) {
        if (tab == 1u && !s->folder_count) thingsos_cubox_refresh_folders();
        else if (tab == 0u && !s->card_count) thingsos_cubox_refresh_list(false);
    }
    app.dirty = true;
}

static void cubox_open_folder(const thingsos_cubox_folder_t* f) {
    const char* name = f->name[0] ? f->name : f->nested_name;
    thingsos_cubox_status(&cubox_ui->status);
    bool same = !strcmp(cubox_ui->status.folder_id, f->id) && cubox_ui->status.card_count > 0;
    cubox_ui->in_folder = true;
    if (!same) {
        cubox_ui->list_start = 0;
        app.cubox_query[0] = 0;
        thingsos_cubox_set_filter(cubox_ui->status.filter, f->id, name, "", true);
    }
    app.dirty = true;
}

static void cubox_page_folders(int dir) {
    const thingsos_cubox_status_t* s = &cubox_ui->status;
    if (dir > 0 && cubox_ui->folder_start + CUBOX_FOLDER_ROWS < s->folder_count) {
        cubox_ui->folder_start += CUBOX_FOLDER_ROWS;
        app.dirty = true;
    } else if (dir < 0 && cubox_ui->folder_start > 0) {
        cubox_ui->folder_start = cubox_ui->folder_start >= CUBOX_FOLDER_ROWS ? cubox_ui->folder_start - CUBOX_FOLDER_ROWS : 0u;
        app.dirty = true;
    }
}

static void cubox_page_cards(int dir) {
    const thingsos_cubox_status_t* s = &cubox_ui->status;
    if (dir > 0) {
        if (cubox_ui->list_start + CUBOX_LIST_ROWS < s->card_count) {
            cubox_ui->list_start += CUBOX_LIST_ROWS;
            app.dirty = true;
        } else if (s->has_more && thingsos_network_connected() && !s->busy) {
            thingsos_cubox_refresh_list(true);
            app.dirty = true;
        }
    } else if (cubox_ui->list_start > 0) {
        cubox_ui->list_start = cubox_ui->list_start >= CUBOX_LIST_ROWS ? cubox_ui->list_start - CUBOX_LIST_ROWS : 0u;
        app.dirty = true;
    }
}

static bool cubox_folder_view(void) {
    return cubox_ui->view == CUBOX_VIEW_LIST && cubox_ui->tab == 1u && !cubox_ui->in_folder;
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
    } else if (cubox_ui->view == CUBOX_VIEW_DETAIL || cubox_ui->view == CUBOX_VIEW_SETTINGS) {
        cubox_ui->open_after_download = false;
        cubox_ui->view = CUBOX_VIEW_LIST;
    } else if (cubox_ui->tab == 1u && cubox_ui->in_folder) {
        cubox_ui->in_folder = false;
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
                cubox_reclaim_psram();
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
                cubox_reclaim_psram();
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
            bool ok = cubox_folder_view() ? thingsos_cubox_refresh_folders() : thingsos_cubox_refresh_list(false);
            if (!ok) {
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

    if (y >= CUBOX_TAB_TOP && y < CUBOX_TAB_BOTTOM) {
        if (x >= 36 && x < 648) cubox_switch_tab(x < 342 ? 0u : 1u);
        return true;
    }

    if (cubox_folder_view()) {
        if (y >= CUBOX_BAR_HIT_TOP && y < CUBOX_BAR_HIT_BOTTOM && x >= 488) {
            if (!thingsos_cubox_refresh_folders()) {
                thingsos_cubox_status(&cubox_ui->status);
                say(cubox_ui->status.message[0] ? cubox_ui->status.message : "无法刷新收藏夹");
            }
            app.dirty = true;
        } else if (y >= CUBOX_FOLDER_TOP && y < CUBOX_FOLDER_TOP + (int)CUBOX_FOLDER_ROWS * CUBOX_FOLDER_STEP) {
            unsigned row = (unsigned)(y - CUBOX_FOLDER_TOP) / (unsigned)CUBOX_FOLDER_STEP;
            if (thingsos_cubox_folder(cubox_ui->folder_start + row, &cubox_ui->scratch_folder)) {
                cubox_open_folder(&cubox_ui->scratch_folder);
            }
        } else if (y >= 1006 && y < 1078) {
            if (x < 240) cubox_page_folders(-1);
            else if (x >= 440) cubox_page_folders(1);
        }
        return true;
    }

    bool in_folder = cubox_ui->tab == 1u;
    if (y >= CUBOX_BAR_HIT_TOP && y < CUBOX_BAR_HIT_BOTTOM) {
        int nf = -1;
        bool search = false;
        if (!in_folder) {
            if (x >= 36 && x < 131) nf = THINGSOS_CUBOX_FILTER_ALL;
            else if (x >= 131 && x < 231) nf = THINGSOS_CUBOX_FILTER_UNREAD;
            else if (x >= 231 && x < 331) nf = THINGSOS_CUBOX_FILTER_STARRED;
            else if (x >= 500 && x < 648) search = true;
        } else {
            if (x >= 36 && x < 334) {
                cubox_ui->in_folder = false;
                app.dirty = true;
                return true;
            }
            if (x >= 336 && x < 415) nf = THINGSOS_CUBOX_FILTER_ALL;
            else if (x >= 415 && x < 493) nf = THINGSOS_CUBOX_FILTER_UNREAD;
            else if (x >= 493 && x < 571) nf = THINGSOS_CUBOX_FILTER_STARRED;
            else if (x >= 571 && x < 648) search = true;
        }
        if (search) {
            feature_editor(5);
        } else if (nf >= 0) {
            cubox_ui->list_start = 0;
            thingsos_cubox_set_filter((thingsos_cubox_filter_t)nf, s->folder_id, s->folder_name, app.cubox_query, true);
            app.dirty = true;
        }
        return true;
    }

    if (!s->card_count && y >= CUBOX_EMPTY_BUTTON && y < CUBOX_EMPTY_BUTTON + 72 && x >= 162 && x < 522) {
        if (app.cubox_query[0]) {
            app.cubox_query[0] = 0;
            cubox_search_submit();
        } else if (in_folder) {
            cubox_ui->in_folder = false;
            app.dirty = true;
        }
        return true;
    }

    if (y >= CUBOX_LIST_TOP && y < CUBOX_LIST_TOP + (int)CUBOX_LIST_ROWS * CUBOX_LIST_STEP && s->card_count > 0) {
        unsigned row = (unsigned)(y - CUBOX_LIST_TOP) / (unsigned)CUBOX_LIST_STEP;
        if (thingsos_cubox_card(cubox_ui->list_start + row, &cubox_ui->scratch_card)) {
            cubox_open_detail(&cubox_ui->scratch_card);
        }
        return true;
    }

    if (y >= 1006 && y < 1078) {
        if (x < 240) cubox_page_cards(-1);
        else if (x >= 440) cubox_page_cards(1);
        return true;
    }

    return true;
}

static bool cubox_scroll(int dy) {
    if (app.page != THINGSOS_UI_CUBOX || !cubox_ready()) return false;
    if (cubox_ui->view != CUBOX_VIEW_LIST) return false;
    if (cubox_folder_view()) cubox_page_folders(dy > 0 ? 1 : -1);
    else cubox_page_cards(dy > 0 ? 1 : -1);
    return true;
}

static bool cubox_key(thingsos_key_action_t action) {
    if (app.page != THINGSOS_UI_CUBOX || !cubox_ready()) return false;
    if (action == THINGSOS_KEY_HOME) {
        navigate(THINGSOS_UI_APPS);
        return true;
    }
    if (action == THINGSOS_KEY_MENU) {
        return cubox_back();
    }
    if (cubox_ui->view == CUBOX_VIEW_LIST) {
        if (action == THINGSOS_KEY_NEXT) return cubox_scroll(1);
        if (action == THINGSOS_KEY_PREVIOUS) return cubox_scroll(-1);
    }
    return true;
}
