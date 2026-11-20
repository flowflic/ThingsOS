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
static size_t wallpaper_card_at(int x,int y) {
    if(x<36 || x>=648 || y<300 || y>=932 || (x>=330 && x<354) || (y>=604 && y<628))return SIZE_MAX;
    return app.wallpaper_start+(y>=628?2:0)+(x>=354?1:0);
}
static bool wallpaper_hold(int x,int y,uint64_t elapsed) {
    if(app.page!=THINGSOS_UI_WALLPAPER || app.wallpaper_step!=2 || elapsed<650 || wallpaper_delete_path[0])return false;
    size_t i=wallpaper_card_at(x,y);
    if(i>=app.browser.count || app.browser.entries[i].directory)return false;
    char path[THINGSOS_RECORD_PATH_BYTES];
    if(!thingsos_browser_path(&app.browser,i,path) || !wallpaper_image_path(path))return false;
    strlcpy(wallpaper_delete_path,path,sizeof(wallpaper_delete_path));app.dirty=true;return true;
}
static bool wallpaper_thumbnail(const char* path,int x,int y,int w,int h) {
    uint64_t key=identity(path),stamp=thingsos_cover_source_stamp(path);
    const thingsos_thumbnail_t* cached=thingsos_thumbnail_find(&app.thumbnails,key,270,244);
    if(cached && cached->version!=stamp)cached=NULL;
    if(!cached) {
        unsigned char *raw=NULL,*pixels=NULL;unsigned iw=0,ih=0;
        FILE* f=fopen(path,"rb");
        if(f) {
            if(!fseek(f,0,SEEK_END)) {
                long bytes=ftell(f);rewind(f);
                if(bytes>0 && bytes<=1024*1024) {
                    raw=thingsos_image_buffer((size_t)bytes);
                    if(raw && fread(raw,1,(size_t)bytes,f)==(size_t)bytes)
                        thingsos_decode_thumbnail(raw,(size_t)bytes,270,244,&pixels,&iw,&ih);
                }
            }
            fclose(f);
        }
        free(raw);
        cached=thingsos_thumbnail_store(&app.thumbnails,key,270,244,pixels,pixels?iw:0,pixels?ih:0);
        thingsos_thumbnail_version(&app.thumbnails,key,270,244,stamp);free(pixels);
    }
    if(!cached || !cached->pixels)return false;
    paint_image_fit(&app.canvas,cached->pixels,cached->width,cached->height,x,y,w,h,false,false,false);return true;
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
                app.wallpaper_preview_ok=cover_from_mode(&app.wallpaper_candidate,36,236,612,740,cached?source:NULL,true);
                centered_label(app.wallpaper_candidate.name,36,1020,26,612);
            } else {
                unsigned char* pixels=NULL;unsigned iw=0,ih=0;
                app.wallpaper_preview_ok=wallpaper_image_path(app.wallpaper_candidate_image) &&
                    wallpaper_decode(app.wallpaper_candidate_image,&pixels,&iw,&ih);
                if(app.wallpaper_preview_ok) {
                    if(iw>ih) {
                        thingsos_rect(&app.canvas,35,433,614,346,170);
                        paint_image_cover(&app.canvas,pixels,iw,ih,36,434,612,344,true);
                        centered_label("横屏壁纸 · 锁屏按 1216 × 684 横向显示",36,830,22,612);
                    } else {
                        /* Same 684:1216 aspect as the lock, so the preview is the final framing. */
                        paint_image_cover(&app.canvas,pixels,iw,ih,128,236,428,761,true);
                    }
                    free(pixels);
                }
                const char* base=strrchr(app.wallpaper_candidate_image,'/');
                centered_label(base?base+1:app.wallpaper_candidate_image,36,1020,26,612);
            }
            if(!app.wallpaper_preview_ok) {
                thingsos_rect(&app.canvas,36,236,612,740,255);
                centered_label("无法预览：图片过大、损坏或没有封面",36,560,24,612);
                centered_label("请使用较小的 JPG / PNG / WebP 图片",36,620,24,612);
            }
        }
        thingsos_rect(&app.canvas,0,1090,684,126,255);
        centered_label("重新选择",36,1150,28,280);
        centered_label(app.wallpaper_preview_ok?"设为壁纸":"无法使用",368,1150,28,280);
        return;
    }
    header(app.wallpaper_step==1?"选择一本书":app.wallpaper_step==2?"选择图片":"壁纸");
    if(app.wallpaper_step==0) {
        button("阅读统计",300);button("指定一本书的封面",430);button("wallpapers 目录中的图片",560);
        label("选择后预览，确认才会替换当前壁纸",36,760,25,612);
        label("图片支持 JPG / PNG / WebP，建议 684×1216 或 1216×684",36,810,24,612);
        button("返回应用",1000);return;
    }
    size_t count=app.wallpaper_step==1?app.book_count:app.browser.count;
    if(app.wallpaper_step==2)label(app.browser.path,36,270,22,612);
    else label("含本地书籍与已缓存的微读书籍",36,270,22,612);
    if(app.wallpaper_step==2) {
        for(size_t row=0;row<4 && app.wallpaper_start+row<count;++row) {
            size_t i=app.wallpaper_start+row;int x=36+(int)(row%2)*318,y=300+(int)(row/2)*328;
            thingsos_rect(&app.canvas,x,y,294,304,210);thingsos_rect(&app.canvas,x+1,y+1,292,302,255);
            char path[THINGSOS_RECORD_PATH_BYTES];bool picture=false;
            if(!app.browser.entries[i].directory && thingsos_browser_path(&app.browser,i,path))
                picture=wallpaper_thumbnail(path,x+12,y+8,270,244);
            if(!picture)centered_label(app.browser.entries[i].directory?"文件夹":"无法预览",x+12,y+136,26,270);
            label(app.browser.entries[i].name,x+12,y+284,23,270);
        }
        label("点击预览 · 长按图片删除",36,1060,22,612);
    } else for(size_t row=0;row<7 && app.wallpaper_start+row<count;++row) {
        size_t i=app.wallpaper_start+row;int y=300+(int)row*88;
        const char* name=app.wallpaper_step==1?app.books[i].name:app.browser.entries[i].name;
        snprintf(text,sizeof(text),"%s%s",app.wallpaper_step==2 && app.browser.entries[i].directory?"目录 · ":"",name);
        label(text,36,y+48,28,612);thingsos_rect(&app.canvas,36,y+76,612,1,210);
    }
    if(!count)label(app.wallpaper_step==1?"暂无书籍，请先添加到书库":"此目录暂无 JPG / PNG / WebP 图片",36,440,26,612);
    size_t per=app.wallpaper_step==2?4:7;
    snprintf(text,sizeof(text),"上一页       %u / %u       下一页",(unsigned)(app.wallpaper_start/per)+1,(unsigned)(count?(count+per-1)/per:1));
    centered_label(text,36,1008,26,612);
    centered_label(app.wallpaper_step==2 && strcmp(app.browser.path,"/sdcard/wallpapers")?"上级目录":"返回壁纸类型",36,1140,28,612);
    if(app.wallpaper_step==2 && app.browser.truncated)label("文件较多，请分放到子目录",36,236,22,612);
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
                    if(wallpaper_scan(dir))app.wallpaper_start=app.browser.count?(start<app.browser.count?start:((app.browser.count-1)/4)*4):0;
                    say("已删除壁纸");
                }
                wallpaper_delete_path[0]=0;
            }
        }
        app.dirty=true;return true;
    }
    if(x<90 && y>=74 && y<200) {
        if(app.wallpaper_step==0)navigate(THINGSOS_UI_APPS);
        else if(app.wallpaper_step==3)app.wallpaper_step=app.wallpaper_kind;
        else app.wallpaper_step=0;
    } else if(app.wallpaper_step==3) {
        if(y>=1090) {
            if(x<342)app.wallpaper_step=app.wallpaper_kind;
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
        if(y>=1000)navigate(THINGSOS_UI_APPS);
        else if(y>=300 && y<395){app.wallpaper_kind=0;app.wallpaper_step=3;app.wallpaper_preview_ok=false;}
        else if(y>=430 && y<525){library_load();app.wallpaper_step=1;app.wallpaper_start=0;}
        else if(y>=560 && y<655) {
            if(media()) {
                mkdir("/sdcard/wallpapers",0775);
                if(wallpaper_scan("/sdcard/wallpapers"))app.wallpaper_step=2;else say("无法读取 wallpapers 目录");
            } else say("请插入 SD 卡");
        }
    } else if(y>=1090) {
        if(app.wallpaper_step==2 && strcmp(app.browser.path,"/sdcard/wallpapers")) {
            char parent[THINGSOS_RECORD_PATH_BYTES];strlcpy(parent,app.browser.path,sizeof(parent));
            char* slash=strrchr(parent,'/');if(slash)*slash=0;wallpaper_scan(parent);
        } else app.wallpaper_step=0;
    } else if(y>=960 && y<1040) {
        size_t count=app.wallpaper_step==1?app.book_count:app.browser.count;
        size_t per=app.wallpaper_step==2?4:7;
        if(x<228 && app.wallpaper_start>=per)app.wallpaper_start-=per;
        if(x>=456 && app.wallpaper_start+per<count)app.wallpaper_start+=per;
    } else if(y>=300 && y<932) {
        size_t i=app.wallpaper_step==2?wallpaper_card_at(x,y):y<916?app.wallpaper_start+(size_t)(y-300)/88:SIZE_MAX;
        if(app.wallpaper_step==1 && i<app.book_count) {
            app.wallpaper_candidate=app.books[i];app.wallpaper_kind=1;app.wallpaper_step=3;app.wallpaper_preview_ok=false;
        } else if(app.wallpaper_step==2 && i<app.browser.count) {
            char path[THINGSOS_RECORD_PATH_BYTES];
            if(thingsos_browser_path(&app.browser,i,path)) {
                if(app.browser.entries[i].directory)wallpaper_scan(path);
                else {strlcpy(app.wallpaper_candidate_image,path,sizeof(app.wallpaper_candidate_image));app.wallpaper_kind=2;app.wallpaper_step=3;app.wallpaper_preview_ok=false;}
            }
        }
    }
    app.dirty=true;return true;
}
