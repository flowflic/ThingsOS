// SPDX-License-Identifier: Apache-2.0
/* A bounded preview owned by the UI task, separate from reader image state. */
static struct {
    bool open;
    unsigned char* pixels;
    unsigned width,height;
    char name[192];
} file_picture;
static bool file_picture_path(const char* path) {
    if(!thingsos_user_file_allowed(path))return false;
    const char* ext=strrchr(path,'.');
    return ext && (!strcasecmp(ext,".png") || !strcasecmp(ext,".jpg") || !strcasecmp(ext,".jpeg") || !strcasecmp(ext,".webp"));
}
static void file_picture_release(void) {
    free(file_picture.pixels);memset(&file_picture,0,sizeof(file_picture));
}
static bool file_picture_open(const char* path) {
    if(!file_picture_path(path))return false;
    reader_cache_clear();file_picture_release();
    const char* disk=path;
#ifdef THINGSOS_HOST_UI
    char actual[1024];snprintf(actual,sizeof(actual),"%s%s",host_picture_root,path+7);disk=actual;
#endif
    FILE* file=fopen(disk,"rb");if(!file){say("图片无法读取");return false;}
    unsigned char* raw=NULL;bool ok=false;long bytes=0;
    if(!fseek(file,0,SEEK_END)) {
        bytes=ftell(file);rewind(file);
        if(bytes>0 && bytes<=1024*1024) {
            raw=thingsos_image_buffer((size_t)bytes);
            if(raw && fread(raw,1,(size_t)bytes,file)==(size_t)bytes)
                ok=thingsos_decode_wallpaper(raw,(size_t)bytes,&file_picture.pixels,&file_picture.width,&file_picture.height);
        }
    }
    fclose(file);free(raw);
    if(!ok){file_picture_release();say(bytes>1024*1024?"图片过大，请通过传书压缩后重新上传":"图片无法预览，请通过传书重新上传");return false;}
    const char* base=strrchr(path,'/');strlcpy(file_picture.name,base?base+1:path,sizeof(file_picture.name));
    file_picture.open=true;app.dirty=app.transition_refresh=true;return true;
}
static bool file_picture_draw(void) {
    if(app.page!=THINGSOS_UI_FILES || !file_picture.open)return false;
    header("图片预览");label(file_picture.name,36,255,25,612);
    paint_image_fit(&app.canvas,file_picture.pixels,file_picture.width,file_picture.height,36,280,612,720,true,true,false);
    char text[64];snprintf(text,sizeof(text),"%u × %u · 等比例预览",file_picture.width,file_picture.height);
    centered_label(text,36,1040,24,612);button("返回目录",1080);return true;
}
static bool file_picture_tap(int x,int y) {
    if(app.page!=THINGSOS_UI_FILES || !file_picture.open)return false;
    if((x<120 && y>=74 && y<200) || y>=1080) {
        file_picture_release();app.dirty=app.transition_refresh=true;
    }
    return true;
}
