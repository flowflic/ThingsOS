// SPDX-License-Identifier: Apache-2.0
#include "thingsos/reader.h"
#include "thingsos/ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(e) do{if(!(e)){fprintf(stderr,"Failed line %d: %s\n",__LINE__,#e);exit(1);}}while(0)
#include "thingsos/pagination.h"
int main(void) {
 thingsos_page_position_t positions[]={{0,0},{0,100},{1,0},{1,200},{2,0}};
 CHECK(thingsos_page_find(positions,5,0,150)==1);
 CHECK(thingsos_page_find(positions,5,1,0)==2);
 CHECK(thingsos_page_find(positions,5,2,999)==4);
 CHECK(thingsos_page_find(positions,5,0,0)==0);
 CHECK(thingsos_book_percent(2,5)==50 && thingsos_book_percent(4,5)==100);
 CHECK(thingsos_page_percent(100,5)==4 && thingsos_page_percent(50,5)==2);
 CHECK(thingsos_page_percent(0,0)==0);
 const char* cache="test-page-cache.dat";
 thingsos_page_position_t* loaded=NULL;size_t cached_count=0;
 CHECK(thingsos_page_cache_save(cache,42,positions,5));
 CHECK(thingsos_page_cache_load(cache,42,&loaded,&cached_count));
 CHECK(cached_count==5 && !memcmp(loaded,positions,sizeof(positions)));free(loaded);
 CHECK(!thingsos_page_cache_load(cache,43,&loaded,&cached_count) && !loaded && !cached_count);
 FILE* damaged=fopen(cache,"r+b");CHECK(damaged);
 CHECK(!fseek(damaged,40,SEEK_SET) && fputc(7,damaged)!=EOF);fclose(damaged);
 CHECK(!thingsos_page_cache_load(cache,42,&loaded,&cached_count));
 damaged=fopen(cache,"wb");CHECK(damaged);fputs("truncated",damaged);fclose(damaged);
 CHECK(!thingsos_page_cache_load(cache,42,&loaded,&cached_count));remove(cache);

 size_t pages[8],count;
 const unsigned char text[]="AB\xe4\xb8\xad" "CD\r\nEF";
 CHECK(thingsos_txt_pages(text,sizeof(text)-1,4,1,pages,8,&count)==THINGSOS_TEXT_OK);
 CHECK(count==3 && pages[0]==0 && pages[1]==5 && pages[2]==9);
 CHECK(thingsos_txt_pages(text,sizeof(text)-1,4,1,pages,1,&count)==THINGSOS_TEXT_CAPACITY);
 CHECK(count==0);
 const unsigned char bad[]={0xed,0xa0,0x80};
 CHECK(thingsos_txt_pages(bad,3,4,1,pages,8,&count)==THINGSOS_TEXT_INVALID);
 const unsigned char truncated[]={0xf0,0x9f};
 CHECK(thingsos_txt_pages(truncated,2,4,1,pages,8,&count)==THINGSOS_TEXT_INVALID);
 CHECK(thingsos_txt_pages((const unsigned char*)"",0,4,1,pages,8,&count)==THINGSOS_TEXT_OK && count==1);
 thingsos_size_t fit;
 CHECK(thingsos_image_fit(1536,1024,612,294,&fit) && fit.width==441 && fit.height==294);
 CHECK(thingsos_image_fit(UINT32_MAX,1,612,294,&fit) && fit.width==612 && fit.height==1);
 CHECK(!thingsos_image_fit(0,1,612,294,&fit));
 CHECK(thingsos_image_fit_width(540,1216,612,&fit) && fit.width==612 && fit.height==1378);
 CHECK(thingsos_image_fit_width(1937,820,612,&fit) && fit.width==612 && fit.height==259);
 CHECK(!thingsos_image_fit_width(0,1,612,&fit));
 CHECK(!thingsos_image_fit_width(1,UINT32_MAX,612,&fit));
 const uint8_t src[]={0,64,128,255};uint8_t dst[4]={7,7,7,7};
 thingsos_gray_t plane={src,2,2,2,4};
 CHECK(!thingsos_image_resize(&plane,dst,3,2,2));
 CHECK(dst[0]==7 && dst[3]==7);
 CHECK(thingsos_image_resize(&plane,dst,4,2,2) && !memcmp(dst,src,4));
 plane.bytes=3;CHECK(!thingsos_image_resize(&plane,dst,4,2,2));
 thingsos_ui_layout_t layout;thingsos_ui_layout(&layout);
 CHECK(layout.body.width==612 && layout.footer.y+layout.footer.height==1216);
 CHECK(thingsos_ui_nav_hit(170,1098)==0 && thingsos_ui_nav_hit(171,1098)==1 && thingsos_ui_nav_hit(342,1098)==2 && thingsos_ui_nav_hit(513,1098)==3);
 CHECK(thingsos_ui_nav_hit(684,1100)==-1 && thingsos_ui_nav_hit(1,1216)==-1);
 thingsos_location_t loc={42,3,15360},restored={0};thingsos_image_view_t view;
 thingsos_image_open(&view,loc);thingsos_image_zoom(&view,50);
 CHECK(view.open && view.zoom_percent==150);
 thingsos_image_pan(&view,INT32_MAX,INT32_MAX,612,408,612,408);
 CHECK(view.pan_x==306 && view.pan_y==204);
 thingsos_image_pan(&view,INT32_MIN,INT32_MIN,612,408,612,408);
 CHECK(view.pan_x==0 && view.pan_y==0);
 thingsos_image_zoom(&view,INT32_MAX);CHECK(view.zoom_percent==300);
 thingsos_image_zoom(&view,INT32_MIN);CHECK(view.zoom_percent==100);
 thingsos_image_close(&view,&restored);
 CHECK(!view.open && restored.book_id==42 && restored.chapter==3 && restored.byte_offset==15360);
 thingsos_runtime_t runtime;thingsos_runtime_init(&runtime);
 thingsos_runtime_navigate(&runtime,THINGSOS_READING,1000);
 thingsos_runtime_image_overlay(&runtime,true,1000);thingsos_runtime_tick(&runtime,2000);
 CHECK(runtime.reading_ms==1000);
 thingsos_runtime_overlay(&runtime,true,2000);thingsos_runtime_tick(&runtime,3000);
 CHECK(runtime.reading_ms==1000);
 puts("PASS: UTF-8 boundaries/capacity, image bounds/aspect ratio, v3 geometry, image return location");return 0;
}
