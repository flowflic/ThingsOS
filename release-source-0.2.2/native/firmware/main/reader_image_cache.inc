// SPDX-License-Identifier: Apache-2.0
/* Two decoded images, at most 2 MiB. A hit returns an owned copy so existing
   inline/viewer callers keep their free() contract. Book close invalidates all. */
static struct {
    unsigned char* pixels;
    unsigned w,h,cw,ch;
    bool comic;
    uint64_t age;
    char path[256];
} reader_images[2];
static uint64_t reader_image_age;
static void reader_image_cache_clear(void) {
    for(unsigned i=0;i<2;++i){free(reader_images[i].pixels);memset(&reader_images[i],0,sizeof(reader_images[i]));}
}
static bool reader_image_cached(const char* path,bool comic,unsigned char** pixels,unsigned* w,unsigned* h) {
    for(unsigned i=0;i<2;++i) {
        if(!reader_images[i].pixels || reader_images[i].comic!=comic || strcmp(path,reader_images[i].path) ||
           reader_images[i].cw!=(unsigned)app.canvas.width || reader_images[i].ch!=(unsigned)app.canvas.height)continue;
        size_t n=(size_t)reader_images[i].w*reader_images[i].h;
        unsigned char* copy=thingsos_image_buffer(n);if(!copy)return false;
        memcpy(copy,reader_images[i].pixels,n);*pixels=copy;*w=reader_images[i].w;*h=reader_images[i].h;
        reader_images[i].age=++reader_image_age;
        ESP_LOGI("ThingsOS_reader","Inline image cache hit %ux%u path=%s",*w,*h,path);
        return true;
    }
    return false;
}
static void reader_image_remember(const char* path,bool comic,const unsigned char* pixels,unsigned w,unsigned h) {
    if(!w || !h || (uint64_t)w*h>1024u*1024u)return;
    unsigned slot=!reader_images[0].pixels?0:!reader_images[1].pixels?1:
        reader_images[0].age<reader_images[1].age?0:1;
    free(reader_images[slot].pixels);memset(&reader_images[slot],0,sizeof(reader_images[slot]));
    size_t n=(size_t)w*h;unsigned char* copy=thingsos_image_buffer(n);if(!copy)return;
    memcpy(copy,pixels,n);reader_images[slot].pixels=copy;
    reader_images[slot].w=w;reader_images[slot].h=h;
    reader_images[slot].cw=(unsigned)app.canvas.width;reader_images[slot].ch=(unsigned)app.canvas.height;
    reader_images[slot].comic=comic;
    reader_images[slot].age=++reader_image_age;
    strlcpy(reader_images[slot].path,path,sizeof(reader_images[slot].path));
}
