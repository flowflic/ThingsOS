// SPDX-License-Identifier: Apache-2.0
/* Native network pages follow the approved prototype, with real device state. */
static void feature_input_layout(bool, int, int);
/* AP is scoped to an awake transfer page without an associated station. */
static void network_hotspot_policy(void) {
    bool wanted=!app.asleep && app.page==THINGSOS_UI_TRANSFER && !thingsos_network_connected();
    if(wanted!=app.hotspot && thingsos_network_mode(wanted)==ESP_OK) {
        app.hotspot=wanted;background_redraw();
    }
}
static void network_connection_poll(void) {
    network_hotspot_policy();
    bool connected=thingsos_network_connected();
    if(connected!=app.wifi_connected) {
        app.wifi_connected=connected;background_redraw();
        if(connected)app.wifi_requested=0;
    }
    if(app.page==THINGSOS_UI_TRANSFER) {
        static char prior_address[80];char address[80];
        thingsos_network_address(app.hotspot,address,sizeof(address));
        if(strcmp(prior_address,address)){strlcpy(prior_address,address,sizeof(prior_address));app.dirty=true;}
    }
}
static void transfer_wifi_status(void) {
    bool connected=thingsos_network_connected();
    const char* text=connected?"WiFi 已连接":app.wifi_requested?"WiFi 连接中":"WiFi 未连接";
    static const uint32_t endings[3][3]={{0x5df2,0x8fde,0x63a5},{0x8fde,0x63a5,0x4e2d},{0x672a,0x8fde,0x63a5}};
    unsigned status_index=connected?0:app.wifi_requested?1:2;
    thingsos_font_size(&app.font,24);int width=0;
    for(const char* p="WiFi ";*p;++p)width+=thingsos_glyph(&app.font,NULL,(unsigned char)*p,0,0);
    for(unsigned i=0;i<3;++i)width+=thingsos_glyph(&app.font,NULL,endings[status_index][i],0,0);
    int shift=649-width-442;
    /* Draw the wireless icon in pixels so it does not depend on symbol fonts. */
    for(int y=125;y<153;++y)for(int x=394;x<434;++x) {
        int dx=x-414,dy=y-154,rr=dx*dx+dy*dy;
        if(dy<0 && abs(dx)<=-dy && ((rr>=650 && rr<=800)||(rr>=290 && rr<=395)||(rr>=70 && rr<=130)))
            thingsos_rect(&app.canvas,x+shift,y,1,1,0);
    }
    thingsos_rect(&app.canvas,412+shift,151,5,5,0);
    if(!connected && !app.wifi_requested)toolbar_stroke(398+shift,126,430+shift,157);
    label(text,442+shift,154,24,width);
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
static int wifi_rows(void) { return wifi_has_card() ? 3 : 4; }
static int wifi_base(void) { return wifi_has_card() ? 490 : 320; }
static void wifi_scan(bool force) {
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
    if (wifi_has_card()) {
        network_outline(36, 245, 612, 184);
        label(thingsos_network_saved_name(), 60, 292, 32, 564);
        label(thingsos_network_connected() ? "已连接" : app.wifi_requested ? "正在连接…" : "尚未连接", 60, 343, 25, 564);
        network_button(thingsos_network_connected()?"断开连接":"再次连接",60,352,240,!thingsos_network_connected());
        network_button("忘记网络", 432, 352, 192, false);
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
    label(app.message[0] ? app.message : "连接后，在同一 WiFi 中打开设备地址传输", 36, 966, 23, 612);
    network_button("手动输入网络", 36, 990, 612, false);
    nav();
}
/* Password and book search share one key geometry for painting and hit testing. */
static bool input_uses_qwerty(void) {return app.editing<2 || app.editing==5 || app.editing==6;}
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
static int wifi_key_hit(int x,int y) {
    input_key_t keys[40];size_t count=input_keyboard_keys(keys);
    for(size_t i=0;i<count;++i)if(x>=keys[i].x && x<keys[i].x+keys[i].w && y>=keys[i].y && y<keys[i].y+(keys[i].y==1000?90:68))return keys[i].code;
    /* Letter rows own their gaps and outer padding. In particular A starts
       30 px to the right of Q; a natural left-edge tap still belongs to A. */
    if(app.input.mode!=THINGSOS_KEYS_SYMBOLS && x>=36 && x<648) {
        int best=-1,distance=INT_MAX;
        for(size_t i=0;i<count;++i) {
            input_key_t* key=&keys[i];
            if(key->code<100 || y<key->y || y>=key->y+68)continue;
            if(key->y==918 && (x<118 || x>=570))continue;
            int d=abs(x-(key->x+key->w/2));if(d<distance){distance=d;best=key->code;}
        }
        if(best>=0)return best;
    }
    return -1;
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
static void keyboard_label(const char* text,int x,int baseline,unsigned px,int width,bool centered) {
    thingsos_font_t* font=keyboard_typeface();
    if(!font) {
        if(centered)centered_label(text,x,baseline,px,width);
        else label(text,x,baseline,px,width);
        return;
    }
    font->ui_size_percent=app.font.ui_size_percent;
    font->ui_weight_override=app.font.ui_weight_override;
    thingsos_font_weight(font,0);thingsos_font_size(font,px);
    unsigned loads=font->glyph_loads,hits=font->bitmap_hits;
    if(centered)thingsos_label_centered(font,&app.canvas,text,x,baseline,width);
    else thingsos_label(font,&app.canvas,text,x,baseline,width);
    app.font.glyph_loads+=font->glyph_loads-loads;
    app.font.bitmap_hits+=font->bitmap_hits-hits;
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
        } else keyboard_label(key->text,key->x,key->y+45,key->code==49?19:key->code>=100?32:25,key->w,true);
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
static void wifi_candidates_draw(void) {
    thingsos_rect(&app.canvas,0,638,684,116,255);
    char count[48];snprintf(count,sizeof(count),"%u / %u 字节",(unsigned)app.input.length,(unsigned)app.input.limit);
    keyboard_label(count,420,664,21,228,false);
    if(app.input.chinese) {
        size_t composed=strlen(app.input.composition);const char* visible=app.input.composition+(composed>26?composed-26:0);
        char composition[96];
        if(composed)snprintf(composition,sizeof(composition),"拼音 %s%s",composed>26?"…":"",visible);
        else strlcpy(composition,thingsos_ime_count(&app.input)?"词语联想":"输入拼音选择词语",sizeof(composition));
        keyboard_label(composition,36,694,26,458,false);
        /* Larger pixel arrows with matching, separated 66 x 38 hit areas. */
        toolbar_stroke(550,676,540,686);toolbar_stroke(540,686,550,696);
        toolbar_stroke(610,676,620,686);toolbar_stroke(620,686,610,696);
        unsigned scale=app.font.ui_size_percent,override=app.font.ui_weight_override;
        app.font.ui_size_percent=app.font.ui_weight_override=0;
        for(size_t i=0;i<THINGSOS_IME_PAGE_SIZE;++i) {
            char candidate[THINGSOS_IME_CANDIDATE_BYTES];
            if(thingsos_ime_candidate(&app.input,app.input.candidate_page*THINGSOS_IME_PAGE_SIZE+i,candidate))
                keyboard_label(candidate,36+(int)i*204,736,strlen(candidate)>18?22:28,196,true);
        }
        app.font.ui_size_percent=scale;app.font.ui_weight_override=override;
    }
}
static void wifi_password_paint(bool layout,bool force) {
    bool field=force || app.input_prev.cursor!=app.input.cursor || app.input_prev.secret!=app.input.secret ||
        app.input_prev.reveal!=app.input.reveal || strcmp(app.input_prev.text,app.input.text);
    bool controls=force || app.input_prev_editing!=app.editing || app.input_prev.reveal!=app.input.reveal ||
        strcmp(app.input_prev_message,app.message);
    bool candidates=force || app.input_prev.length!=app.input.length || app.input_prev.limit!=app.input.limit || app.input_prev.cursor!=app.input.cursor || strcmp(app.input_prev.text,app.input.text) ||
        app.input_prev.chinese!=app.input.chinese || app.input_prev.candidate_page!=app.input.candidate_page ||
        strcmp(app.input_prev.composition,app.input.composition);
    wifi_input_draw(field,controls);
    if(candidates)wifi_candidates_draw();
    if(layout)input_keyboard_draw();
    app.input_prev=app.input;app.input_prev_editing=app.editing;
    strlcpy(app.input_prev_message,app.message,sizeof(app.input_prev_message));
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
