/* SPDX-License-Identifier: Apache-2.0 */
static uint64_t lock_cover_stamp(book_t*,const char*);
#ifdef THINGSOS_HOST_UI
static char host_thumbnail_cache_root[512];
#endif
static bool thumbnail_disk_path(book_t* book,const char* source,char path[640],uint64_t* stamp) {
    const char* root="/sdcard/.thingsos/thumbnails";
#ifdef THINGSOS_HOST_UI
    root=host_thumbnail_cache_root;
#endif
    if(!root[0] || !media())return false;
    *stamp=lock_cover_stamp(book,source);
    if(!*stamp)return false;
    int n=snprintf(path,640,"%s/%016llx.gray",root,(unsigned long long)book->id);
    return n>0 && n<640;
}
static const thingsos_thumbnail_t* thumbnail_disk_load(book_t* book,const char* source) {
    char path[640];uint64_t stamp;
    if(!thumbnail_disk_path(book,source,path,&stamp))return NULL;
    unsigned char* pixels=thingsos_image_buffer(COVER_WIDTH*COVER_HEIGHT);
    if(!pixels)return NULL;
    thingsos_canvas_t canvas={.pixels=pixels,.width=COVER_WIDTH,.height=COVER_HEIGHT};
    const thingsos_thumbnail_t* result=NULL;
    if(thingsos_cover_cache_load(path,stamp,&canvas,0,0,COVER_WIDTH,COVER_HEIGHT) &&
       cover_has_content(pixels,COVER_WIDTH,COVER_HEIGHT)) {
        result=thingsos_thumbnail_store(&app.thumbnails,book->id,COVER_WIDTH,COVER_HEIGHT,pixels,COVER_WIDTH,COVER_HEIGHT);
        thingsos_thumbnail_version(&app.thumbnails,book->id,COVER_WIDTH,COVER_HEIGHT,stamp);
        ESP_LOGI("ThingsOS_cover","Thumbnail SD hit book=%llu bytes=%u",(unsigned long long)book->id,COVER_WIDTH*COVER_HEIGHT);
    }
    free(pixels);return result;
}
static void thumbnail_disk_save(book_t* book,const char* source,const thingsos_thumbnail_t* thumb) {
    char path[640];uint64_t stamp;
    if(!thumb || !thumb->pixels || !thumbnail_disk_path(book,source,path,&stamp))return;
#ifndef THINGSOS_HOST_UI
    mkdir("/sdcard/.thingsos",0775);mkdir("/sdcard/.thingsos/thumbnails",0775);
#endif
    thingsos_canvas_t canvas={.pixels=thumb->pixels,.width=thumb->width,.height=thumb->height};
    bool saved=thingsos_cover_cache_save(path,stamp,&canvas,0,0,thumb->width,thumb->height);
    ESP_LOGI("ThingsOS_cover","Thumbnail SD save book=%llu saved=%d",(unsigned long long)book->id,saved);
}
