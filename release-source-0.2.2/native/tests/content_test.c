// SPDX-License-Identifier: Apache-2.0
#include <stdio.h>
#include "thingsos/touch_queue.h"
#include <stdlib.h>
#include <string.h>
#include "thingsos/graphics.h"
#include "../src/jpeg_decoder.h"
/* Model an existing platform/ROM implementation with an incompatible ABI.
   Thumbnail decoding must use our separately named implementation instead. */
static unsigned platform_jpeg_calls;
JRESULT jd_prepare(JDEC* d,size_t (*input)(JDEC*,uint8_t*,size_t),void* pool,size_t bytes,void* device){
    (void)d;(void)input;(void)pool;(void)bytes;(void)device;++platform_jpeg_calls;return JDR_FMT3;
}
JRESULT jd_decomp(JDEC* d,int (*output)(JDEC*,void*,JRECT*),uint8_t scale){
    (void)d;(void)output;(void)scale;++platform_jpeg_calls;return JDR_FMT3;
}
#include "thingsos/reader_ui.h"
#include "thingsos/guard.h"
#include "thingsos/preview.h"
#include "font_fault.h"
#include FT_OUTLINE_H
#define CHECK(e)                                                   \
    do {                                                           \
        if (!(e)) {                                                \
            fprintf(stderr, "Failed line %d: %s\n", __LINE__, #e); \
            exit(1);                                               \
        }                                                          \
    } while (0)
static void path(char* out, const char* root, const char* name) {
    snprintf(out, 512, "%s/%s", root, name);
}
static unsigned char* read_file(const char* p, size_t* bytes) {
    FILE* f = fopen(p, "rb");
    CHECK(f);
    CHECK(!fseek(f, 0, SEEK_END));
    *bytes = (size_t)ftell(f);
    rewind(f);
    unsigned char* b = malloc(*bytes);
    CHECK(b && fread(b, 1, *bytes, f) == *bytes);
    fclose(f);
    return b;
}
extern void thingsos_font_host_uncached(bool);
static unsigned font_be16(const unsigned char* p){return (unsigned)p[0]*256+p[1];}
static unsigned font_be32(const unsigned char* p){return font_be16(p)*65536u+font_be16(p+2);}
static void font_optional_bold_test(const unsigned char* data,size_t bytes,FT_Face reference) {
    unsigned char* copy=malloc(bytes);CHECK(copy);memcpy(copy,data,bytes);
    unsigned head=0,loca=0,glyf=0;
    for(unsigned i=0;i<font_be16(copy+4);++i) {
        const unsigned char* entry=copy+12+i*16;unsigned offset=font_be32(entry+8);
        if(!memcmp(entry,"head",4))head=offset;
        if(!memcmp(entry,"loca",4))loca=offset;
        if(!memcmp(entry,"glyf",4))glyf=offset;
    }
    CHECK(head && loca && glyf);
    unsigned index=FT_Get_Char_Index(reference,'A'),begin,end;
    if(font_be16(copy+head+50)) {begin=font_be32(copy+loca+index*4);end=font_be32(copy+loca+(index+1)*4);}
    else {begin=font_be16(copy+loca+index*2)*2;end=font_be16(copy+loca+(index+1)*2)*2;}
    CHECK(end>begin+17 && glyf+end<=bytes);
    /* One collapsed contour: optional emboldening rejects its orientation,
       while FreeType can successfully load and rasterize the original. */
    unsigned char* glyph=copy+glyf+begin;memset(glyph,0,end-begin);
    glyph[1]=1;glyph[11]=2;glyph[14]=glyph[15]=glyph[16]=0x31;
    thingsos_font_t font={0};CHECK(thingsos_font_memory(&font,copy,bytes));
    CHECK(!FT_Load_Char((FT_Face)font.face,'A',FT_LOAD_NO_BITMAP|FT_LOAD_NO_SVG));
    CHECK(FT_Outline_Embolden(&((FT_Face)font.face)->glyph->outline,32)!=0);
    CHECK(thingsos_font_weight(&font,1));
    unsigned char pixels[96*96];memset(pixels,255,sizeof(pixels));
    thingsos_canvas_t canvas={pixels,96,96};
    CHECK(thingsos_glyph(&font,&canvas,'A',12,70)>0 && !font.errors);
    thingsos_font_close(&font);free(copy);
}
static void fast_glyph_edges_test(thingsos_font_t* font) {
    static const uint32_t letters[]={0x4eba,0x5165,0x516b,0x6c38,0x7136,0x7ec6,0xff0c,0x3002,'A','a','/','.'};
    unsigned char a[96*96],b[96*96],c[96*96];
    thingsos_canvas_t ca={a,96,96},cb={b,96,96},cc={c,96,96};
    for(unsigned size=0;size<3;++size)for(unsigned weight=0;weight<3;++weight)for(unsigned italic=0;italic<2;++italic) {
        CHECK(thingsos_font_size(font,24+size*12));CHECK(thingsos_font_weight(font,weight));font->italic=italic!=0;
        for(unsigned k=0;k<sizeof(letters)/sizeof(letters[0]);++k) {
            memset(a,255,sizeof(a));memset(b,255,sizeof(b));memset(c,255,sizeof(c));
            font->dither=true;
            int advance=thingsos_glyph(font,&ca,letters[k],12,70);
            CHECK(thingsos_glyph(font,&cb,letters[k],13,72)==advance);
            for(unsigned y=0;y<94;++y)for(unsigned x=0;x<95;++x)CHECK(a[y*96+x]==b[(y+2)*96+x+1]);
            void* bitmaps=font->bitmaps;font->bitmaps=NULL;
            CHECK(thingsos_glyph(font,&cc,letters[k],12,70)==advance);font->bitmaps=bitmaps;
            CHECK(!memcmp(a,c,sizeof(a)));
            memset(c,255,sizeof(c));thingsos_glyph(font,&cc,letters[k],-4,56);
            for(unsigned y=0;y<82;++y)for(unsigned x=0;x<80;++x)CHECK(c[y*96+x]==a[(y+14)*96+x+16]);
            font->dither=false;memset(b,255,sizeof(b));CHECK(thingsos_glyph(font,&cb,letters[k],12,70)==advance);
            for(unsigned i=0;i<sizeof(a);++i) {
                CHECK(a[i]==0 || a[i]==255);
                if(b[i]==255)CHECK(a[i]==255);
                if(b[i]==0)CHECK(a[i]==0);
            }
        }
    }
    font->dither=false;font->italic=false;CHECK(!font->errors);
    puts("FAST_FONT: 216 glyph/size/weight/italic cases: translation, clipping, warm/direct raster, binary output, solid/empty coverage and advances invariant");
}
static void font_stream_test(const char* path) {
    size_t bytes;unsigned char* data=read_file(path,&bytes);
    thingsos_font_t cached={0},direct={0},memory={0};
    CHECK(thingsos_font_memory(&memory,data,bytes));
    font_optional_bold_test(data,bytes,(FT_Face)memory.face);
    CHECK(thingsos_font_file(&cached,path));
    /* A transient allocator failure used to silently produce a blank glyph
       with a normal advance, indistinguishable from successful rendering. */
    unsigned char fault_pixels[96*96];
    thingsos_canvas_t fault_canvas={fault_pixels,96,96};
    memset(fault_pixels,255,sizeof(fault_pixels));
    font_fault_begin(&cached);
    thingsos_glyph(&cached,&fault_canvas,0x4e2d,12,70);
    font_fault_end(&cached);
    CHECK(cached.errors>0);
    bool marked=false;for(size_t i=0;i<sizeof(fault_pixels);++i)marked|=fault_pixels[i]!=255;
    CHECK(marked); /* A bounded retry may show an explicit box, never a blank. */
    unsigned failures=cached.errors;
    memset(fault_pixels,255,sizeof(fault_pixels));
    thingsos_glyph(&cached,&fault_canvas,0x4e2d,12,70);
    CHECK(cached.errors==failures);
    bool ink=false;for(size_t i=0;i<sizeof(fault_pixels);++i)ink|=fault_pixels[i]!=255;
    CHECK(ink);
    thingsos_font_close(&cached);CHECK(thingsos_font_file(&cached,path));
    /* Warm the outline only: now fail allocation of the raster bitmap. */
    CHECK(!FT_Load_Char((FT_Face)cached.face,0x4e2d,FT_LOAD_NO_SVG|FT_LOAD_NO_BITMAP));
    font_fault_begin(&cached);
    thingsos_glyph(&cached,&fault_canvas,0x4e2d,12,70);
    font_fault_end(&cached);
    CHECK(cached.errors>0);
    thingsos_font_close(&cached);CHECK(thingsos_font_file(&cached,path));
    thingsos_font_host_uncached(true);CHECK(thingsos_font_file(&direct,path));
    thingsos_font_host_uncached(false);
    CHECK(cached.signature==direct.signature && cached.signature==memory.signature);
    unsigned char a[96*96],b[96*96],c[96*96];
    thingsos_canvas_t ca={a,96,96},cb={b,96,96},cc={c,96,96};
    for(unsigned pass=0;pass<3;++pass) {
        unsigned px=30+pass*4;
        CHECK(thingsos_font_size(&cached,px)&&thingsos_font_size(&direct,px)&&thingsos_font_size(&memory,px));
        for(uint32_t cp=0x4e00;cp<0x4e80;++cp) {
            memset(a,255,sizeof(a));memset(b,255,sizeof(b));memset(c,255,sizeof(c));
            int advance=thingsos_glyph(&memory,&cc,cp,12,70);
            CHECK(thingsos_glyph(&cached,&ca,cp,12,70)==advance&&thingsos_glyph(&direct,&cb,cp,12,70)==advance);
            CHECK(!memcmp(a,b,sizeof(a))&&!memcmp(a,c,sizeof(a)));
        }
    }
    thingsos_font_io_t hit,raw;
    CHECK(thingsos_font_io(&cached,&hit)&&thingsos_font_io(&direct,&raw));
    CHECK(hit.cache_bytes==32768 && raw.cache_bytes==0 && hit.cache_hits && hit.disk_reads<raw.disk_reads);
    printf("FONT_IO identical glyphs=384 disk_reads cached=%llu direct=%llu cache_bytes=%u\n",
        (unsigned long long)hit.disk_reads,(unsigned long long)raw.disk_reads,hit.cache_bytes);
    thingsos_font_close(&cached);CHECK(thingsos_font_file(&cached,path));
    CHECK(thingsos_font_size(&cached,40)&&thingsos_font_size(&memory,40));
    /* These four characters collided in the old single-slot hash. Retain
       all four while preserving their exact rendered pixels. */
    static const uint32_t colliding[]={0x4e00,0x8a00,0x9000,0x6000};
    for(unsigned pass=0;pass<20;++pass)for(unsigned i=0;i<4;++i) {
        uint32_t cp=colliding[i];
        memset(a,255,sizeof(a));memset(c,255,sizeof(c));
        CHECK(thingsos_glyph(&cached,&ca,cp,12,70)==thingsos_glyph(&memory,&cc,cp,12,70));
        CHECK(!memcmp(a,c,sizeof(a)));
    }
    printf("FONT_BITMAP collision sequence=80 loads=%u hits=%u slots=512\n",cached.glyph_loads,cached.bitmap_hits);
    CHECK(cached.glyph_loads==4 && cached.bitmap_hits==76);
    fast_glyph_edges_test(&memory);
    thingsos_font_close(&cached);thingsos_font_close(&direct);thingsos_font_close(&memory);free(data);
}
static void punctuation_wrap_test(thingsos_font_t* font,thingsos_canvas_t* canvas,const char* root) {
    static const char* endings[]={"，","。","、","；","：","！","？","？！","。”","……","——","）》","」』","〕】","％",",",".",";",":","!","?", ")", "]", "}"};
    static const unsigned sizes[]={30,46,70};
    for(unsigned size=0;size<3;++size) {
        CHECK(thingsos_font_size(font,sizes[size]));CHECK(thingsos_font_weight(font,0));
        int width=4*thingsos_glyph(font,NULL,0x4e2d,0,0);
        for(unsigned i=0;i<sizeof(endings)/sizeof(endings[0]);++i) {
            char text[128],original[128];snprintf(text,sizeof(text),"中中中中%s文",endings[i]);strcpy(original,text);
            thingsos_chapter_t ch={0};ch.text=text;ch.bytes=strlen(text);
            for(unsigned epub=0;epub<2;++epub) {
                size_t end=thingsos_text_page_layout(font,NULL,&ch,0,36,90,width,(int)font->size,145,0,epub!=0,NULL,NULL,NULL);
                /* clreq: a single point mark (，。、) drops the empty half of its box and
                   may hang up to min(margin-8, half its advance) into the 36 px margin, so
                   it stays with the fourth character when that covers its advance. Every
                   other ending moves down together with the last character. */
                bool point=!strcmp(endings[i],"，")||!strcmp(endings[i],"。")||!strcmp(endings[i],"、");
                int mark=point?thingsos_glyph(font,NULL,endings[i][2]==(char)0x8c?0xff0c:endings[i][2]==(char)0x82?0x3002:0x3001,0,0):0;
                int hang=36-8<mark/2?36-8:mark/2;
                bool stays=point && mark*4>=(int)font->size*3 && mark/2+hang>=mark;
                CHECK(end==(stays?15u:9u));
                memset(canvas->pixels,255,684u*1216u);
                CHECK(thingsos_text_page_layout(font,canvas,&ch,0,36,90,width,(int)font->size,145,0,epub!=0,NULL,NULL,NULL)==end);
                CHECK(thingsos_text_page_layout(font,NULL,&ch,end,36,90,width,(int)font->size,145,0,epub!=0,NULL,NULL,NULL)>12);
                thingsos_text_underline_t underlines[2]={{0}};
                underlines[0].begin=9;underlines[0].end=12;
                underlines[1].begin=12;underlines[1].end=ch.bytes-3;
                ch.underlines=underlines;ch.underline_count=2;
                thingsos_text_hits_t hits={0};
                CHECK(thingsos_text_page_layout(font,NULL,&ch,0,36,90,width,600,145,0,epub!=0,NULL,NULL,&hits)==ch.bytes);
                CHECK(hits.line_count==2 && hits.lines[0].y==hits.lines[1].y && hits.lines[1].x>hits.lines[0].x);
                ch.underlines=NULL;ch.underline_count=0;
                CHECK(!strcmp(text,original)); /* Bookmarks and source mappings keep byte offsets. */
            }
        }
    }
    CHECK(thingsos_font_size(font,46));
    char demo[]="阅读时，句末标点应该跟随前面的文字。\n换行测试：中文逗号、句号、问号？！还有右引号。”\n标点组合也要保持完整……继续阅读。";
    thingsos_chapter_t ch={0};ch.text=demo;ch.bytes=strlen(demo);
    memset(canvas->pixels,255,684u*1216u);
    CHECK(thingsos_text_page_layout(font,canvas,&ch,0,48,90,552,1040,145,0,true,NULL,NULL,NULL)==ch.bytes);
    char output[512];path(output,root,"punctuation-wrap.pgm");FILE* f=fopen(output,"wb");CHECK(f);
    fprintf(f,"P5\n684 1216\n255\n");CHECK(fwrite(canvas->pixels,1,684u*1216u,f)==684u*1216u);fclose(f);
    /* Source newlines, headings, oversize glyphs and narrow columns still progress. */
    ch.text="中中中中\n，文";ch.bytes=strlen(ch.text);
    CHECK(thingsos_text_page_layout(font,NULL,&ch,0,36,90,600,(int)font->size,145,0,false,NULL,NULL,NULL)==13);
    ch.text="中，。文";ch.bytes=strlen(ch.text);size_t start=0;
    while(start<ch.bytes){size_t end=thingsos_text_page_layout(font,NULL,&ch,start,36,90,48,46,145,0,false,NULL,NULL,NULL);CHECK(end>start);start=end;}
}
static unsigned drawn_images;
/* Compare actual glyph row positions across full pages with different paragraph
   counts. Identical glyphs isolate layout drift from ascender/descender shapes. */
static void paragraph_grid_test(thingsos_font_t* font, thingsos_canvas_t* canvas,
                                const char* root) {
    char continuous[8192], paragraphs[8192];
    memset(continuous, 'W', sizeof(continuous));
    thingsos_chapter_t chapter = {0};
    static const unsigned sizes[] = {24, 30, 40};
    static const unsigned spacing[] = {125, 145, 175};
    for (unsigned size = 0; size < 3; ++size) {
        for (unsigned gap = 0; gap < 3; ++gap) {
            CHECK(thingsos_font_size(font, sizes[size]));
            CHECK(thingsos_font_weight(font, (size + gap) % 3));
            chapter.text = continuous; chapter.bytes = sizeof(continuous);
            memset(canvas->pixels, 255, 684u * 1216u);
            size_t end = thingsos_text_page_layout(font, canvas, &chapter, 0,
                48, 90, 612, 1040, spacing[gap], 0, true, NULL, NULL, NULL);
            CHECK(end > 0 && end < chapter.bytes);
            unsigned char rows[1216] = {0};
            for (int y = 0; y < 1216; ++y) for (int x = 0; x < 684; ++x)
                if (canvas->pixels[(size_t)y * 684 + x] < 240) rows[y] = 1;
            for (unsigned variant = 0; variant < 3; ++variant) {
                size_t length = 0;
                for (unsigned para = 0; length < sizeof(paragraphs); ++para) {
                    unsigned letters = 13 + variant * 63 + (para % 5) * 7;
                    for (unsigned j = 0; j < letters && length < sizeof(paragraphs); ++j)
                        paragraphs[length++] = 'W';
                    if (length < sizeof(paragraphs)) paragraphs[length++] = '\n';
                }
                chapter.text = paragraphs; chapter.bytes = length;
                size_t start = 0;
                for (unsigned page = 0; page < 3; ++page) {
                    memset(canvas->pixels, 255, 684u * 1216u);
                    end = thingsos_text_page_layout(font, canvas, &chapter, start,
                        48, 90, 612, 1040, spacing[gap], 0, true, NULL, NULL, NULL);
                    CHECK(end > start && end < chapter.bytes);
                    CHECK(end == thingsos_text_page_layout(font, NULL, &chapter, start,
                        48, 90, 612, 1040, spacing[gap], 0, true, NULL, NULL, NULL));
                    for (int y = 0; y < 1216; ++y) {
                        unsigned char ink = 0;
                        for (int x = 0; x < 684; ++x)
                            if (canvas->pixels[(size_t)y * 684 + x] < 240) ink = 1;
                        CHECK(ink == rows[y]);
                    }
                    if (size == 1 && gap == 1 && page == 1) {
                        char filename[64], output[512];
                        snprintf(filename, sizeof(filename), "paragraph-grid-%u.pgm", variant);
                        path(output, root, filename);
                        FILE* file = fopen(output, "wb"); CHECK(file);
                        fprintf(file, "P5\n684 1216\n255\n");
                        CHECK(fwrite(canvas->pixels, 1, 684u * 1216u, file) == 684u * 1216u);
                        fclose(file);
                    }
                    start = end;
                }
            }
        }
    }
    CHECK(thingsos_font_weight(font, 0));
    CHECK(thingsos_font_size(font, 32));
}
/* Leftmost inked column of every text band, top to bottom. */
static unsigned band_starts(const thingsos_canvas_t* canvas, int* starts, unsigned capacity) {
    unsigned count = 0;
    int top = -1;
    for (int y = 0; y <= canvas->height; ++y) {
        int ink = -1;
        if (y < canvas->height)
            for (int x = 0; x < canvas->width; ++x)
                if (canvas->pixels[(size_t)y * canvas->width + x] < 240) { ink = x; break; }
        if (ink >= 0) { if (top < 0) top = y; continue; }
        if (top < 0) continue;
        int best = canvas->width;
        for (int row = top; row < y; ++row)
            for (int x = 0; x < canvas->width; ++x)
                if (canvas->pixels[(size_t)row * canvas->width + x] < 240 && x < best) best = x;
        if (count < capacity) starts[count++] = best;
        top = -1;
    }
    return count;
}
/* The first-line indent moves paragraph heads only: wrapped lines and pages
   that resume inside a paragraph keep the shared gutter, and pagination still
   consumes the same source bytes. */
static void paragraph_indent_test(thingsos_font_t* font, thingsos_canvas_t* canvas,
                                  const char* root) {
    CHECK(thingsos_font_size(font, 32));
    CHECK(thingsos_font_weight(font, 0));
    const unsigned indent = thingsos_reader_indent_px(32, THINGSOS_READER_INDENT_CHARS);
    CHECK(indent == 64); /* two character widths at the current body size */
    const int gutter = 36;
    char text[256];
    size_t length = 0;
    for (unsigned paragraph = 0; paragraph < 2; ++paragraph) {
        for (unsigned i = 0; i < 80; ++i) text[length++] = 'W';
        if (!paragraph) text[length++] = '\n';
    }
    thingsos_chapter_t chapter = {0};
    chapter.text = text; chapter.bytes = length;
    int starts[64];
    memset(canvas->pixels, 255, 684u * 1216u);
    CHECK(thingsos_text_page_layout(font, canvas, &chapter, 0, gutter, 90, 612, 1040, 175, 0,
                                    false, NULL, NULL, NULL) == chapter.bytes);
    unsigned plain_lines = band_starts(canvas, starts, 64);
    CHECK(plain_lines >= 4);
    for (unsigned i = 0; i < plain_lines; ++i) CHECK(starts[i] == gutter);
    memset(canvas->pixels, 255, 684u * 1216u);
    CHECK(thingsos_text_page_layout(font, canvas, &chapter, 0, gutter, 90, 612, 1040, 175,
                                    indent, false, NULL, NULL, NULL) == chapter.bytes);
    unsigned indented_lines = band_starts(canvas, starts, 64);
    unsigned heads = 0;
    for (unsigned i = 0; i < indented_lines; ++i) {
        CHECK(starts[i] == gutter || starts[i] == gutter + (int)indent);
        if (starts[i] == gutter + (int)indent) ++heads;
    }
    CHECK(heads == 2); /* one per paragraph, never a wrapped line */
    char filename[64], output[512];
    snprintf(filename, sizeof(filename), "paragraph-indent.pgm");
    path(output, root, filename);
    FILE* file = fopen(output, "wb"); CHECK(file);
    fprintf(file, "P5\n684 1216\n255\n");
    CHECK(fwrite(canvas->pixels, 1, 684u * 1216u, file) == 684u * 1216u);
    fclose(file);
    /* A page resumed mid-paragraph is a continuation, not a paragraph head. */
    char long_text[4096];
    memset(long_text, 'W', sizeof(long_text));
    chapter.text = long_text; chapter.bytes = sizeof(long_text);
    memset(canvas->pixels, 255, 684u * 1216u);
    size_t first = thingsos_text_page_layout(font, canvas, &chapter, 0, gutter, 90, 612, 1040,
                                             175, indent, false, NULL, NULL, NULL);
    CHECK(first > 0 && first < chapter.bytes);
    CHECK(band_starts(canvas, starts, 64) > 1);
    CHECK(starts[0] == gutter + (int)indent); /* the chapter opens a paragraph */
    memset(canvas->pixels, 255, 684u * 1216u);
    CHECK(thingsos_text_page_layout(font, canvas, &chapter, first, gutter, 90, 612, 1040, 175,
                                    indent, false, NULL, NULL, NULL) > first);
    CHECK(band_starts(canvas, starts, 64) > 1);
    CHECK(starts[0] == gutter);
    /* The setting changes the layout, so the pagination key must see it too. */
    CHECK(thingsos_text_page_layout(font, NULL, &chapter, 0, gutter, 90, 612, 1040, 175, 0,
                                    false, NULL, NULL, NULL) !=
          thingsos_text_page_layout(font, NULL, &chapter, 0, gutter, 90, 612, 1040, 175, indent,
                                    false, NULL, NULL, NULL));
}
/* A footnote marker must publish one comfortable tap box on the page that draws
   it, and a plain chapter must publish none. */
static void footnote_layout_test(thingsos_font_t* font, thingsos_canvas_t* canvas,
                                 const char* root) {
    CHECK(thingsos_font_size(font, 32));
    CHECK(thingsos_font_weight(font, 0));
    thingsos_epub_t* e = calloc(1, sizeof(*e));
    CHECK(e);
    char p[512];
    path(p, root, "footnote.epub");
    CHECK(thingsos_epub_open(e, p));
    thingsos_epub_navigation(e);
    thingsos_chapter_t chapter = { 0 };
    CHECK(thingsos_epub_chapter(e, 0, &chapter));
    CHECK(chapter.footnote_count == 2);
    thingsos_text_hits_t hits = { 0 };
    memset(canvas->pixels, 255, 684u * 1216u);
    CHECK(thingsos_text_page_layout(font, canvas, &chapter, 0, 36, 90, 612, 1040, 175, 0, true,
                                    NULL, NULL, &hits) > 0);
    CHECK(hits.count == 2);
    for (size_t i = 0; i < hits.count; ++i) {
        const thingsos_text_hit_t* box = hits.items + i;
        CHECK(box->begin == chapter.footnotes[i].begin && box->end == chapter.footnotes[i].end);
        CHECK(box->w >= 12 && box->h >= font->size && box->x >= 36 && box->y >= 90);
        CHECK(box->x + box->w <= 36 + 612 && box->y + box->h <= 90 + 1040);
        if (i) CHECK(box->x > hits.items[i - 1].x || box->y > hits.items[i - 1].y);
    }
    char filename[64], output[512];
    snprintf(filename, sizeof(filename), "footnote-marker.pgm");
    path(output, root, filename);
    FILE* file = fopen(output, "wb");
    CHECK(file);
    fprintf(file, "P5\n684 1216\n255\n");
    CHECK(fwrite(canvas->pixels, 1, 684u * 1216u, file) == 684u * 1216u);
    fclose(file);
    /* Measuring without a sink draws nothing and publishes nothing. */
    hits.count = 0;
    (void)thingsos_text_page_layout(font, NULL, &chapter, 0, 36, 90, 612, 1040, 175, 0, true, NULL,
                                    NULL, &hits);
    CHECK(hits.count == 2);
    memset(&hits, 0, sizeof(hits));
    (void)thingsos_text_page_layout(font, NULL, &chapter, 0, 36, 90, 612, 1040, 175, 0, true, NULL,
                                    NULL, NULL);
    CHECK(hits.count == 0);
    thingsos_chapter_close(&chapter);
    CHECK(thingsos_epub_chapter(e, 1, &chapter));
    memset(&hits, 0, sizeof(hits));
    (void)thingsos_text_page_layout(font, NULL, &chapter, 0, 36, 90, 612, 1040, 175, 0, true, NULL,
                                    NULL, &hits);
    CHECK(hits.count == 0);
    thingsos_chapter_close(&chapter);
    thingsos_epub_close(e);
    free(e);
}
typedef struct {unsigned count;int x,y,w,h;} inline_geometry_t;
static bool capture_inline_geometry(void* context,const thingsos_inline_t* image,thingsos_canvas_t* canvas,int x,int y,int w,int h) {
    (void)image;inline_geometry_t* g=context;++g->count;g->x=x;g->y=y;g->w=w;g->h=h;
    CHECK(x>=0 && y>=0 && x+w<=canvas->width && y+h<=canvas->height);
    return true;
}
static void inline_fit_layout_test(thingsos_font_t* font) {
    unsigned saved=font->size;CHECK(thingsos_font_size(font,32));
    thingsos_canvas_t canvas={malloc(1216u*1216u),684,1216};CHECK(canvas.pixels);
    thingsos_chapter_t chapter={0};chapter.text="\ncaption\n";chapter.bytes=strlen(chapter.text);chapter.image_count=1;
    for(unsigned mode=0;mode<2;++mode)for(unsigned wide=0;wide<2;++wide) {
        canvas.width=mode?1216:684;canvas.height=mode?684:1216;
        chapter.images[0].width=wide?1937:540;chapter.images[0].height=wide?820:1216;
        int width=canvas.width-72,height=mode?500:1040;
        inline_geometry_t g={0};
        size_t drawn=thingsos_text_page_layout(font,&canvas,&chapter,0,36,90,width,height,175,0,true,capture_inline_geometry,&g,NULL);
        size_t measured=thingsos_text_page_layout(font,NULL,&chapter,0,36,90,width,height,175,0,true,NULL,NULL,NULL);
        CHECK(drawn>0 && drawn==measured && g.count==1 && g.y==90 && g.y+g.h<=90+height);
        if(wide)CHECK(g.w==width && g.x==36 && g.h==width*820/1937);
        else CHECK(drawn==1 && g.h==height && g.w==height*540/1216 && g.x==36+(width-g.w)/2);
    }
    chapter.text="Text\n\nTail";chapter.bytes=strlen(chapter.text);
    chapter.images[0].offset=5;chapter.images[0].width=540;chapter.images[0].height=1216;
    strcpy(chapter.images[0].alt,"Map");
    inline_geometry_t g={0};
    CHECK(thingsos_text_page_layout(font,&canvas,&chapter,0,36,90,1144,500,175,0,true,capture_inline_geometry,&g,NULL)==5);
    CHECK(g.count==0);
    CHECK(thingsos_text_page_layout(font,&canvas,&chapter,5,36,90,1144,500,175,0,true,capture_inline_geometry,&g,NULL)==6);
    CHECK(g.count==1 && g.y==90 && g.h==500-56-16 && g.w==g.h*540/1216);
    free(canvas.pixels);CHECK(thingsos_font_size(font,saved));
}
static bool draw_image(
    void* context, const thingsos_inline_t* image, thingsos_canvas_t* canvas, int x, int y, int w,
    int h) {
    thingsos_epub_t* epub = context;
    unsigned char *encoded, *gray;
    size_t bytes;
    unsigned iw, ih;
    CHECK(thingsos_zip_read(&epub->zip, image->path, THINGSOS_MEMBER_BYTES, &encoded, &bytes));
    CHECK(thingsos_decode_image(encoded, bytes, &gray, &iw, &ih));
    free(encoded);
    CHECK(x >= 0 && y >= 0 && x + w <= canvas->width && y + h <= canvas->height);
    for (int row = 0; row < h; row++)
        for (int col = 0; col < w; col++)
            canvas->pixels[(size_t)(y + row) * canvas->width + x + col] =
                gray[(size_t)((uint64_t)row * ih / h) * iw + (size_t)((uint64_t)col * iw / w)];
    free(gray);
    drawn_images++;
    return true;
}
int main(int argc, char** argv) {
    CHECK(argc == 2);
    {thingsos_touch_queue_t input={0};thingsos_touch_sample_t sample;
    /* A complete short tap while the UI is blocked must replay as DOWN/UP,
       with physical times preserved (no phantom long press after a scan). */
    thingsos_touch_queue_push(&input,(thingsos_touch_sample_t){100,560,990,1,true});
    thingsos_touch_queue_push(&input,(thingsos_touch_sample_t){120,560,990,1,true});
    thingsos_touch_queue_push(&input,(thingsos_touch_sample_t){150,560,990,0,false});
    CHECK(input.count==2);
    CHECK(thingsos_touch_queue_pop(&input,&sample) && sample.down && sample.at_ms==100);
    CHECK(thingsos_touch_queue_pop(&input,&sample) && !sample.down && sample.at_ms==150);
    /* Keep the initial position, coalesce motion, and always retain release. */
    thingsos_touch_queue_push(&input,(thingsos_touch_sample_t){200,100,200,1,true});
    for(unsigned i=1;i<=80;++i)thingsos_touch_queue_push(&input,(thingsos_touch_sample_t){200+i,(uint16_t)(100+i),200,1,true});
    thingsos_touch_queue_push(&input,(thingsos_touch_sample_t){300,180,200,0,false});
    CHECK(input.count==3 && !input.overflows);
    CHECK(thingsos_touch_queue_pop(&input,&sample) && sample.x==100 && sample.at_ms==200);
    CHECK(thingsos_touch_queue_pop(&input,&sample) && sample.x==180);
    CHECK(thingsos_touch_queue_pop(&input,&sample) && !sample.down);
    thingsos_touch_queue_push(&input,(thingsos_touch_sample_t){310,100,200,1,true});
    thingsos_touch_queue_push(&input,(thingsos_touch_sample_t){320,100,250,1,true});
    thingsos_touch_queue_push(&input,(thingsos_touch_sample_t){330,100,200,1,true});
    thingsos_touch_queue_push(&input,(thingsos_touch_sample_t){340,100,200,0,false});
    CHECK(input.count==4);
    CHECK(thingsos_touch_queue_pop(&input,&sample) && sample.y==200);
    CHECK(thingsos_touch_queue_pop(&input,&sample) && sample.y==250);
    CHECK(thingsos_touch_queue_pop(&input,&sample) && sample.y==200);
    CHECK(thingsos_touch_queue_pop(&input,&sample) && !sample.down);
    for(unsigned i=0;i<THINGSOS_TOUCH_QUEUE_SIZE+2;++i)
        thingsos_touch_queue_push(&input,(thingsos_touch_sample_t){400+i,500,900,(uint8_t)(i%2?0:1),i%2==0});
    CHECK(input.overflows==1);
    CHECK(thingsos_touch_queue_pop(&input,&sample) && sample.count==2 && sample.down);
    CHECK(thingsos_touch_queue_pop(&input,&sample) && !sample.down);
    }
    const char* root = argv[1];
    char p[512], joined[256];
    CHECK(
        thingsos_archive_path("OPS/text/ch1.xhtml", "../images/cover.png", joined) &&
        !strcmp(joined, "OPS/images/cover.png"));
    CHECK(!thingsos_archive_path("OPS/a.xhtml", "../../outside", joined));
    CHECK(!thingsos_archive_path("OPS/a.xhtml", "https://example.com/a", joined));
    CHECK(!thingsos_archive_path("OPS/a.xhtml", "file:/outside", joined));
    CHECK(!thingsos_archive_path("OPS/a.xhtml", "C:outside", joined));
    CHECK(thingsos_archive_path("OPS/a.xhtml", "Text/_*::::.xhtml", joined) &&
          !strcmp(joined,"OPS/Text/_*::::.xhtml"));
    CHECK(thingsos_archive_path("OPS/Text/a.xhtml", "_*::::.xhtml", joined) &&
          !strcmp(joined,"OPS/Text/_*::::.xhtml"));
    CHECK(!thingsos_archive_path("OPS/a.xhtml", "%2e%2e/a", joined));
    thingsos_epub_t* e = calloc(1, sizeof(*e));
    CHECK(e);
    CHECK(thingsos_toc_number_level("3.1.1 Detail")==2);
    CHECK(thingsos_toc_number_level("3.1.1公共电网")==2);
    CHECK(thingsos_toc_number_level("2026.10.20 Date")==0);
    CHECK(thingsos_toc_number_level("3.14version")==0);
    const char* hierarchies[]={"hierarchy-nav.epub","hierarchy-ncx.epub","hierarchy-legacy.epub"};
    for(unsigned type=0;type<3;++type) {
        path(p,root,hierarchies[type]);CHECK(thingsos_epub_open(e,p));thingsos_epub_navigation(e);
        CHECK(e->spine_count==4 && e->chapter_levels[0]==0 && e->chapter_levels[1]==1 && e->chapter_levels[2]==2 && e->chapter_levels[3]==1);
        for(unsigned i=0;i<4;++i) {
            thingsos_chapter_t* sub=calloc(1,sizeof(*sub));CHECK(sub && thingsos_epub_chapter(e,i,sub));
            for(unsigned j=0;j<4;++j){char marker[24];snprintf(marker,sizeof(marker),"ONLY_%u",j);CHECK((strstr(sub->text,marker)!=NULL)==(i==j));}
            thingsos_chapter_close(sub);free(sub);
        }
        thingsos_epub_close(e);
    }
    path(p,root,"footnote-compat.epub");CHECK(thingsos_epub_open(e,p));
    thingsos_chapter_t* compatible=calloc(1,sizeof(*compatible));CHECK(compatible && thingsos_epub_chapter(e,0,compatible));
    CHECK(compatible->footnote_count==3);
    const char* expected_notes[]={"CROSS_FILE_NOTE_BODY","LOCAL_NOTE_BODY","EMPTY_NOTE_BODY"};
    for(unsigned i=0;i<3;++i)CHECK(compatible->footnotes[i].note_bytes && strstr(compatible->notes+compatible->footnotes[i].note_offset,expected_notes[i]));
    CHECK(compatible->image_count==0);thingsos_chapter_close(compatible);free(compatible);thingsos_epub_close(e);
    path(p,root,"navigation.epub");CHECK(thingsos_epub_open(e,p));
    thingsos_epub_navigation_fast(e);CHECK(e->spine_count==2);
    thingsos_epub_navigation_fast(e);CHECK(e->spine_count==2); /* No repeated splitting. */
    CHECK(!strcmp(e->chapter_names[0],"第一章 & 起点"));
    CHECK(!strcmp(e->chapter_names[1],"第二章 终点"));
    thingsos_chapter_t split={0};CHECK(thingsos_epub_chapter(e,0,&split));
    CHECK(strstr(split.text,"ONLY_FIRST") && !strstr(split.text,"ONLY_SECOND"));thingsos_chapter_close(&split);
    CHECK(thingsos_epub_chapter(e,1,&split));
    CHECK(!strstr(split.text,"ONLY_FIRST") && strstr(split.text,"ONLY_SECOND"));thingsos_chapter_close(&split);
    thingsos_epub_close(e);
    /* Footnotes: the marker links to a note body in the same member; a chapter
       without a reference must stay free of footnote state. */
    path(p, root, "footnote.epub");
    CHECK(thingsos_epub_open(e, p));
    thingsos_epub_navigation(e);
    CHECK(e->spine_count == 2);
    thingsos_chapter_t noted = { 0 };
    CHECK(thingsos_epub_chapter(e, 0, &noted));
    CHECK(noted.footnote_count == 2 && noted.footnotes && noted.notes);
    {
        const thingsos_footnote_t* first = noted.footnotes;
        const thingsos_footnote_t* second = noted.footnotes + 1;
        CHECK(first->end > first->begin && first->end <= noted.bytes);
        CHECK(first->end - first->begin == 1 && noted.text[first->begin] == '1');
        CHECK(second->end - second->begin == 1 && noted.text[second->begin] == '2');
        CHECK(second->begin > first->end);
        CHECK(first->note_bytes > 0 && second->note_bytes > first->note_bytes);
        char text[512];
        size_t n = first->note_bytes < sizeof(text) - 1 ? first->note_bytes : sizeof(text) - 1;
        memcpy(text, noted.notes + first->note_offset, n);
        text[n] = 0;
        CHECK(strstr(text, "注解") && strstr(text, "强调") && strstr(text, "结束"));
        CHECK(!strstr(text, "  ") && text[0] != ' ' && text[n - 1] != ' ');
        n = second->note_bytes < sizeof(text) - 1 ? second->note_bytes : sizeof(text) - 1;
        memcpy(text, noted.notes + second->note_offset, n);
        text[n] = 0;
        CHECK(strstr(text, "翻页行为") && !strstr(text, "<"));
    }
    thingsos_chapter_close(&noted);
    CHECK(thingsos_epub_chapter(e, 1, &noted));
    CHECK(noted.footnote_count == 0 && !noted.footnotes);
    thingsos_chapter_close(&noted);
    thingsos_epub_close(e);
    path(p, root, "valid.epub");
    CHECK(thingsos_epub_open(e, p));
    thingsos_epub_navigation_fast(e);
    CHECK(!strcmp(e->chapter_names[0],"第 1 章")); /* Fast open does not scan chapter bodies. */
    thingsos_epub_titles(e);
    CHECK(!strcmp(e->chapter_names[0],"第二章")); /* Heading fallback is available before opening the TOC. */
    CHECK(
        e->spine_count == 2 && !strcmp(e->title, "山野来信 & 测试") &&
        !strcmp(e->cover, "OPS/images/cover.png"));
    thingsos_chapter_t chapter;
    CHECK(thingsos_epub_chapter(e, 0, &chapter));
    CHECK(!strcmp(e->chapter_names[0],"第二章"));
    thingsos_chapter_t* second=calloc(1,sizeof(*second));CHECK(second);
    CHECK(thingsos_epub_chapter(e,1,second));
    CHECK(!strcmp(e->chapter_names[1],"第一章"));
    thingsos_chapter_close(second);free(second);
    CHECK(strstr(chapter.text, "第二章") && strstr(chapter.text, "中"));
    CHECK(!strstr(chapter.text, "secret_script"));
    CHECK(chapter.image_count == 1 && !strcmp(chapter.images[0].path, e->cover) && !chapter.cover_image);
    CHECK(chapter.heading && chapter.heading_end>chapter.heading_start);
    unsigned char* picture;
    size_t bytes;
    CHECK(thingsos_zip_read(&e->zip, e->cover, THINGSOS_MEMBER_BYTES, &picture, &bytes));
    unsigned char* gray;
    unsigned w, h;
    CHECK(thingsos_decode_image(picture, bytes, &gray, &w, &h) && w == 240 && h == 180);
    free(gray);
    free(picture);
    CHECK(!thingsos_zip_read(&e->zip, e->cover, 10, &picture, &bytes));
    path(p, root, "default.ttf");
    font_stream_test(p);
    thingsos_font_t font = { 0 };
    CHECK(thingsos_font_file(&font, p));
    inline_fit_layout_test(&font);
    CHECK(thingsos_font_size(&font, 32));
    unsigned reading_size = THINGSOS_READER_SIZE_MIN;
    for (unsigned expected = 30; expected <= 70; expected += 4) {
        CHECK(reading_size == expected);
        CHECK(thingsos_font_size(&font, reading_size) && font.size == expected);
        CHECK(thingsos_glyph(&font, NULL, 0x5c71, 0, 0) > 0);
        reading_size = thingsos_reader_size_step(reading_size, true);
    }
    CHECK(reading_size == 70 && thingsos_reader_size_step(70, true) == 70);
    CHECK(!thingsos_font_size(&font, 71) && font.size == 70);
    for (unsigned expected = 70; expected > 30; expected -= 4) {
        CHECK(reading_size == expected);
        reading_size = thingsos_reader_size_step(reading_size, false);
    }
    CHECK(reading_size == 30 && thingsos_reader_size_step(30, false) == 30);
    CHECK(thingsos_font_size(&font, 32));
    thingsos_canvas_t canvas = { malloc(684u * 1216u), 684, 1216 };
    CHECK(canvas.pixels);
    for (unsigned px = 30; px <= 70; px += 4) {
        CHECK(thingsos_font_size(&font, px));
        int measured = thingsos_glyph(&font, NULL, 0x5c71, 0, 0);
        CHECK(measured == thingsos_glyph(&font, &canvas, 0x5c71, 36, 150));
        CHECK(measured == thingsos_glyph(&font, NULL, 0x5c71, 0, 0));
    }
    CHECK(thingsos_font_size(&font, 32));
    memset(canvas.pixels, 255, 684u * 1216u);
    CHECK(thingsos_glyph(&font, &canvas, 0x5c71, 36, 150) > 0);
    size_t dark = 0;
    for (size_t i = 0; i < 684u * 1216u; i++)
        if (canvas.pixels[i] < 240) dark++;
    CHECK(dark > 100);
    unsigned char* glyph_copy=malloc(684u*1216u);CHECK(glyph_copy);
    memcpy(glyph_copy,canvas.pixels,684u*1216u);
    unsigned loads=font.glyph_loads,hits=font.bitmap_hits;
    memset(canvas.pixels,255,684u*1216u);
    CHECK(thingsos_glyph(&font,&canvas,0x5c71,36,150)>0);
    CHECK(font.glyph_loads==loads && font.bitmap_hits==hits+1);
    CHECK(!memcmp(glyph_copy,canvas.pixels,684u*1216u));
    void* bitmap_cache=font.bitmaps;void* metric_cache=font.metrics;
    font.bitmaps=font.metrics=NULL;
    memset(canvas.pixels,255,684u*1216u);
    CHECK(thingsos_glyph(&font,&canvas,0x5c71,36,150)>0);
    CHECK(!memcmp(glyph_copy,canvas.pixels,684u*1216u));
    font.bitmaps=bitmap_cache;font.metrics=metric_cache;
    /* A mono glyph must not reuse a gray cache entry (or the reverse).
       Cached and uncached mono rasterization must produce identical bits. */
    font.monochrome=true;
    memset(canvas.pixels,255,684u*1216u);
    CHECK(thingsos_glyph(&font,&canvas,0x5c71,36,150)>0);
    unsigned char* mono_copy=malloc(684u*1216u);CHECK(mono_copy);
    memcpy(mono_copy,canvas.pixels,684u*1216u);
    for(size_t i=0;i<684u*1216u;++i)CHECK(mono_copy[i]==0 || mono_copy[i]==255);
    loads=font.glyph_loads;hits=font.bitmap_hits;
    memset(canvas.pixels,255,684u*1216u);
    CHECK(thingsos_glyph(&font,&canvas,0x5c71,36,150)>0);
    CHECK(font.glyph_loads==loads && font.bitmap_hits==hits+1);
    CHECK(!memcmp(mono_copy,canvas.pixels,684u*1216u));
    font.bitmaps=font.metrics=NULL;memset(canvas.pixels,255,684u*1216u);
    CHECK(thingsos_glyph(&font,&canvas,0x5c71,36,150)>0);
    CHECK(!memcmp(mono_copy,canvas.pixels,684u*1216u));free(mono_copy);
    font.bitmaps=bitmap_cache;font.metrics=metric_cache;font.monochrome=false;
    /* Fast mode dithers the balanced gray raster to 1 bit at blit time, so
       pagination keeps the gray advances and the DU waveform gains anti-aliased
       edges without changing where lines break. */
    font.monochrome=false;font.dither=false;
    int gray_advance=thingsos_glyph(&font,NULL,0x6c38,0,0);
    font.dither=true;
    CHECK(thingsos_glyph(&font,NULL,0x6c38,0,0)==gray_advance);
    memset(canvas.pixels,255,684u*1216u);
    CHECK(thingsos_glyph(&font,&canvas,0x6c38,36,150)>0);
    size_t dither_dark=0;
    for(size_t i=0;i<684u*1216u;++i) {
        CHECK(canvas.pixels[i]==0 || canvas.pixels[i]==255);
        if(canvas.pixels[i]==0)dither_dark++;
    }
    CHECK(dither_dark>100);
    unsigned char* dither_copy=malloc(684u*1216u);CHECK(dither_copy);
    memcpy(dither_copy,canvas.pixels,684u*1216u);
    loads=font.glyph_loads;hits=font.bitmap_hits;
    memset(canvas.pixels,255,684u*1216u);
    CHECK(thingsos_glyph(&font,&canvas,0x6c38,36,150)>0);
    CHECK(font.glyph_loads==loads && font.bitmap_hits==hits+1);
    CHECK(!memcmp(dither_copy,canvas.pixels,684u*1216u));
    /* The 4x4 pattern follows the blit origin, so the cached bitmap stays gray. */
    memset(canvas.pixels,255,684u*1216u);
    CHECK(thingsos_glyph(&font,&canvas,0x6c38,37,150)>0);
    CHECK(memcmp(dither_copy,canvas.pixels,684u*1216u));
    free(dither_copy);font.dither=false;font.monochrome=false;
    memset(canvas.pixels,255,684u*1216u);
    CHECK(thingsos_glyph(&font,&canvas,0x5c71,36,150)>0);
    CHECK(!memcmp(glyph_copy,canvas.pixels,684u*1216u));free(glyph_copy);
    size_t end = thingsos_text_page(&font, &canvas, &chapter, 0, 36, 210, 612, 780, draw_image, e);
    CHECK(end > 0 && end <= chapter.bytes && drawn_images == 1);
    CHECK(thingsos_text_page(&font, &canvas, &chapter, 0, 36, 210, 612, 1, NULL, NULL) == 0);
    path(p, root, "damaged.ttf");
    CHECK(!thingsos_font_file(&font, p) && font.face);
    CHECK(thingsos_glyph(&font, &canvas, 'A', 36, 170) > 0);
    path(p, root, "rendered-reading.pgm");
    FILE* f = fopen(p, "wb");
    CHECK(f);
    fprintf(f, "P5\n684 1216\n255\n");
    CHECK(fwrite(canvas.pixels, 1, 684u * 1216u, f) == 684u * 1216u);
    fclose(f);
    /* Regression: EPUB formatting blank lines must not become empty screen rows.
       The source bytes and stored bookmark offsets remain untouched. */
    const char* plain = "索伊拉：圣地亚哥之母\n奇斯帕斯：圣地亚哥之兄";
    const char* spaced = "\n \n索伊拉：圣地亚哥之母\n\n  \n\t奇斯帕斯：圣地亚哥之兄";
    thingsos_chapter_t sample = {0};
    sample.text = (char*)plain; sample.bytes = strlen(plain);
    unsigned char* reference = malloc(684u*1216u);
    CHECK(reference);
    thingsos_font_size(&font,32);
    memset(canvas.pixels,255,684u*1216u);
    CHECK(thingsos_text_page_layout(&font,&canvas,&sample,0,36,210,612,780,175,0,true,NULL,NULL, NULL)
          == sample.bytes);
    memcpy(reference,canvas.pixels,684u*1216u);
    sample.text = (char*)spaced; sample.bytes = strlen(spaced);
    memset(canvas.pixels,255,684u*1216u);
    CHECK(thingsos_text_page_layout(&font,&canvas,&sample,0,36,210,612,780,175,0,true,NULL,NULL, NULL)
          == sample.bytes);
    CHECK(!memcmp(reference,canvas.pixels,684u*1216u));
    /* TXT intentionally retains explicit empty lines. */
    memset(canvas.pixels,255,684u*1216u);
    thingsos_text_page_layout(&font,&canvas,&sample,0,36,210,612,780,175,0,false,NULL,NULL, NULL);
    CHECK(memcmp(reference,canvas.pixels,684u*1216u));
    free(reference);
    paragraph_grid_test(&font, &canvas, root);
    paragraph_indent_test(&font, &canvas, root);
    punctuation_wrap_test(&font, &canvas, root);
    footnote_layout_test(&font, &canvas, root);
    {
        char original[]="Title\nBook\nTitle\nBody text.";
        thingsos_chapter_t hidden={0},clean={0};
        hidden.text=original;hidden.bytes=strlen(original);hidden.heading=true;hidden.heading_end=5;
        hidden.hidden_start=6;hidden.hidden_end=17;
        clean.text="Title\nBody text.";clean.bytes=strlen(clean.text);clean.heading=true;clean.heading_end=5;
        unsigned char* expected=malloc(684u*1216u);CHECK(expected);
        CHECK(thingsos_font_size(&font,30));memset(canvas.pixels,255,684u*1216u);
        CHECK(thingsos_text_page_layout(&font,&canvas,&clean,0,24,90,636,1040,145,0,true,NULL,NULL, NULL)==clean.bytes);
        memcpy(expected,canvas.pixels,684u*1216u);memset(canvas.pixels,255,684u*1216u);
        CHECK(thingsos_text_page_layout(&font,&canvas,&hidden,0,24,90,636,1040,145,0,true,NULL,NULL, NULL)==hidden.bytes);
        CHECK(!memcmp(expected,canvas.pixels,684u*1216u));
        clean.text="Body text.";clean.bytes=strlen(clean.text);clean.heading=false;
        memset(canvas.pixels,255,684u*1216u);
        CHECK(thingsos_text_page_layout(&font,&canvas,&clean,0,24,90,636,1040,145,0,true,NULL,NULL, NULL)==clean.bytes);
        CHECK(!memcmp(expected,canvas.pixels,684u*1216u)); /* Heading exists only in reader header. */
        CHECK(!strcmp(original,"Title\nBook\nTitle\nBody text."));free(expected);
        CHECK(thingsos_font_size(&font,32));
    }
    char long_text[4096]; memset(long_text,'W',sizeof(long_text));
    sample.text = long_text; sample.bytes = sizeof(long_text);
    size_t compact = thingsos_text_page_layout(&font,NULL,&sample,0,24,210,636,780,145,0,false,NULL,NULL, NULL);
    size_t standard = thingsos_text_page_layout(&font,NULL,&sample,0,36,210,612,780,175,0,false,NULL,NULL, NULL);
    size_t wide = thingsos_text_page_layout(&font,NULL,&sample,0,60,210,564,780,200,0,false,NULL,NULL, NULL);
    CHECK(compact > standard && standard > wide);
    size_t taller=thingsos_text_page_layout(&font,NULL,&sample,0,36,90,612,1040,175,0,false,NULL,NULL, NULL);
    CHECK(taller>standard);
    /* Dry pagination and painted pages must consume exactly the same bytes. */
    size_t painted=thingsos_text_page_layout(&font,&canvas,&sample,0,36,90,612,1040,175,0,false,NULL,NULL, NULL);
    CHECK(taller==painted && font.size==32);
    memset(canvas.pixels,255,684u*1216u);
    thingsos_draw_nav(&font,&canvas,0);
    for(unsigned tab=0;tab<4;++tab) {
        dark=0;
        for(int yy=1117;yy<1153;++yy) for(int xx=tab*171+68;xx<tab*171+104;++xx)
            if(canvas.pixels[(size_t)yy*684+xx]<240) ++dark;
        CHECK(dark>70);
    }
    CHECK(canvas.pixels[1201u*684+52]==0 && canvas.pixels[1201u*684+223]==255);
    path(p,root,"rendered-nav.pgm"); f=fopen(p,"wb"); CHECK(f);
    fprintf(f,"P5\n684 1216\n255\n"); fwrite(canvas.pixels,1,684u*1216u,f); fclose(f);
    thingsos_draw_reader_menu(&font,&canvas,(thingsos_reader_style_t){32,1,1});
    CHECK(canvas.pixels[571u*684+280] == 0); /* minus must not depend on font coverage */
    unsigned selection=99;
    CHECK(thingsos_reader_menu_hit(230,560,&selection)==THINGSOS_MENU_SMALLER);
    CHECK(thingsos_reader_menu_hit(530,560,&selection)==THINGSOS_MENU_LARGER);
    CHECK(thingsos_reader_menu_hit(250,655,&selection)==THINGSOS_MENU_INDENT && selection==0);
    CHECK(thingsos_reader_menu_hit(400,655,&selection)==THINGSOS_MENU_INDENT && selection==1);
    CHECK(thingsos_reader_menu_hit(550,655,&selection)==THINGSOS_MENU_NONE);
    CHECK(thingsos_reader_menu_hit(400,735,&selection)==THINGSOS_MENU_LINE && selection==1);
    CHECK(thingsos_reader_menu_hit(550,815,&selection)==THINGSOS_MENU_MARGIN && selection==2);
    CHECK(thingsos_reader_menu_hit(550,895,&selection)==THINGSOS_MENU_WEIGHT && selection==2);
    CHECK(thingsos_reader_menu_hit(400,975,&selection)==THINGSOS_MENU_RIPPLE && selection==1);
    CHECK(thingsos_reader_menu_hit(550,975,&selection)==THINGSOS_MENU_NONE);
    CHECK(thingsos_reader_menu_hit(220,1005,&selection)==THINGSOS_MENU_NONE);
    CHECK(thingsos_reader_menu_hit(60,1060,&selection)==THINGSOS_MENU_CHAPTERS);
    CHECK(thingsos_reader_menu_hit(300,1060,&selection)==THINGSOS_MENU_BOOKMARKS);
    CHECK(thingsos_reader_menu_hit(500,1060,&selection)==THINGSOS_MENU_JUMP);
    CHECK(thingsos_reader_menu_hit(60,1150,&selection)==THINGSOS_MENU_FONTS);
    CHECK(thingsos_reader_menu_hit(500,1150,&selection)==THINGSOS_MENU_MORE);
    CHECK(thingsos_reader_menu_hit(625,490,&selection)==THINGSOS_MENU_CLOSE);
    path(p,root,"rendered-reader-menu.pgm"); f=fopen(p,"wb"); CHECK(f);
    fprintf(f,"P5\n684 1216\n255\n"); fwrite(canvas.pixels,1,684u*1216u,f); fclose(f);
    free(canvas.pixels);
    thingsos_font_close(&font);
    thingsos_chapter_close(&chapter);
    thingsos_epub_close(e);
    path(p, root, "entity.epub");
    CHECK(!thingsos_epub_open(e, p));
    path(p, root, "crc.epub");
    CHECK(!thingsos_epub_open(e, p));
    path(p, root, "truncated.epub");
    CHECK(!thingsos_epub_open(e, p));
    free(e);
    path(p, root, "picture.jpg");
    picture = read_file(p, &bytes);
    CHECK(thingsos_decode_image(picture, bytes, &gray, &w, &h));
    free(picture);
    free(gray);
    path(p, root, "transparent.png");
    picture = read_file(p, &bytes);
    CHECK(thingsos_decode_image(picture, bytes, &gray, &w, &h) && gray[0] == 255);
    free(gray);
    CHECK(thingsos_decode_wallpaper(picture,bytes,&gray,&w,&h) && gray[0]==255);
    CHECK(thingsos_wallpaper_decode_peak()<=3u*1024u*1024u);
    free(picture);
    free(gray);
    for(unsigned type=0;type<2;++type) {
        path(p,root,type?"opaque-cover.png":"opaque-cover.jpg");picture=read_file(p,&bytes);
        CHECK(thingsos_decode_image(picture,bytes,&gray,&w,&h) && w==1200 && h==1600);
        CHECK(gray[0]>=62 && gray[0]<=66 && gray[(size_t)w*h-1]>=62 && gray[(size_t)w*h-1]<=66);
        free(gray);gray=NULL;
        if(!type) {
            CHECK(thingsos_decode_thumbnail(picture,bytes,148,188,&gray,&w,&h));
            CHECK(platform_jpeg_calls==0);
            CHECK(w==150 && h==200 && gray[0]>=62 && gray[0]<=66 && gray[(size_t)w*h-1]>=62 && gray[(size_t)w*h-1]<=66);
            free(gray);gray=NULL;
        }
        CHECK(!thingsos_decode_wallpaper(picture,bytes,&gray,&w,&h) && !gray);
        CHECK(thingsos_wallpaper_decode_peak()>0 && thingsos_wallpaper_decode_peak()<=3u*1024u*1024u);
        free(picture);free(gray);
    }
    path(p,root,"palette-alpha.png");picture=read_file(p,&bytes);
    CHECK(thingsos_decode_image(picture,bytes,&gray,&w,&h) && w==2 && h==1 && gray[0]==255 && gray[1]==0);
    free(gray);
    CHECK(thingsos_decode_wallpaper(picture,bytes,&gray,&w,&h) && w==2 && gray[0]==255 && gray[1]==0);
    free(picture);free(gray);
    const unsigned char junk[] = { 1, 2, 3 };
    CHECK(!thingsos_decode_image(junk, 3, &gray, &w, &h));
    /* Public-account GIFs display their first frame through the same decoder. */
    const unsigned char gif[]={
        'G','I','F','8','7','a',1,0,1,0,128,0,0,0,0,0,255,255,255,
        44,0,0,0,0,1,0,1,0,0,2,2,68,1,0,59};
    CHECK(thingsos_decode_image(gif,sizeof(gif),&gray,&w,&h) && w==1 && h==1);free(gray);
    thingsos_boot_guard_t guard = { 0 };
    CHECK(!thingsos_guard_begin(&guard, true));
    CHECK(!thingsos_guard_begin(&guard, false));
    CHECK(thingsos_guard_begin(&guard, false));
    thingsos_guard_healthy(&guard);
    CHECK(!thingsos_guard_begin(&guard, false));
    guard.complement = 0;
    CHECK(!thingsos_guard_begin(&guard, false));
    puts(
        "PASS: EPUB spine/cover/images/entities/CRC/truncation/path bounds; FreeType CJK and "
        "damaged-font fallback; JPEG/PNG/transparency; boot guard");
    char preview_path[512];thingsos_preview_t preview;
    path(preview_path,argv[1],"valid.epub");
    CHECK(thingsos_preview_read(preview_path,0,0,&preview) && preview.chapters && preview.text[0]);
    thingsos_preview_close(&preview);
    CHECK(!thingsos_preview_read(preview_path,9999,0,&preview));
    path(preview_path,argv[1],"remote-preview.txt");
    FILE* preview_file=fopen(preview_path,"wb");CHECK(preview_file);
    for(unsigned i=0;i<20000;++i)CHECK(fwrite("中",1,3,preview_file)==3);
    CHECK(!fclose(preview_file));size_t preview_offset=0,preview_bytes=0;
    do {
        CHECK(thingsos_preview_read(preview_path,0,preview_offset,&preview));
        size_t n=strlen(preview.text);CHECK(n && n<=16384 && n%3==0);
        CHECK(!memcmp(preview.text,"中",3));preview_bytes+=n;preview_offset=preview.next;
        thingsos_preview_close(&preview);
    } while(preview_offset);
    CHECK(preview_bytes==60000);
    CHECK(!thingsos_preview_read(preview_path,1,0,&preview));
    CHECK(!thingsos_preview_read("unsupported.bin",0,0,&preview));
    {
        CHECK(thingsos_image_tone_epd(0)==0);
        CHECK(thingsos_image_tone_epd(255)==255);
        CHECK((thingsos_image_tone_epd(45)>>4)==0);
        CHECK((thingsos_image_tone_epd(79)>>4)==0);
        CHECK(thingsos_image_tone_epd(238)==255);
        for(unsigned g=1;g<256;++g)CHECK(thingsos_image_tone_epd((unsigned char)g)>=thingsos_image_tone_epd((unsigned char)(g-1)));
        const unsigned char sample[]={45, 242};
        unsigned char average=0;
        thingsos_canvas_t photo={&average,1,1,false};
        thingsos_image_paint_clipped(&photo,sample,2,1,0,0,1,1,0,0,1,1,true,false);
        CHECK(average==144); /* Ordinary downscaling averages the dark and light pixels. */
        photo.inverted=true;
        thingsos_image_paint_clipped(&photo,sample,2,1,0,0,1,1,0,0,1,1,true,false);
        CHECK(average==(144^255));
        unsigned char* src=malloc(1200u*600u);CHECK(src);
        memset(src,242,1200u*600u);
        for(unsigned y=100;y<102;++y)memset(src+(size_t)y*1200+100,45,800);
        for(unsigned y=100;y<400;++y){src[(size_t)y*1200+300]=45;src[(size_t)y*1200+301]=45;}
        for(unsigned y=420;y<520;++y)for(unsigned x=100;x<500;++x)src[(size_t)y*1200+x]=(unsigned char)(100+(x-100)/8);
        unsigned char* dst=malloc(684u*340u);
        unsigned char* inv=malloc(684u*340u);
        CHECK(dst && inv);
        memset(dst,123,684u*340u);memset(inv,123,684u*340u);
        thingsos_canvas_t ca={dst,684,340,false},cb={inv,684,340,true};
        thingsos_image_paint_clipped(&ca,src,1200,600,36,20,612,294,40,24,644,310,true,true);
        thingsos_image_paint_clipped(&cb,src,1200,600,36,20,612,294,40,24,644,310,true,true);
        CHECK(dst[0]==123 && dst[24u*684u+39u]==123 && dst[24u*684u+644u]==123);
        unsigned nib0_stroke=0,bg_white=0;
        for(unsigned y=24;y<310;++y)for(unsigned x=40;x<644;++x) {
            unsigned char v=dst[(size_t)y*684u+x];
            CHECK(inv[(size_t)y*684u+x]==(unsigned char)(v^255u));
            if(y>=65 && y<=75 && x>=100 && x<=440 && (v>>4)==0)++nib0_stroke;
            if(y>=120 && y<=180 && x>=300 && x<=500 && v==255)++bg_white;
        }
        CHECK(nib0_stroke>=150 && bg_white>5000);
        free(src);free(dst);free(inv);
    }
    return 0;
}
