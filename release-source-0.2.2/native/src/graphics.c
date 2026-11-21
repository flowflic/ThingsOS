// SPDX-License-Identifier: Apache-2.0
#include "thingsos/graphics.h"
#include "thingsos/markdown.h"
#include "thingsos/reader.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H
#include FT_SYSTEM_H
#include FT_MODULE_H
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif
#ifdef ESP_PLATFORM
/* FreeType's default malloc competes with SD/LCD/Wi-Fi for internal SRAM.
   Its outline, interpreter and raster buffers can all live in PSRAM. */
static void* font_allocate(FT_Memory memory,long bytes) {
    (void)memory;
    return bytes>0?heap_caps_malloc((size_t)bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT):NULL;
}
static void font_release(FT_Memory memory,void* block) {(void)memory;heap_caps_free(block);}
static void* font_reallocate(FT_Memory memory,long old,long bytes,void* block) {
    (void)memory;(void)old;
    if(bytes<=0){heap_caps_free(block);return NULL;}
    return heap_caps_realloc(block,(size_t)bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
}
static struct FT_MemoryRec_ font_memory={NULL,font_allocate,font_release,font_reallocate};
#endif
static FT_Error font_library_new(FT_Library* library) {
#ifdef ESP_PLATFORM
    FT_Error error=FT_New_Library(&font_memory,library);
    if(!error){FT_Add_Default_Modules(*library);FT_Set_Default_Properties(*library);}
    return error;
#else
    return FT_Init_FreeType(library);
#endif
}
static void font_library_done(FT_Library library) {
#ifdef ESP_PLATFORM
    /* The shared, stateless memory record is not owned by FreeType. */
    FT_Done_Library(library);
#else
    FT_Done_FreeType(library);
#endif
}
typedef struct { uint32_t codepoint; unsigned size; int advance; bool monochrome; } metric_t;
#define METRIC_SLOTS 8192
typedef struct {
    uint32_t codepoint;
    unsigned size, width, rows, weight;
    int left, top, advance;
    unsigned char* pixels;
    bool monochrome;
    unsigned used;
} bitmap_t;
#define BITMAP_SLOTS 512
static void* cache_allocate(size_t bytes) {
#ifdef ESP_PLATFORM
    /* Small glyphs must never consume the internal DMA heap needed by SDMMC.
       Optional caches use PSRAM exclusively; exhaustion disables caching. */
    return heap_caps_malloc(bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
#else
    return malloc(bytes);
#endif
}
static void* cache_zero(size_t bytes) {
    void* p=cache_allocate(bytes);
    if(p) memset(p,0,bytes);
    return p;
}
static uint64_t signature(const unsigned char* data, size_t n, uint64_t h) {
    for (size_t i=0;i<n;++i) h=(h^data[i])*1099511628211ULL;
    return h;
}
static uint32_t next(const char* p, size_t left, unsigned* length) {
    unsigned char a = (unsigned char)p[0];
    unsigned n = a < 128                  ? 1
                 : a >= 0xc2 && a < 0xe0  ? 2
                 : a >= 0xe0 && a < 0xf0  ? 3
                 : a >= 0xf0 && a <= 0xf4 ? 4
                                          : 0;
    *length = 1;
    if (!n || left < n) return 0xfffd;
    uint32_t cp = a & (n == 1 ? 127 : n == 2 ? 31 : n == 3 ? 15 : 7);
    for (unsigned i = 1; i < n; i++) {
        unsigned char b = (unsigned char)p[i];
        if ((b & 0xc0) != 0x80) return 0xfffd;
        cp = cp << 6 | (b & 63);
    }
    if ((n == 2 && cp < 128) || (n == 3 && cp < 2048) || (n == 4 && cp < 65536) || cp > 0x10ffff ||
        (cp >= 0xd800 && cp <= 0xdfff))
        return 0xfffd;
    *length = n;
    return cp;
}
#define FONT_IO_BLOCK 4096u
#define FONT_IO_SLOTS 8u
typedef struct {
    FT_StreamRec stream;
    FILE* file;
    unsigned char* cache;
    struct { unsigned long base; unsigned bytes, stamp; } slots[FONT_IO_SLOTS];
    unsigned clock;
    thingsos_font_io_t stats;
} font_stream_t;
#ifndef ESP_PLATFORM
static bool font_host_no_cache;
void thingsos_font_host_uncached(bool value) {font_host_no_cache=value;}
#endif
static unsigned long font_read(FT_Stream stream,unsigned long offset,unsigned char* out,unsigned long count) {
    font_stream_t* s=stream->descriptor.pointer;
    if(offset>stream->size)return count?0:1;
    if(!count)return 0;
    if(!out || !s->file)return 0;
    ++s->stats.requests;
    if(count>stream->size-offset)count=stream->size-offset;
    if(!s->cache) {
        if(fseek(s->file,(long)offset,SEEK_SET))return 0;
        ++s->stats.disk_reads;
        size_t got=fread(out,1,count,s->file);s->stats.disk_bytes+=got;
        return (unsigned long)got;
    }
    unsigned long done=0;
    while(done<count) {
        unsigned long position=offset+done,base=position/FONT_IO_BLOCK*FONT_IO_BLOCK;
        unsigned slot=FONT_IO_SLOTS,oldest=0;
        for(unsigned i=0;i<FONT_IO_SLOTS;++i) {
            if(s->slots[i].bytes && s->slots[i].base==base){slot=i;break;}
            if(s->slots[i].stamp<s->slots[oldest].stamp)oldest=i;
        }
        if(slot==FONT_IO_SLOTS) {
            slot=oldest;s->slots[slot].bytes=0;
            unsigned long wanted=stream->size-base;if(wanted>FONT_IO_BLOCK)wanted=FONT_IO_BLOCK;
            if(fseek(s->file,(long)base,SEEK_SET))break;
            ++s->stats.disk_reads;
            size_t got=fread(s->cache+slot*FONT_IO_BLOCK,1,wanted,s->file);
            s->stats.disk_bytes+=got;
            if(got!=wanted)break;
            s->slots[slot].base=base;s->slots[slot].bytes=(unsigned)got;
        }else ++s->stats.cache_hits;
        if(!++s->clock) {
            for(unsigned i=0;i<FONT_IO_SLOTS;++i)s->slots[i].stamp=0;
            s->clock=1;
        }
        s->slots[slot].stamp=s->clock;
        unsigned within=(unsigned)(position-base);
        if(within>=s->slots[slot].bytes)break;
        unsigned long n=s->slots[slot].bytes-within;if(n>count-done)n=count-done;
        memcpy(out+done,s->cache+slot*FONT_IO_BLOCK+within,n);done+=n;
    }
    return done;
}
static void font_stream_close(FT_Stream stream) {
    font_stream_t* s=stream->descriptor.pointer;
    if(s->file){fclose(s->file);s->file=NULL;}
    free(s->cache);s->cache=NULL;
}
bool thingsos_font_io(const thingsos_font_t* f,thingsos_font_io_t* out) {
    if(!f || !f->face || !out)return false;
    FT_Stream stream=((FT_Face)f->face)->stream;
    if(!stream || stream->read!=font_read)return false;
    *out=((font_stream_t*)stream->descriptor.pointer)->stats;return true;
}
void thingsos_font_close(thingsos_font_t* f) {
    if (!f) return;
    font_stream_t* stream=NULL;
    if(f->face && ((FT_Face)f->face)->stream && ((FT_Face)f->face)->stream->read==font_read)
        stream=((FT_Face)f->face)->stream->descriptor.pointer;
    if (f->face) FT_Done_Face(f->face);
    free(stream);
    if (f->library) font_library_done(f->library);
    free(f->metrics);
    if (f->bitmaps) {
        bitmap_t* b = f->bitmaps;
        for (unsigned i=0;i<BITMAP_SLOTS;++i) free(b[i].pixels);
        free(b);
    }
    memset(f, 0, sizeof(*f));
}
static bool install(thingsos_font_t* f, FT_Library library, FT_Face face) {
    if (FT_Select_Charmap(face, FT_ENCODING_UNICODE) || FT_Set_Pixel_Sizes(face, 0, 32)) {
        FT_Done_Face(face);
        font_library_done(library);
        return false;
    }
    thingsos_font_close(f);
    f->face = face;
    f->library = library;
    f->size = 32;
    f->metrics = cache_zero(METRIC_SLOTS*sizeof(metric_t));
    f->bitmaps = cache_zero(BITMAP_SLOTS*sizeof(bitmap_t));
    return true;
}
bool thingsos_font_memory(thingsos_font_t* f, const unsigned char* data, size_t bytes) {
    if (!f || !data || bytes < 64 || bytes > LONG_MAX) return false;
    FT_Library l;
    FT_Face face;
    if (font_library_new(&l)) return false;
    if (FT_New_Memory_Face(l, data, (FT_Long)bytes, 0, &face)) {
        font_library_done(l);
        return false;
    }
    if (!install(f, l, face)) return false;
    f->signature=signature(data,bytes,14695981039346656037ULL);
    return true;
}
bool thingsos_font_file(thingsos_font_t* f, const char* path) {
    if (!f || !path) return false;
    font_stream_t* source=cache_zero(sizeof(*source));
    if(!source)return false;
    source->file=fopen(path,"rb");
    if(!source->file){free(source);return false;}
    /* FreeType owns seeking; our bounded block cache replaces stdio's small
       implicit buffer and keeps all optional font I/O storage out of SRAM. */
    setvbuf(source->file,NULL,_IONBF,0);
    if(fseek(source->file,0,SEEK_END)){fclose(source->file);free(source);return false;}
    long bytes=ftell(source->file);
    if(bytes<64){fclose(source->file);free(source);return false;}
    bool use_cache=true;
#ifdef ESP_PLATFORM
    use_cache=heap_caps_get_free_size(MALLOC_CAP_SPIRAM)>FONT_IO_BLOCK*FONT_IO_SLOTS+512u*1024u;
#else
    use_cache=!font_host_no_cache;
#endif
    if(use_cache)source->cache=cache_allocate(FONT_IO_BLOCK*FONT_IO_SLOTS);
    source->stats.cache_bytes=source->cache?FONT_IO_BLOCK*FONT_IO_SLOTS:0;
    source->stream.size=(unsigned long)bytes;
    source->stream.descriptor.pointer=source;
    source->stream.read=font_read;source->stream.close=font_stream_close;
    FT_Library l;
    FT_Face face;
    if (font_library_new(&l)) {font_stream_close(&source->stream);free(source);return false;}
    FT_Open_Args args={0};args.flags=FT_OPEN_STREAM;args.stream=&source->stream;
    if (FT_Open_Face(l, &args, 0, &face)) {
        font_stream_close(&source->stream);free(source);
        font_library_done(l);
        return false;
    }
    if (!install(f, l, face)) {free(source);return false;}
    FILE* file=fopen(path,"rb");
    unsigned char block[4096]; size_t n;
    uint64_t hash=14695981039346656037ULL;
    if (file) {
        while ((n=fread(block,1,sizeof(block),file))) hash=signature(block,n,hash);
        if (!ferror(file)) f->signature=hash;
        fclose(file);
    }
    return true;
}
bool thingsos_font_size(thingsos_font_t* f, unsigned px) {
    if (f && f->ui_size_percent) {
        px = (px * f->ui_size_percent + 50) / 100;
        if (px < 16) px = 16;
        if (px > 70) px = 70;
    }
    if (f && f->face && f->size == px) return true;
    if (!f || !f->face || px < 16 || px > 70) return false;
    if (FT_Set_Pixel_Sizes(f->face, 0, px)) {++f->errors;return false;}
    f->size = px;
    return true;
}
bool thingsos_font_weight(thingsos_font_t* f, unsigned weight) {
    if (f && f->ui_weight_override) weight = f->ui_weight_override - 1;
    if(!f || weight>2) return false;
    f->weight=weight;
    return true;
}
void thingsos_rect(thingsos_canvas_t* c, int x, int y, int w, int h, unsigned char shade) {
    if (!c || !c->pixels || w <= 0 || h <= 0) return;
    if(c->vertical_text) {
        thingsos_canvas_t physical=*c;physical.width=c->height;physical.height=c->width;physical.vertical_text=false;
        thingsos_rect(&physical,c->height-y-h,x,h,w,shade);return;
    }
    int right = x + w, bottom = y + h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (right > c->width) right = c->width;
    if (bottom > c->height) bottom = c->height;
    for (int j = y; j < bottom; j++)
        for (int i = x; i < right; i++) c->pixels[(size_t)j * c->width + i] = shade;
}
static void glyph_placeholder(thingsos_canvas_t* c,unsigned size,int x,int baseline) {
    int side=(int)size-6;if(side<8)side=8;
    thingsos_rect(c,x+2,baseline-(int)size+3,side,1,0);
    thingsos_rect(c,x+2,baseline-4,side,1,0);
    thingsos_rect(c,x+2,baseline-(int)size+3,1,side,0);
    thingsos_rect(c,x+side+1,baseline-(int)size+3,1,side,0);
}
/* DU remains binary. Distribute coverage within each glyph rather than a
   screen-anchored Bayer grid: identical glyphs retain identical edges after
   pagination/movement. Two bounded error rows cost 1032 bytes of stack and
   no per-glyph allocation; larger custom glyphs use a safe threshold fallback.
   Strong stems and empty/fringe pixels are pinned, so error never deposits
   dots outside the outline or perforates fully covered strokes. */
static void glyph_blit(const thingsos_font_t* f,thingsos_canvas_t* c,
    const unsigned char* pixels,unsigned width,unsigned rows,int pitch,bool mono,
    int left,int top,int x,int baseline,bool rotated,bool corner) {
    if(!pixels || !width || !rows)return;
    enum { ERROR_WIDTH=256 };
    int16_t errors[2][ERROR_WIDTH+2]={{0}};
    bool diffuse=f->dither && !mono && width<=ERROR_WIDTH;
    for(unsigned j=0;j<rows;++j) {
        const unsigned char* row=pixels+(pitch>=0?(size_t)j*pitch:(size_t)(rows-j-1)*(-pitch));
        int direction=diffuse && (j&1u)?-1:1;
        int16_t* current=errors[j&1u];
        int16_t* next=errors[(j+1)&1u];
        if(diffuse)memset(next,0,sizeof(errors[0]));
        for(unsigned n=0;n<width;++n) {
            unsigned i=direction>0?n:width-1-n;
            unsigned char a=mono?((row[i/8]&(0x80u>>(i%8)))?255:0):row[i];
            if(diffuse) {
                int value=(int)a*16+current[i+1];
                unsigned char quantized=a<=16?0:a>=240?255:value>=128*16?255:0;
                /* Suppress diffusion into transparent padding and solid ink. */
                if(a>16 && a<240) {
                    int error=value-(int)quantized*16;
                    int at=(int)i+1;
                    current[at+direction]+=(int16_t)(error*7/16);
                    next[at-direction]+=(int16_t)(error*3/16);
                    next[at]+=(int16_t)(error*5/16);
                    next[at+direction]+=(int16_t)(error/16);
                }
                a=quantized;
            } else if(f->dither)a=a>=128?255:0;
            /* Process the whole bitmap even when clipped, so clipping and
               warm/cold bitmap caches cannot alter the visible edge pattern. */
            int xx=x+left+(int)i+(f->italic?(top-(int)j)/5:0), yy=baseline-top+(int)j;
            int stride=c->width,limit_y=c->height;
            if(c->vertical_text) {
                /* Transpose the line axes while leaving each glyph upright. */
                xx=c->height-baseline+left+(int)i+(f->italic?(top-(int)j)/5:0);
                int offset=(int)f->size-top;
                if(offset+(int)rows>(int)f->size)offset=(int)f->size-(int)rows;
                if(offset<0)offset=0;
                yy=x+offset+(int)j;
                if(rotated) {
                    xx=c->height-baseline+((int)f->size-(int)rows)/2+(int)rows-1-(int)j;
                    yy=x+((int)f->size-(int)width)/2+(int)i;
                } else if(corner) {
                    xx=c->height-baseline+(int)f->size-(int)width-2+(int)i;
                    yy=x+2+(int)j;
                }
                stride=c->height;limit_y=c->width;
                if(xx<c->height-baseline || xx>=c->height-baseline+(int)f->size ||
                   yy<x || yy>=x+(int)f->size)continue;
            }
            if(xx<0 || xx>=stride || yy<0 || yy>=limit_y)continue;
            unsigned char* pixel=&c->pixels[(size_t)yy*stride+xx];
            *pixel=(unsigned char)((unsigned)*pixel*(255-a)/255);
        }
    }
}
static uint32_t vertical_codepoint(FT_Face face,uint32_t cp) {
    uint32_t mapped=cp;
    switch(cp) {
        case ',':case 0xff0c:mapped=0xfe10;break;
        case 0x3001:mapped=0xfe11;break;case 0x3002:mapped=0xfe12;break;
        case ':':case 0xff1a:mapped=0xfe13;break;case ';':case 0xff1b:mapped=0xfe14;break;
        case '!':case 0xff01:mapped=0xfe15;break;case '?':case 0xff1f:mapped=0xfe16;break;
        case 0x2026:mapped=0xfe19;break;case 0x2014:mapped=0xfe31;break;case 0x2013:mapped=0xfe32;break;
        case '(':case 0xff08:mapped=0xfe35;break;case ')':case 0xff09:mapped=0xfe36;break;
        case '{':mapped=0xfe37;break;case '}':mapped=0xfe38;break;
        case '[':mapped=0xfe47;break;case ']':mapped=0xfe48;break;
        case 0x3014:mapped=0xfe39;break;case 0x3015:mapped=0xfe3a;break;
        case 0x3010:mapped=0xfe3b;break;case 0x3011:mapped=0xfe3c;break;
        case 0x300a:mapped=0xfe3d;break;case 0x300b:mapped=0xfe3e;break;
        case 0x3008:mapped=0xfe3f;break;case 0x3009:mapped=0xfe40;break;
        case 0x201c:case 0x300c:mapped=0xfe41;break;case 0x201d:case 0x300d:mapped=0xfe42;break;
        case 0x300e:mapped=0xfe43;break;case 0x300f:mapped=0xfe44;break;
    }
    return mapped!=cp && FT_Get_Char_Index(face,mapped)?mapped:cp;
}
static bool vertical_rotate_fallback(uint32_t cp) {
    switch(cp) {
        case '-':case '_':case '(':case ')':case '[':case ']':case '{':case '}':
        case 0x2013:case 0x2014:case 0x2026:case 0x201c:case 0x201d:
        case 0x3008:case 0x3009:case 0x300a:case 0x300b:case 0x300c:case 0x300d:
        case 0x300e:case 0x300f:case 0x3010:case 0x3011:case 0x3014:case 0x3015:
        case 0xff08:case 0xff09:return true;
        default:return false;
    }
}
int thingsos_glyph(thingsos_font_t* f, thingsos_canvas_t* c, uint32_t cp, int x, int baseline) {
    if (!f || !f->face) return 0;
    FT_Face face = f->face;
    uint32_t original_cp=cp;
    if(f->vertical_text)cp=vertical_codepoint(face,cp);
    bool rotated=f->vertical_text && cp==original_cp && vertical_rotate_fallback(cp);
    bool corner=f->vertical_text && (original_cp==',' || original_cp=='.' || original_cp==0xff0c || original_cp==0x3001 || original_cp==0x3002);
    metric_t* metric = f->metrics ? (metric_t*)f->metrics +
        ((cp * 2654435761u ^ f->size) % METRIC_SLOTS) : NULL;
    if ((!c || !c->pixels) && metric && metric->size == f->size && metric->codepoint == cp && metric->monochrome==f->monochrome)
        return f->vertical_text?(int)f->size:metric->advance;
    bitmap_t* cached=NULL;
    if(f->bitmaps && c && c->pixels) {
        /* Four candidates retain colliding common glyphs without increasing
           the number of cached bitmaps or their pixel allocation budget. */
        unsigned base=((cp*2654435761u ^ f->size ^ f->weight*40503u)%(BITMAP_SLOTS/4))*4;
        bitmap_t* set=(bitmap_t*)f->bitmaps+base;
        unsigned clock=f->bitmap_hits+f->glyph_loads;
        cached=set;
        for(unsigned i=0;i<4;++i) {
            bitmap_t* b=set+i;
            if(b->pixels && b->size==f->size && b->weight==f->weight &&
               b->codepoint==cp && b->monochrome==f->monochrome){cached=b;break;}
            if(!b->pixels || (cached->pixels && clock-b->used>clock-cached->used))cached=b;
        }
    }
    if (c && c->pixels && cached && cached->pixels &&
        cached->size==f->size && cached->weight==f->weight && cached->codepoint==cp && cached->monochrome==f->monochrome) {
        f->bitmap_hits++;
        cached->used=f->bitmap_hits+f->glyph_loads;
        glyph_blit(f,c,cached->pixels,cached->width,cached->rows,(int)cached->width,
            false,cached->left,cached->top,x,baseline,rotated,corner);
        return f->vertical_text?(int)f->size:cached->advance;
    }
#ifdef ESP_PLATFORM
    /* Cached advances are cheap. Yield for actual FreeType work only. */
    static unsigned work;
    if (++work % 64 == 0) vTaskDelay(1);
#endif
    /* Pagination needs the hinted advance, not a rasterized bitmap. */
    FT_Int32 flags = FT_LOAD_NO_SVG | FT_LOAD_NO_BITMAP | (f->monochrome?FT_LOAD_TARGET_MONO:0);
    f->glyph_loads++;
    FT_Error error=FT_Load_Char(face, cp, flags);
    /* Some custom fonts have unusable hint programs but valid outlines. */
    if(error)error=FT_Load_Char(face,cp,flags|FT_LOAD_NO_HINTING|FT_LOAD_NO_AUTOHINT);
    if (error) {
        ++f->errors;
#ifdef ESP_PLATFORM
        ESP_LOGE("ThingsOS_font","Glyph load failed U+%04lx size=%u error=0x%x internal=%u psram=%u",
            (unsigned long)cp,f->size,(unsigned)error,
            (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
            (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
#endif
        glyph_placeholder(c,f->size,x,baseline);
        return (int)f->size;
    }
    FT_GlyphSlot g = face->glyph;
    if(c && c->pixels && f->weight && g->format==FT_GLYPH_FORMAT_OUTLINE) {
        /* Synthetic bold is optional. An unsupported contour orientation
           still has a drawable original outline; it is not a missing glyph. */
        FT_Error bold_error=FT_Outline_Embolden(&g->outline,(FT_Pos)f->weight*32);
#ifdef ESP_PLATFORM
        if(bold_error)ESP_LOGW("ThingsOS_font","Synthetic bold skipped U+%04lx error=0x%x",(unsigned long)cp,(unsigned)bold_error);
#else
        (void)bold_error;
#endif
    }
    int advance = (int)((g->advance.x + 63) / 64);
    if (metric) *metric = (metric_t){ cp, f->size, advance, f->monochrome };
    if (!c || !c->pixels) return f->vertical_text?(int)f->size:advance;
    /* Keep the gray raster for plain/dithered blits; quantization is derived
       from that bitmap, so it is not part of the cache key. */
    error=FT_Render_Glyph(g,f->monochrome?FT_RENDER_MODE_MONO:FT_RENDER_MODE_NORMAL);
    if(error) {
        ++f->errors;
#ifdef ESP_PLATFORM
        ESP_LOGE("ThingsOS_font","Glyph raster failed U+%04lx size=%u error=0x%x internal=%u psram=%u",
            (unsigned long)cp,f->size,(unsigned)error,
            (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
            (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
#endif
        glyph_placeholder(c,f->size,x,baseline);
        return f->vertical_text?(int)f->size:advance;
    }
    FT_Bitmap* b = &g->bitmap;
    if (b->pixel_mode != FT_PIXEL_MODE_GRAY && b->pixel_mode != FT_PIXEL_MODE_MONO) return f->vertical_text?(int)f->size:advance;
    /* At most 2 MiB across 512 slots; allocation failure simply draws normally. */
    if (cached && b->width && b->rows && (size_t)b->width*b->rows<=4096) {
        unsigned char* pixels=cache_allocate((size_t)b->width*b->rows);
        if (pixels) {
            for(unsigned j=0;j<b->rows;++j) {
                const unsigned char* row=b->buffer+(b->pitch>=0?(size_t)j*b->pitch:(size_t)(b->rows-j-1)*(-b->pitch));
                for(unsigned i=0;i<b->width;++i)pixels[(size_t)j*b->width+i]=
                    b->pixel_mode==FT_PIXEL_MODE_MONO?((row[i/8]&(0x80u>>(i%8)))?255:0):row[i];
            }
            free(cached->pixels);
            *cached=(bitmap_t){cp,f->size,b->width,b->rows,f->weight,g->bitmap_left,g->bitmap_top,advance,pixels,f->monochrome,f->bitmap_hits+f->glyph_loads};
        }
    }
    glyph_blit(f,c,b->buffer,b->width,b->rows,b->pitch,b->pixel_mode==FT_PIXEL_MODE_MONO,
        g->bitmap_left,g->bitmap_top,x,baseline,rotated,corner);
    return f->vertical_text?(int)f->size:advance;
}
void thingsos_label(thingsos_font_t* f, thingsos_canvas_t* c, const char* s, int x, int y, int w) {
    if (!s) return;
    size_t n = strlen(s), i = 0;
    int used = 0;
    while (i < n) {
        unsigned bytes;
        uint32_t cp = next(s + i, n - i, &bytes);
        int a = thingsos_glyph(f, NULL, cp, 0, 0);
        if (used + a > w) break;
        thingsos_glyph(f, c, cp, x + used, y);
        used += a;
        i += bytes;
    }
}
static void label_aligned(thingsos_font_t* f, thingsos_canvas_t* c, const char* s,
                          int x, int y, int width, bool right) {
    if (!s || width <= 0) return;
    size_t n = strlen(s), i = 0;
    int measured = 0;
    while (i < n) {
        unsigned bytes;
        uint32_t cp = next(s + i, n - i, &bytes);
        int advance = thingsos_glyph(f, NULL, cp, 0, 0);
        if (measured + advance > width) break;
        measured += advance;
        i += bytes;
    }
    thingsos_label(f, c, s, x + (width - measured) / (right ? 1 : 2), y, measured);
}
void thingsos_label_centered(thingsos_font_t* f, thingsos_canvas_t* c, const char* s,
                             int x, int y, int width) {
    label_aligned(f, c, s, x, y, width, false);
}
void thingsos_label_right(thingsos_font_t* f, thingsos_canvas_t* c, const char* s,
                          int x, int y, int width) {
    label_aligned(f, c, s, x, y, width, true);
}
static bool epub_paragraph_indent(uint32_t cp) {
    return cp == ' ' || cp == '\t' || cp == 0x00a0 || cp == 0x1680 ||
           (cp >= 0x2000 && cp <= 0x200a) || cp == 0x202f || cp == 0x205f || cp == 0x3000;
}
/* Closing punctuation stays with the preceding character at a soft wrap.
   Explicit paragraph breaks and source bytes remain untouched. */
static bool line_start_prohibited(uint32_t cp) {
    switch(cp) {
        case ',': case '.': case ';': case ':': case '!': case '?':
        case '%': case ')': case ']': case '}':
        case 0x2019: case 0x201d: case 0x2026: case 0x2014:
        case 0x3001: case 0x3002: case 0x3009: case 0x300b:
        case 0x300d: case 0x300f: case 0x3011: case 0x3015:
        case 0x3017: case 0x3019: case 0x301b:
        case 0xff01: case 0xff05: case 0xff09: case 0xff0c:
        case 0xff0e: case 0xff1a: case 0xff1b: case 0xff1f:
        case 0xff3d: case 0xff5d: return true;
        default: return false;
    }
}
/* True when start is the first character of a paragraph: at the chapter start,
   or after the line break and indentation whitespace that ended the previous
   one. A page that resumes in the middle of a paragraph reports false, so its
   first line keeps the shared gutter. */
static bool paragraph_begin(const char* text, size_t start) {
    size_t i = start;
    while (i) {
        unsigned char c = (unsigned char)text[i - 1];
        if (c == '\n' || c == '\r') return true;
        if (c == ' ' || c == '\t') { --i; continue; }
        /* U+3000 ideographic space, the indentation most Chinese sources carry. */
        if (i >= 3 && (unsigned char)text[i - 3] == 0xe3 && (unsigned char)text[i - 2] == 0x80 &&
            c == 0x80) { i -= 3; continue; }
        break;
    }
    return i == 0;
}
/* Compact borrowed view also serves note text without allocating an EPUB image array. */
typedef struct {
    const char* text;size_t bytes;
    const thingsos_inline_t* images;size_t image_count;
    size_t heading_start,heading_end,hidden_start,hidden_end;bool heading;
    const thingsos_text_underline_t* underlines;size_t underline_count;
    const thingsos_footnote_t* footnotes;size_t footnote_count;
    const thingsos_chapter_t* markdown;
    unsigned cover_image;
    bool justify; /* reader body text: spread soft-wrap slack across the line */
} text_view_t;
/* One footnote marker spans several glyphs; merge them into a single box and
   pad it slightly so a 17-pixel digit is still a comfortable target. The box is
   clamped to the text area so a padded first or last line cannot overlap the
   header, the footer or another page's touch target. */
static void text_hit_add(thingsos_text_hits_t* hits, const thingsos_footnote_t* note, int x, int y,
                         int w, int line, int top, int bottom) {
    if (!hits) return;
    int left = x < 0 ? 0 : x;
    int upper = y < top ? top : y;
    int lower = y + line > bottom ? bottom : y + line;
    if (lower <= upper) lower = upper + 1;
    for (size_t i = 0; i < hits->count; ++i) {
        thingsos_text_hit_t* hit = hits->items + i;
        if (hit->begin != note->begin) continue;
        int right = left + w > hit->x + hit->w ? left + w : hit->x + hit->w;
        int floor = lower > hit->y + hit->h ? lower : hit->y + hit->h;
        if (left < hit->x) hit->x = left;
        if (upper < hit->y) hit->y = upper;
        hit->w = right - hit->x;
        hit->h = floor - hit->y;
        return;
    }
    if (hits->count >= THINGSOS_TEXT_HIT_LIMIT) return;
    thingsos_text_hit_t* hit = hits->items + hits->count++;
    hit->begin = note->begin;
    hit->end = note->end;
    hit->x = left;
    hit->y = upper;
    hit->w = w;
    hit->h = lower - upper;
}
/* In vertical flow, physical body width is the available baseline extent.
   Use the column count whose fitted pitch is closest to the selected spacing,
   rather than losing a whole column to a few pixels of trailing grid slack.
   Horizontal baseline spacing remains byte-for-byte unchanged. */
static int text_line_advance(const thingsos_font_t* font,unsigned size,unsigned percent,int extent) {
    int nominal=(int)(size*percent/100);
    if(!font->vertical_text || extent<2*(int)size || nominal<=0)return nominal;
    int span=extent-(int)size,gaps=span/nominal;
    if(gaps<1)return nominal;
    int pitch=span/gaps,denser=span/(gaps+1);
    if(denser>=(int)size && abs(denser-nominal)<=abs(pitch-nominal))pitch=denser;
    return pitch;
}
/* Width the glyph at p needs at a line end: its own advance plus the closing
   punctuation that may not start the next line. Bounded lookahead, no
   allocation; does not cross images, hidden text, a style boundary (limit) or
   superscript references. Used by Markdown, UI text and vertical flow. */
static int text_reserve(thingsos_font_t* f,const text_view_t* ch,size_t look,size_t limit,int a,int width) {
    int reserve=a;
    for(unsigned count=0;count<8 && look<ch->bytes;++count) {
        if(look>=limit || (look>=ch->hidden_start && look<ch->hidden_end))break;
        unsigned n;uint32_t following=next(ch->text+look,ch->bytes-look,&n);
        if(!line_start_prohibited(following))break;
        bool boundary=false;
        for(size_t i=0;i<ch->image_count;++i)
            if(ch->images[i].offset==look){boundary=true;break;}
        for(size_t i=0;i<ch->footnote_count && !boundary;++i)
            if(look>=ch->footnotes[i].begin && look<ch->footnotes[i].end)boundary=true;
        if(boundary)break;
        int advance=thingsos_glyph(f,NULL,following,0,0);
        if(advance>width || reserve>width-advance){reserve=a;break;}
        reserve+=advance;look+=n;
    }
    return reserve;
}
/* Horizontal reader body text follows clreq line breaking
   (https://www.w3.org/TR/clreq/#line_breaking): fill the line at normal
   advances first, then fit punctuation that does not fit instead of
   reserving its full width up front and pushing the preceding character down.
   - Closing marks whose ink sits in the left half of the em box (，。、；：
     and closing quotes/brackets) may drop the empty half at a line end.
   - A single point mark (，。、．) may then hang a little into the right
     margin (https://www.w3.org/TR/clreq/#hanging_punctuation), keeping a safe
     distance from the screen edge.
   - Dashes and ellipses stay unbroken with the preceding character; the line
     may tighten its spacing slightly to keep the group at the line end.
   - Opening marks do not end a line.
   Whatever does not fit moves to the next line, and the line it leaves is
   justified (text_line_measure). Paragraph-final lines end naturally. */
static bool line_end_prohibited(uint32_t cp) {
    switch(cp) {
        case '(': case '[': case '{':
        case 0x2018: case 0x201c: case 0x3008: case 0x300a: case 0x300c: case 0x300e:
        case 0x3010: case 0x3014: case 0x3016: case 0x3018: case 0x301a:
        case 0xff08: case 0xff3b: case 0xff5b: return true;
        default: return false;
    }
}
static bool punct_squeezable(uint32_t cp) {
    switch(cp) {
        case 0x3001: case 0x3002: case 0xff0c: case 0xff0e: case 0xff1a: case 0xff1b:
        case 0x2019: case 0x201d: case 0x3009: case 0x300b: case 0x300d: case 0x300f:
        case 0x3011: case 0x3015: case 0x3017: case 0xff09: case 0xff3d: case 0xff5d: return true;
        default: return false;
    }
}
static bool punct_point(uint32_t cp) {return cp==0x3001 || cp==0x3002 || cp==0xff0c || cp==0xff0e;}
static bool punct_group(uint32_t cp) {return cp==0x2014 || cp==0x2026;}
/* Only full-width punctuation has an empty half to drop. */
static int punct_squeeze(const thingsos_font_t* f,uint32_t cp,int a) {
    return punct_squeezable(cp) && a*4>=(int)f->size*3 ? a/2 : 0;
}
#define TEXT_HANG_SAFE_PX 8
/* Right-margin borrowing for one point mark: at most half its advance and
   never closer than TEXT_HANG_SAFE_PX to the screen edge. Reader margins are
   symmetric, so the left gutter is also the right margin. */
static int punct_hang(int margin,int a) {
    int h=margin-TEXT_HANG_SAFE_PX;
    if(h<0)h=0;
    if(h>a/2)h=a/2;
    return h;
}
typedef struct {int reserve,count,squeeze,hang;bool group;} text_tail_t;
static text_tail_t text_tail(thingsos_font_t* f,const text_view_t* ch,size_t look,int a,int width,int margin) {
    text_tail_t t={a,0,0,0,false};uint32_t last=0;int last_advance=0;
    for(unsigned count=0;count<8 && look<ch->bytes;++count) {
        if(look>=ch->hidden_start && look<ch->hidden_end)break;
        unsigned n;uint32_t following=next(ch->text+look,ch->bytes-look,&n);
        if(!line_start_prohibited(following))break;
        bool boundary=false;
        for(size_t i=0;i<ch->image_count;++i)
            if(ch->images[i].offset==look){boundary=true;break;}
        for(size_t i=0;i<ch->footnote_count && !boundary;++i)
            if(look>=ch->footnotes[i].begin && look<ch->footnotes[i].end)boundary=true;
        if(boundary)break;
        int advance=thingsos_glyph(f,NULL,following,0,0);
        if(advance>width || t.reserve>width-advance)return (text_tail_t){a,0,0,0,false};
        t.reserve+=advance;++t.count;t.squeeze+=punct_squeeze(f,following,advance);
        if(punct_group(following))t.group=true;
        last=following;last_advance=advance;look+=n;
    }
    if(t.count==1 && punct_point(last))t.hang=punct_hang(margin,last_advance);
    return t;
}
/* True when the glyph at p starts a new line. force_keep counts closing
   marks already admitted to this line with the glyph before them. */
static bool clreq_wrap(thingsos_font_t* f,const text_view_t* ch,size_t p,unsigned bytes,uint32_t cp,int a,
                       int x,int width,int margin,bool paragraph_ink,int line_glyphs,int* force_keep) {
    if(*force_keep>0){--*force_keep;return false;}
    if(x+a>width)return true;
    if(!paragraph_ink || x<=0 || epub_paragraph_indent(cp) || line_start_prohibited(cp))return false;
    text_tail_t t=text_tail(f,ch,p+bytes,a,width,margin);
    if(t.count && x+t.reserve>width) {
        int over=x+t.reserve-width,room=t.squeeze+t.hang;
        if(t.group)room+=(line_glyphs+t.count)*(int)f->size/12;
        if(over>room)return true;
        *force_keep=t.count;
        return false;
    }
    if(line_end_prohibited(cp) && p+bytes<ch->bytes) {
        unsigned n;uint32_t following=next(ch->text+p+bytes,ch->bytes-p-bytes,&n);
        if(following!='\r' && following!='\n' && x+a+thingsos_glyph(f,NULL,following,0,0)>width)return true;
    }
    return false;
}
/* Measure the line whose first glyph (at p, logical x) was just placed, with
   the same clreq_wrap decisions as the layout. Produces the drawing offset:
   soft-wrapped lines spread their remaining width over the glyph gaps (or
   tighten when a punctuation group was kept), and trailing squeezable marks
   drop their empty halves. Pagination never depends on these offsets.
   Headings, images, hidden text and footnotes are not modelled: false. */
typedef struct {int slack,glyphs,tail;} text_line_t;
static bool text_line_measure(thingsos_font_t* f,const text_view_t* ch,size_t p,int x,int width,int margin,
                              bool paragraph_ink,int force_keep,text_line_t* out) {
    int n=0,squeezed=0,tail=-1,last_advance=0;uint32_t last=0;bool soft=false;
    while(p<ch->bytes && n<512) {
        if(p>=ch->hidden_start && p<ch->hidden_end)return false;
        if(ch->heading && p>=ch->heading_start && p<ch->heading_end)return false;
        if(n)for(size_t i=0;i<ch->image_count;++i)if(ch->images[i].offset==p)return false;
        unsigned bytes;uint32_t cp=next(ch->text+p,ch->bytes-p,&bytes);
        if(cp=='\r' || cp=='\n')break;
        for(size_t i=0;i<ch->footnote_count;++i)
            if(p<ch->footnotes[i].end && p+bytes>ch->footnotes[i].begin)return false;
        int a=thingsos_glyph(f,NULL,cp,0,0);
        if(n && clreq_wrap(f,ch,p,bytes,cp,a,x,width,margin,paragraph_ink,n,&force_keep)){soft=true;break;}
        int sq=punct_squeeze(f,cp,a);
        if(sq){if(tail<0)tail=n;squeezed+=sq;}else{tail=-1;squeezed=0;}
        last=cp;last_advance=a;
        x+=a;++n;
        if(cp!=' ' && cp!='\t')paragraph_ink=true;
        p+=bytes;
    }
    if(!n)return false;
    int content=x-squeezed,hang=0;
    if(tail==n-1 && punct_point(last) && content>width) {
        hang=punct_hang(margin,last_advance);
        if(hang>content-width)hang=content-width;
    }
    int slack=width+hang-content;
    if(!soft) {
        /* Paragraph or chapter end: natural unless punctuation was fitted. */
        if(x<=width)return false;
        if(slack>0)slack=0;
    }
    if(slack>3*(int)f->size)slack=0;
    out->slack=slack;out->glyphs=n;out->tail=tail<0?n:tail;
    return true;
}
static size_t text_view_page(
    thingsos_font_t* f, thingsos_canvas_t* c, const text_view_t* ch, size_t start, int left,
    int top, int width, int height, unsigned line_percent, unsigned indent_px,
    bool epub_paragraphs, thingsos_picture_fn picture, void* context, thingsos_text_hits_t* hits,
    thingsos_layout_continue_fn proceed, void* proceed_context, bool* completed,
    thingsos_text_cursor_t* cursor) {
    if (completed) *completed=true;
    bool resume=cursor && cursor->active;
    if(hits && !resume)hits->glyph_count=0;
    if (!f || !ch || !ch->text || start >= ch->bytes) return ch ? ch->bytes : 0;
    if (width <= 0 || height < (int)f->size) return start;
    thingsos_md_cursor_t* md=thingsos_markdown_cursor(ch->markdown,resume?cursor->offset:start);
    if(ch->markdown && !md){if(completed)*completed=false;return start;}
    unsigned original_weight=f->weight;bool original_italic=f->italic;
    size_t p = start;
    unsigned body_size=f->size, title_size=body_size<61?body_size+9:70;
    bool heading=!epub_paragraphs && ch->heading && p>=ch->heading_start && p<ch->heading_end;
    bool paragraph_ink=false;
    if(heading) thingsos_font_size(f,title_size);
    if (line_percent < 125 || line_percent > 200) line_percent = 145;
    /* Wrapped lines and paragraph starts share the same baseline grid. An
       extra per-paragraph pixel gap accumulates differently on each page and
       leaves its final body line at a different vertical position. */
    /* paragraph_head marks the next drawn glyph as the first of a paragraph,
       so only that line takes the first-line indent. A TXT chapter title is a
       heading, not a body paragraph, and stays at the gutter. */
    bool paragraph_head = indent_px && paragraph_begin(ch->text, start);
    int x = paragraph_head && !heading ? (int)indent_px : 0, line = text_line_advance(f,body_size,line_percent,height),
        y = top + (int)f->size, bottom = top + height;
    int checked_y=INT_MIN;
    /* Justification of the current line: drawing offset only (see text_line_slack). */
    int just_y=INT_MIN,just_slack=0,just_n=0,just_k=0,just_tail=0,just_squeezed=0;bool just_line=false;
    int line_glyphs=0,force_keep=0; /* clreq_wrap state for the current line */
    if(resume) {
        p=cursor->offset;x=cursor->x;y=cursor->y;line=cursor->line;
        heading=cursor->heading;paragraph_ink=cursor->paragraph_ink;paragraph_head=cursor->paragraph_head;
        just_y=cursor->just_y;just_slack=cursor->just_slack;just_n=cursor->just_n;just_k=cursor->just_k;just_line=cursor->just_line;
        just_tail=cursor->just_tail;just_squeezed=cursor->just_squeezed;line_glyphs=cursor->line_glyphs;force_keep=cursor->force_keep;
        thingsos_font_size(f,cursor->size);
    }
    bool paused=false;
    while (p < ch->bytes) {
        if(y!=checked_y) {
            if(proceed && !proceed(proceed_context)) {
                if(completed)*completed=false;
                paused=true;
                break;
            }
            checked_y=y;
        }
        if (p>=ch->hidden_start && p<ch->hidden_end) {p=ch->hidden_end;continue;}
        /* Reader chapter headings live in its fixed header. Keep original
           source offsets for bookmarks, pagination and pre-render cache. */
        if(epub_paragraphs && ch->heading && p>=ch->heading_start && p<ch->heading_end) {
            p=ch->heading_end;
            while(p<ch->bytes && (ch->text[p]=='\n'||ch->text[p]=='\r')) {
                bool anchor=false;
                for(size_t i=0;i<ch->image_count;++i)if(ch->images[i].offset==p)anchor=true;
                if(anchor)break;
                ++p;
            }
            /* Body text after the chapter title opens a paragraph. */
            paragraph_head = indent_px != 0;
            continue;
        }
        bool title=!epub_paragraphs && ch->heading && p>=ch->heading_start && p<ch->heading_end;
        if(title!=heading) {
            heading=title;thingsos_font_size(f,title?title_size:body_size);
        }
        const thingsos_inline_t* img = NULL;
        for (size_t i = 0; i < ch->image_count; i++)
            if (ch->images[i].offset == p) {
                img = ch->images + i;
                break;
            }
        if (img) {
            bool cover=ch->cover_image && img==ch->images+ch->cover_image-1;
            if(cover && !paragraph_ink && y==top+(int)body_size)y=top+12;
            int image_x=left,image_y=y,image_width=width,below=0;
            int picture_height=294,block=294+(int)body_size*2+38;
            bool show_caption=!cover && img->alt[0];
            if(!cover && epub_paragraphs) {
                /* Size against a fresh page. If the whole block cannot fit
                   after the preceding text, start it on the next page. */
                int caption=show_caption?line+16:0;
                if(caption>=height){caption=0;show_caption=false;}
                int limit=height-caption;
                thingsos_size_t fit={(unsigned)width,(unsigned)(limit<294?limit:294)};
                if(img->width && img->height)
                    thingsos_image_contain(f->vertical_text?img->height:img->width,
                        f->vertical_text?img->width:img->height,(unsigned)width,(unsigned)limit,&fit);
                image_y=!paragraph_ink && y==top+(int)body_size?top:y+(paragraph_ink?12:0);
                image_width=(int)fit.width;
                image_x=left+(width-image_width)/2;
                picture_height=(int)fit.height;
                block=picture_height+caption;
                below=(int)body_size+12;
            } else if(!cover && img->width && img->height) {
                int caption=img->alt[0]?line+16:0;
                int limit=height-2*(int)body_size-2*line-caption-12;
                if(limit<height/3)limit=height-2*(int)body_size-line-caption-12;
                if(limit<1)break;
                uint64_t natural=(uint64_t)(unsigned)width*(f->vertical_text?img->width:img->height)/
                    (f->vertical_text?img->height:img->width);
                picture_height=natural>(unsigned)limit?limit:natural?(int)natural:1;
                /* y is a text baseline. Reserve the next glyph's ascent too,
                   so a following title cannot paint into the image/hint. */
                block=picture_height+caption+(int)body_size+12;
            }
            if(cover) {
                /* A first-page cover owns the remaining reading area. Layout
                   and drawing share this calculation, at every font size. */
                int available=bottom-y;
                if(available<=24 || (available<height*3/4 && p>start))break;
                picture_height=available-12;block=available;
            }
            if (image_y + block > bottom) break;
            if (picture && c) picture(context, img, c, image_x, image_y, image_width, picture_height);
            if (show_caption && c) thingsos_label(f, c, img->alt, left, image_y + picture_height + (int)body_size + 8, width);
            y=image_y+block+below;
            x = 0;line_glyphs=0;force_keep=0;
            paragraph_ink=false;
            paragraph_head=false;
        }
        thingsos_md_run_t style={0};
        if(md) {
            if(!thingsos_markdown_style(md,p,&style)){if(completed)*completed=false;break;}
            if(style.flags&MD_HIDE){
                size_t end=style.end;
                for(size_t i=0;i<ch->image_count;++i)if(ch->images[i].offset>p && ch->images[i].offset<end)end=ch->images[i].offset;
                p=end;continue;
            }
            unsigned size=body_size+(style.flags&MD_H1?9:style.flags&MD_H2?6:style.flags&MD_H3?3:0);
            if(size>70)size=70;
            if(size!=f->size) {if(!paragraph_ink)y+=(int)size-(int)f->size;thingsos_font_size(f,size);}
            thingsos_font_weight(f,style.flags&MD_BOLD?2:original_weight);
            f->italic=(style.flags&MD_ITALIC)!=0;
            line=text_line_advance(f,size,line_percent,height);
            if(!paragraph_ink && (style.flags&(MD_H1|MD_H2|MD_H3|MD_CODE|MD_LIST|MD_QUOTE))) {
                x=style.flags&(MD_LIST|MD_QUOTE)?20:0;paragraph_head=false;
            }
            if((style.flags&(MD_QUOTE|MD_LIST)) && x<20)x=20;
        }
        unsigned bytes;
        uint32_t cp = next(ch->text + p, ch->bytes - p, &bytes);
        bool collapse_paragraphs = epub_paragraphs || (md && !(style.flags & MD_CODE));
        /* EPUBs often encode Chinese first-line indentation as literal U+3000
           spaces, which survive the XHTML markup parser. Drop only paragraph-
           leading whitespace so every paragraph starts at the shared gutter;
           with the indent setting on, local TXT keeps the same rule so the
           setting alone decides the first-line indent. Markdown outside fenced
           code blocks collapses blank lines the same way as EPUB so \n\n
           paragraph separators stay on the shared baseline grid. */
        if ((collapse_paragraphs || indent_px) && !(style.flags&MD_CODE) && !paragraph_ink && epub_paragraph_indent(cp)) {
            p += bytes;
            continue;
        }
        if (cp == '\r' || cp == '\n') {
            p += bytes;
            if (cp == '\r' && p < ch->bytes && ch->text[p] == '\n') ++p;
            paragraph_head = indent_px != 0;
            if (epub_paragraphs) {
                /* Do not skip image anchors, even when they point at whitespace. */
                while (p < ch->bytes && (ch->text[p] == '\n' || ch->text[p] == '\r' ||
                       ch->text[p] == ' ' || ch->text[p] == '\t')) {
                    bool anchor = false;
                    for (size_t i = 0; i < ch->image_count; ++i)
                        if (ch->images[i].offset == p) anchor = true;
                    if (anchor) break;
                    ++p;
                }
            }
            if (collapse_paragraphs && !paragraph_ink) continue;
            x = 0;line_glyphs=0;force_keep=0;
            paragraph_ink=false;
            y += line;
            if (y > bottom) break;
            continue;
        }
        if (indent_px && paragraph_head && !title && x == 0) x = (int)indent_px;
        bool note_marker = false;
        const thingsos_footnote_t* note = NULL;
        for (size_t i = 0; i < ch->footnote_count; ++i) {
            if (p < ch->footnotes[i].end && p + bytes > ch->footnotes[i].begin) {
                note_marker = true;
                note = ch->footnotes + i;
                break;
            }
        }
        /* A footnote marker is a superscript: two thirds of the body size, raised
           by a third, so it reads as a reference and is obvious on e-ink. */
        unsigned marker_size = body_size, marker_rise = 0;
        if (note_marker && body_size > 24) {
            marker_size = body_size * 2 / 3;
            marker_rise = body_size / 3;
        }
        int a;
        if (note_marker && marker_rise) {
            thingsos_font_size(f, marker_size);
            a = thingsos_glyph(f, NULL, cp, 0, 0);
            thingsos_font_size(f, body_size);
        } else
            a = thingsos_glyph(f, NULL, cp, 0, 0);
        bool code_tab=cp=='\t' && (style.flags&MD_CODE);
        if(code_tab){int cell=thingsos_glyph(f,NULL,' ',0,0);if(cell<1)cell=(int)body_size/2;int stop=cell*4;a=stop-x%stop;}
        /* Horizontal reader body text: clreq line breaking. Markdown, UI text,
           vertical flow, headings and footnote markers keep the reserve rule. */
        bool clreq=ch->justify && !f->vertical_text && !md && !title && !note_marker;
        bool wrap;
        if(clreq) wrap=clreq_wrap(f,ch,p,bytes,cp,a,x,width,left,paragraph_ink,line_glyphs,&force_keep);
        else {
            int reserve=a;
            if(!note_marker && !(style.flags&MD_CODE) && !epub_paragraph_indent(cp) &&
               !line_start_prohibited(cp))
                /* No backward move of the streaming Markdown cursor: stop at the style run. */
                reserve=text_reserve(f,ch,p+bytes,md?style.end:ch->bytes,a,width);
            int line_left=md && (style.flags&(MD_QUOTE|MD_LIST))?20:0;
            bool keep_end=paragraph_ink && x>line_left && reserve<=width-line_left && x>width-reserve;
            wrap=x + a > width || keep_end;
            force_keep=0;
        }
        if (wrap) {
            x = md && (style.flags&(MD_QUOTE|MD_LIST))?20:0;
            y += line;line_glyphs=0;
        }
        if (y > bottom) break;
        /* First glyph of a body line: measure it once. A soft-wrapped line
           (notably one that ends early to keep closing punctuation with its
           character) spreads the leftover width over its glyph gaps, so the
           right edge stays flush. Logical x and every wrap decision are
           unchanged; only the drawing position gx moves. */
        if(y!=just_y) {
            just_y=y;just_k=0;just_squeezed=0;
            text_line_t measured;
            just_line=clreq && text_line_measure(f,ch,p,x,width,left,paragraph_ink,force_keep,&measured);
            if(just_line){just_slack=measured.slack;just_n=measured.glyphs;just_tail=measured.tail;}
        }
        int gx=x;
        if(just_line) {
            int k=just_k<just_n?just_k:just_n-1;
            if(just_n>1)gx+=(int)((int64_t)just_slack*k/(just_n-1));
            gx-=just_squeezed;
            if(just_k>=just_tail)just_squeezed+=punct_squeeze(f,cp,a);
            ++just_k;
        }
        ++line_glyphs;
        if (c) {
            if (marker_rise) thingsos_font_size(f, marker_size);
            if(style.flags&MD_CODE)thingsos_rect(c,left+gx,y-(int)f->size,a,line,235);
            if(style.flags&MD_QUOTE)thingsos_rect(c,left+4,y-(int)f->size,3,line,100);
            if(!code_tab)thingsos_glyph(f, c, cp, left + gx, y - (int)marker_rise);
            if(style.flags&MD_LINK)thingsos_rect(c,left+gx,y+3,a,1,100);
            if (marker_rise) thingsos_font_size(f, body_size);
            unsigned underline=0;
            if (!note_marker)
                for(size_t i=0;i<ch->underline_count;++i)if(p<ch->underlines[i].end && p+bytes>ch->underlines[i].begin){
                    unsigned style=ch->underlines[i].mine?2:1;if(style>underline)underline=style;
                }
            int underline_offset=line-(int)f->size-4;
            if(underline_offset>10)underline_offset=10;
            if(underline_offset<1)underline_offset=1;
            if(underline)for(int xx=left+gx;xx<left+gx+a;++xx)if(underline==2 || xx%7<3)
                thingsos_rect(c,xx,y+underline_offset,1,3,0);
            /* Dotted rule right under the raised marker: the link affordance. */
            if(note_marker)for(int xx=left+gx;xx<left+gx+a;++xx)if(xx%4<2)
                thingsos_rect(c,xx,y+6-(int)marker_rise,1,2,0);
        }
        if(hits && !note_marker && a>0 && hits->glyph_count<THINGSOS_GLYPH_HIT_LIMIT) {
            hits->glyphs[hits->glyph_count++]=(thingsos_glyph_hit_t){(uint32_t)p,(uint16_t)bytes,
                (int16_t)(left+gx),(int16_t)(y-(int)f->size),(int16_t)a,(int16_t)(f->size+(f->vertical_text?0:10))};
        }
        if(hits && !note_marker && a>0)for(size_t i=0;i<ch->underline_count;++i) {
            const thingsos_text_underline_t* u=ch->underlines+i;
            if(p>=u->end || p+bytes<=u->begin)continue;
            int upper=y-(int)f->size-3,lower=y+10;
            if(upper<top)upper=top;
            if(lower>bottom)lower=bottom;
            if(lower<=upper)continue;
            thingsos_text_hit_t* hit=NULL;
            for(size_t j=0;j<hits->line_count;++j)if(hits->lines[j].begin==u->begin && hits->lines[j].y==upper){hit=hits->lines+j;break;}
            if(hit){int right=left+gx+a+3;if(right>hit->x+hit->w)hit->w=right-hit->x;}
            else if(hits->line_count<THINGSOS_LINE_HIT_LIMIT)hits->lines[hits->line_count++]=(thingsos_text_hit_t){u->begin,u->end,left+gx-3,upper,a+6,lower-upper};
        }
        if (note)
            text_hit_add(hits, note, left + gx - 6,
                         y - (int)marker_rise - (int)marker_size - 4, a + 12,
                         (int)marker_rise + (int)marker_size + 10, top, bottom);
        x += a;
        if(cp!=' ' && cp!='\t') {paragraph_ink=true;paragraph_head=false;}
        p += bytes;
    }
    if(cursor) *cursor=(thingsos_text_cursor_t){p,x,y,line,f->size,paused,heading,paragraph_ink,paragraph_head,
        just_y,just_slack,just_n,just_k,just_line,just_tail,just_squeezed,line_glyphs,force_keep};
    thingsos_font_size(f,body_size);thingsos_font_weight(f,original_weight);f->italic=original_italic;
    thingsos_markdown_cursor_close(md);
    return p;
}
#include "vertical_text.inc"
size_t thingsos_text_page_resume_flow(thingsos_font_t* f,thingsos_canvas_t* c,const thingsos_chapter_t* ch,size_t start,int left,int top,int width,int height,unsigned line_percent,unsigned indent_px,bool epub_paragraphs,thingsos_picture_fn picture,void* context,thingsos_text_hits_t* hits,thingsos_layout_continue_fn proceed,void* proceed_context,bool* completed,thingsos_text_cursor_t* cursor,bool vertical){
    if(!ch){if(completed)*completed=true;return 0;}
    text_view_t view={ch->text,ch->bytes,ch->images,ch->image_count,ch->heading_start,ch->heading_end,ch->hidden_start,ch->hidden_end,ch->heading,ch->underlines,ch->underline_count,ch->footnotes,ch->footnote_count,ch->markdown_index?ch:NULL,ch->cover_image,true};
    if(vertical && f)return vertical_text_page(f,c,&view,start,left,top,width,height,line_percent,indent_px,epub_paragraphs,picture,context,hits,proceed,proceed_context,completed,cursor);
    return text_view_page(f,c,&view,start,left,top,width,height,line_percent,indent_px,epub_paragraphs,picture,context,hits,proceed,proceed_context,completed,cursor);
}
size_t thingsos_text_page_resume(thingsos_font_t* f,thingsos_canvas_t* c,const thingsos_chapter_t* ch,size_t start,int left,int top,int width,int height,unsigned line_percent,unsigned indent_px,bool epub_paragraphs,thingsos_picture_fn picture,void* context,thingsos_text_hits_t* hits,thingsos_layout_continue_fn proceed,void* proceed_context,bool* completed,thingsos_text_cursor_t* cursor){
    return thingsos_text_page_resume_flow(f,c,ch,start,left,top,width,height,line_percent,indent_px,epub_paragraphs,picture,context,hits,proceed,proceed_context,completed,cursor,false);
}
size_t thingsos_text_page_layout_flow(thingsos_font_t* f,thingsos_canvas_t* c,const thingsos_chapter_t* ch,size_t start,int left,int top,int width,int height,unsigned line_percent,unsigned indent_px,bool epub_paragraphs,thingsos_picture_fn picture,void* context,thingsos_text_hits_t* hits,bool vertical){
    return thingsos_text_page_resume_flow(f,c,ch,start,left,top,width,height,line_percent,indent_px,epub_paragraphs,picture,context,hits,NULL,NULL,NULL,NULL,vertical);
}
size_t thingsos_text_page_interruptible(thingsos_font_t* f,thingsos_canvas_t* c,const thingsos_chapter_t* ch,size_t start,int left,int top,int width,int height,unsigned line_percent,unsigned indent_px,bool epub_paragraphs,thingsos_picture_fn picture,void* context,thingsos_text_hits_t* hits,thingsos_layout_continue_fn proceed,void* proceed_context,bool* completed){
    return thingsos_text_page_resume(f,c,ch,start,left,top,width,height,line_percent,indent_px,epub_paragraphs,picture,context,hits,proceed,proceed_context,completed,NULL);
}
size_t thingsos_text_string_page(thingsos_font_t* f,thingsos_canvas_t* c,const char* text,size_t bytes,size_t start,int left,int top,int width,int height,unsigned line_percent){
    text_view_t view={0};view.text=text;view.bytes=bytes;
    return text_view_page(f,c,&view,start,left,top,width,height,line_percent,0,false,NULL,NULL,NULL,NULL,NULL,NULL,NULL);
}
size_t thingsos_text_page_layout(
    thingsos_font_t* f, thingsos_canvas_t* c, const thingsos_chapter_t* ch, size_t start,
    int left,int top,int width,int height,unsigned line_percent,unsigned indent_px,
    bool epub_paragraphs,thingsos_picture_fn picture,void* context,thingsos_text_hits_t* hits) {
    return thingsos_text_page_interruptible(f,c,ch,start,left,top,width,height,line_percent,
        indent_px,epub_paragraphs,picture,context,hits,NULL,NULL,NULL);
}
size_t thingsos_text_page(
    thingsos_font_t* f, thingsos_canvas_t* c, const thingsos_chapter_t* ch, size_t start,
    int left, int top, int width, int height, thingsos_picture_fn picture, void* context) {
    return thingsos_text_page_layout(f, c, ch, start, left, top, width, height, 175, 0, false,
                                   picture, context, NULL);
}

/* GC16 only holds pure black (nibble 0) with zero trailing white pull-up pulses;
   map near-black artwork and printed text inks into nibble 0 and paper whites
   to 255 while keeping midtone curves readable on the E0470 panel. */
static const unsigned char epd_tone_lut[256] = {
      0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
      0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
      0,  0,  0,  0,  0,  0,  0,  0,  1,  1,  1,  1,  1,  1,  2,  2,
      2,  2,  2,  3,  3,  3,  4,  4,  4,  5,  5,  5,  6,  6,  7,  7,
      7,  8,  8,  9,  9, 10, 10, 11, 11, 12, 12, 13, 14, 14, 15, 15,
     16, 17, 17, 18, 19, 19, 20, 21, 21, 22, 23, 24, 24, 25, 26, 27,
     28, 28, 29, 30, 31, 32, 33, 34, 35, 36, 36, 37, 38, 39, 40, 41,
     42, 43, 44, 45, 46, 47, 48, 50, 51, 52, 53, 54, 55, 56, 57, 59,
     60, 61, 62, 63, 65, 66, 67, 68, 70, 71, 72, 73, 75, 76, 77, 79,
     80, 81, 83, 84, 86, 87, 88, 90, 91, 93, 94, 96, 97, 99,100,102,
    103,105,106,108,109,111,113,114,116,117,119,121,122,124,126,127,
    129,131,133,134,136,138,140,141,143,145,147,149,150,152,154,156,
    158,160,162,163,165,167,169,171,173,175,177,179,181,183,185,187,
    189,191,193,195,197,199,202,204,206,208,210,212,214,217,219,221,
    223,225,228,230,232,234,237,239,241,243,246,248,250,253,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255
};

unsigned char thingsos_image_tone_epd(unsigned char gray) {
    return epd_tone_lut[gray];
}

void thingsos_image_paint_clipped(
    thingsos_canvas_t* c, const unsigned char* pixels, unsigned w, unsigned h, int x, int y,
    unsigned ow, unsigned oh, int clip_x0, int clip_y0, int clip_x1, int clip_y1, bool smooth,
    bool reader_tone) {
    if (!c || !c->pixels || c->width <= 0 || c->height <= 0 || !pixels || !w || !h || !ow || !oh)
        return;
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int x1 = x + (int)ow, y1 = y + (int)oh;
    if (clip_x0 > x0) x0 = clip_x0;
    if (clip_y0 > y0) y0 = clip_y0;
    if (clip_x1 < x1) x1 = clip_x1;
    if (clip_y1 < y1) y1 = clip_y1;
    if (x1 > c->width) x1 = c->width;
    if (y1 > c->height) y1 = c->height;
    if (x0 >= x1 || y0 >= y1) return;
    unsigned polarity = c->inverted ? 255u : 0u;
    if (w == ow && h == oh) {
        for (int dy = y0; dy < y1; ++dy) {
            unsigned char* dest = c->pixels + (size_t)dy * c->width + x0;
            const unsigned char* src = pixels + (size_t)(dy - y) * w + (x0 - x);
            if (!reader_tone && !polarity) memcpy(dest, src, (size_t)(x1 - x0));
            else for (int i = 0; i < x1 - x0; ++i) {
                unsigned char v = reader_tone ? epd_tone_lut[src[i]] : src[i];
                dest[i] = (unsigned char)(v ^ polarity);
            }
        }
        return;
    }
    if (smooth && (ow < w || oh < h)) {
        for (int dy = y0; dy < y1; ++dy) {
            unsigned yy = (unsigned)(dy - y);
            unsigned sy0 = (unsigned)((uint64_t)yy * h / oh);
            unsigned sy1 = (unsigned)(((uint64_t)(yy + 1) * h) / oh);
            if (sy1 <= sy0) sy1 = sy0 + 1;
            if (sy1 > h) sy1 = h;
            unsigned char* dest = c->pixels + (size_t)dy * c->width + x0;
            for (int dx = x0; dx < x1; ++dx) {
                unsigned xx = (unsigned)(dx - x);
                unsigned sx0 = (unsigned)((uint64_t)xx * w / ow);
                unsigned sx1 = (unsigned)(((uint64_t)(xx + 1) * w) / ow);
                if (sx1 <= sx0) sx1 = sx0 + 1;
                if (sx1 > w) sx1 = w;
                unsigned sum = 0;
                unsigned count = (sx1 - sx0) * (sy1 - sy0);
                unsigned char cmin = 255, cmax = 0;
                for (unsigned sy = sy0; sy < sy1; ++sy) {
                    const unsigned char* row = pixels + (size_t)sy * w;
                    for (unsigned sx = sx0; sx < sx1; ++sx) {
                        unsigned char p = row[sx];
                        sum += p;
                        if (p < cmin) cmin = p;
                        if (p > cmax) cmax = p;
                    }
                }
                unsigned avg = (sum + count / 2u) / count;
                unsigned contrast = (unsigned)(cmax - cmin);
                if (reader_tone && contrast >= 48u && avg < 232u &&
                    (unsigned)(cmax - avg) <= contrast * 3u / 4u) {
                    unsigned dark_frac = (unsigned)(cmax - avg) * 255u / contrast;
                    unsigned pull = dark_frac * 3u;
                    if (pull > 216u) pull = 216u;
                    avg = (avg * (255u - pull) + (unsigned)cmin * pull + 127u) / 255u;
                }
                unsigned char v = reader_tone ? epd_tone_lut[avg] : (unsigned char)avg;
                *dest++ = (unsigned char)(v ^ polarity);
            }
        }
        return;
    }
    if (smooth) {
        uint64_t stride = (uint64_t)w * 256;
        uint64_t start = (unsigned)(x0 - x) * stride + stride / 2;
        uint64_t step = stride / ow, step_remainder = stride % ow;
        uint64_t start_x = start / ow, start_remainder = start % ow;
        for (int dy = y0; dy < y1; ++dy) {
            uint64_t fy = ((uint64_t)(dy - y) * 256 + 128) * h / oh;
            fy = fy > 128 ? fy - 128 : 0;
            unsigned sy = (unsigned)(fy >> 8), ay = (unsigned)(fy & 255);
            if (sy >= h) sy = h - 1;
            const unsigned char* top = pixels + (size_t)sy * w;
            const unsigned char* bottom = sy + 1 < h ? top + w : top;
            unsigned char* dest = c->pixels + (size_t)dy * c->width + x0;
            uint64_t center = start_x, remainder = start_remainder;
            for (int dx = x0; dx < x1; ++dx) {
                uint64_t fx = center > 128 ? center - 128 : 0;
                unsigned sx = (unsigned)(fx >> 8), ax = (unsigned)(fx & 255);
                if (sx >= w) sx = w - 1;
                unsigned next = sx + 1 < w ? sx + 1 : sx;
                unsigned a = top[sx] * (256 - ax) + top[next] * ax;
                unsigned b = bottom[sx] * (256 - ax) + bottom[next] * ax;
                unsigned px = (a * (256 - ay) + b * ay + 32768) >> 16;
                unsigned char v = reader_tone ? epd_tone_lut[px] : (unsigned char)px;
                *dest++ = (unsigned char)(v ^ polarity);
                center += step;
                remainder += step_remainder;
                if (remainder >= ow) { remainder -= ow; ++center; }
            }
        }
        return;
    }
    for (int dy = y0; dy < y1; ++dy) {
        unsigned yy = (unsigned)(dy - y);
        unsigned sy = (unsigned)((uint64_t)yy * h / oh);
        const unsigned char* row = pixels + (size_t)sy * w;
        unsigned char* dest = c->pixels + (size_t)dy * c->width + x0;
        for (int dx = x0; dx < x1; ++dx) {
            unsigned xx = (unsigned)(dx - x);
            unsigned sx = (unsigned)((uint64_t)xx * w / ow);
            unsigned char px = row[sx];
            *dest++ = (unsigned char)((reader_tone ? epd_tone_lut[px] : px) ^ polarity);
        }
    }
}
