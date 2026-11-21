// SPDX-License-Identifier: Apache-2.0
#include "thingsos/display_buffer.h"
#include "thingsos/thumbnail_cache.h"
#include "thingsos/refresh_mask.h"
#include "thingsos/refresh_policy.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"Display/cache line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
static void* fail_allocate(size_t bytes) { (void)bytes; return NULL; }
static void packing(void) {
    size_t pixels=684u*1216u;
    unsigned char* src=malloc(pixels);
    unsigned char* actual=malloc(THINGSOS_PANEL_BYTES+2);
    unsigned char* expected=malloc(THINGSOS_PANEL_BYTES);
    CHECK(src && actual && expected);
    for(unsigned y=0;y<1216;++y) for(unsigned x=0;x<684;++x)
        src[(size_t)y*684+x]=(unsigned char)(x*13u+y*37u+x*y);
    thingsos_canvas_t c={src,684,1216};
    for(unsigned mono=0;mono<2;++mono) {
        memset(actual,0xa5,THINGSOS_PANEL_BYTES+2);
        memset(expected,0,THINGSOS_PANEL_BYTES);
        /* Independent per-pixel oracle of official rotation and nibble writes. */
        for(unsigned y=0;y<1216;++y) for(unsigned x=0;x<684;++x) {
            unsigned gray=src[(size_t)y*684+x];
            gray=mono?(gray<128?0:15):(gray>>4);
            size_t at=(size_t)(683-x)*608+y/2;
            if(y&1) expected[at]=(unsigned char)((expected[at]&15)|(gray<<4));
            else expected[at]=(unsigned char)((expected[at]&240)|gray);
        }
        CHECK(thingsos_panel_pack(&c,actual+1,THINGSOS_PANEL_BYTES,mono!=0));
        CHECK(actual[0]==0xa5 && actual[THINGSOS_PANEL_BYTES+1]==0xa5);
        CHECK(!memcmp(actual+1,expected,THINGSOS_PANEL_BYTES));
    }
    CHECK(thingsos_panel_pack(&c,expected,THINGSOS_PANEL_BYTES,false));
    memset(actual,0xa5,THINGSOS_PANEL_BYTES+2);
    CHECK(thingsos_panel_pack_rows(&c,actual+1,THINGSOS_PANEL_BYTES,false,0,74));
    for(unsigned row=0;row<684;++row) {
        CHECK(!memcmp(actual+1+row*608,expected+row*608,37));
        for(unsigned col=37;col<608;++col) CHECK(actual[1+row*608+col]==0xa5);
    }
    /* Editor packing must preserve every byte outside its refreshed rows. */
    for(unsigned layout=0;layout<2;++layout) {
        CHECK(thingsos_panel_pack(&c,expected,THINGSOS_PANEL_BYTES,true));
        memset(actual,0xa5,THINGSOS_PANEL_BYTES+2);
        CHECK(thingsos_panel_pack_rows(&c,actual+1,THINGSOS_PANEL_BYTES,true,300,254));
        CHECK(thingsos_panel_pack_rows(&c,actual+1,THINGSOS_PANEL_BYTES,true,638,layout?440:116));
        for(unsigned x=0;x<684;++x)for(unsigned pair=0;pair<608;++pair) {
            unsigned y=pair*2;
            bool changed=(y>=300 && y<554) || (y>=638 && y<(layout?1078u:754u));
            CHECK(actual[1+x*608+pair]==(changed?expected[x*608+pair]:0xa5));
        }
        CHECK(actual[0]==0xa5 && actual[THINGSOS_PANEL_BYTES+1]==0xa5);
    }
    CHECK(!thingsos_panel_pack_rows(&c,actual,THINGSOS_PANEL_BYTES,false,1,74));
    CHECK(!thingsos_panel_pack_rows(&c,actual,THINGSOS_PANEL_BYTES,false,0,73));
    CHECK(!thingsos_panel_pack(&c,actual,THINGSOS_PANEL_BYTES-1,false));
    c.height=1215; CHECK(!thingsos_panel_pack(&c,actual,THINGSOS_PANEL_BYTES,false));
    c.height=1216; c.pixels=NULL; CHECK(!thingsos_panel_pack(&c,actual,THINGSOS_PANEL_BYTES,false));
    free(src);free(actual);free(expected);
}
static void landscape_packing(void) {
    unsigned char* src=malloc(684u*1216u);
    unsigned char* packed=malloc(THINGSOS_PANEL_BYTES+2);CHECK(src && packed);
    thingsos_canvas_t canvas={src,1216,684};
    for(unsigned y=0;y<684;++y)for(unsigned x=0;x<1216;++x)src[(size_t)y*1216+x]=(unsigned char)(x*31+y*13);
    for(unsigned mono=0;mono<2;++mono) {
        memset(packed,0xa5,THINGSOS_PANEL_BYTES+2);
        CHECK(thingsos_panel_pack(&canvas,packed+1,THINGSOS_PANEL_BYTES,mono!=0));
        for(unsigned y=0;y<684;++y)for(unsigned x=0;x<1216;x+=2) {
            unsigned a=src[(size_t)y*1216+x],b=src[(size_t)y*1216+x+1];
            unsigned expected=mono?((a<128?0:15)|(b<128?0:240)):((a>>4)|(b&240));
            CHECK(packed[1+(size_t)y*608+x/2]==expected);
        }
        CHECK(packed[0]==0xa5 && packed[THINGSOS_PANEL_BYTES+1]==0xa5);
    }
    CHECK(!thingsos_panel_pack(&canvas,packed,THINGSOS_PANEL_BYTES-1,false));
    canvas.height=683;CHECK(!thingsos_panel_pack(&canvas,packed,THINGSOS_PANEL_BYTES,false));
    canvas.height=684;unsigned first=0,rows=0;
    CHECK(!thingsos_panel_pack_diff(&canvas,packed,THINGSOS_PANEL_BYTES,false,&first,&rows));
    free(src);free(packed);
}
static void thumbnails(void) {
    thingsos_thumbnail_cache_t c={0};
    unsigned char src[]={0,40,80,120,160,255}, canvas[30];
    const thingsos_thumbnail_t* t=thingsos_thumbnail_store(&c,42,4,5,src,2,3);
    CHECK(t && t->pixels && c.bytes==20);
    /* No upscaling, centered white margins; clipped blit preserves surroundings. */
    CHECK(t->pixels[1u*4+1]==0 && t->pixels[3u*4+2]==255 && t->pixels[0]==255);
    memset(canvas,17,sizeof(canvas));thingsos_canvas_t view={canvas,6,5};
    thingsos_thumbnail_paint(t,&view,-1,1);
    CHECK(canvas[6]==255 && canvas[12]==0 && canvas[13]==40 && canvas[5]==17);
    CHECK(thingsos_thumbnail_find(&c,42,4,5)==t);
    CHECK(!thingsos_thumbnail_find(&c,42,5,5));
    CHECK(thingsos_thumbnail_store(&c,43,4,5,NULL,0,0));
    t=thingsos_thumbnail_find(&c,43,4,5);CHECK(t && !t->pixels);
    CHECK(c.bytes==20);
    thingsos_thumbnail_clear(&c);CHECK(c.bytes==0 && !thingsos_thumbnail_find(&c,42,4,5));
    for(unsigned i=0;i<THINGSOS_THUMBNAIL_SLOTS;++i)
        CHECK(thingsos_thumbnail_store(&c,i,1,1,NULL,0,0));
    CHECK(thingsos_thumbnail_find(&c,0,1,1));
    CHECK(thingsos_thumbnail_store(&c,100,1,1,NULL,0,0));
    CHECK(thingsos_thumbnail_find(&c,0,1,1) && !thingsos_thumbnail_find(&c,1,1,1));
    thingsos_thumbnail_clear(&c);
    for(unsigned i=0;i<12;++i) {
        CHECK(thingsos_thumbnail_store(&c,i,256,256,src,2,3));
        CHECK(c.bytes<=THINGSOS_THUMBNAIL_BYTES);
    }
    CHECK(!thingsos_thumbnail_find(&c,0,256,256));
    CHECK(thingsos_thumbnail_find(&c,11,256,256));
    thingsos_thumbnail_clear(&c);c.allocate=fail_allocate;
    CHECK(!thingsos_thumbnail_store(&c,8,4,5,src,2,3));
    CHECK(!thingsos_thumbnail_find(&c,8,4,5) && c.bytes==0);
    c.allocate=NULL; CHECK(thingsos_thumbnail_store(&c,8,4,5,src,2,3));
    thingsos_thumbnail_clear(&c);
    CHECK(!thingsos_thumbnail_store(&c,1,UINT32_MAX,UINT32_MAX,src,2,3));
    CHECK(!thingsos_thumbnail_store(&c,1,4,5,src,0,3));
}
static void background_diff(void) {
    unsigned char* pixels=malloc(684u*1216u);
    unsigned char* fb=malloc(THINGSOS_PANEL_BYTES+2);
    unsigned char* expected=malloc(THINGSOS_PANEL_BYTES);
    CHECK(pixels && fb && expected);
    thingsos_canvas_t c={pixels,684,1216};
    for(unsigned mono=0;mono<2;++mono) {
        memset(pixels,255,684u*1216u);memset(fb,0xa5,THINGSOS_PANEL_BYTES+2);
        CHECK(thingsos_panel_pack(&c,fb+1,THINGSOS_PANEL_BYTES,mono!=0));
        unsigned first=99,rows=99;
        CHECK(thingsos_panel_pack_diff(&c,fb+1,THINGSOS_PANEL_BYTES,mono!=0,&first,&rows));
        CHECK(!rows && first==0);
        /* Minute clock, pagination footer, newly loaded underline, and both
           corners exercise logical-to-physical rotation and row alignment. */
        const unsigned ys[]={25,1173,401,0,1215};
        const unsigned xs[]={600,36,340,0,683};
        for(unsigned i=0;i<5;++i) {
            pixels[(size_t)ys[i]*684+xs[i]]=0;
            CHECK(thingsos_panel_pack_diff(&c,fb+1,THINGSOS_PANEL_BYTES,mono!=0,&first,&rows));
            CHECK(first==(ys[i]&~1u) && rows==2);
            CHECK(thingsos_panel_pack(&c,expected,THINGSOS_PANEL_BYTES,mono!=0));
            CHECK(!memcmp(fb+1,expected,THINGSOS_PANEL_BYTES));
            CHECK(fb[0]==0xa5 && fb[THINGSOS_PANEL_BYTES+1]==0xa5);
            CHECK(thingsos_panel_pack_diff(&c,fb+1,THINGSOS_PANEL_BYTES,mono!=0,&first,&rows) && !rows);
        }
        /* Raw coverage may change while the panel's 4-bit value stays equal. */
        pixels[1]=254;
        CHECK(thingsos_panel_pack_diff(&c,fb+1,THINGSOS_PANEL_BYTES,mono!=0,&first,&rows) && !rows);
        pixels[0]=255;pixels[684u*1215u+683]=255;
        CHECK(thingsos_panel_pack_diff(&c,fb+1,THINGSOS_PANEL_BYTES,mono!=0,&first,&rows));
        CHECK(first==0 && rows==1216);
    }
    CHECK(!thingsos_panel_pack_diff(&c,fb,THINGSOS_PANEL_BYTES-1,false,NULL,NULL));
    free(expected);free(fb);free(pixels);
}
static void refresh_mask(void) {
    bool rows[8]={0};uint8_t columns[6]={0};
    rows[0]=true;columns[0]=3;
    CHECK(thingsos_refresh_include_rect(rows,8,columns,12,3,2,5,4));
    for(unsigned y=0;y<8;++y)CHECK(rows[y]==(y==0 || (y>=2 && y<6)));
    for(unsigned x=0;x<12;++x)CHECK(((columns[x/2]>>(4*(x%2)))&15)==(x==0?3:(x>=3 && x<8)?15:0));
    CHECK(!thingsos_refresh_include_rect(rows,8,columns,12,11,0,2,1));
    CHECK(!thingsos_refresh_include_rect(rows,8,columns,12,0,7,1,2));
    unsigned char source[36*64],original[36*64],target[36*64+2];
    for(unsigned i=0;i<sizeof(source);++i) source[i]=(unsigned char)(i*53+i/7);
    memcpy(original,source,sizeof(source));memset(target,0xa5,sizeof(target));
    CHECK(thingsos_refresh_mask_equal(source,36,target+1,sizeof(source)));
    for(unsigned f=0;f<36;++f) for(unsigned to=0;to<16;++to) for(unsigned from=0;from<16;++from) {
        unsigned at=f*64+to*4+from/4,shift=6-2*(from%4);
        unsigned got=(target[1+at]>>shift)&3,want=(source[at]>>shift)&3;
        CHECK(got==(from==to?0:want));
    }
    CHECK(!memcmp(source,original,sizeof(source)) && target[0]==0xa5 && target[sizeof(target)-1]==0xa5);
    CHECK(!thingsos_refresh_mask_equal(source,36,target,sizeof(source)-1));
    CHECK(!thingsos_refresh_mask_equal(source,0,target,sizeof(target)));
    CHECK(!thingsos_refresh_mask_equal(source,36,source,sizeof(source)));
    CHECK(!thingsos_refresh_mask_equal(NULL,36,target,sizeof(target)));
}
static void refresh_schedule(void) {
    for(unsigned mode=0;mode<3;++mode) {
        CHECK(!thingsos_refresh_reader_wave(true,true,false,true,false,mode));
        CHECK(!thingsos_refresh_reader_wave(true,true,false,true,true,mode));
        CHECK(!thingsos_refresh_reader_wave(true,false,false,true,true,mode));
        CHECK(!thingsos_refresh_reader_wave(false,true,false,true,true,mode));
        CHECK(!thingsos_refresh_reader_wave(true,true,true,true,true,mode));
        CHECK(thingsos_refresh_reader_wave(true,true,false,false,true,mode)==(mode==0));
        CHECK(!thingsos_refresh_reader_wave(true,true,false,false,false,mode));
    }
    CHECK(!thingsos_refresh_full_scope(false,false));
    CHECK(thingsos_refresh_full_scope(false,true));
    CHECK(thingsos_refresh_full_scope(true,false));
    CHECK(thingsos_refresh_full_scope(true,true));
    CHECK(thingsos_refresh_reader_quality(true,false,0));
    CHECK(!thingsos_refresh_reader_quality(true,true,0));
    CHECK(!thingsos_refresh_reader_quality(false,false,0));
    CHECK(!thingsos_refresh_reader_quality(true,false,1));
    CHECK(!thingsos_refresh_reader_quality(true,false,2));
    for(unsigned interval=1;interval<=12;++interval) {
        unsigned soft=interval-1;
        CHECK(thingsos_refresh_wave_budget(0,soft,interval));
        CHECK(!thingsos_refresh_periodic(true,0,soft,interval));
        ++soft; /* One intermediate DU frame must leave cleanup pending. */
        CHECK(thingsos_refresh_periodic(false,0,soft,interval));
        CHECK(!thingsos_refresh_wave_budget(2,soft,interval));
        CHECK(!thingsos_refresh_wave_budget(1,soft,interval));
    }
    CHECK(thingsos_refresh_wave_budget(0,8,12));
    CHECK(!thingsos_refresh_wave_budget(0,10,12));
    CHECK(thingsos_refresh_periodic(false,2,0,12));
    /* Controls and their first dismissal frame must not trigger a pending
       policy/periodic clean, even in clear mode. Resume on the next body turn. */
    for(unsigned mode=0;mode<3;++mode)for(unsigned policy=0;policy<2;++policy) {
        CHECK(!thingsos_refresh_automatic_clean(true,false,false,policy!=0,mode,14,15));
        CHECK(thingsos_refresh_automatic_clean(false,false,false,policy!=0,mode,14,15));
        CHECK(thingsos_refresh_automatic_clean(false,false,false,true,mode,0,15));
        CHECK(!thingsos_refresh_automatic_clean(true,true,true,policy!=0,mode,14,15));
    }
    CHECK(!thingsos_refresh_automatic_clean(false,false,false,false,0,0,15));
    CHECK(!thingsos_refresh_automatic_clean(false,true,false,false,0,14,15));
    CHECK(!thingsos_refresh_automatic_clean(false,false,true,false,0,14,15));
}
int main(void) { refresh_schedule();packing();landscape_packing();background_diff();thumbnails();refresh_mask(); puts("PASS: panel packing, background row differences, cache, and all 36x16x16 LUT actions: equal pixels hold, changed actions exact, official source untouched");return 0; }
