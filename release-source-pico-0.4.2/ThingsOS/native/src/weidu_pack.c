/* SPDX-License-Identifier: Apache-2.0
 * Streaming, stored ZIP writer and conservative XHTML normalization.
 * Retains bounded anchor/id attributes, note body boundaries and superscript
 * markers; drops styles, executable markup and external navigation attributes.
 */
#include <ctype.h>
#include <errno.h>
#include <sys/stat.h>
#include "thingsos/diagnostics.h"
#include <stdlib.h>
#include <string.h>
#include "thingsos/weidu.h"
#include "thingsos/weidu_stream.h"
#include "thingsos/content.h"
#include "thingsos/mem.h"
typedef char toc_record_size_must_remain_264[(sizeof(thingsos_weidu_chapter_t)==264)?1:-1];
#include "zlib.h"
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif
static bool pack_heap(const char* stage,unsigned index) {
#ifdef ESP_PLATFORM
    /* Whole-heap walks cover PSRAM too. Check at bounded milestones, not
       twice per chapter while normalizing a full book. */
    if(strcmp(stage,"begin") && strcmp(stage,"flushed") &&
       (strcmp(stage,"normalized") || index%16))return true;
    bool ok=heap_caps_check_integrity_all(true);
    if(!ok || !(index%8))ESP_LOGI("ThingsOS_pack","Heap %s item=%u valid=%d stack=%u DMA=%u",stage,index,ok,
        (unsigned)uxTaskGetStackHighWaterMark(NULL),(unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL));
    return ok;
#else
    (void)stage;(void)index;return true;
#endif
}
typedef struct {
    char name[64];
    uint32_t bytes, crc, offset;
} zip_entry_t;
static bool put16(FILE* f, uint16_t n) {
    /* Use the same bulk writer as ZIP payloads. The target libc's byte writer
       stores before checking capacity; fwrite can leave a full buffer behind. */
    const unsigned char bytes[2] = { (unsigned char)n, (unsigned char)(n >> 8) };
    return fwrite(bytes, 1, sizeof(bytes), f) == sizeof(bytes);
}
static bool put32(FILE* f, uint32_t n) {
    return put16(f, (uint16_t)n) && put16(f, (uint16_t)(n >> 16));
}
static int put_byte(int value, FILE* f) {
    unsigned char byte = (unsigned char)value;
    return fwrite(&byte, 1, 1, f) == 1 ? byte : EOF;
}
static int put_text(const char* text, FILE* f) {
    size_t n = strlen(text);
    return fwrite(text, 1, n, f) == n ? 0 : EOF;
}
static bool escaped(FILE* f, const char* s) {
    for (; *s; ++s) {
        const char* entity = *s == '&'   ? "&amp;"
                             : *s == '<' ? "&lt;"
                             : *s == '>' ? "&gt;"
                             : *s == '"' ? "&quot;"
                                         : NULL;
        if (entity) {
            if (put_text(entity, f) == EOF) return false;
        } else if (put_byte((unsigned char)*s, f) == EOF)
            return false;
    }
    return true;
}
/* Preserve source entities, while safely changing single-quoted attributes to
   double-quoted XHTML. Entity decoding is owned by the EPUB reader. */
static bool note_attribute(FILE* f,const char* s) {
    for(;*s;++s) {
        if(*s=='"'){if(put_text("&quot;",f)==EOF)return false;}
        else if(*s=='<'){if(put_text("&lt;",f)==EOF)return false;}
        else if(put_byte((unsigned char)*s,f)==EOF)return false;
    }
    return true;
}
static bool utf8(FILE* f, uint32_t cp) {
    if (!cp || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return false;
    char s[5] = { 0 };
    unsigned n = cp < 128 ? 1 : cp < 2048 ? 2 : cp < 65536 ? 3 : 4;
    for (unsigned i = n - 1; i > 0; --i) {
        s[i] = (char)(128 | (cp & 63));
        cp >>= 6;
    }
    s[0] = (char)((n == 1 ? 0 : n == 2 ? 192 : n == 3 ? 224 : 240) | cp);
    return escaped(f, s);
}
/* Case-insensitive prefix test: attribute names arrive in either case and this
   must compile on both the device and the MSVC host build. */
static bool prefix_ci(const char* text, const char* want) {
    for (; *want; ++text, ++want)
        if (!*text || tolower((unsigned char)*text) != tolower((unsigned char)*want)) return false;
    return true;
}
/* Keep only bounded, quoted structural attributes. Match complete names:
   data-id and data-href must never be mistaken for real anchor attributes. */
static bool tag_attr(const char* tag,const char* key,char* out,size_t cap) {
    const char* p=tag;while(*p && !isspace((unsigned char)*p))++p;
    while(*p) {
        while(isspace((unsigned char)*p))++p;
        const char* name=p;while(*p && !isspace((unsigned char)*p) && *p!='=')++p;
        size_t len=(size_t)(p-name);while(isspace((unsigned char)*p))++p;
        if(*p!='='){if(p==name && *p)++p;continue;}++p;while(isspace((unsigned char)*p))++p;
        if(*p!='\'' && *p!='"'){while(*p && !isspace((unsigned char)*p))++p;continue;}
        char quote=*p++;const char* value=p;while(*p && *p!=quote)++p;
        size_t bytes=(size_t)(p-value);if(!*p)return false;++p;
        if(len==strlen(key) && prefix_ci(name,key)) {
            if(!bytes || bytes>=cap)return false;
            memcpy(out,value,bytes);out[bytes]=0;return true;
        }
    }
    return false;
}
static bool tag_href(const char* tag,char* out,size_t cap) {
    return (tag_attr(tag,"href",out,cap) || tag_attr(tag,"xlink:href",out,cap)) &&
        strchr(out,'#') && !strchr(out,':') && out[0]!='/';
}
static bool note_container(const char* name) {
    return !strcmp(name,"p") || !strcmp(name,"div") || !strcmp(name,"li") ||
        !strcmp(name,"aside") || !strcmp(name,"section") || !strcmp(name,"span") ||
        !strcmp(name,"td") || !strcmp(name,"ol") || !strcmp(name,"ul") || !strcmp(name,"sup");
}
/* Paragraph break; label elements are emitted inline at the note's position. */
static bool plain_body(FILE* in,FILE* out,const thingsos_weidu_control_t* control) {
    unsigned char* block=thingsos_psram_alloc(16384);if(!block)return false;
    bool ok=true;size_t n;
    while(ok && !thingsos_weidu_stopped(control) && (n=fread(block,1,16384,in))) {
        size_t start=0;
        for(size_t i=0;i<n && ok;++i) {
            unsigned char c=block[i];
            if(c>=32 && c!='&' && c!='<' && c!='>' && c!='"')continue;
            if(i>start)ok=fwrite(block+start,1,i-start,out)==i-start;
            if(ok && c=='\n')ok=put_text("</p><p>",out)!=EOF;
            else if(ok && c=='\t')ok=put_byte(c,out)!=EOF;
            else if(ok && c>=32){char text[2]={(char)c,0};ok=escaped(out,text);}
            start=i+1;
        }
        if(ok && n>start)ok=fwrite(block+start,1,n-start,out)==n-start;
        long position=ftell(out);if(position<0 || position>512*1024-64)ok=false;
    }
    ok=ok && !ferror(in) && !thingsos_weidu_stopped(control);free(block);return ok;
}
static bool normalize(const char* source, const char* output, const char* title, bool plain, bool cover,
    const char** reason,const thingsos_weidu_control_t* control,thingsos_weidu_image_cache_fn image,void* image_context) {
    *reason="normalize-write";
    errno=0;
    FILE *in=fopen(source,"rb");
    if(!in){*reason="normalize-open-input";thingsos_diag_event(DIAG_PACK,1,0,0,0,errno);return false;}
    FILE *out=output?fopen(output,"wb"):NULL;
    if(output && !out){int error=errno;fclose(in);*reason="normalize-open-output";thingsos_diag_event(DIAG_PACK,2,0,0,0,error);return false;}
    struct stat source_info={0};
    unsigned source_bytes=fstat(fileno(in),&source_info)==0?(unsigned)source_info.st_size:0;
    errno=0;
    unsigned failure_stage=4;
    int saved_error=0;
    char* input_buffer=thingsos_psram_alloc(16384),*output_buffer=out?thingsos_psram_alloc(16384):NULL;
    if(input_buffer && setvbuf(in,input_buffer,_IOFBF,16384)) {free(input_buffer);input_buffer=NULL;}
    if(out && output_buffer && setvbuf(out,output_buffer,_IOFBF,16384)) {free(output_buffer);output_buffer=NULL;}
    const size_t tag_capacity=16384,url_capacity=1152;
    char* tag_buffer=plain?NULL:thingsos_psram_alloc(tag_capacity+(image?url_capacity:0));
    if(!plain && !tag_buffer){*reason="normalize-alloc";failure_stage=3;}
    unsigned iterations=0,input_steps=0,images=0;
    bool ok = (plain || tag_buffer);
    if(ok && out)ok=put_text(
                  "<?xml version=\"1.0\" encoding=\"UTF-8\"?><html "
                  "xmlns=\"http://www.w3.org/1999/xhtml\"><body><h1>",
                  out) != EOF &&
              escaped(out, title) && put_text("</h1><p>", out) != EOF;
    if(ok && out && cover)ok=put_text("<img src=\"cover.jpg\" alt=\"封面\"/>",out)!=EOF;
    char skip[24] = { 0 };
    unsigned skip_depth = 0;
    bool notes = !plain; /* Targets can live in another chapter with no local reference. */
    bool note_link=false;
    int c;
    if(ok && plain)ok=out?plain_body(in,out,control):true;
    while (!plain && ok && (c = fgetc(in)) != EOF) {
        if(!(++input_steps%2048) && thingsos_weidu_stopped(control)){ok=false;break;}
        if (c == '<' && !plain) {
            /* Only the tag name is used: discard attributes while scanning
               their quotes and the closing delimiter. Inline styles/data
               URLs can be much longer than our fixed prefix buffer. */
            char* tag=tag_buffer;
            size_t n = 0;
            int quote = 0, last = 0;
            while ((c = fgetc(in)) != EOF) {
                if(!(++input_steps%2048) && thingsos_weidu_stopped(control)){ok=false;break;}
                if (quote) {
                    if (c == quote) quote = 0;
                } else if (c == '\'' || c == '"')
                    quote = c;
                else if (c == '>')
                    break;
                if (n + 1 < tag_capacity) tag[n++] = (char)c;
                if (!isspace((unsigned char)c)) last = c;
            }
            if (!ok || c == EOF) {
                if(c==EOF){*reason="normalize-tag-eof";failure_stage=5;}
                ok = false;
                break;
            }
            tag[n] = 0;
            char* p = tag;
            while (isspace((unsigned char)*p)) ++p;
            bool close = *p == '/';
            if (close) ++p;
            char name[24];
            size_t k = 0;
            while (isalnum((unsigned char)*p) && k + 1 < sizeof(name))
                name[k++] = (char)tolower((unsigned char)*p++);
            name[k] = 0;
            if (skip_depth) {
                if (!strcmp(skip, name)) {
                    if (close)
                        --skip_depth;
                    else if (last != '/')
                        ++skip_depth;
                }
                continue;
            }
            if (!close && (!strcmp(name, "head") || !strcmp(name, "script") || !strcmp(name, "style") ||
                           (!strcmp(name, "sup") && !notes) || !strcmp(name, "nav"))) {
                if (last != '/') {
                    strcpy(skip, name);
                    skip_depth = 1;
                }
                continue;
            }
            if (notes) {
                char target[256],id[160],kind[96];
                /* WeRead stores some annotations inside an image's alt, rather
                   than a link. Retain this data, without fetching the icon. */
                if(!close && !strcmp(name,"img") && tag_attr(tag,"class",kind,sizeof(kind)) && strstr(kind,"qqreader-footnote")) {
                    if(!out)continue;
                    char* note=thingsos_psram_alloc(8192);
                    if(!note){*reason="normalize-alloc-note";failure_stage=3;ok=false;break;}
                    if(tag_attr(tag,"alt",note,8192))ok=put_text("<img class=\"qqreader-footnote\" alt=\"",out)!=EOF && note_attribute(out,note) && put_text("\"/>",out)!=EOF;
                    free(note);continue;
                }
                if(!close && !strcmp(name,"span") && tag_attr(tag,"class",kind,sizeof(kind)) && strstr(kind,"reader_footer_note")) {
                    if(!out)continue;
                    char* note=thingsos_psram_alloc(8192);
                    if(!note){*reason="normalize-alloc-note";failure_stage=3;ok=false;break;}
                    if(tag_attr(tag,"data-wr-footernote",note,8192))ok=put_text("<span class=\"reader_footer_note\" data-wr-footernote=\"",out)!=EOF && note_attribute(out,note) && put_text("\">",out)!=EOF;
                    free(note);continue;
                }
                if (!strcmp(name,"a")) {
                    if(close){note_link=false;if(out)ok=put_text("</a>",out)!=EOF;}
                    else {
                        bool note_ref=(tag_attr(tag,"epub:type",kind,sizeof(kind)) && strstr(kind,"noteref")) ||
                            (tag_attr(tag,"role",kind,sizeof(kind)) && strstr(kind,"noteref")) ||
                            (tag_attr(tag,"class",kind,sizeof(kind)) && (strstr(kind,"footnote") || strstr(kind,"noteref")));
                        note_link=note_ref && last!='/';
                        if(out) {
                            ok=put_text("<a",out)!=EOF;
                            if(tag_href(tag,target,sizeof(target)))ok=ok && put_text(" href=\"",out)!=EOF && escaped(out,target) && put_text("\"",out)!=EOF;
                            if(tag_attr(tag,"id",id,sizeof(id)) || tag_attr(tag,"name",id,sizeof(id)))ok=ok && put_text(" id=\"",out)!=EOF && escaped(out,id) && put_text("\"",out)!=EOF;
                            if(note_ref)ok=ok && put_text(" role=\"doc-noteref\"",out)!=EOF;
                            ok=ok && put_text(last=='/'?"/>":">",out)!=EOF;
                        }
                    }
                    continue;
                }
                if(image && !close && (!strcmp(name,"img") || !strcmp(name,"image"))) {
                    if(!note_link && images<THINGSOS_IMAGE_LIMIT &&
                       !(tag_attr(tag,"class",kind,sizeof(kind)) && (strstr(kind,"footnote") || strstr(kind,"noteref")))) {
                        static const char* const sources[]={"data-src","src","href","xlink:href"};
                        char* url=tag_buffer+tag_capacity;
                        char local[41];
                        for(size_t i=0;i<sizeof(sources)/sizeof(sources[0]);++i) {
                            if(!tag_attr(tag,sources[i],url,url_capacity) || !image(image_context,url,local))continue;
                            ++images;
                            if(out) {
                                ok=put_text("<img src=\"",out)!=EOF && put_text(local,out)!=EOF;
                                if(tag_attr(tag,"alt",id,sizeof(id)))
                                    ok=ok && put_text("\" alt=\"",out)!=EOF && note_attribute(out,id);
                                ok=ok && put_text("\"/>",out)!=EOF;
                            }
                            break;
                        }
                    }
                    continue;
                }
                if(note_container(name)) {
                    if(out) {
                        ok=put_text(close?"</":"<",out)!=EOF && put_text(name,out)!=EOF;
                        if(!close && (tag_attr(tag,"id",id,sizeof(id)) || tag_attr(tag,"name",id,sizeof(id))))ok=ok && put_text(" id=\"",out)!=EOF && escaped(out,id) && put_text("\"",out)!=EOF;
                        ok=ok && put_text(!close && last=='/'?"/>":">",out)!=EOF;
                        if(close && (!strcmp(name,"p") || !strcmp(name,"div") || !strcmp(name,"li")))ok=ok && put_text("<br/>",out)!=EOF;
                    }
                    continue;
                }
            }
            if (out && (!strcmp(name, "br") || !strcmp(name, "p") || !strcmp(name, "div") ||
                !strcmp(name, "li") ||
                (name[0] == 'h' && name[1] >= '1' && name[1] <= '6' && !name[2])))
                ok = put_text("</p><p>", out) != EOF;
            continue;
        }
        if (skip_depth || !out) continue;
        if (c == '&' && !plain) {
            char entity[32];
            size_t n = 0;
            int next;
            while ((next = fgetc(in)) != EOF && next != ';' && n + 1 < sizeof(entity) &&
                   !isspace((unsigned char)next) && next != '<')
                entity[n++] = (char)next;
            entity[n] = 0;
            if (next == ';') {
                if (!strcmp(entity, "amp"))
                    ok = escaped(out, "&");
                else if (!strcmp(entity, "lt"))
                    ok = escaped(out, "<");
                else if (!strcmp(entity, "gt"))
                    ok = escaped(out, ">");
                else if (!strcmp(entity, "quot"))
                    ok = escaped(out, "\"");
                else if (!strcmp(entity, "apos"))
                    ok = escaped(out, "'");
                else if (!strcmp(entity, "nbsp"))
                    ok = escaped(out, " ");
                else if (entity[0] == '#') {
                    char* end;
                    bool ishex = entity[1] == 'x' || entity[1] == 'X';
                    uint32_t cp = (uint32_t)strtoul(entity + 1 + ishex, &end, ishex ? 16 : 10);
                    if(*end || !cp || cp>0x10ffff){*reason="normalize-entity";failure_stage=6;ok=false;}
                    else ok=utf8(out,cp);
                } else
                    ok = escaped(out, "&") && escaped(out, entity) && put_byte(';', out) != EOF;
            } else {
                ok = escaped(out, "&") && escaped(out, entity);
                if (next != EOF) ungetc(next, in);
            }
        } else if (c == '\n')
            ok = put_text("</p><p>", out) != EOF;
        else if (c >= 32 || c == '\t') {
            char s[2] = { (char)c, 0 };
            ok = escaped(out, s);
        }
        /* ftell on FAT can acquire the VFS/FAT locks. Query per block rather
           than once per character; the final limit check is still exact. */
        if(out && !(++iterations%2048)) {
            long position=ftell(out);
            if(position<0 || position>512*1024-64){*reason="chapter-limit";ok=false;}
        }
    }
    saved_error=errno;
    long input_position=ftell(in),position=out?ftell(out):0;
    if(ferror(in)){*reason="normalize-read";failure_stage=7;}
    if(out && position>512*1024-64){*reason="chapter-limit";failure_stage=10;}
    if(thingsos_weidu_stopped(control)){*reason="cancelled";failure_stage=11;ok=false;}
    if(skip_depth){*reason="unclosed-markup";failure_stage=12;}
    ok = ok && (!out || (position>=0 && position<=512*1024-64)) && !ferror(in) && !skip_depth;
    if(out && ok && put_text("</p></body></html>",out)==EOF){*reason="normalize-write-tail";failure_stage=4;saved_error=errno;ok=false;}
    if(out && ok && fflush(out)){*reason="normalize-flush";failure_stage=8;saved_error=errno;ok=false;}
    if(!ok && !saved_error)saved_error=errno;
    if(fclose(in) && ok){saved_error=errno;*reason="normalize-close-input";failure_stage=9;ok=false;}
    if(out && fclose(out)){saved_error=errno;*reason="normalize-close-output";failure_stage=9;ok=false;}
    if(!ok && out)thingsos_diag_event(DIAG_PACK,failure_stage,input_position<0?0:(unsigned)input_position,source_bytes,(int)position,saved_error);
    free(input_buffer);free(output_buffer);free(tag_buffer);
    if (!ok && out) remove(output);
    return ok;
}
typedef struct {
    char name[41];
    uint8_t media;
} pack_image_t;
typedef struct {
    const char* dir;
    pack_image_t* items;
    size_t count, capacity, limit;
    char normalized[1152], file[640];
} pack_images_t;
static bool detect_image_media(const char* file, uint8_t* media) {
    unsigned width = 0, height = 0;
    if (!thingsos_file_image_info(file, &width, &height)) return false;
    FILE* f = fopen(file, "rb");
    if (!f) return false;
    unsigned char sig[12] = {0};
    size_t n = fread(sig, 1, sizeof(sig), f);
    fclose(f);
    if (n >= 8 && !memcmp(sig, "\x89PNG\r\n\x1a\n", 8)) *media = 1;
    else if (n >= 6 && (!memcmp(sig, "GIF87a", 6) || !memcmp(sig, "GIF89a", 6))) *media = 2;
    else if (n >= 12 && !memcmp(sig, "RIFF", 4) && !memcmp(sig + 8, "WEBP", 4)) *media = 3;
    else if (n >= 2 && sig[0] == 255 && sig[1] == 216) *media = 0;
    else return false;
    return true;
}
static bool pack_local_image(void* context, char* url, char name[41]) {
    pack_images_t* images = context;
    if (!thingsos_weidu_image_cache_key(url, images->normalized, sizeof(images->normalized), name))
        return false;
    for (size_t i = 0; i < images->count; ++i)
        if (!strcmp(images->items[i].name, name)) return true;
    if (images->count >= images->limit) return false;
    int n = snprintf(images->file, sizeof(images->file), "%s/%s", images->dir, name);
    if (n <= 0 || (size_t)n >= sizeof(images->file)) return false;
    uint8_t media = 0;
    if (!detect_image_media(images->file, &media)) return false;
    if (images->count == images->capacity) {
        size_t next = images->capacity ? images->capacity * 2 : 32;
        if (next > images->limit) next = images->limit;
        pack_image_t* grown = thingsos_psram_realloc(images->items, next * sizeof(*grown));
        if (!grown) return false;
        images->items = grown;
        images->capacity = next;
    }
    memcpy(images->items[images->count].name, name, 41);
    images->items[images->count].media = media;
    ++images->count;
    return true;
}
static bool zip_add(FILE* out, zip_entry_t* e, const char* name, const char* file,const thingsos_weidu_control_t* control) {
    if (strlen(name) >= sizeof(e->name)) return false;
    strcpy(e->name, name);
    FILE* in = fopen(file, "rb");
    if (!in) return false;
    unsigned char* data=thingsos_psram_alloc(16384);
    if(!data){fclose(in);return false;}
    long offset=ftell(out);
    bool ok=offset>=0;
    e->offset=ok?(uint32_t)offset:0;e->bytes=0;e->crc=0;
    /* Seekable ZIP: stream payload and CRC together, then patch the local
       header. Avoid a complete extra SD pass merely to calculate the CRC. */
    ok=ok && put32(out,0x04034b50) && put16(out,20) && put16(out,0x800) &&
       put16(out,0) && put16(out,0) && put16(out,0) && put32(out,0) &&
       put32(out,0) && put32(out,0) && put16(out,(uint16_t)strlen(name)) &&
       put16(out,0) && fwrite(name,1,strlen(name),out)==strlen(name);
    size_t n;
    while(ok && (n=fread(data,1,16384,in))) {
        if(thingsos_weidu_stopped(control)){ok=false;break;}
        if(n>UINT32_MAX-e->bytes){ok=false;break;}
        e->bytes+=(uint32_t)n;e->crc=(uint32_t)crc32(e->crc,data,(uInt)n);
        ok=fwrite(data,1,n,out)==n;
    }
    ok=ok && !ferror(in);
    long end=ftell(out);
    ok=ok && end>=0 && !fseek(out,offset+14,SEEK_SET) && put32(out,e->crc) &&
       put32(out,e->bytes) && put32(out,e->bytes) && !fseek(out,end,SEEK_SET);
    if(fclose(in))ok=false;
    free(data);
    return ok;
}
static bool zip_end(FILE* out, const zip_entry_t* e, unsigned count,const thingsos_weidu_control_t* control) {
    long offset = ftell(out);
    if (offset < 0) return false;
#ifdef ESP_PLATFORM
    ESP_LOGI("ThingsOS_pack", "ZIP directory offset=%ld entries=%u", offset, count);
#endif
    for (unsigned i = 0; i < count; ++i) {
        if(thingsos_weidu_stopped(control))return false;
        size_t n = strlen(e[i].name);
        if (!put32(out, 0x02014b50) || !put16(out, 20) || !put16(out, 20) || !put16(out, 0x800) ||
            !put16(out, 0) || !put16(out, 0) || !put16(out, 0) || !put32(out, e[i].crc) ||
            !put32(out, e[i].bytes) || !put32(out, e[i].bytes) || !put16(out, (uint16_t)n) ||
            !put16(out, 0) || !put16(out, 0) || !put16(out, 0) || !put16(out, 0) ||
            !put32(out, 0) || !put32(out, e[i].offset) || fwrite(e[i].name, 1, n, out) != n)
            return false;
    }
    long end = ftell(out);
    if (end < offset) return false;
    return put32(out, 0x06054b50) && put16(out, 0) && put16(out, 0) &&
           put16(out, (uint16_t)count) && put16(out, (uint16_t)count) &&
           put32(out, (uint32_t)(end - offset)) && put32(out, (uint32_t)offset) && put16(out, 0);
}
bool thingsos_weidu_pack_control(
    const char* target, const thingsos_weidu_book_t* book, const char* dir, const char* toc,
    unsigned count,char* detail,size_t capacity,const thingsos_weidu_control_t* control) {
    const char* step="metadata";unsigned failed_chapter=0;
    if(detail && capacity)snprintf(detail,capacity,"pack-init");
#ifdef ESP_PLATFORM
    TickType_t pack_started=xTaskGetTickCount();
#endif
    /* Only the ZIP32 entry-count boundary applies here, not a book-size policy. */
    if (!count || count > UINT16_MAX-5u || thingsos_weidu_stopped(control) || !pack_heap("begin",0)) return false;
    zip_entry_t* entries = thingsos_psram_calloc(count + 5, sizeof(*entries));
    pack_images_t* images = entries ? thingsos_psram_calloc(1, sizeof(*images)) : NULL;
    if (!entries || !images) {
        free(entries);free(images);
        if(detail && capacity)snprintf(detail,capacity,"pack-memory");
        return false;
    }
    char file[640], raw[640], name[64];
    char cover_path[640];snprintf(cover_path,sizeof(cover_path),"%s",dir);
    char* parent=strrchr(cover_path,'/');
    bool has_cover=false,cover_png=false;
    if(parent && !strchr(book->id,'/') && !strchr(book->id,'\\')) {
        *parent=0;size_t base=strlen(cover_path);
        int n=snprintf(cover_path+base,sizeof(cover_path)-base,"/covers/%s.jpg",book->id);
        if(n>0 && (size_t)n<sizeof(cover_path)-base) {
            FILE* image=fopen(cover_path,"rb");unsigned char sig[8];
            if(image) {
                has_cover=fread(sig,1,8,image)==8;
                cover_png=has_cover && !memcmp(sig,"\x89PNG\r\n\x1a\n",8);
                has_cover=has_cover && (cover_png || (sig[0]==255 && sig[1]==216 && sig[2]==255));
                fclose(image);
            }
        }
    }
    size_t fixed_entries = (size_t)count + (has_cover ? 5u : 4u);
    images->dir = dir;
    images->limit = fixed_entries < THINGSOS_ZIP_ENTRIES ? THINGSOS_ZIP_ENTRIES - fixed_entries : 0;
    FILE *out = fopen(target, "wb"), *f = NULL, *index = NULL;
    bool ok = out != NULL;
    unsigned used = 0;
    snprintf(file, sizeof(file), "%s/mimetype", dir);
    f = fopen(file, "wb");
    if (!f)
        ok = false;
    else {
        if (put_text("application/epub+zip", f) == EOF) ok = false;
        if (fclose(f)) ok = false;
        f = NULL;
    }
    if (ok) ok = zip_add(out, &entries[used++], "mimetype", file,control);
    snprintf(file, sizeof(file), "%s/container.xml", dir);
    f = fopen(file, "wb");
    if (!f)
        ok = false;
    else {
        if (put_text(
                "<?xml version=\"1.0\"?><container "
                "xmlns=\"urn:oasis:names:tc:opendocument:xmlns:container\" "
                "version=\"1.0\"><rootfiles><rootfile full-path=\"OPS/book.opf\" "
                "media-type=\"application/oebps-package+xml\"/></rootfiles></container>",
                f) == EOF)
            ok = false;
        if (fclose(f)) ok = false;
        f = NULL;
    }
    if (ok) ok = zip_add(out, &entries[used++], "META-INF/container.xml", file,control);
    if(ok && has_cover)ok=zip_add(out,&entries[used++],"OPS/cover.jpg",cover_path,control);
    index = fopen(toc, "rb");
    char* index_buffer=index?thingsos_psram_alloc(16384):NULL;
    if(index_buffer && setvbuf(index,index_buffer,_IOFBF,16384)){free(index_buffer);index_buffer=NULL;}
    if (!index) ok = false;
    /* Store the source chapter titles in an EPUB2 navigation document. */
    snprintf(file,sizeof(file),"%s/toc.ncx",dir);
    f=fopen(file,"wb");if(!f)ok=false;
    if(ok)ok=put_text("<?xml version=\"1.0\" encoding=\"UTF-8\"?><ncx xmlns=\"http://www.daisy.org/z3986/2005/ncx/\" version=\"2005-1\"><head/><docTitle><text>",f)!=EOF && escaped(f,book->title) && put_text("</text></docTitle><navMap>",f)!=EOF;
    thingsos_weidu_chapter_t navigation;
    unsigned base_level=16,open_points=0;
    for(unsigned i=0;ok && i<count;++i) {
        if(thingsos_weidu_stopped(control)){ok=false;break;}
        ok=fread(&navigation,sizeof(navigation),1,index)==1;
        if(ok && navigation.level_tag==0xa5 && navigation.level<base_level)base_level=navigation.level;
    }
    if(index)rewind(index);
    for(unsigned i=0;ok && i<count;++i) {
        if(thingsos_weidu_stopped(control)){ok=false;break;}
        ok=fread(&navigation,sizeof(navigation),1,index)==1 && memchr(navigation.title,0,sizeof(navigation.title));
        unsigned level=navigation.level_tag==0xa5 && navigation.level<=15 && base_level<=navigation.level?
            navigation.level-base_level:thingsos_toc_number_level(navigation.title);
        if(level>open_points)level=open_points; /* Never invent missing parents. */
        while(ok && open_points>level){ok=put_text("</navPoint>",f)!=EOF;--open_points;}
        ++open_points;
        char point[192];snprintf(point,sizeof(point),"<navPoint id=\"n%u\" playOrder=\"%u\"><navLabel><text>",i,i+1);
        if(ok)ok=put_text(point,f)!=EOF && escaped(f,navigation.title) && put_text("</text></navLabel>",f)!=EOF;
        snprintf(point,sizeof(point),"<content src=\"chapter-%u.xhtml\"/>",i);
        if(ok)ok=put_text(point,f)!=EOF;
    }
    while(ok && open_points){ok=put_text("</navPoint>",f)!=EOF;--open_points;}
    if(ok)ok=put_text("</navMap></ncx>",f)!=EOF;
    if(f && fclose(f))ok=false;
    f=NULL;
    if(ok)ok=zip_add(out,&entries[used++],"OPS/toc.ncx",file,control);
    if(index)rewind(index);
    thingsos_weidu_chapter_t chapter;
    for (unsigned i = 0; ok && i < count; ++i) {
        ok = !thingsos_weidu_stopped(control) && fread(&chapter, sizeof(chapter), 1, index) == 1 &&
             memchr(chapter.title, 0, sizeof(chapter.title));
        snprintf(raw, sizeof(raw), "%s/chapter-%u.raw", dir, i);
        snprintf(file, sizeof(file), "%s/chapter-%u.xhtml", dir, i);
        snprintf(name, sizeof(name), "OPS/chapter-%u.xhtml", i);
        failed_chapter=i+1;step="toc-read";
        const char* reason="normalize";
        if(ok)thingsos_diag_event(DIAG_PACK,0,i,i+1,chapter.plain,0);
        bool cover_chapter = has_cover && i==0 && thingsos_weidu_cover_chapter(chapter.title);
        if(ok) {ok=normalize(raw,file,chapter.title,chapter.plain,cover_chapter,&reason,control,cover_chapter?NULL:pack_local_image,cover_chapter?NULL:images);step=reason;}
        if(ok){step="heap-check";ok=pack_heap("normalized",i);}
        if(ok){step="zip-write";ok=zip_add(out,&entries[used++],name,file,control) && pack_heap("written",i);}
        remove(file);
    }
    if (index) fclose(index);
    free(index_buffer);
    if(ok)failed_chapter=0;
    if (ok && images->count) {
        zip_entry_t* grown = thingsos_psram_realloc(entries, ((size_t)used + images->count + 1) * sizeof(*entries));
        if (!grown) { ok = false; step = "pack-memory"; }
        else entries = grown;
        for (size_t j = 0; ok && j < images->count; ++j) {
            if (thingsos_weidu_stopped(control)) { ok = false; break; }
            snprintf(file, sizeof(file), "%s/%s", dir, images->items[j].name);
            snprintf(name, sizeof(name), "OPS/%s", images->items[j].name);
            step = "zip-image";
            ok = zip_add(out, &entries[used++], name, file, control);
        }
    }
    if (ok) {
        step = "metadata";
        snprintf(file, sizeof(file), "%s/book.opf", dir);
        f = fopen(file, "wb");
        if (!f)
            ok = false;
        else {
            static const char* const media_types[] = {"image/jpeg", "image/png", "image/gif", "image/webp"};
            ok = ok &&
                 put_text(
                     "<?xml version=\"1.0\"?><package xmlns=\"http://www.idpf.org/2007/opf\" "
                     "version=\"2.0\" unique-identifier=\"id\"><metadata "
                     "xmlns:dc=\"http://purl.org/dc/elements/1.1/\"><dc:identifier id=\"id\">",
                     f) != EOF &&
                 escaped(f, book->id) && put_text("</dc:identifier><dc:title>", f) != EOF &&
                 escaped(f, book->title) && put_text("</dc:title><dc:creator>", f) != EOF &&
                 escaped(f, book->author) &&
                 put_text("</dc:creator><dc:language>zh-CN</dc:language>", f) != EOF;
            if(ok && has_cover)ok=put_text("<meta name=\"cover\" content=\"cover-image\"/>",f)!=EOF;
            ok=ok && put_text("</metadata><manifest>",f)!=EOF;
            if(ok && has_cover)ok=put_text(cover_png?
                "<item id=\"cover-image\" href=\"cover.jpg\" media-type=\"image/png\"/>":
                "<item id=\"cover-image\" href=\"cover.jpg\" media-type=\"image/jpeg\"/>",f)!=EOF;
            char item[192];
            for (unsigned i = 0; ok && i < count; ++i) {
                if(thingsos_weidu_stopped(control)){ok=false;break;}
                int n = snprintf(item, sizeof(item),
                    "<item id=\"c%u\" href=\"chapter-%u.xhtml\" media-type=\"application/xhtml+xml\"/>", i, i);
                ok = n > 0 && (size_t)n < sizeof(item) && put_text(item, f) != EOF;
            }
            for (size_t j = 0; ok && j < images->count; ++j) {
                if(thingsos_weidu_stopped(control)){ok=false;break;}
                int n = snprintf(item, sizeof(item),
                    "<item id=\"img%zu\" href=\"%s\" media-type=\"%s\"/>",
                    j, images->items[j].name, media_types[images->items[j].media < 4 ? images->items[j].media : 0]);
                ok = n > 0 && (size_t)n < sizeof(item) && put_text(item, f) != EOF;
            }
            ok = ok && put_text("<item id=\"toc\" href=\"toc.ncx\" media-type=\"application/x-dtbncx+xml\"/></manifest><spine toc=\"toc\">", f) != EOF;
            for (unsigned i = 0; ok && i < count; ++i) {
                if(thingsos_weidu_stopped(control)){ok=false;break;}
                int n = snprintf(item, sizeof(item), "<itemref idref=\"c%u\"/>", i);
                ok = n > 0 && (size_t)n < sizeof(item) && put_text(item, f) != EOF;
            }
            ok = ok && put_text("</spine></package>", f) != EOF && fflush(f) == 0;
            if (fclose(f)) ok = false;
            f = NULL;
        }
        if (ok) ok = zip_add(out, &entries[used++], "OPS/book.opf", file,control);
    }
    if(ok){step="zip-finalize";failed_chapter=0;}
    ok = ok && pack_heap("directory",0) && zip_end(out, entries, used,control) &&
         pack_heap("directory-written",0) && thingsos_weidu_flush(out) && pack_heap("flushed",0);
    if (out && fclose(out)) ok = false;
    free(images->items);
    free(images);
    free(entries);
    if (!ok) {
        remove(target);
        if(detail && capacity)snprintf(detail,capacity,"%s chapter=%u",step,failed_chapter);
    } else if(detail && capacity)detail[0]=0;
#ifdef ESP_PLATFORM
    ESP_LOGI("ThingsOS_pack","Pack completed chapters=%u result=%d elapsed=%u ms",count,ok,
        (unsigned)((xTaskGetTickCount()-pack_started)*portTICK_PERIOD_MS));
#endif
    return ok;
}
bool thingsos_weidu_pack_detail(const char* target,const thingsos_weidu_book_t* book,const char* dir,const char* toc,unsigned count,char* detail,size_t capacity) {
    return thingsos_weidu_pack_control(target,book,dir,toc,count,detail,capacity,NULL);
}

bool thingsos_weidu_pack(const char* target,const thingsos_weidu_book_t* book,const char* dir,const char* toc,unsigned count) {
    return thingsos_weidu_pack_detail(target,book,dir,toc,count,NULL,0);
}
bool thingsos_weidu_normalize(const char* raw,const char* target,const char* title,bool plain,const char** reason) {
    return normalize(raw,target,title,plain,false,reason,NULL,NULL,NULL);
}
bool thingsos_weidu_normalize_control(const char* raw,const char* target,const char* title,bool plain,const char** reason,const thingsos_weidu_control_t* control) {
    return normalize(raw,target,title,plain,false,reason,control,NULL,NULL);
}
bool thingsos_weidu_scan_images(const char* raw,bool plain,const thingsos_weidu_control_t* control,
    thingsos_weidu_image_cache_fn image,void* context) {
    const char* reason="scan";
    return normalize(raw,NULL,"",plain,false,&reason,control,image,context);
}
bool thingsos_weidu_normalize_images(const char* raw,const char* target,const char* title,bool plain,
    const char** reason,const thingsos_weidu_control_t* control,thingsos_weidu_image_cache_fn image,void* context) {
    return normalize(raw,target,title,plain,false,reason,control,image,context);
}
