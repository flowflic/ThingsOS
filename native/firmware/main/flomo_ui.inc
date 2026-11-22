// SPDX-License-Identifier: Apache-2.0
/* flomo e-ink UI controller (read-only). Runs on the main UI task under thingsos_io_lock. */
typedef enum {
    FLOMO_VIEW_LIST = 0,
    FLOMO_VIEW_DETAIL = 1,
    FLOMO_VIEW_TAGS = 2,
    FLOMO_VIEW_PAIR = 3,
    FLOMO_VIEW_SETTINGS = 4,
    FLOMO_VIEW_RELATED = 5
} flomo_view_t;

#define FLOMO_UI_TEXT_MAX (120u * 1024u)
#define FLOMO_UI_PAGE_STACK 96

#define FLOMO_CARDS_MAX 8
#define FLOMO_CARD_TOP 330
#define FLOMO_CARD_BOTTOM 996
#define FLOMO_BODY_SIZE 25
#define FLOMO_BODY_LINE 37
typedef struct {
    flomo_view_t view;
    flomo_view_t pair_return;
    flomo_view_t detail_return;
    thingsos_flomo_status_t status;
    thingsos_flomo_memo_t memo;
    thingsos_flomo_memo_t scratch;
    thingsos_flomo_tag_t scratch_tag;
    char* text;
    size_t text_len;
    bool text_complete;
    size_t page_start;
    size_t page_next;
    size_t page_stack[FLOMO_UI_PAGE_STACK];
    unsigned page_depth;
    bool loaded_once;
    bool painted;
    bool painted_controls;
    thingsos_ui_page_t painted_page;
    unsigned list_start;
    unsigned tag_start;
    unsigned related_start;
    /* Variable-height memo cards: what the last draw showed and where, plus
       the page starts walked forward so "previous" returns exactly. */
    unsigned shown;
    unsigned card_count;
    int card_top[FLOMO_CARDS_MAX], card_bottom[FLOMO_CARDS_MAX];
    unsigned list_stack[FLOMO_UI_PAGE_STACK], list_depth;
    unsigned related_stack[FLOMO_UI_PAGE_STACK], related_depth;
    unsigned last_serial;
    unsigned pair_generation;
    uint64_t poll_tick;
    char active_id[THINGSOS_FLOMO_ID_LEN];
    char parent_id[THINGSOS_FLOMO_ID_LEN];
} flomo_ui_t;

static flomo_ui_t* flomo_ui;

static bool flomo_ready(void) {
    if (!flomo_ui) {
        flomo_ui = thingsos_psram_calloc(1, sizeof(*flomo_ui));
        if (flomo_ui) flomo_ui->text = thingsos_psram_calloc(1, FLOMO_UI_TEXT_MAX);
        if (flomo_ui && !flomo_ui->text) {
            free(flomo_ui);
            flomo_ui = NULL;
        }
    }
    return flomo_ui != NULL;
}

static bool app_flomo_region(void) {
    return app.page == THINGSOS_UI_FLOMO && flomo_ready() &&
        flomo_ui->painted && flomo_ui->painted_page == app.page &&
        !app.controls_open && !flomo_ui->painted_controls &&
        !app.controls_refresh && !app.feedback_refresh &&
        !app.force_refresh && !app.transition_refresh &&
        !app.lock_wave_pending && !app.message[0];
}

static void app_flomo_presented(bool ok) {
    if (!flomo_ui) return;
    if (app.page == THINGSOS_UI_FLOMO && !app.controls_open) {
        flomo_ui->painted = ok;
        flomo_ui->painted_page = ok ? app.page : (thingsos_ui_page_t)0;
    } else if (!ok || app.page != THINGSOS_UI_FLOMO) {
        flomo_ui->painted = false;
        flomo_ui->painted_page = (thingsos_ui_page_t)0;
    }
    flomo_ui->painted_controls = app.controls_open;
}

static esp_err_t app_flomo_present(void) {
    return thingsos_board_present_metrics(&app.canvas);
}

static void flomo_pair_link(char* link, size_t cap) {
    char address[96];
    thingsos_network_address(false, address, sizeof(address));
    const char* host = strstr(address, "://");
    if (strncmp(address, "http://", 7)) {
        link[0] = 0;
        return;
    }
    snprintf(link, cap, "http://%s:8083/", host ? host + 3 : address);
}

/* "2026-11-17T18:45:47+08:00" -> "2026-11-17 18:45" */
static void flomo_time(const char* iso, char* out, size_t cap) {
    if (!iso || strlen(iso) < 16) {
        strlcpy(out, iso ? iso : "", cap);
        return;
    }
    snprintf(out, cap, "%.10s %.5s", iso, iso + 11);
}

static void flomo_meta_line(const thingsos_flomo_memo_t* m, char* out, size_t cap) {
    char when[24];
    flomo_time(m->created_at, when, sizeof(when));
    snprintf(out, cap, "%s%s%s%s%s", when, m->tags[0] ? " · " : "", m->tags,
             m->has_image ? " · [图片]" : "", m->has_voice ? " · [语音]" : "");
}

static void flomo_load_detail(void) {
    if (!flomo_ui->active_id[0]) return;
    size_t len = 0;
    bool complete = false;
    if (thingsos_flomo_detail_copy(flomo_ui->active_id, &flomo_ui->memo, flomo_ui->text,
                                   FLOMO_UI_TEXT_MAX, &len, &complete)) {
        if (len != flomo_ui->text_len) {
            flomo_ui->page_start = 0;
            flomo_ui->page_depth = 0;
        }
        flomo_ui->text_len = len;
        flomo_ui->text_complete = complete;
    }
}

static void flomo_enter(void) {
    if (!flomo_ready()) return;
    thingsos_flomo_status(&flomo_ui->status);
    flomo_ui->last_serial = flomo_ui->status.serial;
    if (!flomo_ui->loaded_once) {
        flomo_ui->loaded_once = true;
        if (media()) thingsos_flomo_load_offline();
        thingsos_flomo_status(&flomo_ui->status);
        if (flomo_ui->status.configured && thingsos_network_connected() && !flomo_ui->status.busy)
            thingsos_flomo_refresh_list(false);
    }
    /* Leaving the app stops pairing; do not come back to a dead QR screen. */
    if (flomo_ui->view == FLOMO_VIEW_PAIR && !flomo_ui->status.pairing &&
        flomo_ui->status.config_phase != THINGSOS_FLOMO_CONFIG_VERIFYING)
        flomo_ui->view = flomo_ui->pair_return;
    if (flomo_ui->view == FLOMO_VIEW_DETAIL) flomo_load_detail();
    if (app.page != THINGSOS_UI_FLOMO) {
        flomo_ui->painted = false;
        flomo_ui->painted_page = (thingsos_ui_page_t)0;
    }
    navigate(THINGSOS_UI_FLOMO);
}

static void flomo_open_detail(const thingsos_flomo_memo_t* memo, flomo_view_t from) {
    if (!flomo_ready() || !memo) return;
    strlcpy(flomo_ui->active_id, memo->id, sizeof(flomo_ui->active_id));
    flomo_ui->memo = *memo;
    flomo_ui->text_len = (size_t)-1;
    flomo_ui->detail_return = from;
    flomo_ui->page_start = 0;
    flomo_ui->page_depth = 0;
    flomo_ui->view = FLOMO_VIEW_DETAIL;
    flomo_load_detail();
    app.dirty = true;
    thingsos_flomo_status(&flomo_ui->status);
    bool have_related = !strcmp(flomo_ui->status.related_for, memo->id);
    if ((!flomo_ui->text_complete || !have_related) && thingsos_network_connected() &&
        flomo_ui->status.configured && !flomo_ui->status.busy)
        thingsos_flomo_fetch_detail(memo->id);
}

static void flomo_search_submit(void) {
    if (!flomo_ready()) return;
    flomo_ui->list_start = 0;
    flomo_ui->view = FLOMO_VIEW_LIST;
    thingsos_flomo_mode_t mode = flomo_ui->status.mode == THINGSOS_FLOMO_MODE_REVIEW
                                     ? THINGSOS_FLOMO_MODE_RECENT : flomo_ui->status.mode;
    thingsos_flomo_set_filter(mode, flomo_ui->status.tag, app.flomo_query, true);
    app.dirty = true;
}

static void flomo_start_pair(flomo_view_t ret) {
    if (!flomo_ready()) return;
    thingsos_flomo_status(&flomo_ui->status);
    flomo_ui->pair_return = ret;
    flomo_ui->pair_generation = flomo_ui->status.connection_serial;
    flomo_ui->view = FLOMO_VIEW_PAIR;
    thingsos_flomo_pair(true);
    thingsos_flomo_status(&flomo_ui->status);
    app.dirty = true;
}

static void flomo_poll(void) {
    if (app.page != THINGSOS_UI_FLOMO || !flomo_ready()) return;
    uint64_t t = now();
    thingsos_flomo_status(&flomo_ui->status);
    if (flomo_ui->view == FLOMO_VIEW_PAIR && flomo_ui->status.pairing && t - flomo_ui->poll_tick >= 1000) {
        flomo_ui->poll_tick = t;
        app.dirty = true;
    }
    if (flomo_ui->status.serial == flomo_ui->last_serial) return;
    flomo_ui->last_serial = flomo_ui->status.serial;
    if (flomo_ui->view == FLOMO_VIEW_PAIR &&
        flomo_ui->status.config_phase == THINGSOS_FLOMO_CONFIG_READY &&
        flomo_ui->status.connection_serial != flomo_ui->pair_generation) {
        flomo_ui->view = FLOMO_VIEW_LIST;
        flomo_ui->list_start = 0;
    }
    if (flomo_ui->view == FLOMO_VIEW_DETAIL) flomo_load_detail();
    app.dirty = true;
}

/* Card list after the official flomo layout: grey time, body with list
   bullets, grey #tag blocks; heights follow the content. */
static uint32_t flomo_utf8(const char* s, size_t* i) {
    unsigned char c = (unsigned char)s[*i];
    uint32_t cp = c; unsigned n = 0;
    if (c >= 0xf0) { cp = c & 7; n = 3; } else if (c >= 0xe0) { cp = c & 15; n = 2; } else if (c >= 0xc0) { cp = c & 31; n = 1; }
    ++*i;
    while (n-- && s[*i]) cp = (cp << 6) | ((unsigned char)s[(*i)++] & 63);
    return cp;
}
static int flomo_text_width(const char* text, size_t len, unsigned size) {
    thingsos_font_size(ui_font(), size);
    int w = 0;
    for (size_t i = 0; i < len && text[i];) w += thingsos_glyph(ui_font(), NULL, flomo_utf8(text, &i), 0, 0);
    return w;
}
/* Preview as displayed: "- " list items become "• ". */
static void flomo_card_text(const thingsos_flomo_memo_t* m, char* out, size_t cap) {
    size_t o = 0; bool line_start = true;
    for (size_t i = 0; m->preview[i] && o + 4 < cap; ++i) {
        if (line_start && m->preview[i] == '-' && m->preview[i + 1] == ' ') {
            memcpy(out + o, "\xe2\x80\xa2 ", 4); o += 4; ++i; line_start = false; continue;
        }
        out[o++] = m->preview[i];
        line_start = m->preview[i] == '\n';
    }
    out[o] = 0;
}
static int flomo_body_height(unsigned lines) {
    return lines ? FLOMO_BODY_SIZE + (int)(lines - 1) * FLOMO_BODY_LINE : 0;
}
/* Lines the body needs (1..max), measured with the same layout it is drawn with. */
static unsigned flomo_body_lines(const char* text, unsigned max) {
    size_t len = strlen(text);
    if (!len) return 1;
    thingsos_font_size(ui_font(), FLOMO_BODY_SIZE);
    for (unsigned n = 1; n < max; ++n)
        if (thingsos_text_string_page(ui_font(), NULL, text, len, 0, 0, 0, 572, flomo_body_height(n), 150) >= len) return n;
    return max;
}
static int flomo_tags_height(const thingsos_flomo_memo_t* m) {return m->tags[0] ? 12 + 36 : 0;}
static int flomo_card_height(unsigned lines, const thingsos_flomo_memo_t* m) {
    return 16 + 30 + 14 + flomo_body_height(lines) + flomo_tags_height(m) + 20;
}
/* Secondary text: dark grey that still reads clearly on e-ink (black -> ~64). */
static void flomo_soften(int x, int y, int w, int h) {
    for (int yy = y; yy < y + h; ++yy)
        for (int xx = x; xx < x + w; ++xx) {
            unsigned char* p = &app.canvas.pixels[(size_t)yy * app.canvas.width + xx];
            *p = (unsigned char)(255 - (255 - *p) * 3 / 4);
        }
}
static void flomo_card_frame(int x, int y, int w, int h) {
    const unsigned char ink = 100;
    thingsos_rect(&app.canvas, x + 6, y, w - 12, 2, ink); thingsos_rect(&app.canvas, x + 6, y + h - 2, w - 12, 2, ink);
    thingsos_rect(&app.canvas, x, y + 6, 2, h - 12, ink); thingsos_rect(&app.canvas, x + w - 2, y + 6, 2, h - 12, ink);
    for (int i = 0; i < 6; ++i) { /* small rounded corners */
        int d = 6 - i;
        thingsos_rect(&app.canvas, x + i, y + d - 1, 1, 2, ink); thingsos_rect(&app.canvas, x + w - 1 - i, y + d - 1, 1, 2, ink);
        thingsos_rect(&app.canvas, x + i, y + h - d - 1, 1, 2, ink); thingsos_rect(&app.canvas, x + w - 1 - i, y + h - d - 1, 1, 2, ink);
    }
}
static void flomo_draw_tags_row(const char* tags, int x, int y, int width) {
    int cx = x;
    for (const char* t = tags; *t;) {
        while (*t == ' ') ++t;
        if (!*t) break;
        const char* end = strchr(t, ' ');
        size_t len = end ? (size_t)(end - t) : strlen(t);
        char chip[96]; snprintf(chip, sizeof(chip), "%.*s", (int)(len < 90 ? len : 90), t);
        int w = flomo_text_width(chip, strlen(chip), 20) + 20;
        if (w > width) w = width;
        if (cx + w > x + width) break;
        thingsos_rect(&app.canvas, cx, y, w, 36, 228);
        label(chip, cx + 10, y + 26, 20, w - 20);
        cx += w + 10;
        t += len;
    }
}
static void flomo_draw_card(const thingsos_flomo_memo_t* m, int y, unsigned lines, int h) {
    flomo_card_frame(36, y, 612, h);
    char when[24]; flomo_time(m->created_at, when, sizeof(when));
    label(when, 56, y + 42, 21, 400);
    const char* media = m->has_image && m->has_voice ? "图片·语音" : m->has_image ? "图片" : m->has_voice ? "语音" : NULL;
    if (media) {
        int w = flomo_text_width(media, strlen(media), 19) + 14;
        thingsos_rect(&app.canvas, 628 - w, y + 18, w, 30, 255);
        thingsos_rect(&app.canvas, 628 - w, y + 18, w, 1, 140); thingsos_rect(&app.canvas, 628 - w, y + 47, w, 1, 140);
        thingsos_rect(&app.canvas, 628 - w, y + 18, 1, 30, 140); thingsos_rect(&app.canvas, 627, y + 18, 1, 30, 140);
        centered_label(media, 628 - w, y + 40, 19, w);
    }
    /* Grey time and media: lighten the ink drawn so far in the card header. */
    flomo_soften(56, y + 14, 572, 40);
    char body[THINGSOS_FLOMO_PREVIEW_LEN + 64]; flomo_card_text(m, body, sizeof(body));
    thingsos_font_size(ui_font(), FLOMO_BODY_SIZE);
    thingsos_text_string_page(ui_font(), &app.canvas, body, strlen(body), 0, 56, y + 60, 572, flomo_body_height(lines), 150);
    if (m->tags[0]) flomo_draw_tags_row(m->tags, 56, y + 60 + flomo_body_height(lines) + 12, 572);
}
/* Draw cards from start until the page is full; returns how many were shown. */
static unsigned flomo_draw_cards(bool related, unsigned start, unsigned total) {
    int y = FLOMO_CARD_TOP;
    flomo_ui->card_count = 0;
    for (unsigned i = start; i < total && flomo_ui->card_count < FLOMO_CARDS_MAX; ++i) {
        thingsos_flomo_memo_t* m = &flomo_ui->scratch;
        if (!(related ? thingsos_flomo_related(i, m) : thingsos_flomo_memo(i, m))) break;
        char body[THINGSOS_FLOMO_PREVIEW_LEN + 64]; flomo_card_text(m, body, sizeof(body));
        unsigned lines = flomo_body_lines(body, 4);
        int h = flomo_card_height(lines, m);
        /* Shrink an oversized first card; later cards move to the next page. */
        while (y + h > FLOMO_CARD_BOTTOM && lines > 1 && flomo_ui->card_count == 0) h = flomo_card_height(--lines, m);
        if (y + h > FLOMO_CARD_BOTTOM && flomo_ui->card_count) break;
        flomo_draw_card(m, y, lines, h);
        flomo_ui->card_top[flomo_ui->card_count] = y;
        flomo_ui->card_bottom[flomo_ui->card_count] = y + h;
        ++flomo_ui->card_count;
        y += h + 14;
    }
    return flomo_ui->card_count;
}
static int flomo_card_at(int y) {
    for (unsigned i = 0; i < flomo_ui->card_count; ++i)
        if (y >= flomo_ui->card_top[i] && y < flomo_ui->card_bottom[i]) return (int)i;
    return -1;
}
static void flomo_draw_pager(unsigned start, unsigned shown, unsigned count, bool more) {
    char pstr[48];
    if (count) snprintf(pstr, sizeof(pstr), "%u-%u / %u", start + 1u, start + (shown ? shown : 1u), count);
    else strlcpy(pstr, "0 / 0", sizeof(pstr));
    reader_control("上一页", 36, 1006, 190, false);
    centered_label(pstr, 236, 1046, 24, 212);
    reader_control(more ? "加载更多" : "下一页", 458, 1006, 190, false);
}

static void flomo_draw_list(const thingsos_flomo_status_t* s) {
    header("flomo");
    if (s->configured) {
        reader_control(s->busy ? "同步中" : "刷新", 460, 124, 90, false);
        reader_control("设置", 558, 124, 90, false);
    }
    if (!s->configured && !s->memo_count) {
        centered_label("连接你的 flomo", 36, 330, 36, 612);
        centered_label("浏览、搜索笔记与每日回顾，查看相关笔记", 36, 405, 24, 612);
        reader_control("手机扫码配对", 36, 520, 612, true);
        centered_label("需要 flomo PRO 会员的 MCP Token，只读不写入", 36, 650, 22, 612);
        if (s->message[0]) centered_label(s->message, 36, 730, 22, 612);
        nav();
        return;
    }
    cubox_chip("最近", 36, 228, 100, 22, s->mode == THINGSOS_FLOMO_MODE_RECENT && !app.flomo_query[0]);
    cubox_chip("回顾", 146, 228, 100, 22, s->mode == THINGSOS_FLOMO_MODE_REVIEW);
    cubox_chip("标签", 256, 228, 110, 22, s->mode == THINGSOS_FLOMO_MODE_TAG);
    cubox_chip(app.flomo_query[0] ? "搜索*" : "搜索", 508, 228, 140, 22, app.flomo_query[0] != 0);

    char bar[300];
    if (s->mode == THINGSOS_FLOMO_MODE_REVIEW) strlcpy(bar, "今日每日回顾", sizeof(bar));
    else if (s->mode == THINGSOS_FLOMO_MODE_TAG && app.flomo_query[0])
        snprintf(bar, sizeof(bar), "#%s · 词：%s", s->tag, app.flomo_query);
    else if (s->mode == THINGSOS_FLOMO_MODE_TAG) snprintf(bar, sizeof(bar), "#%s · 点此更换标签", s->tag);
    else if (app.flomo_query[0]) snprintf(bar, sizeof(bar), "搜索：%s", app.flomo_query);
    else strlcpy(bar, "全部笔记 · 按时间倒序", sizeof(bar));
    label(bar, 36, 312, 22, 450);
    label(s->busy ? "同步中…" : (!thingsos_network_connected() || s->offline_mode) ? "离线缓存" : "在线",
          520, 312, 22, 128);
    flomo_soften(36, 290, 612, 30);

    if (flomo_ui->list_start >= s->memo_count) flomo_ui->list_start = 0;
    if (!s->memo_count) {
        centered_label(s->busy ? "正在获取 flomo 笔记…" : (s->message[0] ? s->message : "没有符合条件的笔记"),
                       36, 540, 27, 612);
        if (app.flomo_query[0]) reader_control("清空搜索关键词", 162, 620, 360, false);
    } else {
        if (!flomo_ui->list_start) flomo_ui->list_depth = 0;
        flomo_ui->shown = flomo_draw_cards(false, flomo_ui->list_start, s->memo_count);
    }
    if (!s->memo_count) { flomo_ui->shown = 0; flomo_ui->card_count = 0; }
    bool at_last = flomo_ui->list_start + flomo_ui->shown >= s->memo_count;
    flomo_draw_pager(flomo_ui->list_start, flomo_ui->shown, s->memo_count, at_last && s->has_more && thingsos_network_connected());
    if (s->message[0]) label(s->message, 36, 1095, 20, 612);
    nav();
}

static void flomo_draw_detail(const thingsos_flomo_status_t* s) {
    header("笔记");
    char meta[320];
    flomo_meta_line(&flomo_ui->memo, meta, sizeof(meta));
    label(meta, 36, 250, 22, 612);
    thingsos_rect(&app.canvas, 36, 272, 612, 1, 180);
    if (flomo_ui->page_start > flomo_ui->text_len) flomo_ui->page_start = 0;
    thingsos_font_size(ui_font(), 27);
    flomo_ui->page_next = thingsos_text_string_page(
        ui_font(), &app.canvas, flomo_ui->text, flomo_ui->text_len, flomo_ui->page_start,
        36, 292, 612, 650, 160);
    if (flomo_ui->page_next <= flomo_ui->page_start) flomo_ui->page_next = flomo_ui->text_len;

    const char* note = NULL;
    bool fetching = s->busy && s->op == THINGSOS_FLOMO_OP_DETAIL && !strcmp(s->active_id, flomo_ui->memo.id);
    if (fetching) note = "正在获取全文…";
    else if (!flomo_ui->text_complete) note = "当前为摘要，联网后可获取全文";
    if (note && flomo_ui->page_next >= flomo_ui->text_len) label(note, 36, 970, 21, 612);

    bool related = !strcmp(s->related_for, flomo_ui->memo.id) && s->related_count;
    reader_control("上一页", 36, 1006, 190, false);
    char mid[32];
    if (related) snprintf(mid, sizeof(mid), "相关 %u", s->related_count);
    else strlcpy(mid, fetching ? "加载中" : "相关笔记", sizeof(mid));
    reader_control(mid, 247, 1006, 190, false);
    reader_control("下一页", 458, 1006, 190, false);
    if (s->message[0] && !fetching) label(s->message, 36, 1095, 20, 612);
    nav();
}

static void flomo_draw_related(const thingsos_flomo_status_t* s) {
    header("相关笔记");
    label("与当前笔记内容相关", 36, 300, 22, 612);
    flomo_soften(36, 276, 612, 30);
    if (flomo_ui->related_start >= s->related_count) flomo_ui->related_start = 0;
    if (!flomo_ui->related_start) flomo_ui->related_depth = 0;
    flomo_ui->card_count = 0;flomo_ui->shown = 0;
    if (!s->related_count) centered_label("没有找到相关笔记", 36, 540, 27, 612);
    else flomo_ui->shown = flomo_draw_cards(true, flomo_ui->related_start, s->related_count);
    flomo_draw_pager(flomo_ui->related_start, flomo_ui->shown, s->related_count, false);
    nav();
}

static void flomo_draw_tags(const thingsos_flomo_status_t* s) {
    header("选择标签");
    reader_control(s->busy ? "同步中…" : "同步标签列表", 36, 226, 612, false);
    unsigned total = 1u + s->tag_count;
    if (flomo_ui->tag_start >= total) flomo_ui->tag_start = 0;
    for (unsigned row = 0; row < 5 && flomo_ui->tag_start + row < total; ++row) {
        unsigned idx = flomo_ui->tag_start + row;
        int y = 312 + (int)row * 130;
        char title[THINGSOS_FLOMO_TAG_LEN + 8];
        bool selected;
        if (idx == 0) {
            strlcpy(title, "全部笔记", sizeof(title));
            selected = s->mode != THINGSOS_FLOMO_MODE_TAG;
        } else if (thingsos_flomo_tag(idx - 1u, &flomo_ui->scratch_tag)) {
            snprintf(title, sizeof(title), "#%s", flomo_ui->scratch_tag.name);
            selected = s->mode == THINGSOS_FLOMO_MODE_TAG && !strcmp(s->tag, flomo_ui->scratch_tag.name);
        } else {
            continue;
        }
        if (selected) thingsos_rect(&app.canvas, 36, y + 6, 6, 92, 0);
        label(title, 54, y + 62, 28, 550);
        label(">", 615, y + 62, 26, 30);
        thingsos_rect(&app.canvas, 36, y + 116, 612, 1, 190);
    }
    if (!s->tag_count && !s->busy) centered_label(s->message[0] ? s->message : "点击上方按钮同步标签", 36, 560, 24, 612);
    reader_control("上一页", 36, 1006, 292, false);
    reader_control("下一页", 356, 1006, 292, false);
    nav();
}

static void flomo_draw_pair(const thingsos_flomo_status_t* s) {
    header("连接 flomo");
    char line[256], link[128];
    flomo_pair_link(link, sizeof(link));
    centered_label("手机与设备连接同一 WiFi", 36, 265, 26, 612);
    if (s->config_phase == THINGSOS_FLOMO_CONFIG_VERIFYING || s->config_phase == THINGSOS_FLOMO_CONFIG_FAILED) {
        bool verifying = s->config_phase == THINGSOS_FLOMO_CONFIG_VERIFYING;
        centered_label(verifying ? "正在验证 flomo…" : "flomo 连接未完成", 36, 370, 34, 612);
        thingsos_font_size(ui_font(), 26);
        thingsos_text_string_page(ui_font(), &app.canvas, s->message, strlen(s->message), 0, 48, 430, 588, 200, 145);
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
        centered_label("扫码粘贴 flomo MCP Token，无需解锁密码", 36, 690, 24, 612);
        snprintf(line, sizeof(line), "配对码：%s", s->code);
        centered_label(line, 36, 765, 38, 612);
        snprintf(line, sizeof(line), "剩余 %u 秒", s->pair_seconds);
        centered_label(line, 36, 810, 24, 612);
        reader_control("刷新配对码", 36, 868, 612, false);
        reader_control("关闭手机配对", 36, 974, 612, false);
    }
    nav();
}

static void flomo_draw_settings(const thingsos_flomo_status_t* s) {
    header("flomo 设置");
    setting_row("连接状态", s->configured ? "已连接（只读）" : "未连接", 260);
    setting_row("服务", "flomoapp.com/mcp", 365);
    char cnt[64];
    snprintf(cnt, sizeof(cnt), "%u 条笔记 · %u 个标签", s->memo_count, s->tag_count);
    setting_row("同步数据", cnt, 470);
    setting_row("本地缓存", "/sdcard/.thingsos/flomo", 575);
    reader_control(s->configured ? "更新 Token / 手机配对" : "手机扫码配对", 36, 745, 612, true);
    if (s->configured) reader_control("断开 flomo 连接", 36, 850, 612, false);
    reader_control("返回列表", 36, 955, 612, false);
    if (s->message[0]) label(s->message, 36, 1080, 22, 612);
    nav();
}

static bool flomo_draw(void) {
    if (app.page != THINGSOS_UI_FLOMO || !flomo_ready()) return false;
    thingsos_flomo_status(&flomo_ui->status);
    flomo_ui->last_serial = flomo_ui->status.serial;
    const thingsos_flomo_status_t* s = &flomo_ui->status;
    switch (flomo_ui->view) {
    case FLOMO_VIEW_DETAIL: flomo_draw_detail(s); break;
    case FLOMO_VIEW_TAGS: flomo_draw_tags(s); break;
    case FLOMO_VIEW_PAIR: flomo_draw_pair(s); break;
    case FLOMO_VIEW_SETTINGS: flomo_draw_settings(s); break;
    case FLOMO_VIEW_RELATED: flomo_draw_related(s); break;
    default: flomo_draw_list(s); break;
    }
    return true;
}

static bool flomo_back(void) {
    if (!flomo_ready()) return false;
    if (flomo_ui->view == FLOMO_VIEW_PAIR) {
        thingsos_flomo_pair(false);
        flomo_ui->view = flomo_ui->pair_return;
    } else if (flomo_ui->view == FLOMO_VIEW_DETAIL) {
        flomo_ui->view = flomo_ui->detail_return == FLOMO_VIEW_RELATED ? FLOMO_VIEW_RELATED : FLOMO_VIEW_LIST;
    } else if (flomo_ui->view == FLOMO_VIEW_RELATED) {
        /* Back to the memo whose related list this is. */
        strlcpy(flomo_ui->active_id, flomo_ui->parent_id, sizeof(flomo_ui->active_id));
        flomo_ui->detail_return = FLOMO_VIEW_LIST;
        flomo_ui->text_len = (size_t)-1;
        flomo_ui->view = FLOMO_VIEW_DETAIL;
        flomo_load_detail();
    } else if (flomo_ui->view == FLOMO_VIEW_TAGS || flomo_ui->view == FLOMO_VIEW_SETTINGS) {
        flomo_ui->view = FLOMO_VIEW_LIST;
    } else {
        navigate(THINGSOS_UI_APPS);
        return true;
    }
    app.dirty = true;
    return true;
}

static void flomo_say_status(const char* fallback) {
    thingsos_flomo_status(&flomo_ui->status);
    say(flomo_ui->status.message[0] ? flomo_ui->status.message : fallback);
}

static bool flomo_page(int dir) {
    if (flomo_ui->view == FLOMO_VIEW_DETAIL) {
        if (dir > 0 && flomo_ui->page_next < flomo_ui->text_len) {
            if (flomo_ui->page_depth < FLOMO_UI_PAGE_STACK) flomo_ui->page_stack[flomo_ui->page_depth++] = flomo_ui->page_start;
            flomo_ui->page_start = flomo_ui->page_next;
            app.dirty = true;
        } else if (dir < 0 && flomo_ui->page_depth) {
            flomo_ui->page_start = flomo_ui->page_stack[--flomo_ui->page_depth];
            app.dirty = true;
        } else if (dir < 0 && flomo_ui->page_start) {
            flomo_ui->page_start = 0;
            app.dirty = true;
        }
        return true;
    }
    const thingsos_flomo_status_t* s = &flomo_ui->status;
    if (flomo_ui->view == FLOMO_VIEW_LIST || flomo_ui->view == FLOMO_VIEW_RELATED) {
        bool related = flomo_ui->view == FLOMO_VIEW_RELATED;
        unsigned* start = related ? &flomo_ui->related_start : &flomo_ui->list_start;
        unsigned* stack = related ? flomo_ui->related_stack : flomo_ui->list_stack;
        unsigned* depth = related ? &flomo_ui->related_depth : &flomo_ui->list_depth;
        unsigned total = related ? s->related_count : s->memo_count;
        unsigned step = flomo_ui->shown ? flomo_ui->shown : 1u;
        if (dir > 0) {
            if (*start + step < total) {
                if (*depth < FLOMO_UI_PAGE_STACK) stack[(*depth)++] = *start;
                *start += step;
                app.dirty = true;
            } else if (!related && s->has_more && thingsos_network_connected() && !s->busy) {
                if (!thingsos_flomo_refresh_list(true)) flomo_say_status("加载失败");
                app.dirty = true;
            }
        } else if (*start > 0) {
            *start = *depth ? stack[--*depth] : 0u;
            app.dirty = true;
        }
        return true;
    }
    unsigned* start = &flomo_ui->tag_start;
    unsigned step = 5u;
    unsigned total = 1u + s->tag_count;
    if (dir > 0) {
        if (*start + step < total) {
            *start += step;
            app.dirty = true;
        } else if (flomo_ui->view == FLOMO_VIEW_LIST && s->has_more && thingsos_network_connected() && !s->busy) {
            if (!thingsos_flomo_refresh_list(true)) flomo_say_status("加载失败");
            app.dirty = true;
        }
    } else if (*start > 0) {
        *start = *start >= step ? *start - step : 0u;
        app.dirty = true;
    }
    return true;
}

static bool flomo_tap(int x, int y) {
    if (app.page != THINGSOS_UI_FLOMO || !flomo_ready()) return false;
    if (x < 120 && y >= 74 && y < 200) return flomo_back();
    const thingsos_flomo_status_t* s = &flomo_ui->status;

    if (flomo_ui->view == FLOMO_VIEW_PAIR) {
        if (y >= 868 && y < 940) {
            if (s->config_phase != THINGSOS_FLOMO_CONFIG_VERIFYING) flomo_start_pair(flomo_ui->pair_return);
        } else if (y >= 974 && y < 1046) {
            thingsos_flomo_pair(false);
            flomo_ui->view = flomo_ui->pair_return;
            app.dirty = true;
        }
        return true;
    }
    if (flomo_ui->view == FLOMO_VIEW_SETTINGS) {
        if (y >= 745 && y < 817) {
            flomo_start_pair(FLOMO_VIEW_SETTINGS);
        } else if (s->configured && y >= 850 && y < 922) {
            if (!thingsos_flomo_disconnect()) say("请等待当前网络操作完成");
            else {
                say("已断开 flomo 连接");
                flomo_ui->view = FLOMO_VIEW_LIST;
            }
            app.dirty = true;
        } else if (y >= 955 && y < 1027) {
            flomo_ui->view = FLOMO_VIEW_LIST;
            app.dirty = true;
        }
        return true;
    }
    if (flomo_ui->view == FLOMO_VIEW_TAGS) {
        if (y >= 226 && y < 294) {
            if (!thingsos_flomo_refresh_tags()) flomo_say_status("无法同步标签");
            app.dirty = true;
        } else if (y >= 312 && y < 962) {
            unsigned idx = flomo_ui->tag_start + (unsigned)(y - 312) / 130u;
            flomo_ui->list_start = 0;
            if (idx == 0) {
                flomo_ui->view = FLOMO_VIEW_LIST;
                thingsos_flomo_set_filter(THINGSOS_FLOMO_MODE_RECENT, "", app.flomo_query, true);
            } else if (thingsos_flomo_tag(idx - 1u, &flomo_ui->scratch_tag)) {
                flomo_ui->view = FLOMO_VIEW_LIST;
                thingsos_flomo_set_filter(THINGSOS_FLOMO_MODE_TAG, flomo_ui->scratch_tag.name, app.flomo_query, true);
            }
            app.dirty = true;
        } else if (y >= 1006 && y < 1078) {
            flomo_page(x < 342 ? -1 : 1);
        }
        return true;
    }
    if (flomo_ui->view == FLOMO_VIEW_DETAIL) {
        if (y >= 1006 && y < 1078) {
            if (x < 236) flomo_page(-1);
            else if (x >= 448) flomo_page(1);
            else if (!strcmp(s->related_for, flomo_ui->memo.id)) {
                flomo_ui->related_start = 0;
                strlcpy(flomo_ui->parent_id, flomo_ui->memo.id, sizeof(flomo_ui->parent_id));
                flomo_ui->view = FLOMO_VIEW_RELATED;
                app.dirty = true;
            } else if (!s->busy) {
                if (!thingsos_flomo_fetch_detail(flomo_ui->memo.id)) flomo_say_status("无法获取相关笔记");
                app.dirty = true;
            }
        } else if (y >= 272 && y < 1000) {
            flomo_page(x < 342 ? -1 : 1);
        }
        return true;
    }
    if (flomo_ui->view == FLOMO_VIEW_RELATED) {
        if (flomo_card_at(y) >= 0) {
            unsigned idx = flomo_ui->related_start + (unsigned)flomo_card_at(y);
            if (thingsos_flomo_related(idx, &flomo_ui->scratch)) flomo_open_detail(&flomo_ui->scratch, FLOMO_VIEW_RELATED);
        } else if (y >= 1006 && y < 1078) {
            flomo_page(x < 240 ? -1 : 1);
        }
        return true;
    }

    /* FLOMO_VIEW_LIST */
    if (s->configured && y >= 120 && y < 195) {
        if (x >= 460 && x < 554) {
            if (!thingsos_flomo_refresh_list(false)) flomo_say_status("刷新失败");
            app.dirty = true;
            return true;
        }
        if (x >= 556 && x < 652) {
            flomo_ui->view = FLOMO_VIEW_SETTINGS;
            app.dirty = true;
            return true;
        }
    }
    if (!s->configured && !s->memo_count) {
        if (y >= 520 && y < 600) flomo_start_pair(FLOMO_VIEW_LIST);
        return true;
    }
    if (y >= 222 && y < 284) {
        flomo_ui->list_start = 0;
        if (x >= 36 && x < 141) {
            app.flomo_query[0] = 0;
            thingsos_flomo_set_filter(THINGSOS_FLOMO_MODE_RECENT, "", "", true);
        } else if (x >= 141 && x < 251) {
            thingsos_flomo_set_filter(THINGSOS_FLOMO_MODE_REVIEW, "", "", true);
        } else if (x >= 251 && x < 371) {
            flomo_ui->tag_start = 0;
            flomo_ui->view = FLOMO_VIEW_TAGS;
            if (!s->tag_count && thingsos_network_connected() && s->configured && !s->busy) thingsos_flomo_refresh_tags();
        } else if (x >= 500 && x < 648) {
            feature_editor(5);
        }
        app.dirty = true;
        return true;
    }
    if (y >= 284 && y < 324 && x < 500 && s->mode == THINGSOS_FLOMO_MODE_TAG) {
        flomo_ui->tag_start = 0;
        flomo_ui->view = FLOMO_VIEW_TAGS;
        app.dirty = true;
        return true;
    }
    if (!s->memo_count && app.flomo_query[0] && y >= 620 && y < 692) {
        app.flomo_query[0] = 0;
        flomo_search_submit();
        return true;
    }
    if (s->memo_count && flomo_card_at(y) >= 0) {
        unsigned idx = flomo_ui->list_start + (unsigned)flomo_card_at(y);
        if (thingsos_flomo_memo(idx, &flomo_ui->scratch)) flomo_open_detail(&flomo_ui->scratch, FLOMO_VIEW_LIST);
        return true;
    }
    if (y >= 1006 && y < 1078) {
        if (x < 240) flomo_page(-1);
        else if (x >= 440) flomo_page(1);
    }
    return true;
}

static bool flomo_scroll(int dy) {
    if (app.page != THINGSOS_UI_FLOMO || !flomo_ready()) return false;
    if (flomo_ui->view == FLOMO_VIEW_PAIR || flomo_ui->view == FLOMO_VIEW_SETTINGS) return false;
    return flomo_page(dy > 0 ? 1 : -1);
}

static bool flomo_key(thingsos_key_action_t action) {
    if (app.page != THINGSOS_UI_FLOMO || !flomo_ready()) return false;
    if (action == THINGSOS_KEY_MENU) return flomo_back();
    if (flomo_ui->view != FLOMO_VIEW_PAIR && flomo_ui->view != FLOMO_VIEW_SETTINGS) {
        if (action == THINGSOS_KEY_NEXT) return flomo_page(1);
        if (action == THINGSOS_KEY_PREVIOUS) return flomo_page(-1);
    }
    return true;
}
