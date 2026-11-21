// SPDX-License-Identifier: Apache-2.0
/* Native network pages follow the approved prototype, with real device state. */
static void feature_input_layout(bool, int, int);
static void network_wake_after_sleep(void) {
    /* A manual-connect UI deadline must not include time spent asleep. The
       network layer owns the fresh, bounded background reconnect attempt. */
    app.wifi_requested=0;
    thingsos_network_wake();
}
/* AP is scoped to an awake transfer page without an associated station. */
static void network_hotspot_policy(void) {
    bool wanted=app.wifi_enabled && !app.asleep && app.page==THINGSOS_UI_TRANSFER && !thingsos_network_connected();
    if(wanted!=app.hotspot && thingsos_network_mode(wanted)==ESP_OK) {
        app.hotspot=wanted;background_redraw();
    }
}
static void network_connection_poll(void) {
    network_hotspot_policy();
    bool connected=thingsos_network_connected();
    if(connected!=app.wifi_connected) {
        app.wifi_connected=connected;background_redraw();
    }
    if(app.wifi_requested && (connected || !thingsos_network_connecting())) {
        app.wifi_requested=0;
        if(!connected) {
            const char* failure=thingsos_network_failure();
            say(failure[0] ? failure : "连接未完成，请重新连接");
        }
    }
    if(app.page==THINGSOS_UI_TRANSFER) {
        static char prior_address[80];char address[80];
        thingsos_network_address(app.hotspot,address,sizeof(address));
        if(strcmp(prior_address,address)){strlcpy(prior_address,address,sizeof(prior_address));app.dirty=true;}
    }
}
static void network_outline(int x, int y, int w, int h) {
    thingsos_rect(&app.canvas, x, y, w, 2, 0);
    thingsos_rect(&app.canvas, x, y + h - 2, w, 2, 0);
    thingsos_rect(&app.canvas, x, y, 2, h, 0);
    thingsos_rect(&app.canvas, x + w - 2, y, 2, h, 0);
}
static void network_button(const char* text, int x, int y, int w, bool primary) {
    if (primary) thingsos_rect(&app.canvas, x, y, w, 68, 225);
    network_outline(x, y, w, 68);
    centered_label(text, x + 8, y + 45, 27, w - 16);
}
static bool wifi_has_card(void) {
    return thingsos_network_connected() || thingsos_network_saved_name()[0];
}
static int wifi_rows(void) { return wifi_has_card() ? 2 : 4; }
static int wifi_base(void) { return wifi_has_card() ? 570 : 360; }
static void wifi_scan(bool force) {
    if(!app.wifi_enabled){app.wifi_scan_pending=false;return;}
    app.list_start = 0;
    if(!app.wifi_scan_running && !app.wifi_scan_pending &&
       (force || !app.wifi_scan_valid || now()-app.wifi_scan_at>=30000))app.wifi_scan_pending=true;
    app.dirty = true;
}
static void wifi_scan_step(void) {
    /* Start only after the entry frame has been presented. No radio work in navigate(). */
    if(app.wifi_scan_pending && !app.wifi_scan_running && !app.dirty) {
        app.wifi_scan_pending=false;
        if(app.page==THINGSOS_UI_WIFI && !app.asleep) {
            esp_err_t e=thingsos_network_scan_start();
            app.wifi_scan_running=e==ESP_OK;
            if(e!=ESP_OK)say("暂时无法扫描，请稍后重试");
        }
    }
    if(!app.wifi_scan_running)return;
    thingsos_wifi_result_t results[12];size_t count=0;bool done=false;
    esp_err_t error=thingsos_network_scan_poll(results,12,&count,&done);
    if(!done)return;
    app.wifi_scan_running=false;app.wifi_scanned=true;
    if(error==ESP_OK) {
        memcpy(app.wifi_networks,results,count*sizeof(*results));app.wifi_count=count;
        app.wifi_scan_valid=true;app.wifi_scan_at=now();
        if(app.list_start>=count)app.list_start=0;
    }
    if(app.page==THINGSOS_UI_WIFI && !app.asleep) {
        if(error!=ESP_OK && !app.wifi_requested)say("扫描失败，请重新扫描");
        app.dirty=true;
    }
}
static void wifi_draw(void) {
    header("WiFi 网络");
    label(thingsos_network_connected() ? thingsos_network_saved_name() : "选择可用网络", 36, 195, 24, 612);
    label("WiFi",36,287,30,400);reader_control(app.wifi_enabled?"开启":"关闭",504,235,144,app.wifi_enabled);
    if(!app.wifi_enabled){centered_label("WiFi 已关闭",36,490,34,612);centered_label("开启后连接已保存网络",36,555,25,612);nav();return;}
    if (wifi_has_card()) {
        network_outline(36,330,612,184);
        label(thingsos_network_saved_name(),60,377,32,564);
        label(thingsos_network_connected()?"已连接":app.wifi_requested?"正在连接…":"尚未连接",60,428,25,564);
        network_button(thingsos_network_connected()?"断开连接":"再次连接",60,438,240,!thingsos_network_connected());
        network_button("忘记网络",432,438,192,false);
    }
    int base = wifi_base(), rows = wifi_rows();
    label(app.wifi_scan_pending || app.wifi_scan_running ? "可用网络 · 正在扫描…" : "可用网络", 36, base - 30, 25, 612);
    for (int i = 0; i < rows && app.list_start + i < app.wifi_count; ++i) {
        thingsos_wifi_result_t* w = &app.wifi_networks[app.list_start + i];
        int y = base + i * 114;
        bool selected = thingsos_network_connected() && !strcmp(w->ssid, thingsos_network_saved_name());
        if (selected) thingsos_rect(&app.canvas, 36, y, 602, 113, 235);
        for (int bar = 0; bar < 3; ++bar)
            thingsos_rect(&app.canvas, 40 + bar * 11, y + 64 - bar * 10, 6, 10 + bar * 10, 0);
        label(w->ssid, 94, y + 43, 30, 495);
        char detail[96];
        snprintf(detail, sizeof(detail), "%s · %s", w->rssi >= -55 ? "信号强" : w->rssi >= -75 ? "信号良好" : "信号较弱", selected ? "已连接" : thingsos_network_saved(w->ssid) ? "已保存 · 点击连接" : w->open ? "开放网络" : "需要密码");
        label(detail, 94, y + 83, 23, 495);
        label(">", 610, y + 65, 30, 28);
        thingsos_rect(&app.canvas, 36, y + 113, 602, 1, 180);
    }
    if (!app.wifi_count) centered_label(app.wifi_scan_pending || app.wifi_scan_running ? "正在搜索 WiFi，可返回或手动输入网络" : app.wifi_scanned ? "未发现网络 · 可重新扫描" : "点击重新扫描搜索网络", 36, base + 150, 25, 612);
    if (app.wifi_count > (size_t)rows) {
        int h = rows * 114, thumb = h * rows / (int)app.wifi_count;
        if (thumb < 24) thumb = 24;
        size_t start = app.list_start < app.wifi_count - rows ? app.list_start : app.wifi_count - rows;
        thingsos_rect(&app.canvas, 650, base, 5, h, 210);
        thingsos_rect(&app.canvas, 650, base + (int)(start * (h - thumb) / (app.wifi_count - rows)), 5, thumb, 0);
    }
    network_button(app.wifi_scan_pending || app.wifi_scan_running ? "扫描中…" : app.wifi_scanned ? "重新扫描" : "扫描网络", 36, 856, 298, false);
    network_button("使用热点", 350, 856, 298, false);
    label(app.message[0] ? app.message : "连接后，在同一 WiFi 中打开设备地址传书", 36, 966, 23, 612);
    network_button("手动输入网络", 36, 990, 612, false);
    nav();
}
/* Password and book search share one key geometry for painting and hit testing. */
static bool input_uses_qwerty(void) {return app.editing<2 || app.editing==5 || app.editing==6;}
#include "../components/thingsos_board/keyboard_touch.h"
/* Convert the hardware sample once, before either contact or release dispatch.
   wifi_key_hit() and all candidate helpers continue to use painted coordinates. */
static void keyboard_touch_coordinates(int* x,int* y) {
    if(app.page==THINGSOS_UI_PASSWORD && input_uses_qwerty() && *y>=638 && *y<1094)
        thingsos_metalio_keyboard_coordinates(x,y);
}
typedef struct {int x,y,w,code;char text[20];} input_key_t;
static void input_key_add(input_key_t* keys,size_t* count,int x,int y,int w,int code,const char* text) {
    input_key_t* key=&keys[(*count)++];
    *key=(input_key_t){.x=x,.y=y,.w=w,.code=code};strlcpy(key->text,text,sizeof(key->text));
}
static size_t input_keyboard_keys(input_key_t keys[40]) {
    size_t count=0;
    bool symbols=app.input.mode==THINGSOS_KEYS_SYMBOLS;
    for(unsigned row=0;row<3;++row) {
        const char* chars=symbols?
            (app.wifi_symbols_more?(row==0?"[]{}<>\\|~`":row==1?"\"',.!@#$%)":":;/?_-=+("):
                                    (row==0?"1234567890":row==1?"!@#$%^&*()":"-_+=/:;?.")):
            (row==0?"qwertyuiop":row==1?"asdfghjkl":"zxcvbnm");
        int length=(int)strlen(chars),left=symbols?36:row==2?122:36+(10-length)*61/2;
        for(int col=0;col<length;++col) {
            char value=chars[col];
            if(!symbols && app.input.mode==THINGSOS_KEYS_UPPER)value=(char)(value-'a'+'A');
            char text[2]={symbols?value:(char)(chars[col]-'a'+'A'),0};
            input_key_add(keys,&count,left+col*61,754+(int)row*82,55,100+(unsigned char)value,text);
        }
    }
    if(!symbols)input_key_add(keys,&count,36,918,78,40,"");
    input_key_add(keys,&count,symbols?585:570,918,symbols?55:78,43,"");
    input_key_add(keys,&count,36,1000,90,41,symbols?"ABC":"123");
    input_key_add(keys,&count,134,1000,58,symbols?40:50,symbols?"更多":app.input.chinese?"，":",");
    input_key_add(keys,&count,200,1000,240,42,"空格");
    input_key_add(keys,&count,448,1000,76,49,app.input.chinese?"中 / 英":"英 / 中");
    input_key_add(keys,&count,532,1000,116,48,app.editing==5?"搜索":app.editing==1?"连接":"下一步");
    return count;
}
/* Every keycap is painted 68 px tall (input_key_border draws exactly that many
   rows) on all four rows, including the bottom one, so hit testing uses the same
   height and the same outer rectangle: a tap below or beside a drawn keycap must
   not type a key the keyboard never drew there. The old test gave the bottom row
   90 px of height and clamped x into 28..655 before choosing the nearest centre,
   so a tap in the blank strip under the space bar (or 20 px left of "q") typed a
   key that is not under the finger, and a tap 7 px above the space bar could be
   claimed by "中 / 英" because that key's centre is nearer even though the point
   is well inside the space bar's own width. */
#define INPUT_KEY_HEIGHT 68
#define INPUT_KEY_TOP 746      /* top of the keyboard's painted white area */
#define INPUT_KEY_BOTTOM 1076  /* its bottom edge: nothing below it is a key */
#define INPUT_KEY_LEFT 28
#define INPUT_KEY_RIGHT 656
static int input_key_row(const input_key_t* key) {
    if(key->y>=1000)return 3;
    int row=(key->y-754)/82;
    return row<0?0:row>2?2:row;
}
static int input_key_row_top(int row) {return row==3?1000:754+row*82;}
static int input_key_edge(int key_x,int key_w,int x) {
    return x<key_x?key_x-x:x>=key_x+key_w?x-(key_x+key_w-1):0;
}
static int wifi_key_hit(int x,int y) {
    if(x<INPUT_KEY_LEFT || x>=INPUT_KEY_RIGHT || y<INPUT_KEY_TOP || y>=INPUT_KEY_BOTTOM)return -1;
    input_key_t keys[40];size_t count=input_keyboard_keys(keys);
    for(size_t i=0;i<count;++i) {
        int top=input_key_row_top(input_key_row(&keys[i]));
        if(x>=keys[i].x && x<keys[i].x+keys[i].w && y>=top && y<top+INPUT_KEY_HEIGHT)
            return keys[i].code;
    }
    /* The painted keycaps leave 6 px between columns and 14 px between rows. A
       tap in one of those seams used to return -1, which read as an unresponsive
       key ("0 needs several presses"). Resolve a seam in two independent steps:
       first the row whose painted band is vertically nearest, then the keycap of
       that row whose painted edge is horizontally nearest. Keeping the row fixed
       is what stops an action key from taking a letter-row tap (and the wide
       space bar from taking a tap on the "中 / 英" edge). Equal distances keep
       the left-hand key, so the split is stable from one tap to the next. */
    int row=0,best_dy=INT_MAX;
    for(int candidate=0;candidate<4;++candidate) {
        int top=input_key_row_top(candidate);
        int dy=y<top?top-y:y>=top+INPUT_KEY_HEIGHT?y-(top+INPUT_KEY_HEIGHT-1):0;
        if(dy<best_dy){best_dy=dy;row=candidate;}
    }
    int best=-1,best_dx=INT_MAX;
    for(size_t i=0;i<count;++i) {
        if(input_key_row(&keys[i])!=row)continue;
        int d=input_key_edge(keys[i].x,keys[i].w,x);
        if(d<best_dx){best_dx=d;best=keys[i].code;}
    }
    return best;
}
static thingsos_font_t keyboard_font;
static thingsos_font_t* keyboard_typeface(void) {
    if(!keyboard_font.face) {
#ifdef THINGSOS_HOST_UI
        bool loaded=thingsos_font_file(&keyboard_font,THINGSOS_KEYBOARD_FONT_PATH);
#else
        extern const unsigned char start[] asm("_binary_keyboard_sans_ttf_start");
        extern const unsigned char end[] asm("_binary_keyboard_sans_ttf_end");
        bool loaded=thingsos_font_memory(&keyboard_font,start,(size_t)(end-start));
#endif
        if(!loaded)return NULL;
        keyboard_font.monochrome=true;
    }
    return &keyboard_font;
}
static unsigned keyboard_scaled_px(unsigned px,unsigned maximum) {
    unsigned scale=app.font.ui_size_percent?app.font.ui_size_percent:100;
    px=(px*scale+50)/100;
    return px>maximum?maximum:px<16?16:px;
}
static void keyboard_label(const char* text,int x,int baseline,unsigned px,int width,bool centered,unsigned maximum) {
    px=keyboard_scaled_px(px,maximum);
    thingsos_font_t* font=keyboard_typeface();
    if(!font) {
        unsigned scale=app.font.ui_size_percent;app.font.ui_size_percent=0;
        if(centered)centered_label(text,x,baseline,px,width);
        else label(text,x,baseline,px,width);
        app.font.ui_size_percent=scale;
        return;
    }
    font->ui_size_percent=0;
    font->ui_weight_override=app.font.ui_weight_override;
    thingsos_font_weight(font,0);thingsos_font_size(font,px);
    unsigned loads=font->glyph_loads,hits=font->bitmap_hits;
    if(centered)thingsos_label_centered(font,&app.canvas,text,x,baseline,width);
    else thingsos_label(font,&app.canvas,text,x,baseline,width);
    app.font.glyph_loads+=font->glyph_loads-loads;
    app.font.bitmap_hits+=font->bitmap_hits-hits;
    font->ui_size_percent=app.font.ui_size_percent;
}
static void input_key_border(int x,int y,int w) {
    for(int row=0;row<68;++row) {
        int edge=row<34?row:67-row;
        int inset=0;
        if(edge<8) {
            int dy=7-edge,span=8;
            while(span*span+dy*dy>64)--span;
            inset=8-span;
        }
        if(edge<2)thingsos_rect(&app.canvas,x+inset,y+row,w-2*inset,1,0);
        else {
            thingsos_rect(&app.canvas,x+inset,y+row,2,1,0);
            thingsos_rect(&app.canvas,x+w-inset-2,y+row,2,1,0);
        }
    }
}
static void input_keyboard_draw(void) {
    thingsos_rect(&app.canvas,28,746,628,330,255);
    input_key_t keys[40];size_t count=input_keyboard_keys(keys);
    for(size_t i=0;i<count;++i) {
        input_key_t* key=&keys[i];
        input_key_border(key->x,key->y,key->w);
        int cx=key->x+key->w/2,cy=key->y+32;
        if(key->code==40 && app.input.mode!=THINGSOS_KEYS_SYMBOLS) {
            toolbar_stroke(cx-13,cy,cx,cy-14);toolbar_stroke(cx,cy-14,cx+13,cy);
            toolbar_stroke(cx-13,cy,cx-6,cy);toolbar_stroke(cx+6,cy,cx+13,cy);
            toolbar_stroke(cx-6,cy,cx-6,cy+12);toolbar_stroke(cx+6,cy,cx+6,cy+12);
            toolbar_stroke(cx-6,cy+12,cx+6,cy+12);
            if(app.input.mode==THINGSOS_KEYS_UPPER)thingsos_rect(&app.canvas,cx-8,cy+17,16,2,0);
        } else if(key->code==43) {
            toolbar_stroke(cx-17,cy,cx-7,cy-12);toolbar_stroke(cx-7,cy-12,cx+16,cy-12);
            toolbar_stroke(cx+16,cy-12,cx+16,cy+12);toolbar_stroke(cx+16,cy+12,cx-7,cy+12);
            toolbar_stroke(cx-7,cy+12,cx-17,cy);toolbar_stroke(cx-3,cy-5,cx+7,cy+5);toolbar_stroke(cx-3,cy+5,cx+7,cy-5);
        } else keyboard_label(key->text,key->x,key->y+45,key->code==49?19:key->code>=100?32:25,key->w,true,48);
    }
    /* DU drives black/white glyphs on white, without gray keycaps or shadows. */
    for(int y=746;y<1076;++y)for(int x=28;x<656;++x) {
        unsigned char* pixel=&app.canvas.pixels[(size_t)y*684+x];
        *pixel=*pixel<128?0:255;
    }
}
static void wifi_input_draw(bool field,bool controls) {
    bool search=app.editing==5;
    if(field) {
        thingsos_rect(&app.canvas,36,300,612,120,255);
        network_outline(36,300,612,120);feature_input_layout(true,0,0);
    }
    if(!controls)return;
    thingsos_rect(&app.canvas,36,440,612,114,255);
    if(app.editing==1)network_button(app.input.reveal?"隐藏密码":"显示密码",36,440,298,false);
    else {network_button("<",36,440,140,false);network_button(">",194,440,140,false);}
    network_button("清空",350,440,298,false);
    label(app.message[0]?app.message:search?(app.input_return==THINGSOS_UI_FILES?"清空后搜索，显示当前目录全部文件":"清空后搜索，即可显示全部书籍"):app.editing==1 && !app.wifi_requires_password?"开放网络密码可留空，点击输入框移动光标":"点击输入框可移动光标",36,542,23,612);
}
/* Rows of the candidate band, measured from the keyboard's first keycap at 746.
   A 28 px CJK glyph reaches 24 px above its baseline and 1-2 px below, so the
   candidate baseline needs more than two glyph heights of clearance: the band
   used to end at 736, only 9 px above the white rectangle that erases and
   repaints the keyboard, and any UI font scale (ui_px up to 34 is 113 %) or a
   one-pixel baseline drift made the descender land on that edge, which reads as
   the bottom of the character being cut off. Two extra lines share this band,
   so the count row and the composition row move up with the candidates and the
   composition stops before the count column instead of running underneath it. */
#define INPUT_COUNT_BASELINE 660
#define INPUT_COMPOSITION_BASELINE 692
#define INPUT_CANDIDATE_BASELINE 730
#define INPUT_CANDIDATE_WIDTH 196
#define INPUT_COUNT_LEFT 420
#define INPUT_COUNT_WIDTH 228
#define INPUT_COMPOSITION_WIDTH 458
/* Candidate paging and commit share these rectangles with the drawing code, so
   a candidate that is painted is always the candidate that a tap commits. */
#define INPUT_CANDIDATE_BAND_TOP 700
#define INPUT_CANDIDATE_BAND_BOTTOM 746
#define INPUT_PAGE_PREV_LEFT 510
#define INPUT_PAGE_PREV_RIGHT 576
#define INPUT_PAGE_NEXT_LEFT 584
#define INPUT_PAGE_NEXT_RIGHT 650
#define INPUT_PAGE_BAND_TOP 666
#define INPUT_PAGE_BAND_BOTTOM 704
static bool input_hit_page(int x,int y) {
    if(y<INPUT_PAGE_BAND_TOP || y>=INPUT_PAGE_BAND_BOTTOM)return false;
    return (x>=INPUT_PAGE_PREV_LEFT && x<INPUT_PAGE_PREV_RIGHT) ||
        (x>=INPUT_PAGE_NEXT_LEFT && x<INPUT_PAGE_NEXT_RIGHT);
}
static bool input_hit_candidate(int x,int y) {
    return y>=INPUT_CANDIDATE_BAND_TOP && y<INPUT_CANDIDATE_BAND_BOTTOM &&
        x>=36 && x<36+THINGSOS_IME_PAGE_SIZE*204;
}
static size_t input_candidate_index(int x) {return (size_t)(x-36)/204;}
/* One type size for the whole candidate row. The row used to shrink each word
   that happened to be longer than 18 characters to 22 px while its neighbours
   stayed at 28 px, so a page of association candidates rendered three different
   glyph heights side by side, and the shrunken one read as a half-painted
   character. Measure the row first, then pick the largest size that fits all
   three columns, so every candidate on a page is drawn at the same size. */
static uint32_t input_utf8_next(const char** cursor) {
    const unsigned char* p=(const unsigned char*)*cursor;
    unsigned n=p[0]<0x80?1:p[0]>=0xc2 && p[0]<0xe0?2:p[0]>=0xe0 && p[0]<0xf0?3:p[0]>=0xf0 && p[0]<=0xf4?4:0;
    if(!n){++*cursor;return 0xfffd;}
    uint32_t cp=p[0]&(n==1?0x7f:n==2?0x1f:n==3?0x0f:0x07);
    for(unsigned i=1;i<n;++i) {
        if((p[i]&0xc0)!=0x80){++*cursor;return 0xfffd;}
        cp=(cp<<6)|(p[i]&0x3f);
    }
    *cursor+=n;
    return cp;
}
static bool input_text_fits(thingsos_font_t* font,const char* text,int width) {
    int used=0;
    for(const char* p=text;*p;) {
        used+=thingsos_glyph(font,NULL,input_utf8_next(&p),0,0);
        if(used>width)return false;
    }
    return true;
}
/* Tail of a long composition that still fits the row. The old code kept the last
   26 *bytes*, which both dropped more pinyin than the row can hold and could
   split a multi-byte character in half; measuring whole characters keeps the
   line legible and never emits a replacement glyph. */
static const char* input_text_tail(thingsos_font_t* font,const char* text,int width) {
    const char* start=text;
    for(const char* p=text;*p;) {
        const char* step=p;
        input_utf8_next(&step);
        if(input_text_fits(font,step,width)){start=p;break;}
        p=step;
    }
    while(*start && ((unsigned char)start[0]&0xc0)==0x80)++start;
    return start;
}
static unsigned input_candidate_size(char words[THINGSOS_IME_PAGE_SIZE][THINGSOS_IME_CANDIDATE_BYTES],size_t count) {
    thingsos_font_t* font=keyboard_typeface();
    static const unsigned steps[]={28,26,24,22,20,18};
    const size_t total=sizeof(steps)/sizeof(steps[0]);
    if(!font)return steps[0];
    /* Measure with the same weight and scale the paint loop will use, otherwise
       a scaled or emboldened UI would pick a size that then overflows. */
    unsigned scale=font->ui_size_percent,weight=font->ui_weight_override;
    font->ui_size_percent=font->ui_weight_override=0;
    unsigned chosen=steps[total-1];
    for(size_t s=0;s<total;++s) {
        thingsos_font_size(font,steps[s]);
        bool fits=true;
        for(size_t i=0;i<count && fits;++i)fits=input_text_fits(font,words[i],INPUT_CANDIDATE_WIDTH);
        if(fits){chosen=steps[s];break;}
    }
    font->ui_size_percent=scale;font->ui_weight_override=weight;
    return chosen;
}
static void wifi_candidates_draw(void) {
    thingsos_rect(&app.canvas,0,638,684,116,255);
    char count[48];snprintf(count,sizeof(count),"%u / %u 字节",(unsigned)app.input.length,(unsigned)app.input.limit);
    keyboard_label(count,INPUT_COUNT_LEFT,INPUT_COUNT_BASELINE,21,INPUT_COUNT_WIDTH,false,26);
    if(app.input.chinese) {
        char composition[THINGSOS_IME_COMPOSITION_LIMIT+16];
        thingsos_font_t* font=keyboard_typeface();
        if(app.input.composition[0]) {
            /* Keep the whole composition while it fits; once it no longer does,
               show as much of its tail as the row holds, marked with an ellipsis,
               so the caret characters the user just typed stay visible. */
            const char* tail=app.input.composition;
            if(font) {
                unsigned scale=font->ui_size_percent;
                font->ui_size_percent=0;
                thingsos_font_size(font,keyboard_scaled_px(26,30));
                int room=INPUT_COMPOSITION_WIDTH-(int)thingsos_glyph(font,NULL,0x2026u,0,0)-8;
                tail=input_text_tail(font,app.input.composition,room);
                font->ui_size_percent=scale;
            }
            if(tail==app.input.composition)
                snprintf(composition,sizeof(composition),"拼音 %s",app.input.composition);
            else
                snprintf(composition,sizeof(composition),"拼音 …%s",tail);
        }
        else strlcpy(composition,thingsos_ime_count(&app.input)?"词语联想":"输入拼音选择词语",sizeof(composition));
        keyboard_label(composition,36,INPUT_COMPOSITION_BASELINE,26,INPUT_COMPOSITION_WIDTH,false,30);
        /* Larger pixel arrows with matching, separated 66 x 38 hit areas. */
        toolbar_stroke(550,676,540,686);toolbar_stroke(540,686,550,696);
        toolbar_stroke(610,676,620,686);toolbar_stroke(620,686,610,696);
        char words[THINGSOS_IME_PAGE_SIZE][THINGSOS_IME_CANDIDATE_BYTES];
        size_t shown=0;
        for(size_t i=0;i<THINGSOS_IME_PAGE_SIZE;++i) {
            words[i][0]=0;
            if(thingsos_ime_candidate(&app.input,app.input.candidate_page*THINGSOS_IME_PAGE_SIZE+i,words[i]))++shown;
        }
        unsigned px=input_candidate_size(words,shown);
        unsigned scale=app.font.ui_size_percent,override=app.font.ui_weight_override;
        app.font.ui_size_percent=app.font.ui_weight_override=0;
        for(size_t i=0;i<shown;++i)
            keyboard_label(words[i],36+(int)i*204,INPUT_CANDIDATE_BASELINE,px,INPUT_CANDIDATE_WIDTH,true,28);
        app.font.ui_size_percent=scale;app.font.ui_weight_override=override;
    }
}
static void wifi_password_paint(bool layout,bool force) {
    static thingsos_input_t previous;
    static char previous_message[sizeof(app.message)];
    static unsigned previous_editing;
    bool field=force || previous.cursor!=app.input.cursor || previous.secret!=app.input.secret ||
        previous.reveal!=app.input.reveal || strcmp(previous.text,app.input.text);
    bool controls=force || previous_editing!=app.editing || previous.reveal!=app.input.reveal ||
        strcmp(previous_message,app.message);
    bool candidates=force || previous.length!=app.input.length || previous.limit!=app.input.limit || previous.cursor!=app.input.cursor || strcmp(previous.text,app.input.text) ||
        previous.chinese!=app.input.chinese || previous.candidate_page!=app.input.candidate_page ||
        strcmp(previous.composition,app.input.composition);
    wifi_input_draw(field,controls);
    if(candidates)wifi_candidates_draw();
    if(layout)input_keyboard_draw();
    previous=app.input;previous_editing=app.editing;
    strlcpy(previous_message,app.message,sizeof(previous_message));
}
static void wifi_password_update(bool layout) {wifi_password_paint(layout,false);}
static void wifi_password_draw(void) {
    bool search=app.editing==5;
    if(app.editing==6) {
        header(app.annotation_edit.thought[0]?"编辑想法":"写想法");
        label(app.wd_id[0]?(app.annotation_manual?"先保存到本机，可在更多设置中同步":"保存后自动同步微读私密笔记"):"笔记随本地书籍保存在 SD 卡",36,195,24,612);
        label("我的想法",36,275,26,612);network_button("取消",36,560,298,false);network_button("保存",350,560,298,true);
        wifi_password_paint(true,true);return;
    }
    header(search?(app.input_return==THINGSOS_UI_FILES?"搜索文件":app.input_return==THINGSOS_UI_CUBOX?"搜索 Cubox 收藏":app.input_return==THINGSOS_UI_FLOMO?"搜索 flomo 笔记":app.input_return==THINGSOS_UI_WEIDU?"搜索微读书名":"搜索书名"):app.editing==1?"连接 WiFi":"WiFi 名称");
    label(search?(app.input_return==THINGSOS_UI_FILES?"按文件名筛选当前目录":app.input_return==THINGSOS_UI_CUBOX?"按标题或正文关键词筛选 Cubox":app.input_return==THINGSOS_UI_FLOMO?"多个关键词用空格分隔，需同时匹配":"输入部分书名，找到想读的书"):app.editing==1?app.wifi_ssid:"手动添加隐藏网络",36,195,24,612);
    label(search?(app.input_return==THINGSOS_UI_FILES?"文件名关键词":app.input_return==THINGSOS_UI_CUBOX?"Cubox 搜索词":app.input_return==THINGSOS_UI_FLOMO?"flomo 搜索词":"书名关键词"):app.editing==1?"网络密码":"网络名称",36,275,26,612);
    network_button("取消",36,560,298,false);
    network_button(search?"搜索":app.editing==1?"连接":"下一步",350,560,298,true);
    wifi_password_paint(true,true);
}
