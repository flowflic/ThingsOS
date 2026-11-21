// SPDX-License-Identifier: Apache-2.0
/* Explicit source selection. Image cards reuse bounded thumbnail storage. */
static char wallpaper_delete_path[THINGSOS_RECORD_PATH_BYTES];
static bool wallpaper_image_path(const char* path) {
    if(!path || strncmp(path,"/sdcard/wallpapers/",sizeof("/sdcard/wallpapers/")-1) || !thingsos_user_file_allowed(path))return false;
    const char* ext=strrchr(path,'.');
    return ext && (!strcasecmp(ext,".jpg") || !strcasecmp(ext,".jpeg") || !strcasecmp(ext,".webp") || !strcasecmp(ext,".png"));
}
static void wallpaper_book_set(book_t* book,const char* path,const char* title) {
    memset(book,0,sizeof(*book));strlcpy(book->path,path,sizeof(book->path));
    const char* base=strrchr(path,'/');base=base?base+1:path;
    strlcpy(book->name,title && title[0]?title:base,sizeof(book->name));
    const char* ext=strrchr(path,'.');book->epub=ext && !strcasecmp(ext,".epub");
    book->id=identity(path);
}
static void refresh_options(const char* title,unsigned selected,int y) {
    thingsos_rect(&app.canvas,36,y,612,1,170);label(title,48,y+54,26,168);
    for(unsigned i=0;i<4;++i) {
        char text[16];snprintf(text,sizeof(text),"%u次",5+i*5);
        int x=228+(int)i*105;centered_label(text,x,y+54,24,105);
        if(selected==5+i*5)thingsos_rect(&app.canvas,x+14,y+70,77,3,0);
    }
}
static const char* wallpaper_actual_path(const char* path,char* buf,size_t cap) {
#ifdef THINGSOS_HOST_UI
    if(host_picture_root[0] && path && !strncmp(path,"/sdcard/",8)) {
        snprintf(buf,cap,"%s%s",host_picture_root,path+7);
        return buf;
    }
#endif
    (void)buf;(void)cap;
    return path;
}
static bool wallpaper_decode(const char* path,unsigned char** pixels_out,unsigned* w_out,unsigned* h_out) {
    char actual[1024];
    const char* disk=wallpaper_actual_path(path,actual,sizeof(actual));
    FILE* file=fopen(disk,"rb");if(!file)return false;
    unsigned char *raw=NULL,*pixels=NULL;bool ok=false;unsigned iw=0,ih=0;
    if(!fseek(file,0,SEEK_END)) {
        long bytes=ftell(file);rewind(file);
        if(bytes>0 && bytes<=1024*1024) {
            raw=thingsos_image_buffer((size_t)bytes);
            if(raw && fread(raw,1,(size_t)bytes,file)==(size_t)bytes)
                ok=thingsos_decode_wallpaper(raw,(size_t)bytes,&pixels,&iw,&ih);
        }
    }
    fclose(file);free(raw);
    if(ok) { *pixels_out=pixels;*w_out=iw;*h_out=ih;return true; }
    free(pixels);return false;
}
static uint64_t wallpaper_picture_stamp(const char* disk) {
    uint64_t source_stamp=thingsos_cover_source_stamp(disk);
    return source_stamp?cache_hash(source_stamp,&wallpaper_render_version,sizeof(wallpaper_render_version)):0;
}
static bool wallpaper_lock_picture(const char* path) {
    if(!wallpaper_image_path(path))return false;
    reader_cache_clear();
    char actual[1024];
    const char* disk=wallpaper_actual_path(path,actual,sizeof(actual));
    uint64_t stamp=wallpaper_picture_stamp(disk);
    const char* root="/sdcard/.thingsos/lock-covers";
#ifdef THINGSOS_HOST_UI
    root=host_lock_cache_root;
#endif
    char cache[640];snprintf(cache,sizeof(cache),"%s/selected-image%s.gray",root,
        app.canvas.inverted?"-dark":"");
    if(stamp && root[0]) {
        /* Try landscape cache first, then portrait cache; header records exact dimensions. */
        app.canvas.width=1216;app.canvas.height=684;
        if(thingsos_cover_cache_load(cache,stamp,&app.canvas,0,0,1216,684))return true;
        app.canvas.width=684;app.canvas.height=1216;
        if(thingsos_cover_cache_load(cache,stamp,&app.canvas,0,0,684,1216))return true;
    }
    unsigned char* pixels=NULL;unsigned iw=0,ih=0;
    if(!wallpaper_decode(path,&pixels,&iw,&ih)) {
        app.canvas.width=684;app.canvas.height=1216;
        return false;
    }
    int lw=iw>ih?1216:684,lh=iw>ih?684:1216;
    app.canvas.width=(unsigned)lw;app.canvas.height=(unsigned)lh;
    paint_image_cover(&app.canvas,pixels,iw,ih,0,0,lw,lh,true);
    free(pixels);
    if(root[0] && stamp && stamp==wallpaper_picture_stamp(disk)) {
#ifndef THINGSOS_HOST_UI
        mkdir("/sdcard/.thingsos",0775);
#endif
        mkdir(root,0775);thingsos_cover_cache_save(cache,stamp,&app.canvas,0,0,lw,lh);
    }
    return true;
}
static bool wallpaper_scan(const char* path) {
    wallpaper_delete_path[0]=0;
    if(strcmp(path,"/sdcard/wallpapers") && strncmp(path,"/sdcard/wallpapers/",sizeof("/sdcard/wallpapers/")-1))return false;
    if(!thingsos_browser_scan(&app.browser,"/sdcard",path))return false;
    size_t count=0;
    for(size_t i=0;i<app.browser.count;++i) {
        char full[THINGSOS_RECORD_PATH_BYTES];
        if(app.browser.entries[i].directory || (thingsos_browser_path(&app.browser,i,full) && wallpaper_image_path(full)))
            app.browser.entries[count++]=app.browser.entries[i];
    }
    app.browser.count=count;app.wallpaper_start=0;return true;
}
/* Layout (2026-11-21 UE): home shows the current wallpaper and three source
   rows; books and pictures use a 3 x 2 grid in lock-screen proportion. */
#define WALL_GRID_PER 6u
#define WALL_THUMB 300
#define WALL_ROW_TOP 632
#define WALL_ROW_STEP 130
#define WALL_PAGER_TOP 1040
#define WALL_ACTION_TOP 1070
static bool wallpaper_view_only; /* home "全屏预览": look at the current wallpaper */
static int wallpaper_cell_x(unsigned col) {return 36+(int)col*212;}
/* Grid cell under a touch, or SIZE_MAX for gaps. Rows: picture + caption. */
static size_t wallpaper_grid_at(int x,int y,int top,int pitch,int cell_h) {
    int col=x<36?-1:x<224?0:x<248?-1:x<436?1:x<460?-1:x<648?2:-1;
    int row=y<top?-1:y<top+cell_h?0:y<top+pitch?-1:y<top+pitch+cell_h?1:-1;
    if(col<0 || row<0)return SIZE_MAX;
    return app.wallpaper_start+(size_t)(row*3+col);
}
static size_t wallpaper_card_at(int x,int y) {return wallpaper_grid_at(x,y,296,350,340);}
static size_t wallpaper_book_at(int x,int y) {return wallpaper_grid_at(x,y,290,370,350);}
static bool wallpaper_hold(int x,int y,uint64_t elapsed) {
    if(app.page!=THINGSOS_UI_WALLPAPER || app.wallpaper_step!=2 || elapsed<650 || wallpaper_delete_path[0])return false;
    size_t i=wallpaper_card_at(x,y);
    if(i>=app.browser.count || app.browser.entries[i].directory)return false;
    char path[THINGSOS_RECORD_PATH_BYTES];
    if(!thingsos_browser_path(&app.browser,i,path) || !wallpaper_image_path(path))return false;
    strlcpy(wallpaper_delete_path,path,sizeof(wallpaper_delete_path));app.dirty=true;return true;
}
/* Bounded thumbnail (cached per path and source stamp), painted aspect-fill
   so cards and the home preview show the lock-screen framing. */
static const thingsos_thumbnail_t* wallpaper_thumb(const char* path) {
    char actual[1024];const char* disk=wallpaper_actual_path(path,actual,sizeof(actual));
    uint64_t key=identity(path),stamp=thingsos_cover_source_stamp(disk);
    const thingsos_thumbnail_t* cached=thingsos_thumbnail_find(&app.thumbnails,key,WALL_THUMB,WALL_THUMB);
    if(cached && cached->version!=stamp)cached=NULL;
    if(!cached) {
        unsigned char *raw=NULL,*pixels=NULL;unsigned iw=0,ih=0;
        FILE* f=fopen(disk,"rb");
        if(f) {
            if(!fseek(f,0,SEEK_END)) {
                long bytes=ftell(f);rewind(f);
                if(bytes>0 && bytes<=1024*1024) {
                    raw=thingsos_image_buffer((size_t)bytes);
                    if(raw && fread(raw,1,(size_t)bytes,f)==(size_t)bytes)
                        thingsos_decode_thumbnail(raw,(size_t)bytes,WALL_THUMB,WALL_THUMB,&pixels,&iw,&ih);
                }
            }
            fclose(f);
        }
        free(raw);
        cached=thingsos_thumbnail_store(&app.thumbnails,key,WALL_THUMB,WALL_THUMB,pixels,pixels?iw:0,pixels?ih:0);
        thingsos_thumbnail_version(&app.thumbnails,key,WALL_THUMB,WALL_THUMB,stamp);free(pixels);
    }
    return cached && cached->pixels?cached:NULL;
}
/* Portrait pictures fill the box in lock-screen framing. Landscape pictures
   lock horizontally (1216 x 684), so they are letterboxed whole and marked. */
static void wallpaper_paint_thumb(const thingsos_thumbnail_t* t,int x,int y,int w,int h) {
    if(t->width>t->height) {
        thingsos_rect(&app.canvas,x,y,w,h,235);
        paint_image_fit(&app.canvas,t->pixels,t->width,t->height,x,y,w,h-36,true,true,false);
        centered_label("横屏",x,y+h-10,20,w);
    } else paint_image_cover(&app.canvas,t->pixels,t->width,t->height,x,y,w,h,true);
}
static bool wallpaper_thumbnail(const char* path,int x,int y,int w,int h) {
    const thingsos_thumbnail_t* t=wallpaper_thumb(path);
    if(!t)return false;
    wallpaper_paint_thumb(t,x,y,w,h);return true;
}
/* "使用中" pill: white text on black, fixed size for the three-character label. */
static void wallpaper_badge(int x,int y) {
    const int w=80,h=30;
    thingsos_rect(&app.canvas,x,y,w,h,255);
    centered_label("使用中",x,y+22,18,w);
    for(int yy=y;yy<y+h;++yy)for(int xx=x;xx<x+w;++xx){unsigned char* p=&app.canvas.pixels[(size_t)yy*app.canvas.width+xx];*p=(unsigned char)(255-*p);}
}
static void wallpaper_frame(int x,int y,int w,int h,unsigned char ink) {
    thingsos_rect(&app.canvas,x,y,w,2,ink);thingsos_rect(&app.canvas,x,y+h-2,w,2,ink);
    thingsos_rect(&app.canvas,x,y,2,h,ink);thingsos_rect(&app.canvas,x+w-2,y,2,h,ink);
}
static void wallpaper_source_icon(unsigned kind,int x,int y) {
    wallpaper_frame(x,y,64,64,0);wallpaper_frame(x+1,y+1,62,62,0);
    if(kind==0) {
        thingsos_rect(&app.canvas,x+14,y+36,8,16,0);thingsos_rect(&app.canvas,x+28,y+24,8,28,0);thingsos_rect(&app.canvas,x+42,y+14,8,38,0);
    } else if(kind==1) {
        wallpaper_frame(x+18,y+12,30,40,0);thingsos_rect(&app.canvas,x+24,y+12,3,40,0);
    } else {
        thingsos_rect(&app.canvas,x+44,y+16,7,7,0);
        for(int i=0;i<16;++i){thingsos_rect(&app.canvas,x+12+i,y+48-i,3,3,0);thingsos_rect(&app.canvas,x+27+i,y+33+i,3,3,0);}
        for(int i=0;i<10;++i){thingsos_rect(&app.canvas,x+36+i,y+48-i,3,3,0);thingsos_rect(&app.canvas,x+45+i,y+39+i,3,3,0);}
    }
}
static void wallpaper_pager(size_t count) {
    char text[32];
    snprintf(text,sizeof(text),"%u / %u",(unsigned)(app.wallpaper_start/WALL_GRID_PER)+1,
        (unsigned)(count?(count+WALL_GRID_PER-1)/WALL_GRID_PER:1));
    reader_control("上一页",36,WALL_PAGER_TOP,190,false);
    centered_label(text,236,WALL_PAGER_TOP+44,24,212);
    reader_control("下一页",458,WALL_PAGER_TOP,190,false);
}
/* Reading-statistics wallpaper rendered off screen, then scaled into the card. */
static bool wallpaper_stats_thumb(int x,int y,int w,int h) {
    unsigned char* full=thingsos_psram_calloc(684u*1216u,1);
    if(!full)return false;
    thingsos_canvas_t saved=app.canvas;
    app.canvas.pixels=full;app.canvas.width=684;app.canvas.height=1216;memset(full,255,684u*1216u);
    unsigned period=app.stats_period;size_t start=app.list_start;
    uint32_t first=app.stats_first,last=app.stats_last;
    app.stats_period=1;app.list_start=0;stats_draw(true);
    app.stats_period=period;app.list_start=start;app.stats_first=first;app.stats_last=last;
    app.canvas=saved;
    paint_image_cover(&app.canvas,full,684,1216,x,y,w,h,true);
    free(full);return true;
}
static void wallpaper_home(void) {
    label("当前壁纸",36,262,22,300);
    int tx=36,ty=280,tw=150,th=267;
    thingsos_rect(&app.canvas,tx-1,ty-1,tw+2,th+2,170);thingsos_rect(&app.canvas,tx,ty,tw,th,255);
    struct stat info;const char* kind="阅读统计";char name[200]="每次锁屏自动更新";char detail[96]="";bool ok=true;
    if(app.lock_wallpaper==1) {
        kind="书籍封面";strlcpy(name,app.wallpaper_book.name[0]?app.wallpaper_book.name:"未选择",sizeof(name));
        ok=app.wallpaper_book.path[0] && media() && !stat(app.wallpaper_book.path,&info) && S_ISREG(info.st_mode);
        if(ok)cover(&app.wallpaper_book,tx,ty,tw,th);
    } else if(app.lock_wallpaper==2) {
        kind="图片";const char* base=strrchr(app.wallpaper_image,'/');
        strlcpy(name,base?base+1:app.wallpaper_image,sizeof(name));
        const thingsos_thumbnail_t* t=media()?wallpaper_thumb(app.wallpaper_image):NULL;ok=t!=NULL;
        if(t){wallpaper_paint_thumb(t,tx,ty,tw,th);
            strlcpy(detail,t->width>t->height?"横屏 · 锁屏横向显示":"竖屏",sizeof(detail));}
    } else ok=wallpaper_stats_thumb(tx,ty,tw,th);
    if(!ok){centered_label("不可用",tx,ty+th/2+10,24,tw);strlcpy(detail,"锁屏暂用阅读统计",sizeof(detail));}
    label(kind,214,330,30,434);label(name,214,378,24,434);
    if(detail[0])label(detail,214,418,22,434);
    reader_control("全屏预览",214,466,180,false);
    label("更换壁纸",36,612,22,300);
    thingsos_rect(&app.canvas,36,WALL_ROW_TOP-2,612,1,170);
    static const char* const titles[3]={"阅读统计","书籍封面","图片"};
    static const char* const notes[3]={"本周阅读时长与进度，每次锁屏自动更新","从书库中选择一本书的封面","SD 卡 wallpapers 目录中的图片"};
    for(unsigned i=0;i<3;++i) {
        int y=WALL_ROW_TOP+(int)i*WALL_ROW_STEP;
        wallpaper_source_icon(i,44,y+33);
        label(titles[i],132,y+58,30,300);
        label(notes[i],132,y+104,21,470);
        if(app.lock_wallpaper==i)wallpaper_badge(132+(i==2?2:4)*34+14,y+30);
        label(">",612,y+70,30,30);
        thingsos_rect(&app.canvas,36,y+WALL_ROW_STEP-1,612,1,200);
    }
    label("建议尺寸：竖屏 684 × 1216，横屏 1216 × 684",36,1060,21,612);
    label("先预览，确认后才会替换当前壁纸",36,1096,21,612);
}
static void wallpaper_actions(void) {
    thingsos_rect(&app.canvas,0,WALL_ACTION_TOP-10,684,1216-WALL_ACTION_TOP+10,255);
    if(wallpaper_view_only){reader_control("返回",36,WALL_ACTION_TOP,612,false);return;}
    reader_control("重新选择",36,WALL_ACTION_TOP,298,false);
    reader_control(app.wallpaper_preview_ok?"设为壁纸":"无法使用",350,WALL_ACTION_TOP,298,app.wallpaper_preview_ok);
    if(!app.wallpaper_preview_ok)cubox_dim(352,WALL_ACTION_TOP+2,294,64);
}
static void wallpaper_draw(void) {
    char text[256];
    if(app.wallpaper_step!=2)wallpaper_delete_path[0]=0;
    if(wallpaper_delete_path[0]) {
        header("删除壁纸");
        centered_label("确认删除这张图片？",36,360,30,612);
        const char* name=strrchr(wallpaper_delete_path,'/');
        label(name?name+1:wallpaper_delete_path,48,440,26,588);
        label("删除后无法恢复",48,510,24,588);
        button("取消",650);button("确认删除",790);return;
    }
    if(app.wallpaper_step==3) {
        if(app.wallpaper_kind==0) {
            unsigned period=app.stats_period;size_t start=app.list_start;
            uint32_t first=app.stats_first,last=app.stats_last;
            app.stats_period=1;app.list_start=0;stats_draw(true);
            app.stats_period=period;app.list_start=start;app.stats_first=first;app.stats_last=last;
            app.wallpaper_preview_ok=true;
        } else {
            header("壁纸预览");reader_cache_clear();
            if(app.wallpaper_kind==1) {
                char id[64],source[640];bool cached=thingsos_weidu_cache_id(app.wallpaper_candidate.path,id,sizeof(id));
                if(cached)snprintf(source,sizeof(source),"/sdcard/.thingsos/weidu/covers/%s.jpg",id);
                app.wallpaper_preview_ok=cover_from_mode(&app.wallpaper_candidate,36,236,612,700,cached?source:NULL,true);
                centered_label(app.wallpaper_candidate.name,36,990,26,612);
            } else {
                unsigned char* pixels=NULL;unsigned iw=0,ih=0;
                app.wallpaper_preview_ok=wallpaper_image_path(app.wallpaper_candidate_image) &&
                    wallpaper_decode(app.wallpaper_candidate_image,&pixels,&iw,&ih);
                if(app.wallpaper_preview_ok) {
                    if(iw>ih) {
                        thingsos_rect(&app.canvas,35,433,614,346,170);
                        paint_image_cover(&app.canvas,pixels,iw,ih,36,434,612,344,true);
                        snprintf(text,sizeof(text),"横屏 · %u × %u · 锁屏按 1216 × 684 横向显示",iw,ih);
                    } else {
                        /* Same 684:1216 aspect as the lock, so the preview is the final framing. */
                        paint_image_cover(&app.canvas,pixels,iw,ih,142,236,400,711,true);
                        snprintf(text,sizeof(text),"竖屏 · %u × %u · 锁屏按 684 × 1216 裁切",iw,ih);
                    }
                    centered_label(text,36,1030,21,612);
                    free(pixels);
                }
                const char* base=strrchr(app.wallpaper_candidate_image,'/');
                centered_label(base?base+1:app.wallpaper_candidate_image,36,990,25,612);
            }
            if(!app.wallpaper_preview_ok) {
                thingsos_rect(&app.canvas,36,236,612,700,255);
                centered_label("无法预览：图片过大、损坏或没有封面",36,560,24,612);
                centered_label("请使用较小的 JPG / PNG / WebP 图片",36,620,24,612);
            }
        }
        wallpaper_actions();
        return;
    }
    header(app.wallpaper_step==1?"选择书籍封面":app.wallpaper_step==2?"选择图片":"壁纸");
    if(app.wallpaper_step==0){wallpaper_home();return;}
    if(app.wallpaper_step==1) {
        snprintf(text,sizeof(text),"本地书籍与已缓存的微读书籍 · 共 %u 本",(unsigned)app.book_count);
        label(text,36,262,22,612);
        for(unsigned cell=0;cell<WALL_GRID_PER && app.wallpaper_start+cell<app.book_count;++cell) {
            size_t i=app.wallpaper_start+cell;int x=wallpaper_cell_x(cell%3),y=290+(int)(cell/3)*370;
            thingsos_rect(&app.canvas,x-1,y-1,190,270,170);
            cover(&app.books[i],x,y,188,268);
            thingsos_font_size(ui_font(),23);
            thingsos_text_string_page(ui_font(),&app.canvas,app.books[i].name,strlen(app.books[i].name),0,x,y+282,188,64,130);
            if(app.lock_wallpaper==1 && app.books[i].id==app.wallpaper_book.id)wallpaper_badge(x+8,y+8);
        }
        if(!app.book_count)centered_label("暂无书籍，请先添加到书库",36,540,26,612);
        wallpaper_pager(app.book_count);
        return;
    }
    bool sub=strcmp(app.browser.path,"/sdcard/wallpapers")!=0;
    if(sub)cubox_chip("< 上级",36,228,116,22,false);
    label(app.browser.path+7,sub?166:36,262,22,sub?482:612);
    for(unsigned cell=0;cell<WALL_GRID_PER && app.wallpaper_start+cell<app.browser.count;++cell) {
        size_t i=app.wallpaper_start+cell;int x=wallpaper_cell_x(cell%3),y=296+(int)(cell/3)*350;
        char path[THINGSOS_RECORD_PATH_BYTES];bool picture=false;
        if(app.browser.entries[i].directory) {
            wallpaper_frame(x,y,188,280,0);
            wallpaper_frame(x+54,y+110,80,60,0);wallpaper_frame(x+55,y+111,78,58,0);thingsos_rect(&app.canvas,x+54,y+102,30,10,0);
            picture=true;
        } else {
            thingsos_rect(&app.canvas,x-1,y-1,190,282,190);thingsos_rect(&app.canvas,x,y,188,280,255);
            if(thingsos_browser_path(&app.browser,i,path))picture=wallpaper_thumbnail(path,x,y,188,280);
            if(picture && app.lock_wallpaper==2 && !strcmp(path,app.wallpaper_image))wallpaper_badge(x+8,y+8);
        }
        if(!picture)centered_label("无法预览",x,y+150,24,188);
        label(app.browser.entries[i].name,x,y+314,20,188);
    }
    if(!app.browser.count)centered_label("此目录暂无 JPG / PNG / WebP 图片",36,540,26,612);
    centered_label(app.browser.truncated?"文件较多，请分放到子目录":"点击预览 · 长按图片删除",36,1010,21,612);
    wallpaper_pager(app.browser.count);
}
static bool wallpaper_tap(int x,int y) {
    if(app.wallpaper_step!=2)wallpaper_delete_path[0]=0;
    if(wallpaper_delete_path[0]) {
        if((x<90 && y>=74 && y<200) || (y>=650 && y<745))wallpaper_delete_path[0]=0;
        else if(y>=790 && y<885) {
            if(!media() || thingsos_network_busy() || thingsos_weidu_busy())say("请等待 SD 卡和后台任务就绪");
            else if(app.lock_wallpaper==2 && !strcmp(app.wallpaper_image,wallpaper_delete_path))say("正在使用，请先更换壁纸");
            else {
                struct stat st;
                if(!wallpaper_image_path(wallpaper_delete_path) || stat(wallpaper_delete_path,&st) || !S_ISREG(st.st_mode) || remove(wallpaper_delete_path))say("删除失败，请检查 SD 卡");
                else {
                    char dir[THINGSOS_RECORD_PATH_BYTES];strlcpy(dir,app.browser.path,sizeof(dir));size_t start=app.wallpaper_start;
                    thingsos_thumbnail_clear(&app.thumbnails);
                    if(wallpaper_scan(dir))app.wallpaper_start=app.browser.count?(start<app.browser.count?start:((app.browser.count-1)/WALL_GRID_PER)*WALL_GRID_PER):0;
                    say("已删除壁纸");
                }
                wallpaper_delete_path[0]=0;
            }
        }
        app.dirty=true;return true;
    }
    if(x<90 && y>=74 && y<200) {
        if(app.wallpaper_step==0)navigate(THINGSOS_UI_APPS);
        else if(app.wallpaper_step==3 && !wallpaper_view_only)app.wallpaper_step=app.wallpaper_kind;
        else app.wallpaper_step=0;
        wallpaper_view_only=false;
    } else if(app.wallpaper_step==3) {
        if(y>=WALL_ACTION_TOP && y<WALL_ACTION_TOP+90) {
            if(wallpaper_view_only){wallpaper_view_only=false;app.wallpaper_step=0;}
            else if(x<342)app.wallpaper_step=app.wallpaper_kind;
            else if(app.wallpaper_preview_ok) {
                unsigned previous=app.lock_wallpaper;
                book_t old_book=app.wallpaper_book;
                char old_image[THINGSOS_RECORD_PATH_BYTES];strlcpy(old_image,app.wallpaper_image,sizeof(old_image));
                app.lock_wallpaper=app.wallpaper_kind;
                if(app.wallpaper_kind==1)app.wallpaper_book=app.wallpaper_candidate;
                if(app.wallpaper_kind==2)strlcpy(app.wallpaper_image,app.wallpaper_candidate_image,sizeof(app.wallpaper_image));
                if(preferences_save()) {navigate(THINGSOS_UI_APPS);say("已设置壁纸");}
                else {
                    app.lock_wallpaper=previous;app.wallpaper_book=old_book;
                    strlcpy(app.wallpaper_image,old_image,sizeof(app.wallpaper_image));
                    say("设置保存失败，请重试");
                }
            }
        }
    } else if(app.wallpaper_step==0) {
        if(x>=214 && x<394 && y>=466 && y<534) {
            /* Look at the current wallpaper; the action bar only returns. */
            app.wallpaper_kind=app.lock_wallpaper<3?app.lock_wallpaper:0;app.wallpaper_preview_ok=false;
            if(app.wallpaper_kind==1)app.wallpaper_candidate=app.wallpaper_book;
            if(app.wallpaper_kind==2)strlcpy(app.wallpaper_candidate_image,app.wallpaper_image,sizeof(app.wallpaper_candidate_image));
            wallpaper_view_only=true;app.wallpaper_step=3;
        } else if(y>=WALL_ROW_TOP && y<WALL_ROW_TOP+WALL_ROW_STEP){app.wallpaper_kind=0;app.wallpaper_step=3;app.wallpaper_preview_ok=false;}
        else if(y>=WALL_ROW_TOP+WALL_ROW_STEP && y<WALL_ROW_TOP+2*WALL_ROW_STEP){library_load();app.wallpaper_step=1;app.wallpaper_start=0;}
        else if(y>=WALL_ROW_TOP+2*WALL_ROW_STEP && y<WALL_ROW_TOP+3*WALL_ROW_STEP) {
            if(media()) {
                mkdir("/sdcard/wallpapers",0775);
                if(wallpaper_scan("/sdcard/wallpapers"))app.wallpaper_step=2;else say("无法读取 wallpapers 目录");
            } else say("请插入 SD 卡");
        }
    } else if(y>=WALL_PAGER_TOP && y<WALL_PAGER_TOP+68) {
        size_t count=app.wallpaper_step==1?app.book_count:app.browser.count;
        if(x<240 && app.wallpaper_start>=WALL_GRID_PER)app.wallpaper_start-=WALL_GRID_PER;
        if(x>=440 && app.wallpaper_start+WALL_GRID_PER<count)app.wallpaper_start+=WALL_GRID_PER;
    } else if(app.wallpaper_step==2 && x>=36 && x<152 && y>=222 && y<284 && strcmp(app.browser.path,"/sdcard/wallpapers")) {
        char parent[THINGSOS_RECORD_PATH_BYTES];strlcpy(parent,app.browser.path,sizeof(parent));
        char* slash=strrchr(parent,'/');if(slash)*slash=0;wallpaper_scan(parent);
    } else if(app.wallpaper_step==1) {
        size_t i=wallpaper_book_at(x,y);
        if(i<app.book_count){app.wallpaper_candidate=app.books[i];app.wallpaper_kind=1;app.wallpaper_step=3;app.wallpaper_preview_ok=false;}
    } else if(app.wallpaper_step==2) {
        size_t i=wallpaper_card_at(x,y);
        char path[THINGSOS_RECORD_PATH_BYTES];
        if(i<app.browser.count && thingsos_browser_path(&app.browser,i,path)) {
            if(app.browser.entries[i].directory)wallpaper_scan(path);
            else {strlcpy(app.wallpaper_candidate_image,path,sizeof(app.wallpaper_candidate_image));app.wallpaper_kind=2;app.wallpaper_step=3;app.wallpaper_preview_ok=false;}
        }
    }
    app.dirty=true;return true;
}
