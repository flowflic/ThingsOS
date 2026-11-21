/* SPDX-License-Identifier: Apache-2.0
 * Main-task-owned selection, local notes and explicit private synchronization.
 */
#ifndef ANNOTATION_ROOT
#define ANNOTATION_ROOT "/sdcard/.thingsos/annotations"
#endif
static bool reader_annotations_queue(bool);
static void reader_annotations_refresh_cloud(void);
static void reader_annotations_cloud_load(void);
static void reader_annotations_dismiss(void) {
    bool restore=app.annotation_from_notes;
    app.annotation_from_notes=false;
    app.annotation_delete_confirm=false;
    app.annotation_delete_line=false;
    app.annotation_open=app.annotation_selecting=app.annotation_dragging=false;
    if(restore)app.wd_notes_open=app.wd_long_open=true;
    thingsos_runtime_overlay(&app.runtime,restore,now());app.dirty=true;
}
static void reader_annotations_close(void) {
    free(app.annotations);app.annotations=NULL;app.annotation_loaded=false;
    app.annotation_storage_error=false;app.annotation_open=app.annotation_remote=false;
    app.annotation_delete_confirm=false;
    app.annotation_selecting=app.annotation_dragging=false;
    app.annotation_crc_valid=false;
    app.annotation_from_notes=false;app.annotation_remote_id[0]=0;
}
static bool reader_annotations_store(void) {
    if(!app.opened || !media() || app.runtime.location.book_id==1)return false;
    const char* owner=app.wd_id[0]?app.wd_owner:"";
    if(app.wd_id[0] && !owner[0])return false;
    if(app.annotations && app.annotations->book==app.runtime.location.book_id && !strcmp(app.annotations->owner,owner))
        return app.annotation_loaded;
    reader_annotations_close();app.annotations=thingsos_psram_calloc(1,sizeof(*app.annotations));
    if(!app.annotations)return false;
    app.annotation_loaded=thingsos_annotations_load(app.annotations,ANNOTATION_ROOT,app.runtime.location.book_id,owner);
    app.annotation_storage_error=!app.annotation_loaded;return app.annotation_loaded;
}
static uint32_t reader_annotations_source(void) {
    return app.epub_open?app.epub.chapter_sources[app.chapter_index]:app.chapter_index;
}
static void reader_annotations_prepare(void) {
    bool loaded=reader_annotations_store(),fresh=!app.annotation_crc_valid;
    if(fresh) {
        app.annotation_text_crc=thingsos_annotation_crc(app.chapter.text,app.chapter.bytes);
        app.annotation_crc_valid=true;
    }
    size_t used=0;
    if(app.wd_id[0])for(size_t i=0;i<app.wd_line_count && used<WD_DRAW_LIMIT;++i) {
        bool removed=false;
        if(loaded && app.wd_lines[i].mine)for(unsigned j=0;j<app.annotations->count;++j) {
            const thingsos_annotation_t* n=&app.annotations->notes[j];
            if(n->deleted && n->chapter==reader_annotations_source() && n->text_crc==app.annotation_text_crc &&
               n->begin==app.chapter.source_start+app.wd_lines[i].begin && n->end==app.chapter.source_start+app.wd_lines[i].end)removed=true;
        }
        if(!removed)app.annotation_lines[used++]=app.wd_lines[i];
    }
    size_t local_count=0;
    if(loaded)for(unsigned i=0;i<app.annotations->count;++i) {
        const thingsos_annotation_t* n=&app.annotations->notes[i];size_t base=app.chapter.source_start;
        if(n->deleted)continue;
        if(n->chapter!=reader_annotations_source() || n->text_crc!=app.annotation_text_crc || n->begin<base ||
           n->end>base+app.chapter.bytes || n->end-n->begin!=strlen(n->quote) ||
           memcmp(app.chapter.text+n->begin-base,n->quote,n->end-n->begin))continue;
        if(used<sizeof(app.annotation_lines)/sizeof(*app.annotation_lines)) {
            app.annotation_lines[used++]=(thingsos_text_underline_t){n->begin-base,n->end-base,true};
            ++local_count;
        }
    }
    app.chapter.underlines=app.annotation_lines;app.chapter.underline_count=used;
    if(fresh && local_count)reader_cache_clear(); /* An old adjacent frame may predate attachment. */
}
static const char* reader_annotations_status(const thingsos_annotation_t* n) {
    if(n->deleted)return "划线已删除 · 待同步";
    if(!n->thought[0] && n->review_id[0])return "本机想法已删除 · 云端待同步";
    switch(n->sync) {
        case THINGSOS_NOTE_PENDING:return "已保存本机 · 待同步";
        case THINGSOS_NOTE_SENDING:return "已保存本机 · 正在同步";
        case THINGSOS_NOTE_SYNCED:return "已同步微读";
        case THINGSOS_NOTE_FAILED:return "已保存本机 · 同步失败，可重试";
        case THINGSOS_NOTE_UNCERTAIN:return "本机已保存 · 云端结果待核对";
        default:return "已保存本机";
    }
}
static void reader_annotations_cloud_list(void) {
    app.annotation_cloud_count=0;unsigned count=0;
    if(!app.wd_id[0] || !thingsos_wd_device_personal_info(app.wd_id,&count))return;
    thingsos_wd_personal_t* p=thingsos_psram_alloc(sizeof(*p));if(!p)return;
    for(unsigned i=0;i<count && i<WD_NOTE_LIMIT;++i) {
        if(!thingsos_wd_device_personal(app.wd_id,i,p))continue;
        bool duplicate=false;
        if(app.annotations)for(unsigned j=0;j<app.annotations->count;++j) {
            const thingsos_annotation_t* n=&app.annotations->notes[j];
            if(p->note.range && !strcmp(n->uid,p->note.uid) && n->source_begin==p->note.begin && n->source_end>=n->source_begin &&
               n->source_end-n->source_begin==p->note.length && !strcmp(n->quote,p->quote)){duplicate=true;break;}
        }
        if(!duplicate)app.annotation_cloud_indices[app.annotation_cloud_count++]=i;
    }
    free(p);
}
static void reader_annotations_enter(void) {
    if(!reader_annotations_store()){say(app.annotation_storage_error?"笔记文件异常，保留原文件":"暂无法保存笔记，请检查 SD 卡或微读登录");return;}
    app.annotation_open=app.annotation_list=true;app.annotation_remote=false;
    app.annotation_from_notes=false;
    app.annotation_list_start=0;app.menu=false;app.footnote_open=false;
    reader_annotations_cloud_list();
    thingsos_runtime_overlay(&app.runtime,true,now());app.dirty=true;
}
static const thingsos_glyph_hit_t* reader_annotations_glyph(int x,int y,bool nearest) {
    if(app.text_hits_chapter!=app.chapter_index || app.text_hits_start!=app.start)return NULL;
    const thingsos_glyph_hit_t* best=NULL;int distance=INT_MAX;
    for(size_t i=0;i<app.text_hits.glyph_count;++i) {
        const thingsos_glyph_hit_t* g=&app.text_hits.glyphs[i];
        int dx=x<g->x?g->x-x:x>=g->x+g->w?x-g->x-g->w+1:0;
        int dy=y<g->y?g->y-y:y>=g->y+g->h?y-g->y-g->h+1:0;
        int d=dx*dx+dy*dy;
        if(d<distance){distance=d;best=g;}
    }
    return best && distance<=(nearest?2500:100)?best:NULL;
}
static void reader_annotations_drag(int x,int y) {
    const thingsos_glyph_hit_t* g=reader_annotations_glyph(x,y,true);if(!g)return;
    app.annotation_begin=g->offset<app.annotation_anchor?g->offset:app.annotation_anchor;
    size_t end=g->offset+g->bytes;app.annotation_end=end>app.annotation_anchor_end?end:app.annotation_anchor_end;
    app.dirty=true;
}
static bool reader_annotations_contact(int x,int y) {
    if(!app.annotation_selecting || y>=reader_height()-180)return false;
    const thingsos_glyph_hit_t* g=reader_annotations_glyph(x,y,true);if(!g)return false;
    size_t middle=app.annotation_begin+(app.annotation_end-app.annotation_begin)/2;
    if(g->offset<=middle) {
        app.annotation_anchor_end=app.annotation_end;app.annotation_anchor=app.annotation_end-1;
        while(app.annotation_anchor && ((unsigned char)app.chapter.text[app.annotation_anchor]&192)==128)--app.annotation_anchor;
    } else {
        app.annotation_anchor=app.annotation_begin;app.annotation_anchor_end=app.annotation_begin+1;
        while(app.annotation_anchor_end<app.chapter.bytes && ((unsigned char)app.chapter.text[app.annotation_anchor_end]&192)==128)++app.annotation_anchor_end;
    }
    app.annotation_dragging=true;reader_annotations_drag(x,y);return true;
}
static bool reader_annotations_hold(int x,int y) {
    if(app.page!=THINGSOS_UI_READING || app.menu || app.annotation_open || app.footnote_open || app.image_view.open ||
       app.wd_notes_open || app.wd_thought_open || app.wd_cloud_prompt || app.wd_consent)return false;
    const thingsos_glyph_hit_t* g=reader_annotations_glyph(x,y,false);if(!g)return false;
    app.annotation_anchor=app.annotation_begin=g->offset;app.annotation_end=g->offset+g->bytes;
    app.annotation_anchor_end=app.annotation_end;
    app.annotation_selecting=app.annotation_dragging=true;
    thingsos_runtime_overlay(&app.runtime,true,now());app.dirty=true;return true;
}
static void reader_annotations_selection_draw(void) {
    if(!app.annotation_selecting)return;
    for(size_t i=0;i<app.text_hits.glyph_count;++i) {
        const thingsos_glyph_hit_t* g=&app.text_hits.glyphs[i];
        if(g->offset>=app.annotation_begin && g->offset<app.annotation_end)
            thingsos_rect(&app.canvas,g->x,g->y+g->h-4,g->w,3,0);
    }
    int y=reader_height()-168,w=(reader_width()-96)/3;
    thingsos_rect(&app.canvas,16,y-62,reader_width()-32,170,255);
    thingsos_rect(&app.canvas,16,y-18,reader_width()-32,2,0);
    const char* texts[]={"划线","写想法","取消"};
    for(unsigned i=0;i<3;++i) {
        int x=32+(int)i*(w+16);thingsos_rect(&app.canvas,x,y,w,70,0);thingsos_rect(&app.canvas,x+2,y+2,w-4,66,255);
        centered_label(texts[i],x,y+47,28,w);
    }
    label("拖动调整选区",36,y-30,22,reader_width()-72);
}
static bool reader_annotations_selection_note(void) {
    if(!reader_annotations_store()){say(app.annotation_storage_error?"笔记文件异常，未覆盖原文件":"请检查 SD 卡或先登录微读账号");return false;}
    size_t a=app.annotation_begin,b=app.annotation_end;
    if(a>=b || b>app.chapter.bytes || b-a>THINGSOS_ANNOTATION_QUOTE){say("选中文字过长，请缩小选区");return false;}
    for(size_t i=0;i<app.chapter.footnote_count;++i)if(a<app.chapter.footnotes[i].end && b>app.chapter.footnotes[i].begin) {
        say("请分段选择，避开脚注标记");return false;
    }
    memset(&app.annotation_edit,0,sizeof(app.annotation_edit));
    thingsos_annotation_t* n=&app.annotation_edit;
    n->chapter=reader_annotations_source();n->begin=(uint32_t)(a+app.chapter.source_start);n->end=(uint32_t)(b+app.chapter.source_start);
    n->text_crc=app.annotation_text_crc;memcpy(n->quote,app.chapter.text+a,b-a);n->quote[b-a]=0;
    n->updated=wall_time();n->sync=app.wd_id[0]?THINGSOS_NOTE_PENDING:THINGSOS_NOTE_LOCAL;
    if(app.wd_id[0]) {
        wd_prepare();thingsos_weidu_chapter_t toc;
        if(!thingsos_wd_toc(WD_ROOT,app.wd_id,n->chapter,&toc) || !thingsos_wd_source(&app.wd_map,a,&n->source_begin) ||
           !thingsos_wd_source(&app.wd_map,b,&n->source_end) || n->source_begin>=n->source_end) {
            say("正文位置未通过校验，未保存划线");return false;
        }
        strlcpy(n->uid,toc.uid,sizeof(n->uid));
    }
    /* Reuse the same local annotation when selecting the exact range again. */
    for(unsigned i=0;i<app.annotations->count;++i) {
        const thingsos_annotation_t* old=&app.annotations->notes[i];
        if(old->chapter==n->chapter && old->begin==n->begin && old->end==n->end && old->text_crc==n->text_crc){
            if(old->deleted){say("这条划线正在等待同步删除");return false;}
            *n=*old;break;
        }
    }
    app.annotation_remote=false;return true;
}
static bool reader_annotations_save(void) {
    if(!reader_annotations_store())return false;
    if(app.annotation_edit.review_id[0] && !app.annotation_edit.thought[0]){say("已同步的想法不能在此清空，请在微读中删除");return false;}
    app.annotation_edit.updated=wall_time();
    if(app.wd_id[0])app.annotation_edit.sync=THINGSOS_NOTE_PENDING;
    if(!thingsos_annotations_put(app.annotations,&app.annotation_edit)){say("保存失败，原有笔记已保留");return false;}
    reader_cache_clear();app.annotation_selecting=app.annotation_dragging=false;
    app.annotation_open=true;app.annotation_list=false;app.annotation_page=0;app.dirty=true;
    say(app.wd_id[0]?(app.annotation_manual?"已保存，可在更多设置中手动同步":"已保存，将自动同步微读"):"划线与想法已保存到 SD 卡");
    app.message_until=now()+2200;return true;
}
static bool reader_annotations_delete_thought(void) {
    if(app.annotation_remote || !app.annotation_edit.id || !reader_annotations_store())return false;
    if(app.annotation_edit.sync==THINGSOS_NOTE_SENDING){say("正在同步，请完成后再删除");return false;}
    thingsos_annotation_t* changed=thingsos_psram_alloc(sizeof(*changed));if(!changed)return false;
    *changed=app.annotation_edit;changed->thought[0]=0;changed->updated=wall_time();changed->error=0;
    changed->sync=app.wd_id[0]?THINGSOS_NOTE_PENDING:THINGSOS_NOTE_LOCAL;
    bool line=app.annotation_delete_line;
    changed->deleted=line?1:0;
    bool queued=line && app.wd_id[0] && (changed->bookmark_id[0] || changed->review_id[0] ||
        app.annotation_edit.sync==THINGSOS_NOTE_UNCERTAIN);
    bool ok=line && !queued?thingsos_annotations_remove(app.annotations,changed->id):thingsos_annotations_put(app.annotations,changed);
    if(ok){app.annotation_edit=*changed;app.annotation_page=0;app.annotation_delete_confirm=false;app.dirty=true;}
    free(changed);
    if(ok && line){reader_annotations_dismiss();reader_cache_clear();}
    say(ok?(line?(queued?(app.annotation_manual?"已删除，可在更多设置中手动同步":"已删除，将自动同步微读"):"划线及附带想法已删除"):"想法已删除，下划线已保留"):"删除失败，原记录已保留");
    if(ok)app.message_until=now()+2200;
    return ok;
}
static bool reader_annotations_draw(void) {
    if(!app.annotation_open || !app.annotation_list || app.page!=THINGSOS_UI_READING)return false;
    int width=reader_width(),height=reader_height();
    unsigned scale=app.font.ui_size_percent,weight=app.font.ui_weight_override;
    app.font.ui_size_percent=app.font.ui_weight_override=0;thingsos_font_weight(&app.font,0);
    label("<",36,150,30,60);centered_label("我的划线和想法",110,154,30,width-220);
    thingsos_rect(&app.canvas,36,198,width-72,1,190);
    unsigned rows=app.landscape_enabled?2:5,local=app.annotations?app.annotations->count:0,total=local+app.annotation_cloud_count;
    char count[96];snprintf(count,sizeof(count),"共 %u 条",total);label(count,36,254,22,200);
    thingsos_font_size(ui_font(),22);
    thingsos_label_right(ui_font(),&app.canvas,app.wd_id[0]?(app.annotation_manual?"手动同步":"自动同步"):"保存在本机",width-280,254,244);
    for(unsigned row=0;row<rows && app.annotation_list_start+row<total;++row) {
        unsigned index=app.annotation_list_start+row;int y=280+(int)row*140;
        if(index<local) {
            const thingsos_annotation_t* n=&app.annotations->notes[index];
            label(n->quote,48,y+32,26,width-96);
            label(n->thought[0]?n->thought:"暂无想法",48,y+68,23,width-96);
            label(reader_annotations_status(n),48,y+105,20,width-96);
        } else {
            thingsos_wd_personal_t* note=thingsos_psram_alloc(sizeof(*note));
            if(note && thingsos_wd_device_personal(app.wd_id,app.annotation_cloud_indices[index-local],note)) {
                label(note->quote[0]?note->quote:"云端划线",48,y+32,26,width-96);
                label(note->note.text,48,y+68,23,width-96);label("已缓存",48,y+105,20,width-96);
            }
            free(note);
        }
        thingsos_rect(&app.canvas,36,y+125,width-72,1,210);
    }
    if(!total)centered_label("长按正文选字，添加划线或想法",36,360,25,width-72);
    int footer=height-120;
    thingsos_rect(&app.canvas,36,footer-20,width-72,1,190);
    centered_label("上一页",36,footer+36,26,180);centered_label("下一页",width-216,footer+36,26,180);
    snprintf(count,sizeof(count),"%u / %u",total?app.annotation_list_start/rows+1:0,(total+rows-1)/rows);
    centered_label(count,width/2-90,footer+36,22,180);
    app.font.ui_size_percent=scale;app.font.ui_weight_override=weight;return true;
}
static const char* reader_annotations_thought(void) {
    const char* text=app.annotation_remote?app.wd_note.text:app.annotation_edit.thought;
    return text[0]?text:"暂无想法，点击“编辑”添加。";
}
typedef struct {
    int top,height,quote_y,quote_height,status_y,actions_y,body_y,body_height,footer_y;
    bool paginated;
} annotation_card_geometry_t;
static int reader_annotations_fit_height(const char* text,int width,int maximum) {
    size_t bytes=strlen(text);
    for(int height=42;height<=maximum;height+=42)
        if(wd_thought_body(NULL,text,bytes,0,0,0,width,height)>=bytes)return height;
    return 0;
}
static annotation_card_geometry_t reader_annotations_card_geometry(void) {
    bool wide=reader_landscape_frame();int width=wide?1100:570,maximum=wide?570:916;
    annotation_card_geometry_t g={0};
    g.quote_y=wide?74:160;
    g.quote_height=reader_annotations_fit_height(app.annotation_edit.quote,wide?1100:549,84);
    if(!g.quote_height)g.quote_height=84;
    g.status_y=g.quote_y+g.quote_height+32;
    g.body_y=g.status_y+44;
    g.body_height=reader_annotations_fit_height(reader_annotations_thought(),width,maximum-g.body_y-100);
    if(!g.body_height) {
        g.paginated=true;g.body_height=(maximum-g.body_y-190)/42*42;
        if(g.body_height<42)g.body_height=42;
    }
    if(app.annotation_delete_confirm){g.paginated=false;g.body_height=210;}
    g.footer_y=g.body_y+g.body_height+24;
    g.actions_y=g.footer_y+(g.paginated?90:0);
    g.height=g.actions_y+70;
    if(app.annotation_delete_confirm){g.actions_y=g.status_y+44;g.body_y=g.actions_y+44;g.height=g.body_y+210+24;}
    g.top=(reader_height()-g.height)/2;
    if(g.top<90)g.top=90;
    g.quote_y+=g.top;g.status_y+=g.top;g.actions_y+=g.top;g.body_y+=g.top;g.footer_y+=g.top;
    return g;
}
static size_t reader_annotations_card_page(thingsos_canvas_t* canvas,size_t start,const annotation_card_geometry_t* g) {
    const char* text=reader_annotations_thought();
    return wd_thought_body(canvas,text,strlen(text),start,57,g->body_y,
        reader_landscape_frame()?1100:570,g->body_height);
}
static void reader_annotations_card_draw(void) {
    if(!app.annotation_open || app.annotation_list || (app.page!=THINGSOS_UI_READING && app.page!=THINGSOS_UI_WEIDU))return;
    bool wide=reader_landscape_frame();unsigned scale=app.font.ui_size_percent,override=app.font.ui_weight_override;
    app.font.ui_size_percent=app.font.ui_weight_override=0;thingsos_font_weight(&app.font,0);
    annotation_card_geometry_t g=reader_annotations_card_geometry();
    wd_thought_card_frame_at("我的想法",g.top,g.height);
    label(app.annotation_from_notes?"< 划线":"< 列表",57,g.top+(wide?50:70),24,140);
    if(!wide){label("划线原文",57,g.top+140,22,570);thingsos_rect(&app.canvas,57,g.quote_y+2,3,g.quote_height,100);}
    wd_thought_body(&app.canvas,app.annotation_edit.quote,strlen(app.annotation_edit.quote),0,
        wide?57:78,g.quote_y,wide?1100:549,g.quote_height);
    label(app.annotation_remote?"我的云端想法":reader_annotations_status(&app.annotation_edit),57,g.status_y,22,wide?1100:570);
    if(app.annotation_delete_confirm) {
        label(app.annotation_edit.thought[0]?"删除这条划线及附带想法？":"删除这条划线？",57,g.actions_y,28,wide?1100:570);
        label("取消后将保留原记录。",57,g.body_y+32,24,wide?1100:570);
        if(app.annotation_edit.review_id[0] || (app.annotation_delete_line && app.annotation_edit.bookmark_id[0]))label(app.annotation_manual?"手动同步后，云端记录也将删除。":"将自动同步删除云端对应记录。",57,g.body_y+70,22,wide?1100:570);
        weidu_button("取消",57,g.body_y+110,180);
        weidu_button("确认删除",wide?979:447,g.body_y+110,180);
        app.font.ui_size_percent=scale;app.font.ui_weight_override=override;return;
    }
    thingsos_rect(&app.canvas,57,g.actions_y-20,wide?1100:570,1,204);
    if(app.annotation_remote)centered_label("刷新",57,g.actions_y+38,26,wide?1100:570);
    else if(!app.annotation_edit.deleted) {
        int cell=wide?550:285;
        centered_label("编辑",57,g.actions_y+38,26,cell);
        centered_label("删除",57+cell,g.actions_y+38,26,cell);
        thingsos_rect(&app.canvas,57+cell,g.actions_y+12,1,30,210);
    }
    app.annotation_next=reader_annotations_card_page(&app.canvas,app.annotation_page,&g);
    if(g.paginated) {
        unsigned page=1,total=0;size_t at=0,bytes=strlen(reader_annotations_thought());
        do {size_t end=reader_annotations_card_page(NULL,at,&g);++total;if(at==app.annotation_page)page=total;if(end<=at)break;at=end;}while(at<bytes);
        char info[48];snprintf(info,sizeof(info),"%u / %u",page,total);
        thingsos_rect(&app.canvas,57,g.footer_y-20,wide?1100:570,1,180);
        weidu_button("上一页",57,g.footer_y,180);weidu_button("下一页",wide?979:447,g.footer_y,180);
        centered_label(info,wide?450:245,g.footer_y+45,24,wide?316:194);
    }
    app.font.ui_size_percent=scale;app.font.ui_weight_override=override;
}
static void reader_annotations_cloud_load(void) {
    thingsos_wd_personal_t* p=thingsos_psram_calloc(1,sizeof(*p));if(!p)return;
    if(thingsos_wd_device_personal_find(app.annotation_remote_id,app.annotation_edit.uid,
        app.annotation_edit.source_begin,app.annotation_edit.source_end,p)) {
        app.wd_note=p->note;
        if(p->quote[0])strlcpy(app.annotation_edit.quote,p->quote,sizeof(app.annotation_edit.quote));
    } else {
        memset(&app.wd_note,0,sizeof(app.wd_note));
        strlcpy(app.wd_note.text,"这段划线暂无已缓存的个人想法，可点击“刷新云端”核对。",sizeof(app.wd_note.text));
    }
    free(p);app.annotation_page=0;
    /* Device-written notes stay editable when entered from a cached cloud
       underline. Never replace newer unsynced text with a cloud snapshot. */
    if(app.annotations && app.opened && !strcmp(app.annotation_remote_id,app.wd_id) &&
       !strcmp(app.annotations->owner,app.wd_owner))for(unsigned i=0;i<app.annotations->count;++i) {
        const thingsos_annotation_t* n=&app.annotations->notes[i];
        if(!strcmp(n->uid,app.annotation_edit.uid) && n->source_begin==app.annotation_edit.source_begin &&
           n->source_end==app.annotation_edit.source_end && !strcmp(n->quote,app.annotation_edit.quote)) {
            app.annotation_edit=*n;app.annotation_remote=false;break;
        }
    }
}
static void reader_annotations_refresh_cloud(void) {
    const char* id=app.annotation_remote?app.annotation_remote_id:app.wd_id;
    if(!id[0] || !wd_request(WD_DEVICE_PERSONAL,id,NULL))say("暂无法刷新，旧想法缓存已保留");
    else say("正在读取我的云端想法");
}
static void wd_own_thought_enter(const char* id,const thingsos_wd_note_t* note) {
    if(!id || !id[0] || !note->range || !note->length || note->begin>UINT32_MAX-note->length) {
        say("此划线缺少有效位置，无法关联想法");return;
    }
    memset(&app.annotation_edit,0,sizeof(app.annotation_edit));
    app.annotation_from_notes=app.wd_notes_open;
    strlcpy(app.annotation_remote_id,id,sizeof(app.annotation_remote_id));
    strlcpy(app.annotation_edit.uid,note->uid,sizeof(app.annotation_edit.uid));
    app.annotation_edit.source_begin=note->begin;app.annotation_edit.source_end=note->begin+note->length;
    strlcpy(app.annotation_edit.quote,note->text,sizeof(app.annotation_edit.quote));
    app.wd_notes_open=app.wd_long_open=app.wd_thought_open=false;
    app.annotation_open=app.annotation_remote=true;app.annotation_list=false;
    reader_annotations_cloud_load();thingsos_runtime_overlay(&app.runtime,true,now());app.dirty=true;
}
static bool reader_annotations_auto_pending(const thingsos_annotation_t* n) {
    return n->sync==THINGSOS_NOTE_PENDING || (n->sync==THINGSOS_NOTE_FAILED &&
        (n->error==WD_NETWORK || n->error==WD_DNS || n->error==WD_MEMORY || n->error==WD_CLOCK));
}
static bool reader_annotations_queue(bool all) {
    if(!app.wd_id[0] || !reader_annotations_store()){say("本地书籍的笔记保存在本机");return false;}
    if(!thingsos_network_connected() || thingsos_weidu_busy()){say("本机笔记已保存，请联网且微读空闲后同步");return false;}
    thingsos_annotation_t* original=thingsos_psram_calloc(1,sizeof(*original));if(!original)return false;
    bool found=false;
    for(unsigned i=0;i<app.annotations->count;++i) {
        const thingsos_annotation_t* n=&app.annotations->notes[i];
        if(!n->uid[0] || n->sync==THINGSOS_NOTE_SENDING || (app.annotation_auto_job && !reader_annotations_auto_pending(n)) ||
           (all?n->sync==THINGSOS_NOTE_SYNCED:n->id!=app.annotation_edit.id))continue;
        *original=*n;found=true;break;
    }
    if(!found){free(original);app.annotation_batch=false;if(!app.annotation_auto_job)say("没有待同步笔记");app.annotation_auto_job=false;return false;}
    thingsos_annotation_t* sending=thingsos_psram_alloc(sizeof(*sending));if(!sending){free(original);return false;}
    *sending=*original;sending->sync=THINGSOS_NOTE_SENDING;
    bool ok=thingsos_annotations_put(app.annotations,sending);
    if(ok) {
        ok=thingsos_wd_device_annotation_request(app.wd_id,app.annotations->book,app.annotations->owner,original);
        if(!ok)thingsos_annotations_put(app.annotations,original);
    }
    if(ok){app.annotation_batch=all;if(app.annotation_edit.id==sending->id)app.annotation_edit=*sending;if(!app.annotation_auto_job)say("正在同步私密笔记");}
    else if(!app.annotation_auto_job)say("同步未开始，本机笔记已保留");
    free(sending);free(original);return ok;
}
static void reader_annotations_auto_poll(void) {
    if(app.annotation_manual || app.annotation_batch || !app.opened || app.page!=THINGSOS_UI_READING || !app.wd_id[0] ||
       !app.annotations || app.annotation_open || app.annotation_selecting || app.menu || app.footnote_open ||
       app.wd_notes_open || app.wd_thought_open || app.wd_cloud_prompt || app.wd_consent || app.dirty ||
       !app.reader_ready_at || now()<app.reader_ready_at+3000 || now()<app.activity_at+2500 ||
       now()<app.annotation_auto_after || thingsos_board_touch_contact_pending() ||
       !thingsos_network_connected() || thingsos_weidu_busy())return;
    for(unsigned i=0;i<app.annotations->count;++i)if(reader_annotations_auto_pending(&app.annotations->notes[i]) && app.annotations->notes[i].uid[0]) {
        app.annotation_auto_job=true;app.annotation_auto_after=now()+(app.annotations->notes[i].sync==THINGSOS_NOTE_FAILED?60000:5000);
        reader_annotations_queue(true);return;
    }
}
static void reader_annotations_poll(void) {
    thingsos_wd_device_status_t next;thingsos_wd_device_status(&next);
    if(next.serial==app.annotation_status_serial)return;
    app.annotation_status_serial=next.serial;
    if(next.action==WD_DEVICE_PERSONAL) {
        if(app.annotation_open && !strcmp(next.book,app.wd_id))reader_annotations_cloud_list();
        if(app.annotation_remote && app.annotation_open && !strcmp(next.book,app.annotation_remote_id)) {
            if(next.personal==WD_OK)reader_annotations_cloud_load();
            else say("刷新我的想法失败，旧缓存已保留");
            app.dirty=true;
        }
        return;
    }
    if(next.action!=WD_DEVICE_NOTE_WRITE || !next.note_book || !next.note_id)return;
    thingsos_annotations_t* store=app.annotations;bool temporary=false;
    if(!store || store->book!=next.note_book || strcmp(store->owner,next.note_owner)) {
        store=thingsos_psram_calloc(1,sizeof(*store));temporary=true;
        if(!store || !thingsos_annotations_load(store,ANNOTATION_ROOT,next.note_book,next.note_owner)){free(store);return;}
    }
    thingsos_annotation_t* result=thingsos_psram_alloc(sizeof(*result));bool saved=false;
    if(result)for(unsigned i=0;i<store->count;++i) {
        const thingsos_annotation_t* n=&store->notes[i];
        if(n->id!=next.note_id || thingsos_annotation_crc(n->thought,strlen(n->thought))!=next.note_content_crc)continue;
        *result=*n;result->sync=next.note_state;result->error=next.note_error;
        strlcpy(result->bookmark_id,next.note_bookmark_id,sizeof(result->bookmark_id));
        strlcpy(result->review_id,next.note_review_id,sizeof(result->review_id));
        saved=result->deleted && result->sync==THINGSOS_NOTE_SYNCED?thingsos_annotations_remove(store,result->id):thingsos_annotations_put(store,result);
        if(saved && result->deleted && !temporary){wd_forget_map();reader_cache_clear();}
        if(saved && !temporary && app.annotation_edit.id==result->id)app.annotation_edit=*result;
        break;
    }
    free(result);if(temporary)free(store);
    if(saved && app.annotation_list && !temporary)reader_annotations_cloud_list();
    if(app.annotation_open && app.page==THINGSOS_UI_READING){
        if(app.annotation_auto_job)background_redraw();
        else {app.dirty=true;say(saved?(next.note_state==THINGSOS_NOTE_SYNCED?"私密笔记已上传并回读确认":"本机笔记保留，云端状态待核对或重试"):"结果保存失败，本机原记录已保留");}
    }
    if(app.annotation_batch && !temporary && saved && next.note_state==THINGSOS_NOTE_SYNCED && !(app.annotation_auto_job && app.annotation_manual))reader_annotations_queue(true);
    else app.annotation_batch=app.annotation_auto_job=false;
}
static bool reader_annotations_tap(int x,int y) {
    if(app.page!=THINGSOS_UI_READING && !(app.page==THINGSOS_UI_WEIDU && app.annotation_open && app.annotation_remote))return false;
    int width=reader_width(),height=reader_height();
    if(app.annotation_open) {
        if(!app.annotation_list) {
            bool wide=reader_landscape_frame();
            annotation_card_geometry_t g=reader_annotations_card_geometry();
            int right=wide?1188:656,top=g.top,bottom=g.top+g.height,header=g.top+(wide?60:100);
            if(app.annotation_delete_confirm) {
                if(y>=g.body_y+110 && y<g.body_y+178 && x>=(wide?979:447) && x<(wide?1159:627))reader_annotations_delete_thought();
                else {app.annotation_delete_confirm=false;app.dirty=true;}
                return true;
            }
            if(x<28 || x>=right || y<top || y>=bottom || (x>=right-86 && y<header)) {
                reader_annotations_dismiss();return true;
            }
            if(y<header && x<210) {
                if(app.annotation_from_notes)reader_annotations_dismiss();
                else {app.annotation_list=true;app.annotation_page=0;app.dirty=true;}
                return true;
            }
            int footer=g.footer_y;
            if(y>=g.actions_y && y<g.actions_y+62) {
                if(app.annotation_remote)reader_annotations_refresh_cloud();
                else if(!app.annotation_edit.deleted) {
                    int middle=57+(wide?550:285);
                    if(x<middle)feature_editor(6);
                    else if(app.annotation_edit.sync==THINGSOS_NOTE_SENDING)say("正在同步，请稍后删除");
                    else {app.annotation_delete_line=true;app.annotation_delete_confirm=true;}
                }
            } else if(g.paginated && y>=footer && y<footer+68) {
                if(x>=57 && x<237 && app.annotation_page) {
                    size_t at=0,previous=0;
                    while(at<app.annotation_page){previous=at;size_t end=reader_annotations_card_page(NULL,at,&g);if(end<=at)break;at=end;}
                    app.annotation_page=previous;
                } else if(x>=(wide?979:447) && x<(wide?1159:627) && app.annotation_next<strlen(reader_annotations_thought()))app.annotation_page=app.annotation_next;
            }
            app.dirty=true;return true;
        }
        if(y<180 && x<230){reader_annotations_dismiss();return true;}
        if(app.annotation_list) {
            if(y>=height-120 && y<height-40) {
                unsigned rows=app.landscape_enabled?2:5;
                if(x<width/3)app.annotation_list_start=app.annotation_list_start>rows?app.annotation_list_start-rows:0;
                else if(x>width*2/3 && app.annotations && app.annotation_list_start+rows<app.annotations->count+app.annotation_cloud_count)app.annotation_list_start+=rows;
            } else if(y>=280 && y<height-140) {
                unsigned row=(unsigned)(y-280)/140;
                if(row>=(app.landscape_enabled?2u:5u))return true;
                if(app.annotations && app.annotation_list_start+row<app.annotations->count) {
                    app.annotation_edit=app.annotations->notes[app.annotation_list_start+row];app.annotation_list=false;
                    app.annotation_remote=false;app.annotation_page=0;
                } else if(app.annotations && app.annotation_list_start+row<app.annotations->count+app.annotation_cloud_count) {
                    thingsos_wd_personal_t* p=thingsos_psram_alloc(sizeof(*p));
                    if(p && thingsos_wd_device_personal(app.wd_id,app.annotation_cloud_indices[app.annotation_list_start+row-app.annotations->count],p)) {
                        memset(&app.annotation_edit,0,sizeof(app.annotation_edit));
                        strlcpy(app.annotation_edit.uid,p->note.uid,sizeof(app.annotation_edit.uid));
                        strlcpy(app.annotation_edit.quote,p->quote,sizeof(app.annotation_edit.quote));
                        app.annotation_edit.source_begin=p->note.begin;app.annotation_edit.source_end=p->note.begin+p->note.length;
                        app.wd_note=p->note;app.annotation_list=false;app.annotation_remote=true;app.annotation_page=0;
                        app.annotation_from_notes=false;strlcpy(app.annotation_remote_id,app.wd_id,sizeof(app.annotation_remote_id));
                    }
                    free(p);
                }
            }
        }
        app.dirty=true;return true;
    }
    if(app.annotation_selecting) {
        if(y>=height-180 && y<height-80) {
            unsigned action=x<(width/3)?0:x<width*2/3?1:2;
            if(action==2)reader_annotations_dismiss();
            else if(reader_annotations_selection_note()) {
                if(!action)reader_annotations_save();
                else {app.annotation_selecting=false;app.annotation_open=true;app.annotation_list=false;feature_editor(6);}
            }
        } else reader_annotations_contact(x,y);
        return true;
    }
    if(!app.menu && !app.footnote_open && app.annotations && app.text_hits_chapter==app.chapter_index && app.text_hits_start==app.start) {
        for(size_t i=0;i<app.text_hits.line_count;++i) {
            const thingsos_text_hit_t* h=&app.text_hits.lines[i];
            if(x<h->x || x>=h->x+h->w || y<h->y || y>=h->y+h->h)continue;
            for(unsigned j=0;j<app.annotations->count;++j) {
                const thingsos_annotation_t* n=&app.annotations->notes[j];
                if(!n->deleted && n->chapter==reader_annotations_source() && n->text_crc==app.annotation_text_crc &&
                   n->begin==h->begin+app.chapter.source_start && n->end==h->end+app.chapter.source_start) {
                    app.annotation_edit=*n;app.annotation_open=true;app.annotation_list=app.annotation_remote=false;
                    app.annotation_page=0;thingsos_runtime_overlay(&app.runtime,true,now());app.dirty=true;return true;
                }
            }
        }
    }
    return false;
}
