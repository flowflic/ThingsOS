/* SPDX-License-Identifier: Apache-2.0 */
static void file_delete_request(size_t);
static size_t feature_path(const char*,const char*);
static void file_browser_icon(bool directory,int y) {
    int x=directory?42:47,top=y+17,w=directory?36:26;
    thingsos_rect(&app.canvas,x,top,w,34,0);
    thingsos_rect(&app.canvas,x+2,top+2,w-4,30,255);
    if(directory) {
        thingsos_rect(&app.canvas,x,top-5,16,7,0);
        thingsos_rect(&app.canvas,x+2,top-3,12,5,255);
        thingsos_rect(&app.canvas,x+2,top+8,w-4,2,0);
    } else {
        thingsos_rect(&app.canvas,x+w-9,top,9,9,255);
        thingsos_rect(&app.canvas,x+w-10,top,2,10,0);
        thingsos_rect(&app.canvas,x+w-10,top+8,10,2,0);
        for(int i=0;i<9;++i)thingsos_rect(&app.canvas,x+w-9+i,top+i,2,2,0);
        thingsos_rect(&app.canvas,x+6,top+17,w-12,2,0);
        thingsos_rect(&app.canvas,x+6,top+24,w-12,2,0);
    }
}
static void file_browser_draw(void) {
    header("文件夹");
    label(media()?"SD 卡 · 文字列表":"未检测到 SD 卡",36,195,23,612);
    char info[320];
    snprintf(info,sizeof(info),app.file_query[0]?"搜索：%s":"%s",app.file_query[0]?app.file_query:
        media() && app.browser.path[7]?app.browser.path+7:"/");
    label(info,36,270,26,612);
    const char* actions[]={"上一级","根目录","搜索","更多"};
    for(int i=0;i<4;++i)weidu_button(actions[i],36+i*157,300,141);
    size_t count=thingsos_browser_matches(&app.browser,app.file_query);
    size_t maximum=count?(count-1)/THINGSOS_FILE_ROWS*THINGSOS_FILE_ROWS:0;
    if(app.list_start>maximum)app.list_start=maximum;
    for(size_t row=0;media() && row<THINGSOS_FILE_ROWS && app.list_start+row<count;++row) {
        size_t at=thingsos_browser_match(&app.browser,app.file_query,app.list_start+row);
        const thingsos_file_entry_t* e=&app.browser.entries[at];int y=390+(int)row*72;
        file_browser_icon(e->directory,y);
        label(e->name,100,y+32,26,e->directory?510:450);
        if(e->directory)label(">",610,y+45,26,28);
        else {
            snprintf(info,sizeof(info),"%.1f KB",(double)e->size/1024.0);label(info,100,y+58,18,430);
            label("›",610,y+44,24,28);
        }
        thingsos_rect(&app.canvas,36,y+71,602,1,200);
    }
    if(!count)centered_label(media()?"没有匹配的文件":"请插入 SD 卡",36,550,28,612);
    if(count>THINGSOS_FILE_ROWS) {
        int height=576*THINGSOS_FILE_ROWS/(int)count;if(height<24)height=24;
        thingsos_rect(&app.canvas,650,390,5,576,210);
        thingsos_rect(&app.canvas,650,390+(int)(app.list_start*(576-height)/maximum),5,height,0);
    }
    weidu_button("上一页",36,990,180);weidu_button("下一页",447,990,180);
    snprintf(info,sizeof(info),"%u / %u · %u 项%s",(unsigned)(count?app.list_start/THINGSOS_FILE_ROWS+1:0),
        (unsigned)((count+THINGSOS_FILE_ROWS-1)/THINGSOS_FILE_ROWS),(unsigned)count,app.browser.truncated?"+":"");
    centered_label(info,220,1035,21,220);nav();
    if(app.file_more) {
        thingsos_rect(&app.canvas,28,400,628,440,0);thingsos_rect(&app.canvas,30,402,624,436,255);
        if(!media() || strcmp(app.browser.path,"/sdcard/fonts"))weidu_button(media()?"设为书库目录":"重新扫描 SD 卡",57,440,570);
        weidu_button("打开 Example.txt（只读）",57,565,570);
        weidu_button("关闭",57,710,570);
    }
    if(app.file_action_open && app.file_action_index<app.browser.count) {
        thingsos_rect(&app.canvas,28,420,628,440,0);thingsos_rect(&app.canvas,30,422,624,436,255);
        centered_label(app.browser.entries[app.file_action_index].name,57,474,26,570);
        weidu_button("打开",57,510,570);weidu_button("删除",57,620,570);weidu_button("取消",57,730,570);
    }
}
static bool file_browser_tap(int x,int y) {
    if(app.file_action_open) {
        app.file_action_open=false;app.dirty=true;
        if(x<28 || x>=656)return true;
        size_t index=app.file_action_index;
        if(index>=app.browser.count)return true;
        char current[THINGSOS_RECORD_PATH_BYTES];
        if(!thingsos_browser_path(&app.browser,index,current) || strcmp(current,app.file_action_path)) {say("目录已变化，请重新选择书籍");return true;}
        if(y>=620 && y<688){file_delete_request(index);return true;}
        if(y>=510 && y<578) {
            char path[THINGSOS_RECORD_PATH_BYTES];if(!thingsos_browser_path(&app.browser,index,path))return true;
            size_t before=app.list_start,b=feature_path(path,app.browser.entries[index].name);
            if(b==SIZE_MAX || !open_book(b))say("书籍无法打开");
            else {app.reader_from_files=true;app.file_return_start=before;}
        }
        return true;
    }
    if(app.file_more) {
        app.file_more=false;
        if(y>=440 && y<508 && !media()) {
            if(read_pico_sd_remount()!=ESP_OK)say("SD 未挂载，请检查插卡");
            else {feature_mount();scan();thingsos_browser_scan(&app.browser,"/sdcard","/sdcard");app.file_query[0]=0;app.list_start=0;}
        } else if(y>=440 && y<508 && strcmp(app.browser.path,"/sdcard/fonts") && media()) {
            strlcpy(app.records.library_path,app.browser.path,sizeof(app.records.library_path));
            app.records_dirty=true;feature_save();thingsos_network_library(app.records.library_path);
            if(!thingsos_network_recover_files())say("文件恢复失败，旧内容保留");
            scan();navigate(THINGSOS_UI_LIBRARY);
        } else if(y>=565 && y<633){if(!open_book(app.book_count))say("内置示例打开失败");}
        app.dirty=true;return true;
    }
    if(y>=1098)return false;
    if(y>=300 && y<368) {
        int action=(x-36)/157;
        if(x<36 || action>3)return true;
        if(action==2){feature_editor(5);return true;}
        if(action==3){app.file_more=true;app.dirty=true;return true;}
        if(!media()){say("请插入 SD 卡后重试");return true;}
        if(action==1)thingsos_browser_scan(&app.browser,"/sdcard","/sdcard");
        else if(strcmp(app.browser.path,"/sdcard") && !thingsos_browser_parent(&app.browser,"/sdcard"))say("上级目录不可用");
        app.file_query[0]=0;app.list_start=0;app.dirty=true;return true;
    }
    if(!media()){say("请插入 SD 卡后重试");return true;}
    size_t count=thingsos_browser_matches(&app.browser,app.file_query);
    if(y>=990 && y<1058) {
        if(x<216)app.list_start=app.list_start>THINGSOS_FILE_ROWS?app.list_start-THINGSOS_FILE_ROWS:0;
        else if(x>=447 && app.list_start+THINGSOS_FILE_ROWS<count)app.list_start+=THINGSOS_FILE_ROWS;
        app.dirty=true;return true;
    }
    if(x>=644 && y>=390 && y<966 && count>THINGSOS_FILE_ROWS) {
        app.list_start=(size_t)(y-390)*((count-1)/THINGSOS_FILE_ROWS*THINGSOS_FILE_ROWS)/575;app.dirty=true;return true;
    }
    if(x>=36 && x<644 && y>=390 && y<966) {
        size_t index=thingsos_browser_match(&app.browser,app.file_query,app.list_start+(size_t)(y-390)/72);
        if(index>=app.browser.count)return true;
        if(app.browser.entries[index].directory) {
            if(!thingsos_browser_enter(&app.browser,"/sdcard",index))say("目录无法打开");
            else {app.file_query[0]=0;app.list_start=0;}
        } else {
            char path[THINGSOS_RECORD_PATH_BYTES];if(!thingsos_browser_path(&app.browser,index,path))return true;
            if(thingsos_book_path(path)) {
                app.file_action_index=index;app.file_action_open=true;
                strlcpy(app.file_action_path,path,sizeof(app.file_action_path));
            } else if(file_picture_path(path))file_picture_open(path);
            else if(thingsos_font_allowed(path)) {
                font_scan();for(size_t i=0;i<app.font_count;++i) {char candidate[640];snprintf(candidate,sizeof(candidate),"/sdcard/fonts/%s",app.fonts[i]);
                    if(!strcmp(candidate,path)){font_preview((int)i);navigate(THINGSOS_UI_FONTS);return true;}}
                say("请将字体放在 /fonts 目录");
            } else say("请选择书籍、字体或 PNG/JPG 图片");
        }
        app.dirty=true;
    }
    return true;
}
