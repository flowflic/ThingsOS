// SPDX-License-Identifier: Apache-2.0
#include "thingsos/markdown.h"
#include "thingsos/graphics.h"
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"Markdown failed %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static unsigned style(thingsos_chapter_t* ch,size_t at) {
    thingsos_md_cursor_t* c=thingsos_markdown_cursor(ch,at);CHECK(c);
    thingsos_md_run_t r;CHECK(thingsos_markdown_style(c,at,&r));
    thingsos_markdown_cursor_close(c);return r.flags;
}
static void prepare(thingsos_chapter_t* ch,const char* path,const char* text) {
    memset(ch,0,sizeof(*ch));ch->bytes=strlen(text);ch->text=malloc(ch->bytes+1);CHECK(ch->text);
    memcpy(ch->text,text,ch->bytes+1);CHECK(thingsos_markdown_prepare(path,ch));
}
static bool two_rows(void* context) {
    unsigned* rows=context;if(!*rows)return false;--*rows;return true;
}
static void resume_equivalent(thingsos_font_t* font,thingsos_chapter_t* ch,thingsos_canvas_t* full) {
    unsigned char* pixels=malloc(684u*1216u);CHECK(pixels);memset(pixels,255,684u*1216u);
    thingsos_canvas_t slice={pixels,684,1216};thingsos_text_cursor_t cursor={0};
    thingsos_text_hits_t* a=calloc(1,sizeof(*a)),*b=calloc(1,sizeof(*b));CHECK(a && b);
    memset(full->pixels,255,684u*1216u);
    size_t expected=thingsos_text_page_layout(font,full,ch,0,36,20,612,1100,145,0,false,NULL,NULL,a);
    bool complete=false;size_t end=0;unsigned calls=0;
    while(!complete) {
        unsigned rows=2;
        end=thingsos_text_page_resume(font,&slice,ch,0,36,20,612,1100,145,0,false,NULL,NULL,b,two_rows,&rows,&complete,&cursor);
        CHECK(++calls<100 && font->size==32 && font->weight==1 && !font->italic);
    }
    CHECK(end==expected && !memcmp(pixels,full->pixels,684u*1216u) && !memcmp(a,b,sizeof(*a)));
    free(pixels);free(a);free(b);
}
int main(int argc,char** argv) {
    CHECK(argc==2);char path[700],image[256],fontpath[700];
    snprintf(path,sizeof(path),"%s/lightweight.md",argv[1]);
    CHECK(thingsos_markdown_path("A.MD") && thingsos_markdown_path("A.Markdown") && !thingsos_markdown_path("a.txt"));
    CHECK(thingsos_markdown_image_path(path,"picture.png",image));
    const char* bad[]={"../x","a/../x","/x","https://x/y","a\\x","a/%2e%2e/x","a//b","./x","a#b"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i)CHECK(!thingsos_markdown_image_path(path,bad[i],image));
    thingsos_chapter_t* ch=calloc(1,sizeof(*ch));CHECK(ch);
    const char* source="# Heading\n## Smaller\nPlain **bold *nested*** and *italic*, `**literal**`.\n> Quote\n- Item\n1. Number\n[link](https://example.com/a(b))\n![Caption](picture.png)\n![remote](https://example.com/p.png)\n\\*escaped* and **unclosed\n```c\n**code**\n```\nAfter\n";
    prepare(ch,path,source);CHECK(!strcmp(ch->text,source) && ch->image_count==1);
    CHECK(style(ch,0)&MD_HIDE);CHECK(style(ch,2)&MD_H1);
    CHECK(style(ch,(size_t)(strstr(source,"Smaller")-source))&MD_H2);
    CHECK(style(ch,(size_t)(strstr(source,"bold")-source))&MD_BOLD);
    CHECK(style(ch,(size_t)(strstr(source,"italic")-source))&MD_ITALIC);
    CHECK((style(ch,(size_t)(strstr(source,"nested")-source))&(MD_BOLD|MD_ITALIC))==(MD_BOLD|MD_ITALIC));
    CHECK(style(ch,(size_t)(strstr(source,"literal")-source))&MD_CODE);
    CHECK(style(ch,(size_t)(strstr(source,"Quote")-source))&MD_QUOTE);
    CHECK(style(ch,(size_t)(strstr(source,"Item")-source))&MD_LIST);
    CHECK(style(ch,(size_t)(strstr(source,"link")-source))&MD_LINK);
    CHECK(style(ch,(size_t)(strstr(source,"**code**")-source))==MD_CODE);
    CHECK(style(ch,(size_t)(strstr(source,"After")-source))==0);
    CHECK(style(ch,(size_t)(strstr(source,"unclosed")-source))==0);
    CHECK(ch->images[0].offset==(size_t)(strstr(source,"![Caption]")-source));
    CHECK(style(ch,ch->images[0].offset)&MD_HIDE);
    thingsos_chapter_close(ch);
    /* Far more than one RAM window; direct seek and sequential traversal must
       return identical source ranges without dropping later styles. */
    char* dense=malloc(100000);CHECK(dense);size_t n=0;
    for(unsigned i=0;i<2500;++i)n+=(size_t)sprintf(dense+n,"**bold** *italic* `code` %u\n",i);
    prepare(ch,path,dense);thingsos_md_cursor_t* c=thingsos_markdown_cursor(ch,0);CHECK(c);
    for(size_t at=0;at<ch->bytes;) {thingsos_md_run_t r;CHECK(thingsos_markdown_style(c,at,&r));CHECK(r.begin<=at && r.end>at);if(at%61==0)CHECK(style(ch,at)==r.flags);at=r.end;}
    CHECK(style(ch,n-10)&MD_CODE);thingsos_markdown_cursor_close(c);free(dense);
    /* Renderer and layout use the same style iterator; settings are restored. */
    snprintf(fontpath,sizeof(fontpath),"%s/default.ttf",argv[1]);thingsos_font_t font={0};CHECK(thingsos_font_file(&font,fontpath));
    thingsos_font_size(&font,32);thingsos_font_weight(&font,1);
    unsigned char* pixels=malloc(684*1216);CHECK(pixels);memset(pixels,255,684*1216);
    thingsos_canvas_t canvas={pixels,684,1216};
    size_t layout=thingsos_text_page_layout(&font,NULL,ch,0,36,20,612,1100,145,0,false,NULL,NULL,NULL);
    size_t drawn=thingsos_text_page_layout(&font,&canvas,ch,0,36,20,612,1100,145,0,false,NULL,NULL,NULL);
    CHECK(layout==drawn && drawn>0 && drawn<ch->bytes && font.size==32 && font.weight==1 && !font.italic);
    resume_equivalent(&font,ch,&canvas);
    c=thingsos_markdown_cursor(ch,drawn);CHECK(c);thingsos_md_run_t r;CHECK(thingsos_markdown_style(c,drawn,&r));thingsos_markdown_cursor_close(c);
    FILE* corrupt=fopen(ch->markdown_index,"r+b");CHECK(corrupt);uint32_t zero=0;CHECK(!fseek(corrupt,20,SEEK_SET) && fwrite(&zero,4,1,corrupt)==1 && !fclose(corrupt));
    c=thingsos_markdown_cursor(ch,0);if(c){CHECK(!thingsos_markdown_style(c,0,&r));thingsos_markdown_cursor_close(c);}
    thingsos_chapter_close(ch);
    prepare(ch,path,source);resume_equivalent(&font,ch,&canvas);thingsos_chapter_close(ch);
    char* longline=malloc(13000);CHECK(longline);memset(longline,'a',12998);longline[12998]='\n';longline[12999]=0;
    prepare(ch,path,longline);CHECK(ch->bytes==12999 && style(ch,12000)==0);thingsos_chapter_close(ch);free(longline);
    prepare(ch,path,"Before\n~~~\n*unclosed code\n");CHECK(style(ch,16)&MD_CODE);thingsos_chapter_close(ch);
    /* Prose follows closing-punctuation rules; literal code retains its columns. */
    thingsos_font_size(&font,32);
    int prose_width=4*thingsos_glyph(&font,NULL,0x4e2d,0,0);
    prepare(ch,path,"中中中中，文\n");
    CHECK(thingsos_text_page_layout(&font,NULL,ch,0,36,20,prose_width,32,145,0,false,NULL,NULL,NULL)==9);
    CHECK(thingsos_text_page_layout(&font,&canvas,ch,0,36,20,prose_width,32,145,0,false,NULL,NULL,NULL)==9);
    thingsos_chapter_close(ch);
    prepare(ch,path,"```\n中中中中，文\n```\n");
    CHECK(style(ch,4)&MD_CODE);
    CHECK(thingsos_text_page_layout(&font,NULL,ch,4,36,20,prose_width,32,145,0,false,NULL,NULL,NULL)==16);
    thingsos_chapter_close(ch);
    /* Blank lines (\n\n) between Markdown prose/list paragraphs collapse onto the
       same baseline grid as EPUB, while blank lines inside code blocks stay intact. */
    {
        unsigned char* p_single=malloc(684u*1216u);
        unsigned char* p_code=malloc(684u*1216u);
        CHECK(p_single && p_code);
        thingsos_canvas_t c_single={p_single,684,1216},c_code={p_code,684,1216};
        memset(p_single,255,684u*1216u);
        prepare(ch,path,"Alpha\n- Beta\n");
        thingsos_text_page_layout(&font,&c_single,ch,0,36,20,612,1100,145,0,false,NULL,NULL,NULL);
        thingsos_chapter_close(ch);
        prepare(ch,path,"Alpha\n\n- Beta\n\n\n");
        memset(pixels,255,684u*1216u);
        CHECK(thingsos_text_page_layout(&font,&canvas,ch,0,36,20,612,1100,145,0,false,NULL,NULL,NULL)==ch->bytes);
        CHECK(!memcmp(pixels,p_single,684u*1216u));
        resume_equivalent(&font,ch,&canvas);
        thingsos_chapter_close(ch);
        int line_h=(int)(font.size*145u/100u);
        prepare(ch,path,"```\nX\n\nY\n```\n");
        memset(p_code,255,684u*1216u);
        thingsos_text_page_layout(&font,&c_code,ch,0,36,20,612,1100,145,0,false,NULL,NULL,NULL);
        thingsos_chapter_close(ch);
        prepare(ch,path,"```\nX\nY\n```\n");
        memset(pixels,255,684u*1216u);
        thingsos_text_page_layout(&font,&canvas,ch,0,36,20,612,1100,145,0,false,NULL,NULL,NULL);
        CHECK(memcmp(p_code,pixels,684u*1216u)!=0);
        int code_top=20+line_h,code_double_top=20+line_h*2;
        bool y_in_row1=false,y_in_row2=false;
        for(int dy=code_top;dy<code_top+(int)font.size+10;++dy)
            for(int dx=36;dx<120;++dx)if(pixels[(size_t)dy*684u+dx]<200)y_in_row1=true;
        for(int dy=code_double_top;dy<code_double_top+(int)font.size+10;++dy)
            for(int dx=36;dx<120;++dx)if(p_code[(size_t)dy*684u+dx]<200)y_in_row2=true;
        CHECK(y_in_row1 && y_in_row2);
        thingsos_chapter_close(ch);
        free(p_single);free(p_code);
    }
    free(ch);free(pixels);thingsos_font_close(&font);
    printf("Markdown parser, SD windows, source positions, image guards, corruption and render/layout passed; cursor=%u bytes\n",(unsigned)sizeof(thingsos_md_cursor_t));return 0;
}
