// SPDX-License-Identifier: Apache-2.0
#include "thingsos/graphics.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"vertical:%d: %s\n",__LINE__,#x);exit(1);}}while(0)
static unsigned stops;
static bool column_budget(void* unused){(void)unused;return ++stops<3;}
static unsigned images;
static void full_width_columns(thingsos_font_t* font,const thingsos_chapter_t* ch,thingsos_text_hits_t* hits) {
    const unsigned spacing[]={125,145,175};
    for(unsigned size=30;size<=70;size+=4)for(unsigned wide=0;wide<2;++wide)
        for(unsigned margin=12;margin<=36;margin+=12)for(unsigned s=0;s<3;++s) {
            int width=(wide?1216:684)-(int)margin*2;
            CHECK(thingsos_font_size(font,size));memset(hits,0,sizeof(*hits));
            size_t end=thingsos_text_page_layout_flow(font,NULL,ch,0,(int)margin,90,width,1040,
                spacing[s],0,true,NULL,NULL,hits,true);
            CHECK(end>0&&end<ch->bytes&&hits->glyph_count>20);
            int left=margin+width;for(size_t i=0;i<hits->glyph_count;++i)
                if(hits->glyphs[i].x<left)left=hits->glyphs[i].x;
            if(left-(int)margin>=(int)size){
                fprintf(stderr,"Unused full column: size=%u wide=%u margin=%u spacing=%u width=%d gap=%d\n",
                    size,wide,margin,spacing[s],width,left-(int)margin);
                CHECK(left-(int)margin<(int)size);
            }
        }
}
static bool image_draw(void* context,const thingsos_inline_t* image,thingsos_canvas_t* c,int x,int y,int w,int h){
    (void)context;CHECK(!c->vertical_text && image->width==240 && image->height==360);
    CHECK(x>=36 && y>=90 && x+w<=648 && y+h<=1130 && w>0 && h>0);
    CHECK(abs(w*3-h*2)<=4);++images;
    thingsos_rect(c,x,y,w,h,180);thingsos_rect(c,x,y,w/4,h,0);return true;
}
static void save(const char* directory,const char* name,const thingsos_canvas_t* c){
    char path[1024];snprintf(path,sizeof(path),"%s/%s.pgm",directory,name);
    FILE* f=fopen(path,"wb");CHECK(f);fprintf(f,"P5\n%d %d\n255\n",c->width,c->height);
    CHECK(fwrite(c->pixels,1,(size_t)c->width*c->height,f)==(size_t)c->width*c->height);CHECK(!fclose(f));
}
int main(int argc,char** argv){
    CHECK(argc==3);thingsos_font_t font={0};CHECK(thingsos_font_file(&font,argv[1]));
    CHECK(thingsos_font_size(&font,42));
    thingsos_chapter_t* ch=calloc(1,sizeof(*ch));CHECK(ch);
    char text[10000]={0};for(unsigned i=0;i<100;++i)strcat(text,"天地玄黄宇宙洪荒日月盈昃辰宿列张");
    ch->text=text;ch->bytes=strlen(text);
    size_t plane=684u*1216u;
    unsigned char* a=malloc(plane+32),*b=malloc(plane+32),*horizontal=malloc(plane);CHECK(a&&b&&horizontal);
    memset(a,0x6b,plane+32);memset(b,0x7c,plane+32);
    thingsos_canvas_t c={a+16,684,1216,false,false},d={b+16,684,1216,false,false};
    thingsos_text_hits_t *hits=calloc(1,sizeof(*hits)),*sliced=calloc(1,sizeof(*sliced)),*measured=calloc(1,sizeof(*measured));CHECK(hits&&sliced&&measured);
    full_width_columns(&font,ch,hits);CHECK(thingsos_font_size(&font,42));
    for(unsigned wide=0;wide<2;++wide)for(unsigned immersive=0;immersive<2;++immersive){
        c.width=d.width=wide?1216:684;c.height=d.height=wide?684:1216;
        int top=immersive?12:90,width=c.width-72,height=c.height-(immersive?24:176);
        memset(c.pixels,255,plane);memset(d.pixels,255,plane);memset(hits,0,sizeof(*hits));memset(sliced,0,sizeof(*sliced));memset(measured,0,sizeof(*measured));
        size_t end=thingsos_text_page_layout_flow(&font,&c,ch,0,36,top,width,height,145,0,true,NULL,NULL,hits,true);
        CHECK(end>0 && end<ch->bytes && !font.vertical_text && !c.vertical_text);
        CHECK(thingsos_text_page_layout_flow(&font,NULL,ch,0,36,top,width,height,145,0,true,NULL,NULL,measured,true)==end);
        CHECK(hits->glyph_count>30 && measured->glyph_count==hits->glyph_count);
        CHECK(!memcmp(hits->glyphs,measured->glyphs,hits->glyph_count*sizeof(hits->glyphs[0])));
        CHECK(hits->glyphs[0].x==36+width-42 && hits->glyphs[0].y==top);
        CHECK(hits->glyphs[1].x==hits->glyphs[0].x && hits->glyphs[1].y==top+42);
        bool new_column=false;
        for(size_t i=0;i<hits->glyph_count;++i){
            thingsos_glyph_hit_t* g=hits->glyphs+i;
            CHECK(g->x>=36 && g->x+g->w<=36+width && g->y>=top && g->y+g->h<=top+height);
            CHECK(g->bytes==3 && g->offset==i*3);
            if(i && g->x!=hits->glyphs[i-1].x){CHECK(g->x<hits->glyphs[i-1].x && g->y==top);new_column=true;}
        }
        CHECK(new_column);
        for(int y=0;y<c.height;++y)for(int x=0;x<c.width;++x)
            if(x<36 || x>=36+width || y<top || y>=top+height)CHECK(c.pixels[(size_t)y*c.width+x]==255);
        thingsos_text_cursor_t cursor={0};bool complete=false;size_t resumed=0;
        unsigned slices=0;
        while(!complete){stops=0;resumed=thingsos_text_page_resume_flow(&font,&d,ch,0,36,top,width,height,145,0,true,NULL,NULL,sliced,column_budget,NULL,&complete,&cursor,true);CHECK(++slices<100);}
        CHECK(resumed==end && slices>1 && !memcmp(c.pixels,d.pixels,plane));
        CHECK(hits->glyph_count==sliced->glyph_count && !memcmp(hits->glyphs,sliced->glyphs,hits->glyph_count*sizeof(hits->glyphs[0])));
        size_t at=0;unsigned pages=0;
        while(at<ch->bytes){size_t next=thingsos_text_page_layout_flow(&font,NULL,ch,at,36,top,width,height,145,0,true,NULL,NULL,NULL,true);CHECK(next>at && next<=ch->bytes);CHECK(next==ch->bytes || ((unsigned char)text[next]&0xc0)!=0x80);at=next;CHECK(++pages<100);}
        if(!wide && !immersive)save(argv[2],"vertical-columns",&c);
    }
    for(unsigned i=0;i<16;++i)CHECK(a[i]==0x6b && a[plane+16+i]==0x6b && b[i]==0x7c && b[plane+16+i]==0x7c);
    c.width=684;c.height=1216;memset(c.pixels,255,plane);
    ch->text="山川与日月，文字与时光。\n“春江花月夜”（一二三）……\nEnglish ABC 123\n上下左右，天地之间。";ch->bytes=strlen(ch->text);
    thingsos_text_page_layout(&font,&c,ch,0,36,90,612,1040,145,0,true,NULL,NULL,NULL);memcpy(horizontal,c.pixels,plane);
    memset(c.pixels,255,plane);memset(hits,0,sizeof(*hits));
    thingsos_text_underline_t underline={0,18,true};ch->underlines=&underline;ch->underline_count=1;
    thingsos_footnote_t footnote={0};ch->footnotes=&footnote;
    ch->footnotes[0].begin=18;ch->footnotes[0].end=21;ch->footnote_count=1;
    CHECK(thingsos_text_page_layout_flow(&font,&c,ch,0,36,90,612,1040,145,84,true,NULL,NULL,hits,true)==ch->bytes);
    CHECK(hits->count==1 && hits->line_count && hits->items[0].x>=36 && hits->items[0].x<648);
    CHECK(hits->lines[0].h>hits->lines[0].w);
    save(argv[2],"vertical-punctuation",&c);
    memset(d.pixels,255,plane);d.width=684;d.height=1216;memset(sliced,0,sizeof(*sliced));
    thingsos_text_cursor_t note_cursor={0};bool note_complete=false;unsigned note_slices=0;
    while(!note_complete){stops=0;CHECK(thingsos_text_page_resume_flow(&font,&d,ch,0,36,90,612,1040,145,84,true,NULL,NULL,sliced,column_budget,NULL,&note_complete,&note_cursor,true)<=ch->bytes);CHECK(++note_slices<20);}
    CHECK(!memcmp(c.pixels,d.pixels,plane)&&hits->count==sliced->count&&hits->line_count==sliced->line_count);
    CHECK(!memcmp(hits->items,sliced->items,hits->count*sizeof(hits->items[0])));
    CHECK(!memcmp(hits->lines,sliced->lines,hits->line_count*sizeof(hits->lines[0])));
    ch->underline_count=ch->footnote_count=0;
    memset(c.pixels,255,plane);thingsos_text_page_layout_flow(&font,&c,ch,0,36,90,612,1040,145,0,true,NULL,NULL,NULL,false);
    CHECK(!memcmp(horizontal,c.pixels,plane) && !font.vertical_text);
    ch->text="\n图片之后的正文";ch->bytes=strlen(ch->text);ch->image_count=1;ch->images[0].offset=0;ch->images[0].width=240;ch->images[0].height=360;
    memset(c.pixels,255,plane);CHECK(thingsos_text_page_layout_flow(&font,&c,ch,0,36,90,612,1040,145,0,true,image_draw,NULL,NULL,true)>0);
    CHECK(images==1);save(argv[2],"vertical-image",&c);
    ch->image_count=0;ch->text="-";ch->bytes=1;memset(c.pixels,255,plane);
    CHECK(thingsos_text_page_layout_flow(&font,&c,ch,0,36,90,612,1040,145,0,true,NULL,NULL,NULL,true)==1);
    int xmin=684,xmax=0,ymin=1216,ymax=0;
    for(int y=90;y<132;++y)for(int x=606;x<648;++x)if(c.pixels[y*684+x]<128){if(x<xmin)xmin=x;if(x>xmax)xmax=x;if(y<ymin)ymin=y;if(y>ymax)ymax=y;}
    CHECK(ymax-ymin>xmax-xmin && xmin<=xmax);
    thingsos_font_close(&font);free(ch);free(a);free(b);free(horizontal);free(hits);free(sliced);free(measured);
    puts("PASS: vertical columns, UTF-8 page continuity, horizontal equivalence, portrait/landscape/immersive bounds, resumable pixels/hits, punctuation and upright images");return 0;
}
