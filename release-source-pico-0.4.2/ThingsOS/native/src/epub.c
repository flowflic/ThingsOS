// SPDX-License-Identifier: Apache-2.0
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "thingsos/content.h"
#include "thingsos/mem.h"
#include "thingsos/weidu_stream.h"
/* Bounded XML tokenizer: no DTD, external entities, executable markup or network. */
typedef struct {
    const char *start, *end, *next;
    char name[64];
    bool close, self;
} tag_t;
static bool tag(const char* p, const char* end, tag_t* t) {
    if (p >= end || *p != '<') return false;
    memset(t, 0, sizeof(*t));
    t->start = p;
    char quote = 0;
    const char* q = p + 1;
    for (; q < end; q++) {
        if (quote) {
            if (*q == quote) quote = 0;
        } else if (*q == '\'' || *q == '"')
            quote = *q;
        else if (*q == '>')
            break;
    }
    if (q >= end) return false;
    t->end = q;
    t->next = q + 1;
    p++;
    if (*p == '/') {
        t->close = true;
        p++;
    }
    const char* n = p;
    while (p < q && (isalnum((unsigned char)*p) || *p == ':' || *p == '_' || *p == '-')) p++;
    const char* local = n;
    for (const char* k = n; k < p; k++)
        if (*k == ':') local = k + 1;
    size_t len = (size_t)(p - local);
    if (len >= sizeof(t->name)) return false;
    memcpy(t->name, local, len);
    t->self = q > t->start && q[-1] == '/';
    return true;
}
static bool attr(const tag_t* t, const char* key, char* out, size_t cap) {
    const char* p = t->start + 1;
    while (p < t->end) {
        while (p < t->end && !isspace((unsigned char)*p)) p++;
        while (p < t->end && isspace((unsigned char)*p)) p++;
        const char* k = p;
        while (p < t->end && (isalnum((unsigned char)*p) || *p == '_' || *p == '-' || *p == ':'))
            p++;
        size_t n = (size_t)(p - k);
        while (p < t->end && isspace((unsigned char)*p)) p++;
        if (p == t->end || *p != '=') {
            if (p < t->end) p++;
            continue;
        }
        p++;
        while (p < t->end && isspace((unsigned char)*p)) p++;
        if (p == t->end || (*p != '\'' && *p != '"')) return false;
        char quote = *p++;
        const char* v = p;
        while (p < t->end && *p != quote) p++;
        if (p == t->end) return false;
        size_t bytes = (size_t)(p - v);
        p++;
        if (n == strlen(key) && !memcmp(k, key, n)) {
            if (bytes >= cap) return false;
            memcpy(out, v, bytes);
            out[bytes] = 0;
            return true;
        }
    }
    return false;
}
static bool valid_xml(const unsigned char* s, size_t n) {
    if (memchr(s, 0, n) || strstr((const char*)s, "<!ENTITY")) return false;
    const char* d = strstr((const char*)s, "<!DOCTYPE");
    if (d) {
        const char* end = strchr(d, '>');
        if (!end || memchr(d, '[', (size_t)(end - d))) return false;
    }
    return true;
}
static size_t decode(const char* p, size_t n, char* out, size_t cap) {
    size_t used = 0;
    for (size_t i = 0; i < n && used + 1 < cap; i++) {
        if (p[i] == '&') {
            const char* semi = memchr(p + i, ';', n - i > 16 ? 16 : n - i);
            if (semi) {
                size_t len = (size_t)(semi - (p + i));
                uint32_t cp = 0;
                if (len == 3 && !memcmp(p + i, "&lt", 3))
                    cp = '<';
                else if (len == 3 && !memcmp(p + i, "&gt", 3))
                    cp = '>';
                else if (len == 4 && !memcmp(p + i, "&amp", 4))
                    cp = '&';
                else if (len == 5 && !memcmp(p + i, "&quot", 5))
                    cp = '"';
                else if (len == 5 && !memcmp(p + i, "&apos", 5))
                    cp = '\'';
                else if (len >= 3 && p[i + 1] == '#') {
                    char tmp[20];
                    memcpy(tmp, p + i + 2, len - 2);
                    tmp[len - 2] = 0;
                    char* last;
                    errno = 0;
                    unsigned long parsed = strtoul(
                        tmp + (tmp[0] == 'x' || tmp[0] == 'X'), &last,
                        (tmp[0] == 'x' || tmp[0] == 'X') ? 16 : 10);
                    cp = !errno && !*last && parsed <= 0x10ffff ? (uint32_t)parsed : 0;
                }
                if (cp && cp <= 0x10ffff && !(cp >= 0xd800 && cp <= 0xdfff)) {
                    unsigned k = cp < 128 ? 1 : cp < 2048 ? 2 : cp < 65536 ? 3 : 4;
                    if (used + k >= cap) break;
                    if (k == 1)
                        out[used++] = (char)cp;
                    else {
                        for (unsigned j = k - 1; j > 0; j--)
                            out[used + j] = (char)(128 | (cp & 63)), cp >>= 6;
                        out[used] = (char)((k == 2 ? 192 : k == 3 ? 224 : 240) | cp);
                        used += k;
                    }
                    i += (size_t)(semi - (p + i));
                    continue;
                }
            }
        }
        out[used++] = p[i];
    }
    out[used] = 0;
    return used;
}
/* EPUB hrefs are URI references; ZIP names contain decoded UTF-8 bytes. */
static bool epub_path(const char* base,const char* ref,char out[256]) {
    char decoded[256];size_t used=0;
    for(size_t i=0;ref[i] && used+1<sizeof(decoded);++i) {
        unsigned char c=(unsigned char)ref[i];
        if(c=='%') {
            if(!ref[i+1] || !ref[i+2] || !isxdigit((unsigned char)ref[i+1]) || !isxdigit((unsigned char)ref[i+2]))return false;
            char hex[3]={ref[i+1],ref[i+2],0};c=(unsigned char)strtoul(hex,NULL,16);i+=2;
            if(!c || c=='#' || c=='?')return false;
        }
        decoded[used++]=(char)c;
    }
    if(used==sizeof(decoded)-1)return false;
    decoded[used]=0;return thingsos_archive_path(base,decoded,out);
}
typedef struct {
    const char* key;
    unsigned index;
} epub_lookup_t;

static int compare_lookup(const void* a, const void* b) {
    const epub_lookup_t* left = a;
    const epub_lookup_t* right = b;
    int order = strcmp(left->key, right->key);
    if (order) return order;
    return (int)left->index - (int)right->index;
}

static const epub_lookup_t* lookup_first(const epub_lookup_t* entries, size_t count, const char* key) {
    size_t lo = 0, hi = count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (strcmp(entries[mid].key, key) < 0) lo = mid + 1;
        else hi = mid;
    }
    return lo < count && !strcmp(entries[lo].key, key) ? &entries[lo] : NULL;
}

bool thingsos_epub_reserve_chapters(thingsos_epub_t* e, size_t count) {
    if (!e || count > THINGSOS_SPINE_LIMIT) return false;
    if (count <= e->chapter_capacity) {
        if(!e->chapter_fragments && !e->chapter_fragments_deferred && e->chapter_capacity) {
            e->chapter_fragments=thingsos_psram_calloc(e->chapter_capacity,sizeof(*e->chapter_fragments));
            return e->chapter_fragments!=NULL;
        }
        return true;
    }
    char (*names)[160] = thingsos_psram_calloc(count, sizeof(*names));
    bool need_fragments=e->chapter_fragments || !e->chapter_fragments_deferred;
    char (*fragments)[160] = need_fragments?thingsos_psram_calloc(count, sizeof(*fragments)):NULL;
    unsigned* spine=thingsos_psram_calloc(count,sizeof(*spine));
    unsigned* sources=thingsos_psram_calloc(count,sizeof(*sources));
    uint8_t* levels=thingsos_psram_calloc(count,sizeof(*levels));
    if (!names || (need_fragments && !fragments) || !spine || !sources || !levels) {
        free(names);free(fragments);free(spine);free(sources);free(levels);return false;
    }
    if (e->chapter_capacity) {
        memcpy(names, e->chapter_names, e->chapter_capacity * sizeof(*names));
        if(e->chapter_fragments)memcpy(fragments, e->chapter_fragments, e->chapter_capacity * sizeof(*fragments));
        memcpy(spine,e->spine,e->chapter_capacity*sizeof(*spine));
        memcpy(sources,e->chapter_sources,e->chapter_capacity*sizeof(*sources));
        memcpy(levels,e->chapter_levels,e->chapter_capacity*sizeof(*levels));
    }
    free(e->chapter_names); free(e->chapter_fragments);
    free(e->spine);free(e->chapter_sources);free(e->chapter_levels);
    e->spine=spine;e->chapter_sources=sources;e->chapter_levels=levels;
    e->chapter_names = names; e->chapter_fragments = fragments;
    e->chapter_capacity = count;
    return true;
}
void thingsos_epub_close(thingsos_epub_t* e) {
    if (!e) return;
    for(size_t i=0;i<e->item_count;++i)
        if(e->items[i].flags&THINGSOS_EPUB_OWN_PATH)free((void*)e->items[i].path);
    thingsos_zip_close(&e->zip);
    free(e->items);
    free(e->spine);free(e->chapter_sources);free(e->chapter_levels);
    free(e->chapter_names);
    free(e->chapter_fragments);
    free(e->section_toc);
    memset(e, 0, sizeof(*e));
}
#include "txt_source.inc"
bool thingsos_epub_open(thingsos_epub_t* e, const char* path) {
    if (!e) return false;
    memset(e, 0, sizeof(*e));
    e->chapter_fragments_deferred=true;
    if(thingsos_weidu_stream_open(e,path))return true;
    const char* filename=strrchr(path,'/');filename=filename?filename+1:path;
    e->weidu_cache=!strncmp(filename,"微读-",strlen("微读-"));
    if (!thingsos_zip_open(&e->zip, path)) return false;
    unsigned char* xml = NULL;
    epub_lookup_t* ids = NULL;
    epub_lookup_t* zip_paths = NULL;
    char (*resource_ids)[80] = NULL;
    size_t n;
    if (!thingsos_zip_read(&e->zip, "mimetype", 64, &xml, &n)) goto fail;
    /* 有些制作工具会在媒体类型后追加 CR/LF；能标识 EPUB 的是这 20 字节前缀，
       尾部的空白按兼容处理。 / Some writers append CR/LF after the media type;
       the 20-byte prefix identifies the EPUB, trailing whitespace is tolerated. */
    bool mime = n >= 20 && !memcmp(xml, "application/epub+zip", 20);
    for (size_t i = 20; mime && i < n; ++i)
        if (xml[i] != '\r' && xml[i] != '\n' && xml[i] != ' ') mime = false;
    free(xml);
    xml = NULL;
    if (!mime) goto fail;
    if (!thingsos_zip_read(&e->zip, "META-INF/container.xml", THINGSOS_XML_BYTES, &xml, &n) ||
        !valid_xml(xml, n))
        goto fail;
    for (const char* p = (char*)xml; p < (char*)xml + n; p++) {
        if (*p != '<') continue;
        tag_t t;
        if (!tag(p, (char*)xml + n, &t)) goto fail;
        if (!strcmp(t.name, "rootfile")) {
            char ref[256];
            if (attr(&t, "full-path", ref, sizeof(ref)) && thingsos_archive_path("", ref, e->opf))
                break;
        }
        p = t.next - 1;
    }
    free(xml);
    xml = NULL;
    if (!e->opf[0]) goto fail;
    if (!thingsos_zip_read(&e->zip, e->opf, THINGSOS_PACKAGE_BYTES, &xml, &n) || !valid_xml(xml, n))
        goto fail;
    size_t resources = 0, chapters = 0;
    for (const char* p = (char*)xml; p < (char*)xml + n; ++p) {
        if (*p != '<') continue;
        tag_t t;
        if (!tag(p, (char*)xml + n, &t)) goto fail;
        if (!t.close && !strcmp(t.name, "item") && ++resources > THINGSOS_SPINE_LIMIT) goto fail;
        if (!t.close && !strcmp(t.name, "itemref")) {
            char linear[16];
            if ((!attr(&t, "linear", linear, sizeof(linear)) || strcmp(linear, "no")) &&
                ++chapters > THINGSOS_SPINE_LIMIT) goto fail;
        }
        p = t.next - 1;
    }
    if (!resources || !chapters || !thingsos_epub_reserve_chapters(e, chapters)) goto fail;
    e->items = thingsos_psram_calloc(resources, sizeof(*e->items));
    ids = thingsos_psram_alloc(resources * sizeof(*ids));
    resource_ids=thingsos_psram_calloc(resources,sizeof(*resource_ids));
    zip_paths=thingsos_psram_alloc(e->zip.count*sizeof(*zip_paths));
    if (!e->items || !ids || !resource_ids || !zip_paths) goto fail;
    for(size_t i=0;i<e->zip.count;++i)zip_paths[i]=(epub_lookup_t){e->zip.entries[i].name,(unsigned)i};
    qsort(zip_paths,e->zip.count,sizeof(*zip_paths),compare_lookup);
    char cover_id[80] = { 0 };
    bool in_guide = false;
    for (const char* p = (char*)xml; p < (char*)xml + n; p++) {
        if (*p != '<') continue;
        tag_t t;
        if (!tag(p, (char*)xml + n, &t)) goto fail;
        if (!t.close && !strcmp(t.name, "title")) {
            const char* end = strchr(t.next, '<');
            if (end) decode(t.next, (size_t)(end - t.next), e->title, sizeof(e->title));
        }
        if (!t.close && !strcmp(t.name, "creator")) {
            const char* end = strchr(t.next, '<');
            if (end) decode(t.next, (size_t)(end - t.next), e->author, sizeof(e->author));
        }
        if (!t.close && !strcmp(t.name, "meta")) {
            char name[80];
            if (attr(&t, "name", name, sizeof(name)) && !strcmp(name, "cover"))
                attr(&t, "content", cover_id, sizeof(cover_id));
        }
        if (!strcmp(t.name, "guide")) in_guide = !t.close && !t.self;
        if (in_guide && !t.close && !strcmp(t.name, "reference")) {
            char type[32], href[256], ref[256];
            char* target=NULL;
            if (attr(&t, "type", type, sizeof(type))) {
                if (!strcmp(type,"cover")) target=e->cover_document;
                else if (!strcmp(type,"toc")) target=e->toc_document;
            }
            if (target && !target[0] && attr(&t, "href", href, sizeof(href))) {
                decode(href, strlen(href), ref, sizeof(ref));
                if (!epub_path(e->opf, ref, target)) target[0] = 0;
            }
        }
        if (!t.close && !strcmp(t.name, "item")) {
            if (e->item_count >= THINGSOS_SPINE_LIMIT) goto fail;
            thingsos_epub_item_t* v = e->items + e->item_count;
            char ref[256],path[256],type[64],properties[96]="";
            if (!attr(&t, "id", resource_ids[e->item_count], sizeof(*resource_ids)) ||
                !attr(&t, "href", ref, sizeof(ref)) || !epub_path(e->opf, ref, path) ||
                !attr(&t, "media-type", type, sizeof(type)))
                goto fail;
            const epub_lookup_t* existing=lookup_first(zip_paths,e->zip.count,path);
            if(existing)v->path=e->zip.entries[existing->index].name;
            else {
                /* Missing optional resources retain their path and fail only
                   if the reader actually attempts to load them. */
                char* owned=thingsos_psram_alloc(strlen(path)+1);
                if(!owned)goto fail;
                strcpy(owned,path);v->path=owned;v->flags|=THINGSOS_EPUB_OWN_PATH;
            }
            v->kind=!strcmp(type,"application/xhtml+xml")?THINGSOS_EPUB_XHTML:
                !strcmp(type,"text/css")?THINGSOS_EPUB_CSS:
                !strcmp(type,"application/x-dtbncx+xml")?THINGSOS_EPUB_NCX:THINGSOS_EPUB_OTHER;
            attr(&t,"properties",properties,sizeof(properties));
            if(strstr(properties,"nav"))v->flags|=THINGSOS_EPUB_NAV;
            if(strstr(properties,"cover-image"))v->flags|=THINGSOS_EPUB_COVER;
            ids[e->item_count] = (epub_lookup_t){ resource_ids[e->item_count], (unsigned)e->item_count };
            ++e->item_count;
        }
        p = t.next - 1;
    }
    qsort(ids, e->item_count, sizeof(*ids), compare_lookup);
    for (size_t i = 1; i < e->item_count; ++i) {
        /* Some exporters reuse one ID for CSS files addressed by href. */
        if (!strcmp(ids[i - 1].key, ids[i].key) &&
            (e->items[ids[i - 1].index].kind!=THINGSOS_EPUB_CSS ||
             e->items[ids[i].index].kind!=THINGSOS_EPUB_CSS)) goto fail;
    }
    for (size_t i = 0; i < e->item_count; i++)
        if ((e->items[i].flags&THINGSOS_EPUB_COVER) ||
            (cover_id[0] && !strcmp(resource_ids[i], cover_id)))
            strcpy(e->cover, e->items[i].path);
    for (const char* p = (char*)xml; p < (char*)xml + n; p++) {
        if (*p != '<') continue;
        tag_t t;
        if (!tag(p, (char*)xml + n, &t)) goto fail;
        if (!t.close && !strcmp(t.name, "itemref")) {
            char id[80], linear[16];
            if (!attr(&t, "idref", id, sizeof(id))) goto fail;
            if (attr(&t, "linear", linear, sizeof(linear)) && !strcmp(linear, "no")) {
                p = t.next - 1;
                continue;
            }
            const epub_lookup_t* match = lookup_first(ids, e->item_count, id);
            if (!match || e->spine_count >= THINGSOS_SPINE_LIMIT ||
                e->items[match->index].kind!=THINGSOS_EPUB_XHTML)
                goto fail;
            e->chapter_sources[e->spine_count]=(unsigned)e->spine_count;
            e->spine[e->spine_count++] = match->index;
        }
        p = t.next - 1;
    }
    if (!e->spine_count) goto fail;
    free(ids);
    free(resource_ids);free(zip_paths);
    free(xml);
    for(size_t i=0;i<e->spine_count;++i) snprintf(e->chapter_names[i],160,"第 %u 章",(unsigned)i+1);
    if (!e->title[0]) strcpy(e->title, "未命名书籍");
    return true;
fail:
    free(ids);
    free(resource_ids);free(zip_paths);
    free(xml);
    thingsos_epub_close(e);
    return false;
}

bool thingsos_epub_resolve_cover(thingsos_epub_t* e) {
    if (e->cover[0]) return true;
    if (!e->cover_document[0]) return false;
    unsigned char* xml;
    size_t bytes;
    if (!thingsos_zip_read(&e->zip, e->cover_document, THINGSOS_XML_BYTES, &xml, &bytes))
        return false;
    bool found = false;
    const char* end = (char*)xml + bytes;
    if (valid_xml(xml, bytes)) {
        for (const char* p = (char*)xml; p < end; ++p) {
            tag_t t;
            if (*p != '<' || !tag(p, end, &t)) continue;
            p = t.next - 1;
            if (t.close) continue;
            char href[256], ref[256], path[256];
            bool image = !strcmp(t.name, "img") && attr(&t, "src", href, sizeof(href));
            if (!image && !strcmp(t.name, "image"))
                image = attr(&t, "href", href, sizeof(href)) || attr(&t, "xlink:href", href, sizeof(href));
            if (!image) continue;
            decode(href, strlen(href), ref, sizeof(ref));
            unsigned width, height;
            if (epub_path(e->cover_document, ref, path) &&
                thingsos_zip_image_info(&e->zip, path, &width, &height)) {
                strcpy(e->cover, path);
                found = true;
                break;
            }
        }
    }
    free(xml);
    return found;
}
/* Captions may contain spans, emphasis and XML entities. */
static void caption_text(const char* p,const char* end,const char* closing,char out[160]) {
    char raw[640];size_t used=0;
    while(p<end) {
        if(*p=='<') {
            tag_t t;if(!tag(p,end,&t))break;
            if(t.close && !strcmp(t.name,closing))break;
            p=t.next;continue;
        }
        if(used+1<sizeof(raw))raw[used++]=*p;
        ++p;
    }
    raw[used]=0;char decoded[640];decode(raw,used,decoded,sizeof(decoded));
    used=0;bool space=false;
    for(p=decoded;*p && used+1<160;++p) {
        if(isspace((unsigned char)*p)){space=used>0;continue;}
        if(space && used+2<160)out[used++]=' ';
        space=false;out[used++]=*p;
    }
    /* Do not leave a truncated UTF-8 sequence. */
    if(*p && used) {
        size_t lead=used-1;while(lead && ((unsigned char)out[lead]&192)==128)--lead;
        unsigned char c=(unsigned char)out[lead];size_t need=c<128?1:c<224?2:c<240?3:4;
        if(used-lead<need)used=lead;
    }
    out[used]=0;
}
/* Infer only an explicit dotted numeric prefix followed by whitespace or non-ASCII text, e.g.
   3.1.1 Title. Dates, versions embedded in words and plain chapter numbers
   are not treated as hierarchy. Original EPUB nesting takes priority. */
unsigned thingsos_toc_number_level(const char* title) {
    if(!title)return 0;
    const unsigned char* p=(const unsigned char*)title;
    while(*p==' ' || *p=='\t')++p;
    unsigned groups=0;
    for(;;) {
        unsigned digits=0;while(isdigit(*p)){++digits;++p;}
        if(!digits || digits>3)return 0;
        ++groups;
        if(*p!='.')break;
        if(groups==6)return 0;
        ++p;
    }
    if(*p && *p!=' ' && *p!='\t' && *p<128)return 0;
    return groups>1?groups-1:0;
}
/* Recover source section headings even from older WeRead packages which
   flattened their tags. Match complete, unique decoded lines, never substrings
   in prose. These local reader offsets are independent of cloud source maps. */
size_t thingsos_epub_raw_headings(const unsigned char* xml,size_t bytes,
    const thingsos_chapter_t* chapter,unsigned index,unsigned base,
    thingsos_toc_entry_t* out,size_t cap) {
    if(!xml || !chapter || !chapter->text || !valid_xml(xml,bytes))return 0;
    size_t count=0,last=0;const char* end=(const char*)xml+bytes;
    bool body=false;unsigned skip=0;
    for(const char* p=(const char*)xml;p<end && count<cap;++p) {
        tag_t t;if(*p!='<' || !tag(p,end,&t))continue;
        p=t.next-1;
        if(!strcmp(t.name,"body")){body=!t.close;continue;}
        if(!body)continue;
        if(!strcmp(t.name,"script") || !strcmp(t.name,"style") || !strcmp(t.name,"nav")) {
            if(t.close){if(skip)--skip;}else if(!t.self)++skip;
        }
        if(skip || t.close || t.name[0]!='h' || t.name[1]<'1' || t.name[1]>'6' || t.name[2])continue;
        char kind[256]="",title[160];
        if(attr(&t,"class",kind,sizeof(kind)) && strstr(kind,"sigil_not_in_toc"))continue;
        caption_text(t.next,end,t.name,title);if(!title[0])continue;
        size_t found=SIZE_MAX,matches=0;
        for(size_t at=last;at<chapter->bytes;) {
            size_t finish=at;while(finish<chapter->bytes && chapter->text[finish]!='\n')++finish;
            size_t first=at,used=0;char line[160];bool pending=false,overflow=false;
            while(first<finish && isspace((unsigned char)chapter->text[first]))++first;
            for(size_t k=first;k<finish;++k) {
                unsigned char c=(unsigned char)chapter->text[k];
                if(isspace(c)){pending=used>0;continue;}
                if(used+(pending?1:0)+1>=sizeof(line)){overflow=true;break;}
                if(pending)line[used++]=' ';
                pending=false;line[used++]=(char)c;
            }
            line[used]=0;
            if(!overflow && !strcmp(line,title)){found=first;++matches;}
            at=finish<chapter->bytes?finish+1:finish;
        }
        if(matches!=1 || !found || found==SIZE_MAX)continue;
        if(out) {
            thingsos_toc_entry_t* entry=&out[count];memset(entry,0,sizeof(*entry));
            entry->chapter=index;entry->offset=(uint32_t)found;
            unsigned depth=(unsigned)(t.name[1]-'1');
            unsigned level=base+(depth?depth:1);
            entry->level=(uint8_t)(level>15?15:level);
            strcpy(entry->title,title);
        }
        ++count;last=found+strlen(title);
        /* Resume matching at the following line. */
        while(last<chapter->bytes && chapter->text[last]!='\n')++last;
        if(last<chapter->bytes)++last;
    }
    return count;
}
typedef struct {unsigned next;char fragment[160],name[160];uint8_t level;} nav_entry_t;
/* Links are one-based so zero marks the end of each resource's TOC entries. */
typedef struct {unsigned first,last,count;} nav_group_t;
void thingsos_epub_navigation_fast(thingsos_epub_t* e) {
    if(!e || !e->spine_count || e->navigation_loaded)return;
    nav_entry_t* entries=NULL;
    size_t count=0,capacity=0;
    epub_lookup_t* paths=thingsos_psram_alloc(e->spine_count*sizeof(*paths));
    nav_group_t* groups=thingsos_psram_calloc(e->item_count,sizeof(*groups));
    if(!paths || !groups)goto finish;
    for(size_t ch=0;ch<e->spine_count;++ch)
        paths[ch]=(epub_lookup_t){e->items[e->spine[ch]].path,(unsigned)ch};
    qsort(paths,e->spine_count,sizeof(*paths),compare_lookup);
    /* Prefer the EPUB3 TOC. EPUB2 NCX is a fallback, not another override. */
    for(unsigned pass=0;pass<2 && !count;++pass) for(size_t member=0;member<e->item_count;++member) {
        thingsos_epub_item_t* item=&e->items[member];
        bool ncx=item->kind==THINGSOS_EPUB_NCX;
        if(pass ? !ncx : (ncx || !(item->flags&THINGSOS_EPUB_NAV)))continue;
        unsigned char* xml=NULL;size_t n;
        if(!thingsos_zip_read(&e->zip,item->path,THINGSOS_PACKAGE_BYTES,&xml,&n))continue;
        if(!valid_xml(xml,n)){free(xml);continue;}
        char caption[160]="";bool toc=ncx;unsigned nav_depth=0,list_depth=0,point_depth=0;
        size_t candidates=0;
        for(const char* p=(char*)xml;p<(char*)xml+n;++p) {
            tag_t t;if(*p!='<' || !tag(p,(char*)xml+n,&t))continue;
            if(!t.close && !strcmp(t.name,ncx?"content":"a") && candidates<THINGSOS_SPINE_LIMIT-count)++candidates;
            p=t.next-1;
        }
        if(count+candidates>capacity) {
            nav_entry_t* grown=thingsos_psram_realloc(entries,(count+candidates)*sizeof(*entries));
            if(!grown){free(xml);continue;}
            entries=grown;capacity=count+candidates;
        }
        for(const char* p=(char*)xml;p<(char*)xml+n;++p) {
            tag_t t;if(*p!='<' || !tag(p,(char*)xml+n,&t))continue;
            if(!ncx && !strcmp(t.name,"nav")) {
                if(t.close){if(nav_depth)--nav_depth;if(!nav_depth)toc=false;}
                else {char type[96]="";attr(&t,"epub:type",type,sizeof(type));
                    toc=!type[0] || strstr(type,"toc")!=NULL;nav_depth=1;}
            }
            if(toc && !ncx && !strcmp(t.name,"ol")) {
                if(t.close){if(list_depth)--list_depth;}else if(!t.self)++list_depth;
            }
            if(ncx && !strcmp(t.name,"navPoint")) {
                if(t.close){if(point_depth)--point_depth;}
                else {caption[0]=0;if(!t.self)++point_depth;}
            }
            if(!t.close && ncx && !strcmp(t.name,"text"))caption_text(t.next,(char*)xml+n,"text",caption);
            if(toc && !t.close && ((!ncx && !strcmp(t.name,"a")) || (ncx && !strcmp(t.name,"content")))) {
                char ref[256],resolved[256];
                if(!ncx)caption_text(t.next,(char*)xml+n,"a",caption);
                if(caption[0] && attr(&t,ncx?"src":"href",ref,sizeof(ref))) {
                    char unescaped[256];decode(ref,strlen(ref),unescaped,sizeof(unescaped));
                    const epub_lookup_t* match=epub_path(item->path,unescaped,resolved)?
                        lookup_first(paths,e->spine_count,resolved):NULL;
                    if(match && count<capacity) {
                        nav_group_t* group=&groups[e->spine[match->index]];
                        char* fragment=strchr(unescaped,'#');fragment=fragment?fragment+1:"";
                        unsigned link=group->first;
                        while(link && strcmp(entries[link-1].fragment,fragment))
                            link=entries[link-1].next;
                        if(!link && strlen(fragment)<160) {
                            unsigned level=ncx?(point_depth?point_depth-1:0):(list_depth?list_depth-1:0);
                            nav_entry_t* entry=&entries[count++];
                            entry->level=(uint8_t)(level>15?15:level);
                            entry->next=0;
                            strcpy(entry->fragment,fragment);
                            strcpy(entry->name,caption);
                            if(group->last)entries[group->last-1].next=(unsigned)count;
                            else group->first=(unsigned)count;
                            group->last=(unsigned)count;
                            ++group->count;
                        }
                    }
                }
            }
            p=t.next-1;
        }
        free(xml);
    }
    if(!count)goto finish;
    /* The spine copy is as long as the book's chapter list; keep it out of the
       main task stack (an 8 KB frame is enough to overflow a deep open path). */
    size_t old=e->spine_count;
    unsigned* original=thingsos_psram_alloc(old*sizeof(*original));
    if(original)memcpy(original,e->spine,old*sizeof(*original));
    size_t total=0;
    if(original)for(size_t ch=0;ch<old;++ch) {
        unsigned found=groups[original[ch]].count;
        total+=found?found:1;
        if(total>THINGSOS_SPINE_LIMIT)break;
    }
    for(size_t i=0;i<count;++i)if(entries[i].fragment[0]){e->chapter_fragments_deferred=false;break;}
    if(original && total<=THINGSOS_SPINE_LIMIT && thingsos_epub_reserve_chapters(e,total)) {
        size_t used=0;
        for(size_t ch=0;ch<old;++ch) {
            unsigned first=groups[original[ch]].first;
            if(!first) {
                e->chapter_levels[used]=0;e->spine[used]=original[ch];e->chapter_sources[used]=(unsigned)ch;
                if(e->chapter_fragments)e->chapter_fragments[used][0]=0;
                snprintf(e->chapter_names[used],160,"第 %u 章",(unsigned)used+1);++used;
            }
            for(unsigned link=first;link;link=entries[link-1].next) {
                const nav_entry_t* entry=&entries[link-1];
                e->spine[used]=original[ch];e->chapter_sources[used]=(unsigned)ch;
                e->chapter_levels[used]=entry->level;strcpy(e->chapter_names[used],entry->name);
                if(e->chapter_fragments)strcpy(e->chapter_fragments[used],entry->fragment);
                ++used;
            }
        }
        e->spine_count=used;
    }
    free(original);
finish:
    free(paths);
    free(groups);
    free(entries);
    bool nested=false;for(size_t ch=0;ch<e->spine_count;++ch)if(e->chapter_levels[ch])nested=true;
    e->navigation_loaded=true;e->navigation_nested=nested;
    if(!nested)for(size_t ch=0;ch<e->spine_count;++ch)e->chapter_levels[ch]=(uint8_t)thingsos_toc_number_level(e->chapter_names[ch]);
}
bool thingsos_epub_titles_step(thingsos_epub_t* e) {
    if(!e || !e->spine_count || e->plain_text)return true;
    thingsos_epub_navigation_fast(e);
    /* Missing TOCs and older cached WeRead EPUBs still have real headings.
       Decode one bounded member at a time, so all titles exist before TOC draw. */
    if(e->titles_next<e->spine_count) {
        size_t ch=e->titles_next++;
        char fallback[40];snprintf(fallback,sizeof(fallback),"第 %u 章",(unsigned)ch+1);
        if(!strcmp(e->chapter_names[ch],fallback)) {
            thingsos_chapter_t* chapter=thingsos_psram_calloc(1,sizeof(*chapter));
            if(chapter){thingsos_epub_chapter(e,ch,chapter);thingsos_chapter_close(chapter);free(chapter);}
        }
        if(!e->navigation_nested)e->chapter_levels[ch]=(uint8_t)thingsos_toc_number_level(e->chapter_names[ch]);
    }
    return e->titles_next>=e->spine_count;
}
void thingsos_epub_titles(thingsos_epub_t* e) {
    while(!thingsos_epub_titles_step(e)){}
}
void thingsos_epub_navigation(thingsos_epub_t* e) {
    thingsos_epub_navigation_fast(e);thingsos_epub_titles(e);
}
void thingsos_chapter_close(thingsos_chapter_t* c) {
    if (!c) return;
    free(c->text);
    free(c->footnotes);
    free(c->notes);
    free(c->markdown_index);
    memset(c, 0, sizeof(*c));
}
static size_t header_space(const char* s,size_t at,size_t end) {
    while(at<end) {
        unsigned char ch=(unsigned char)s[at];
        if(ch==' ' || ch=='\t' || ch=='\r' || ch=='\n') ++at;
        else if(at+1<end && ch==0xc2 && (unsigned char)s[at+1]==0xa0)at+=2;
        else if(at+2<end && ch==0xe3 && (unsigned char)s[at+1]==0x80 && (unsigned char)s[at+2]==0x80)at+=3;
        else break;
    }
    return at;
}
static size_t header_match(const char* text,size_t at,size_t end,const char* title) {
    size_t pos=0,n=strlen(title);
    if(!n)return SIZE_MAX;
    while((pos=header_space(title,pos,n))<n) {
        at=header_space(text,at,end);
        if(at>=end || text[at++]!=title[pos++])return SIZE_MAX;
    }
    /* Never remove a title that is merely the prefix of a body sentence. */
    size_t after=header_space(text,at,end);
    bool line_end=after==end;
    for(size_t i=at;i<after;++i)if(text[i]=='\r' || text[i]=='\n')line_end=true;
    if(!line_end)return SIZE_MAX;
    return after;
}
static void cached_header(thingsos_epub_t* e,size_t index,thingsos_chapter_t* c) {
    if(!e->weidu_cache || strcmp(e->opf,"OPS/book.opf") || !c->heading ||
       c->heading_start || c->heading_end>512 || c->image_count)return;
    const char* title=e->chapter_names[index];
    if(header_match(c->text,0,c->heading_end,title)!=c->heading_end)return;
    size_t start=header_space(c->text,c->heading_end,c->bytes),end=c->bytes<1024?c->bytes:1024;
    size_t duplicate=header_match(c->text,start,end,title);
    if(duplicate==SIZE_MAX) {
        size_t after_book=header_match(c->text,start,end,e->title);
        if(after_book!=SIZE_MAX)duplicate=header_match(c->text,after_book,end,title);
    }
    if(duplicate!=SIZE_MAX) {
        /* Keep original byte offsets for saved bookmarks and reading position. */
        c->hidden_start=start;c->hidden_end=duplicate;
    }
}
/* Footnote support: a reference is an in-document link (<a href="#id">) and the
   note body is the element carrying that id, normally an aside/div/list item in
   the same XHTML member. Only the reference range and the resolved text are
   kept, so reading position and bookmarks stay on the original byte offsets. */
static size_t note_normalize(char* text, size_t bytes) {
    size_t out = 0;
    bool space = false;
    for (size_t i = 0; i < bytes; ++i) {
        unsigned char ch = (unsigned char)text[i];
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') { space = out != 0; continue; }
        if (space) text[out++] = ' ';
        space = false;
        text[out++] = (char)ch;
    }
    if(out) {
        size_t begin=out-1;while(begin && ((unsigned char)text[begin]&192)==128)--begin;
        unsigned char lead=(unsigned char)text[begin];size_t width=lead<128?1:lead<224?2:lead<240?3:4;
        if(out-begin<width)out=begin;
    }
    text[out]=0;return out;
}
static bool note_marker_only(const char* text) {
    if(!strcmp(text,"↩") || !strcmp(text,"↑") || !strcmp(text,"↩︎"))return true;
    for(const unsigned char* p=(const unsigned char*)text;*p;++p)
        if(!isdigit(*p) && !strchr(" []()*.\t\r\n",*p))return false;
    return true;
}
static bool note_uri(char* text) {
    size_t used=0;
    for(size_t i=0;text[i];++i) {
        unsigned char c=(unsigned char)text[i];
        if(c=='%') {
            if(!text[i+1] || !text[i+2] || !isxdigit((unsigned char)text[i+1]) || !isxdigit((unsigned char)text[i+2]))return false;
            char hex[3]={text[i+1],text[i+2],0};c=(unsigned char)strtoul(hex,NULL,16);i+=2;
        }
        if(c<32 || c==127)return false;
        text[used++]=(char)c;
    }
    text[used]=0;return true;
}
static size_t note_text(const char* xml, size_t n, const char* id, char* out, size_t cap) {
    const char* end = xml + n;
    for (const char* p = xml; p < end;) {
        if (*p != '<') { ++p; continue; }
        tag_t t;
        if (!tag(p, end, &t)) break;
        char owner[160];
        if (!t.close && (attr(&t, "id", owner, sizeof(owner)) || attr(&t, "name", owner, sizeof(owner))) &&
            !strcmp(owner, id)) {
            size_t used = 0;
            int depth = t.self?0:1;
            const char* after = t.next;
            for (const char* q = t.next; q < end && depth;) {
                if (*q != '<') {
                    const char* r = memchr(q, '<', (size_t)(end - q));
                    if (!r) r = end;
                    if (used + 1 < cap) used += decode(q, (size_t)(r - q), out + used, cap - 1 - used);
                    q = r;
                    continue;
                }
                tag_t u;
                if (!tag(q, end, &u)) break;
                if (!strcmp(u.name, t.name)) {
                    if (u.close) {
                        if (!--depth) { after = u.next; break; }
                    } else if (!u.self) depth++;
                } else if (!u.close && (!strcmp(u.name,"br") || !strcmp(u.name,"p") || !strcmp(u.name,"div") || !strcmp(u.name,"li")) && used && out[used-1]!=' ' && used + 1 < cap) {
                    out[used++] = ' ';
                }
                q = u.next;
            }
            if (used) {
                out[used] = 0;used=note_normalize(out,used);
                /* Numbered/backlink-only anchors label the following note;
                   the marker itself is not the annotation body. */
                if(used && !((!strcmp(t.name,"a") || !strcmp(t.name,"span")) && note_marker_only(out)))return used;
                used=0;
            }
            /* An empty marker (the common "<a id=fn1></a>note" shape, and what
               the WeRead packer emits) takes the text that follows it, up to the
               next block boundary. */
            for (const char* q = after; q < end && used + 1 < cap;) {
                if (*q != '<') {
                    const char* r = memchr(q, '<', (size_t)(end - q));
                    if (!r) r = end;
                    if (used + 1 < cap) used += decode(q, (size_t)(r - q), out + used, cap - 1 - used);
                    q = r;
                    continue;
                }
                tag_t u;
                if (!tag(q, end, &u)) break;
                bool block = !strcmp(u.name, "p") || !strcmp(u.name, "div") || !strcmp(u.name, "li") ||
                             !strcmp(u.name, "aside") || !strcmp(u.name, "section") ||
                             !strcmp(u.name, "table") || !strcmp(u.name, "body") ||
                             (u.name[0] == 'h' && u.name[1] >= '1' && u.name[1] <= '6' &&
                              !u.name[2]) ||
                             !strcmp(u.name, "br");
                if (block && used) break;
                if (!u.close && !strcmp(u.name, "br") && used + 1 < cap) out[used++] = ' ';
                q = u.next;
            }
            out[used] = 0;
            return note_normalize(out, used);
        }
        p = t.next;
    }
    return 0;
}
/* Note bodies usually sit in the same member; "notes.xhtml#fn1" style links put
   them in another one, so that member is loaded on demand (bounded XML cap). */
static size_t note_lookup(thingsos_epub_t* e, const char* chapter_path, const char* file,
                          const char* xml, size_t n, const char* id, char* out, size_t cap) {
    if(!file[0])return note_text(xml,n,id,out,cap);
    if(!e)return 0;
    char target[256];
    if (!thingsos_archive_path(chapter_path, file, target)) return 0;
    if(!strcmp(target,chapter_path))return note_text(xml,n,id,out,cap);
    size_t bytes;
    unsigned char* other = NULL;
    size_t other_bytes = 0;
    if (!thingsos_zip_read(&e->zip, target, THINGSOS_XML_BYTES, &other, &other_bytes)) return 0;
    bytes = note_text((char*)other, other_bytes, id, out, cap);
    free(other);
    return bytes;
}
static bool note_add(thingsos_epub_t* e, thingsos_chapter_t* c, const char* chapter_path,
                     const char* file, const char* xml, size_t n, size_t begin, size_t end,
                     const char* id) {
    while (begin < end && (c->text[begin] == ' ' || c->text[begin] == '\t' ||
                           c->text[begin] == '\r' || c->text[begin] == '\n'))
        ++begin;
    while (end > begin && (c->text[end - 1] == ' ' || c->text[end - 1] == '\t' ||
                           c->text[end - 1] == '\r' || c->text[end - 1] == '\n'))
        --end;
    if (begin >= end || c->footnote_count >= THINGSOS_FOOTNOTE_LIMIT) return false;
    if (!c->footnotes) {
        c->footnotes = thingsos_psram_calloc(THINGSOS_FOOTNOTE_LIMIT, sizeof(*c->footnotes));
        if (!c->footnotes) return false;
    }
    thingsos_footnote_t* f = c->footnotes + c->footnote_count;
    f->begin = begin;
    f->end = end;
    f->note_offset = c->notes_bytes;
    f->note_bytes = 0;
    if (!c->notes) c->notes = thingsos_psram_alloc(THINGSOS_NOTE_BYTES);
    if (c->notes && c->notes_bytes + 2 < THINGSOS_NOTE_BYTES) {
        size_t available=THINGSOS_NOTE_BYTES-c->notes_bytes;
        if(available>8192)available=8192;
        size_t bytes = note_lookup(e, chapter_path, file, xml, n, id, c->notes + c->notes_bytes,
                                   available);
        if (bytes) {
            f->note_bytes = bytes;
            c->notes_bytes += bytes + 1;
        }
    }
    c->footnote_count++;
    return true;
}
static void note_reference(thingsos_epub_t* e,thingsos_chapter_t* c,const char* path,const char* file,
                           const char* xml,size_t n,size_t begin,const char* id,bool explicit_note) {
    bool blank=true;for(size_t i=begin;i<c->bytes;++i)if(!isspace((unsigned char)c->text[i]))blank=false;
    if(blank && explicit_note && c->bytes+8<=n) {
        c->bytes=begin;
        int bytes=snprintf(c->text+c->bytes,n+1-c->bytes,"[%u]",(unsigned)c->footnote_count+1);
        if(bytes>0)c->bytes+=(size_t)bytes;
    }
    note_add(e,c,path,file,xml,n,begin,c->bytes,id);
}
/* Inline WeRead notes carry their body in the icon's alt attribute. */
static bool note_inline(thingsos_chapter_t* c,const tag_t* t,size_t text_capacity) {
    char kind[128];
    bool image=!strcmp(t->name,"img") && attr(t,"class",kind,sizeof(kind)) && strstr(kind,"qqreader-footnote");
    bool span=!strcmp(t->name,"span") && attr(t,"class",kind,sizeof(kind)) && strstr(kind,"reader_footer_note");
    if(!image && !span)return false;
    if(c->footnote_count>=THINGSOS_FOOTNOTE_LIMIT || c->bytes+8>=text_capacity)return true;
    char* value=thingsos_psram_alloc(8192);
    if(!value)return true;
    bool found=attr(t,image?"alt":"data-wr-footernote",value,8192);
    if(found && value[0]) {
        if(!c->footnotes)c->footnotes=thingsos_psram_calloc(THINGSOS_FOOTNOTE_LIMIT,sizeof(*c->footnotes));
        if(!c->notes)c->notes=thingsos_psram_alloc(THINGSOS_NOTE_BYTES);
        if(c->footnotes && c->notes && c->notes_bytes+2<THINGSOS_NOTE_BYTES) {
            size_t available=THINGSOS_NOTE_BYTES-c->notes_bytes;
            if(available>8192)available=8192;
            char* body=c->notes+c->notes_bytes;
            size_t bytes=decode(value,strlen(value),body,available);
            bytes=note_normalize(body,bytes);
            if(bytes) {
                thingsos_footnote_t* f=c->footnotes+c->footnote_count;
                f->begin=c->bytes;f->note_offset=c->notes_bytes;f->note_bytes=bytes;
                int label=snprintf(c->text+c->bytes,text_capacity-c->bytes,"[%u]",(unsigned)c->footnote_count+1);
                if(label>0){c->bytes+=(size_t)label;f->end=c->bytes;c->notes_bytes+=bytes+1;++c->footnote_count;}
            }
        }
    }
    free(value);return true;
}
bool thingsos_epub_chapter(thingsos_epub_t* e, size_t index, thingsos_chapter_t* c) {
    if (!e || !c || index >= e->spine_count) return false;
    memset(c, 0, sizeof(*c));
    if(e->plain_text)return txt_source_chapter(e,index,c);
    unsigned char* xml = NULL;
    size_t n;
    const thingsos_epub_item_t* item=&e->items[e->spine[index]];
    const char* path=item->path?item->path:"";
    /* A publisher can put its HTML table of contents in the spine. It uses
       the navigation member budget even when displayed as a reading page. */
    size_t limit=!strcmp(path,e->toc_document)?THINGSOS_PACKAGE_BYTES:THINGSOS_XML_BYTES;
    if (!(e->weidu_online?thingsos_weidu_stream_xml(e,(unsigned)index,&xml,&n):
          thingsos_zip_read(&e->zip, path, limit, &xml, &n)) || !valid_xml(xml, n)) {
        free(xml);
        return false;
    }
    c->text = thingsos_psram_alloc(n + 1);
    if (!c->text) {
        free(xml);
        return false;
    }
    size_t range_start=0,range_end=SIZE_MAX;
    const char* start_id=e->chapter_fragments?e->chapter_fragments[index]:"";
    const char* end_id=e->chapter_fragments && index+1<e->spine_count && e->spine[index+1]==e->spine[index]
        ? e->chapter_fragments[index+1] : "";
    bool body = false;
    unsigned skip = 0;
    char link_id[160] = { 0 };
    char link_file[192] = { 0 };
    size_t link_begin = 0;bool link_note=false;
    const char *p = (char*)xml, *end = p + n;
    while (p < end) {
        if (*p != '<') {
            const char* q = memchr(p, '<', (size_t)(end - p));
            if (!q) q = end;
            if (body && !skip)
                c->bytes += decode(p, (size_t)(q - p), c->text + c->bytes, n + 1 - c->bytes);
            p = q;
            continue;
        }
        if (end - p >= 4 && !memcmp(p, "<!--", 4)) {
            const char* q = strstr(p + 4, "-->");
            if (!q) goto fail;
            p = q + 3;
            continue;
        }
        tag_t t;
        if (!tag(p, end, &t)) goto fail;
        p = t.next;
        if (!strcmp(t.name, "body")) {
            body = !t.close;
            continue;
        }
        if (!body) continue;
        if (!strcmp(t.name, "script") || !strcmp(t.name, "style")) {
            if (!t.close && !t.self)
                skip++;
            else if (t.close && skip)
                skip--;
            continue;
        }
        if (skip) continue;
        if(!t.close) {
            char id[160];
            if(attr(&t,"id",id,sizeof(id)) || attr(&t,"name",id,sizeof(id))) {
                if(start_id[0] && !strcmp(id,start_id)){range_start=c->bytes;c->heading=false;c->heading_start=c->heading_end=0;}
                if(end_id[0] && !strcmp(id,end_id))range_end=c->bytes;
            }
        }
        if (!strcmp(t.name, "a")) {
            if (!t.close) {
                char ref[256];
                if (!link_id[0] &&
                    (attr(&t, "href", ref, sizeof(ref)) ||
                     attr(&t, "xlink:href", ref, sizeof(ref)))) {
                    char unescaped[256];decode(ref,strlen(ref),unescaped,sizeof(unescaped));
                    if(!note_uri(unescaped))continue;
                    const char* hash = strchr(unescaped, '#');
                    if (hash && hash[1] && !strchr(hash, '?')) {
                        size_t file_bytes = (size_t)(hash - unescaped);
                        link_file[0] = 0;
                        if (file_bytes && file_bytes < sizeof(link_file)) {
                            memcpy(link_file, unescaped, file_bytes);
                            link_file[file_bytes] = 0;
                        }
                        strncpy(link_id, hash + 1, sizeof(link_id) - 1);
                        link_id[sizeof(link_id) - 1] = 0;
                        link_begin = c->bytes;
                        char kind[128]="";link_note=(attr(&t,"epub:type",kind,sizeof(kind)) && strstr(kind,"noteref")) ||
                            (attr(&t,"role",kind,sizeof(kind)) && strstr(kind,"noteref")) ||
                            (attr(&t,"class",kind,sizeof(kind)) && (strstr(kind,"footnote") || strstr(kind,"noteref")));
                        if(t.self){note_reference(e,c,path,link_file,(char*)xml,n,link_begin,link_id,link_note);link_id[0]=link_file[0]=0;link_note=false;}
                    }
                }
            } else if (link_id[0]) {
                note_reference(e,c,path,link_file,(char*)xml,n,link_begin,link_id,link_note);
                link_id[0] = link_file[0] = 0;link_note=false;
            }
        }
        if (!t.close && link_id[0] && link_note && (!strcmp(t.name,"img") || !strcmp(t.name,"image")))continue;
        if (!t.close && note_inline(c,&t,n+1))continue;
        if (!t.close && (!strcmp(t.name, "img") || !strcmp(t.name, "image"))) {
            if (c->image_count >= THINGSOS_IMAGE_LIMIT) goto fail;
            thingsos_inline_t* img = c->images + c->image_count;
            char ref[256];
            if ((attr(&t, "src", ref, sizeof(ref)) || attr(&t, "href", ref, sizeof(ref)) ||
                 attr(&t, "xlink:href", ref, sizeof(ref))) &&
                epub_path(path, ref, img->path)) {
                img->offset = c->bytes;
                attr(&t, "alt", img->alt, sizeof(img->alt));
                unsigned iw=0,ih=0;
                bool sized;
                if(e->weidu_online) {
                    char file[640];
                    if(!thingsos_weidu_stream_image_path(e,img->path,file,sizeof(file)))continue;
                    sized=thingsos_file_image_info(file,&iw,&ih);
                } else sized=thingsos_zip_image_info(&e->zip,img->path,&iw,&ih);
                if(sized){img->width=(uint16_t)iw;img->height=(uint16_t)ih;}
                c->image_count++;
                if (c->bytes < n) c->text[c->bytes++] = '\n';
            }
        }
        if (!strcmp(t.name, "br") ||
            (!t.close && (!strcmp(t.name, "p") || !strcmp(t.name, "h1") || !strcmp(t.name, "h2") || !strcmp(t.name,"h3") || !strcmp(t.name,"h4") || !strcmp(t.name,"h5") || !strcmp(t.name,"h6") ||
                          !strcmp(t.name, "div") || !strcmp(t.name, "li")))) {
            if (c->bytes && c->text[c->bytes - 1] != '\n' && c->bytes < n)
                c->text[c->bytes++] = '\n';
        }
        if(t.name[0]=='h' && t.name[1]>='1' && t.name[1]<='6' && !t.name[2]) {
            if(!t.close && !c->heading) {c->heading=true;c->heading_start=c->bytes;c->heading_end=c->bytes;}
            else if(t.close && c->heading && c->heading_end==c->heading_start) c->heading_end=c->bytes;
        }
    }
    if(range_end>c->bytes)range_end=c->bytes;
    if(range_start>range_end)range_start=0;
    c->source_start=range_start;
    if(range_start || range_end<c->bytes) {
        size_t images=0;
        for(size_t i=0;i<c->image_count;++i)if(c->images[i].offset>=range_start && c->images[i].offset<range_end) {
            c->images[images]=c->images[i];c->images[images++].offset-=range_start;
        }
        c->image_count=images;
        size_t notes=0;
        for(size_t i=0;i<c->footnote_count;++i){
            thingsos_footnote_t f=c->footnotes[i];
            if(f.begin<range_start || f.end>range_end)continue;
            f.begin-=range_start;f.end-=range_start;
            c->footnotes[notes++]=f;
        }
        c->footnote_count=notes;
        if(c->heading_start>=range_start && c->heading_end<=range_end) {
            c->heading_start-=range_start;c->heading_end-=range_start;
        } else {c->heading=false;c->heading_start=c->heading_end=0;}
        c->bytes=range_end-range_start;memmove(c->text,c->text+range_start,c->bytes);
    }
    c->text[c->bytes] = 0;
    char fallback[40];snprintf(fallback,sizeof(fallback),"第 %u 章",(unsigned)index+1);
    if (!strcmp(e->chapter_names[index],fallback) && c->heading_end>c->heading_start) {
        size_t length=c->heading_end-c->heading_start;
        if(length<sizeof(e->chapter_names[index])) {
            memcpy(e->chapter_names[index],c->text+c->heading_start,length);
            e->chapter_names[index][length]=0;
        }
    }
    if(!strcmp(e->chapter_names[index],fallback)) {
        for(const char* q=(char*)xml;q<(char*)xml+n;++q) {
            tag_t t;if(*q!='<' || !tag(q,(char*)xml+n,&t))continue;
            if(!t.close && !strcmp(t.name,"title")) {
                char title[160];caption_text(t.next,(char*)xml+n,"title",title);
                if(title[0])strcpy(e->chapter_names[index],title);
                break;
            }
            q=t.next-1;
        }
    }
    cached_header(e,index,c);
    if(index==0 && c->image_count) {
        const char* name=strrchr(path,'/');name=name?name+1:path;
        const char* title=e->chapter_names[index];
        bool identified=(e->cover[0] && !strcmp(c->images[0].path,e->cover)) ||
            !strcmp(title,"封面") || !strcmp(title,"Cover") || !strcmp(title,"cover") ||
            !strcmp(name,"cover.xhtml") || !strcmp(name,"cover.html");
        bool prefix_empty=true;
        for(size_t at=0;at<c->images[0].offset;++at) {
            if((c->heading && at>=c->heading_start && at<c->heading_end) || (at>=c->hidden_start && at<c->hidden_end))continue;
            if(!isspace((unsigned char)c->text[at])){prefix_empty=false;break;}
        }
        if(identified && prefix_empty)c->cover_image=1;
    }
    free(xml);
    return true;
fail:
    free(xml);
    thingsos_chapter_close(c);
    return false;
}
