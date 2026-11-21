/* SPDX-License-Identifier: Apache-2.0
 * Bounded streaming JSON and an independent login/shelf/download workflow.
 */
#include "thingsos/weidu.h"
#include "thingsos/diagnostics.h"
#include <errno.h>
#include "thingsos/mem.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "thingsos/content.h"
#include "thingsos/files.h"
#include "thingsos/weidu_stream.h"
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif
#ifdef _WIN32
#include <direct.h>
#include <io.h>
#define wd_mkdir(p) _mkdir(p)
#else
#include <unistd.h>
#define wd_mkdir(p) mkdir(p, 0775)
#endif
static void copy(char* out, size_t cap, const char* s) {
    if (cap) {
        size_t n = strlen(s);
        if (n >= cap) {
            n = cap - 1;
            while (n && ((unsigned char)s[n] & 0xc0) == 0x80) --n;
        }
        memcpy(out, s, n);
        out[n] = 0;
    }
}
bool thingsos_weidu_flush(FILE* f) {
    if (fflush(f) || ferror(f)) return false;
#ifdef _WIN32
    return _commit(_fileno(f)) == 0;
#else
    return fsync(fileno(f)) == 0;
#endif
}
static bool id_ok(const char* s) {
    if (!s || !*s || strlen(s) >= 128) return false;
    for (const unsigned char* p = (const unsigned char*)s; *p; ++p)
        if (!isalnum(*p) && *p != '-' && *p != '_') return false;
    return true;
}
/* The account shelf also carries followed 公众号 entries ("MP_WXS_…" accounts and
   "MP_…" articles). They belong to the 公众号 list, never to the book shelf. */
static bool shelf_book(const char* id) { return id[0] && strncmp(id, "MP_", 3); }
/* Real accounts hold several thousand books. The earlier 4096-record and
   2 MiB bounds rejected those shelves as "over budget", so the device kept
   an empty shelf. The response streams to SD, so the size bound is disk
   traffic rather than RAM. */
#define WD_SHELF_MAX_RECORDS 20000u
#define WD_SHELF_MAX_BYTES (24u * 1024u * 1024u)
static bool path(char* out, size_t cap, const char* root, const char* name) {
    int n = snprintf(out, cap, "%s/%s", root, name);
    return n > 0 && (size_t)n < cap;
}
/* Atomic replacement keeps a recoverable previous image, including rename failures. */
static bool commit_file(const char* temporary, const char* final) {
    char backup[640];
    int n = snprintf(backup, sizeof(backup), "%s.bak", final);
    if (n < 0 || (size_t)n >= sizeof(backup)) return false;
    FILE* old = fopen(final, "rb");
    bool existed = old != NULL;
    if (old) fclose(old);
    if (existed) {
        remove(backup);
        if (rename(final, backup)) return false;
    }
    if (rename(temporary, final)) {
        if (existed) rename(backup, final);
        return false;
    }
    if (existed) remove(backup);
    return true;
}
typedef bool (*json_event)(void*, const char*, const char*, char, unsigned);
typedef struct {
    FILE* f;
    const char* memory;
    size_t offset, length;
    bool truncate_strings, truncate_all_values, allow_truncate, overflow;
    char value[4096];
    json_event event;
    void* ctx;
    thingsos_weidu_control_t control;
    size_t checked;
    unsigned char input[8192];
    size_t input_at,input_bytes;
} json_t;
static int get(json_t* j) {
    if(!(++j->checked%1024) && thingsos_weidu_stopped(&j->control))return EOF;
    if(j->memory)return j->offset<j->length?(unsigned char)j->memory[j->offset++]:EOF;
    if(j->input_at==j->input_bytes) {
        j->input_bytes=fread(j->input,1,sizeof(j->input),j->f);j->input_at=0;
        if(!j->input_bytes)return EOF;
    }
    return j->input[j->input_at++];
}
static void back(json_t* j, int c) {
    if(c==EOF)return;
    if (j->memory) {
        if (j->offset) --j->offset;
    } else if(j->input_at)--j->input_at;
}
static int space(json_t* j) {
    int c;
    do {
        c = get(j);
    } while (c != EOF && isspace((unsigned char)c));
    return c;
}
static int hex(int c) {
    return c >= '0' && c <= '9'   ? c - '0'
           : c >= 'a' && c <= 'f' ? c - 'a' + 10
           : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                  : -1;
}
static bool unicode(json_t* j, unsigned* cp) {
    *cp = 0;
    for (unsigned i = 0; i < 4; ++i) {
        int h = hex(get(j));
        if (h < 0) return false;
        *cp = (*cp << 4) | (unsigned)h;
    }
    return true;
}
static bool string(json_t* j) {
    j->overflow=false;
    size_t n = 0;
    int c;
    while ((c = get(j)) != EOF && c != '"') {
        if (c < 32) return false;
        if (c == '\\') {
            c = get(j);
            if (c == 'u') {
                unsigned cp;
                if (!unicode(j, &cp)) return false;
                if (cp >= 0xd800 && cp <= 0xdbff) {
                    unsigned low;
                    if (get(j) != '\\' || get(j) != 'u' || !unicode(j, &low) || low < 0xdc00 ||
                        low > 0xdfff)
                        return false;
                    cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
                } else if (cp >= 0xdc00 && cp <= 0xdfff)
                    return false;
                if (!cp) return false;
                unsigned bytes = cp < 128 ? 1 : cp < 2048 ? 2 : cp < 65536 ? 3 : 4;
                if (j->overflow || n + bytes >= sizeof(j->value)) {if(!j->allow_truncate)return false;j->overflow=true;continue;}
                for (unsigned k = bytes - 1; k > 0; --k) {
                    j->value[n + k] = (char)(128 | (cp & 63));
                    cp >>= 6;
                }
                j->value[n] = (char)((bytes == 1   ? 0
                                      : bytes == 2 ? 192
                                      : bytes == 3 ? 224
                                                   : 240) |
                                     cp);
                n += bytes;
                continue;
            }
            c = c == 'n'   ? '\n'
                : c == 'r' ? '\r'
                : c == 't' ? '\t'
                : c == 'b' ? '\b'
                : c == 'f' ? '\f'
                           : c;
            if (c != '"' && c != '\\' && c != '/' && c != '\n' && c != '\r' && c != '\t' &&
                c != '\b' && c != '\f')
                return false;
        }
        if(j->overflow || n + 1 >= sizeof(j->value)){if(!j->allow_truncate)return false;j->overflow=true;continue;}
        j->value[n++] = (char)c;
    }
    if(j->overflow && n){
        size_t lead=n-1;while(lead && ((unsigned char)j->value[lead]&192)==128)--lead;
        unsigned char a=(unsigned char)j->value[lead];size_t width=a<128?1:a<224?2:a<240?3:4;
        if(n-lead<width)n=lead;
    }
    j->value[n] = 0;
    return c == '"';
}
static bool value(json_t* j, const char* key, unsigned depth) {
    if (depth > 16) return false;
    int c = space(j);
    if (c == '{' || c == '[') {
        int end = c == '{' ? '}' : ']';
        bool object = c == '{';
        if (!j->event(j->ctx, key, "", (char)c, depth)) return false;
        int next = space(j);
        if (next == end) return j->event(j->ctx, key, "", (char)end, depth);
        if (next == EOF) return false;
        back(j, next);
        for (;;) {
            char child[96] = { 0 };
            if (object) {
                j->allow_truncate=j->truncate_all_values;
                if (space(j) != '"' || !string(j)) return false;
                if (strlen(j->value) < sizeof(child)) copy(child, sizeof(child), j->value);
                else if (!j->truncate_all_values) return false;
                if (space(j) != ':') return false;
            }
            if (!value(j, child, depth + 1)) return false;
            next = space(j);
            if (next == end) break;
            if (next != ',') return false;
        }
        return j->event(j->ctx, key, "", (char)end, depth);
    }
    char kind = 's';
    if (c == '"') {
        j->allow_truncate=j->truncate_all_values ||
            (j->truncate_strings && (!strcmp(key,"markText")||!strcmp(key,"content")||!strcmp(key,"htmlContent")));
        if (!string(j)) return false;
        if(j->overflow)kind='t';
    } else {
        size_t n = 0;
        kind = 'n';
        while (c != EOF && !isspace((unsigned char)c) && c != ',' && c != ']' && c != '}') {
            if (n + 1 >= sizeof(j->value)) return false;
            j->value[n++] = (char)c;
            c = get(j);
        }
        if (c != EOF) back(j, c);
        j->value[n] = 0;
        if (!strcmp(j->value, "true") || !strcmp(j->value, "false") || !strcmp(j->value, "null"))
            kind = 'b';
        else {
            const char* p = j->value;
            if (*p == '-') ++p;
            if (*p == '0')
                ++p;
            else {
                if (*p < '1' || *p > '9') return false;
                while (isdigit((unsigned char)*p)) ++p;
            }
            if (*p == '.') {
                ++p;
                if (!isdigit((unsigned char)*p)) return false;
                while (isdigit((unsigned char)*p)) ++p;
            }
            if (*p == 'e' || *p == 'E') {
                ++p;
                if (*p == '+' || *p == '-') ++p;
                if (!isdigit((unsigned char)*p)) return false;
                while (isdigit((unsigned char)*p)) ++p;
            }
            if (!n || *p) return false;
        }
    }
    return j->event(j->ctx, key, j->value, kind, depth);
}
static bool json_file(const char* file, json_event event, void* ctx, bool truncate_payloads,const thingsos_weidu_control_t* control) {
    json_t* j = thingsos_psram_calloc(1, sizeof(*j));
    if (!j) return false;
    j->f = fopen(file, "rb");
    j->event = event;
    j->ctx = ctx;
    j->truncate_strings = truncate_payloads;
    if(control)j->control=*control;
    bool ok = j->f && value(j, "", 0) && space(j) == EOF && !ferror(j->f);
    if (j->f) fclose(j->f);
    free(j);
    return ok;
}
typedef struct {
    thingsos_weidu_book_t* books;
    thingsos_weidu_book_t book;
    unsigned capacity, count, array_depth;
    bool in_data, root_books, wrapped_books, in_book, has_info, in_info;
    const char* cover_id;
    char* cover_url;
    size_t cover_capacity;
    char row_cover[640];
} catalog_t;
static bool catalog_fields(void* context,const char* key,const char* text,char type,unsigned depth) {
    catalog_t* catalog=context;
    if(depth==1 && !strcmp(key,"data"))catalog->in_data=type=='{';
    if(type=='[' && !strcmp(key,"books")) {
        if(depth==1 && !catalog->root_books) {
            catalog->root_books=true;
            catalog->count=0;
            if(catalog->cover_url)catalog->cover_url[0]=0;
            catalog->array_depth=depth;
        } else if(depth==2 && catalog->in_data && !catalog->root_books && !catalog->wrapped_books) {
            catalog->wrapped_books=true;
            catalog->array_depth=depth;
        }
    }
    if(!catalog->array_depth)return true;
    unsigned book_depth=catalog->array_depth+1;
    if(depth<book_depth) {
        if(type==']')catalog->array_depth=0;
    } else if(depth==book_depth) {
        if(type=='{') {
            memset(&catalog->book,0,sizeof(catalog->book));
            catalog->row_cover[0]=0;
            catalog->in_book=true;
            catalog->has_info=catalog->in_info=false;
        } else if(type=='}' && catalog->in_book) {
            catalog->in_book=false;
            if(catalog->cover_id) {
                if(!catalog->cover_url[0] && !strcmp(catalog->book.id,catalog->cover_id)) {
                    if(!thingsos_weidu_cover_url(catalog->row_cover,catalog->cover_url,catalog->cover_capacity))
                        catalog->cover_url[0]=0;
                }
                return true;
            }
            if(!catalog->book.id[0] || !catalog->book.title[0] || catalog->count>=catalog->capacity)return true;
            for(unsigned i=0;i<catalog->count;++i)
                if(!strcmp(catalog->books[i].id,catalog->book.id))return true;
            catalog->books[catalog->count++]=catalog->book;
        }
    } else if(catalog->in_book) {
        if(depth==book_depth+1 && !strcmp(key,"bookInfo")) {
            if(type=='{' && !catalog->has_info) {
                memset(&catalog->book,0,sizeof(catalog->book));
                catalog->row_cover[0]=0;
                catalog->has_info=catalog->in_info=true;
            } else if(type=='}')catalog->in_info=false;
        }
        bool book_field=catalog->has_info?catalog->in_info && depth==book_depth+2:depth==book_depth+1;
        if(!book_field)return true;
        if(!strcmp(key,"bookId") && (type=='s' || type=='n')) {
            size_t n=strlen(text);
            if(n && n<sizeof(catalog->book.id) && strspn(text,"0123456789")==n)copy(catalog->book.id,sizeof(catalog->book.id),text);
        }
        if(type=='s' || type=='t') {
            if(!strcmp(key,"title"))copy(catalog->book.title,sizeof(catalog->book.title),text);
            if(!strcmp(key,"author"))copy(catalog->book.author,sizeof(catalog->book.author),text);
        }
        if(catalog->cover_id && type=='s' && !strcmp(key,"cover") && strlen(text)<sizeof(catalog->row_cover))
            copy(catalog->row_cover,sizeof(catalog->row_cover),text);
    }
    return true;
}
static thingsos_weidu_error_t catalog_parse(const char* response,size_t bytes,catalog_t* catalog) {
#ifdef ESP_PLATFORM
    json_t* json=heap_caps_calloc(1,sizeof(*json),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
#else
    json_t* json=calloc(1,sizeof(*json));
#endif
    if(!json)return WD_MEMORY;
    json->memory=response;json->length=bytes;
    json->event=catalog_fields;json->ctx=catalog;json->truncate_all_values=true;
    bool ok=value(json,"",0) && space(json)==EOF && (catalog->root_books || catalog->wrapped_books);
    free(json);
    return ok?WD_OK:WD_PROTOCOL;
}
thingsos_weidu_error_t thingsos_weidu_catalog_results(const char* response,size_t bytes,
        thingsos_weidu_book_t* books,unsigned capacity,unsigned* count) {
    catalog_t catalog={.books=books,.capacity=capacity};
    thingsos_weidu_error_t result=catalog_parse(response,bytes,&catalog);
    *count=result==WD_OK?catalog.count:0;
    return result;
}
bool thingsos_weidu_catalog_cover(const char* response,size_t bytes,const char* id,char* url,size_t capacity) {
    if(!id_ok(id) || !capacity)return false;
    catalog_t catalog={.cover_id=id,.cover_url=url,.cover_capacity=capacity};
    url[0]=0;
    return catalog_parse(response,bytes,&catalog)==WD_OK && url[0];
}
static bool public_image_url(const char* host, char* out, size_t cap,bool redirect) {
    const char* slash = strchr(host,'/');
    if (!slash || slash==host || (size_t)(slash-host)>=128) return false;
    char name[128];size_t n=(size_t)(slash-host);
    for(size_t i=0;i<n;++i) { if(!isalnum((unsigned char)host[i]) && host[i]!='.' && host[i]!='-')return false; name[i]=(char)tolower((unsigned char)host[i]); }
    name[n]=0;
    /* Reader assets can redirect to this WeRead CDN. Keep the exception exact
       and confined to redirects from an already validated public image URL. */
    const char* suffixes[]={"qq.com","qpic.cn","myqcloud.com"};
    bool allowed=redirect && !strcmp(name,"wrco-40036.sh.gfp.tencent-cloud.com");
    for(unsigned i=0;i<3;++i) { size_t k=strlen(suffixes[i]);if(n>=k && !strcmp(name+n-k,suffixes[i]) && (n==k || name[n-k-1]=='.'))allowed=true; }
    if(!allowed || strchr(slash,'\r') || strchr(slash,'\n'))return false;
    int written=snprintf(out,cap,"https://%s",host);
    return written>0 && (size_t)written<cap;
}
bool thingsos_weidu_cover_url(const char* source, char* out, size_t cap) {
    const char* host = !strncmp(source,"https://",8)?source+8:
                       !strncmp(source,"http://",7)?source+7:NULL;
    return host && public_image_url(host,out,cap,false);
}
bool thingsos_weidu_image_redirect_url(const char* current,const char* location,char* out,size_t cap) {
    if(!*location || *location=='#')return false;
    for(const unsigned char* p=(const unsigned char*)location;*p;++p)
        if(*p<=32 || *p==127 || strchr("\\\"'<>",*p))return false;
    if(!strncmp(location,"https://",8))return public_image_url(location+8,out,cap,true);
    if(!strncmp(location,"//",2))return public_image_url(location+2,out,cap,true);

    /* The current URL has already passed the public HTTPS host check. */
    const char* path=strchr(current+8,'/');
    if(!path || memchr(location,':',strcspn(location,"/?#")))return false;
    const char* end=path;
    if(*location!='/') {
        end+=strcspn(path,"?#");
        if(*location!='?')while(end>path && end[-1]!='/')--end;
    }
    size_t prefix=(size_t)(end-current),length=strlen(location);
    if(prefix>=cap || length>=cap-prefix)return false;
    memcpy(out,current,prefix);
    memcpy(out+prefix,location,length+1);
    return true;
}
static bool cancelled(thingsos_weidu_client_t* c);
static bool cover_path(const char* root,const char* id,char* file,size_t capacity) {
    if(!id_ok(id))return false;
    int n=snprintf(file,capacity,"%s/covers/%s.jpg",root,id);
    return n>0 && (size_t)n<capacity;
}
typedef struct {
    char normalized[640], dir[600], target[700], tmp[710];
} cover_cache_scratch_t;
bool thingsos_weidu_cache_cover(thingsos_weidu_client_t* c, const char* id, const char* url) {
    if(!c->io.image || cancelled(c))return false;
    cover_cache_scratch_t* sc = thingsos_psram_alloc(sizeof(*sc));
    if(!sc)return false;
    bool saved = false;
    unsigned width,height;
    if(cover_path(c->root,id,sc->target,sizeof(sc->target)) &&
       thingsos_weidu_cover_url(url,sc->normalized,sizeof(sc->normalized))) {
        if(thingsos_file_image_info(sc->target,&width,&height)) {
            saved = true;
        } else {
            snprintf(sc->dir,sizeof(sc->dir),"%s/covers",c->root);wd_mkdir(sc->dir);
            snprintf(sc->tmp,sizeof(sc->tmp),"%s.part",sc->target);
            thingsos_weidu_error_t e=c->io.image(c->io.context,sc->normalized,sc->tmp,256u*1024u);
            saved=e==WD_OK && !cancelled(c) && thingsos_file_image_info(sc->tmp,&width,&height) && commit_file(sc->tmp,sc->target);
            remove(sc->tmp);
        }
    }
    free(sc);
    return saved;
}
typedef struct {
    char uid[128], vid[128], token[384], logic[64], book_id[64];
    bool success;
    int error, array_depth;
    bool array_seen, in_record;
    unsigned records, limit;
    const char* array;
    FILE* output;
    thingsos_weidu_client_t* covers;
    char cover_url[512];
    const thingsos_weidu_book_t* cover_books;
    size_t cover_count;
    thingsos_weidu_book_t book;
    thingsos_weidu_chapter_t chapter;
} parsed_t;
static bool fields(void* raw, const char* key, const char* text, char type, unsigned depth) {
    parsed_t* p = raw;
    if (type == '[' && p->array && (!strcmp(key, p->array) ||
        (!strcmp(p->array,"chapterInfos") && !strcmp(key,"updated")))) {
        p->array_seen = true;
        p->array_depth = (int)depth;
    }
    if (type == '{' && p->array_depth >= 0 && (int)depth == p->array_depth + 1) {
        p->in_record = true;
        memset(&p->book, 0, sizeof(p->book));
        p->cover_url[0]=0;
        memset(&p->chapter, 0, sizeof(p->chapter));
    }
    if (type == '}' && p->in_record && (int)depth == p->array_depth + 1) {
        p->in_record = false;
        bool shelf = !strcmp(p->array, "books");
        if (!(shelf ? id_ok(p->book.id) : id_ok(p->chapter.uid))) return false;
        if (p->records >= p->limit) {
            p->error = -9001;
            return false;
        }
        if (p->output && fwrite(
                             shelf ? (void*)&p->book : (void*)&p->chapter,
                             shelf ? sizeof(p->book) : sizeof(p->chapter), 1, p->output) != 1) {
            p->error = -9002;
            return false;
        }
        if(p->covers && p->cover_url[0]) {
            if(p->covers->io.cancelled && p->covers->io.cancelled(p->covers->io.context))return false;
            for(size_t i=0;i<p->cover_count;++i)if(!strcmp(p->cover_books[i].id,p->book.id)) {thingsos_weidu_cache_cover(p->covers,p->book.id,p->cover_url);break;}
        }
        ++p->records;
    }
    if (type == ']' && p->array_depth == (int)depth) p->array_depth = -1;
    if (type != 's' && type != 'n' && type != 'b') return true;
    if (!strcmp(key, "errcode") || !strcmp(key, "errCode")) p->error = atoi(text);
    if (!strcmp(key, "uid")) {
        if (strlen(text) >= sizeof(p->uid)) return false;
        copy(p->uid, sizeof(p->uid), text);
    }
    if (!strcmp(key, "bookId")) copy(p->book_id, sizeof(p->book_id), text);
    if (!strcmp(key, "webLoginVid") || !strcmp(key, "vid") || !strcmp(key, "userVid") ||
        !strcmp(key, "user_vid"))
        copy(p->vid, sizeof(p->vid), text);
    if (!strcmp(key, "accessToken")) {
        if (strlen(text) >= sizeof(p->token)) return false;
        copy(p->token, sizeof(p->token), text);
    }
    if (!strcmp(key, "logicCode")) copy(p->logic, sizeof(p->logic), text);
    if (!strcmp(key, "succeed") || !strcmp(key, "succ"))
        p->success = !strcmp(text, "true") || !strcmp(text, "1");
    if (p->in_record && (int)depth == p->array_depth + 2) {
        if (!strcmp(key, "bookId")) {
            if (strlen(text) >= sizeof(p->book.id)) return false;
            copy(p->book.id, sizeof(p->book.id), text);
        }
        if (!strcmp(key, "title")) {
            copy(p->book.title, sizeof(p->book.title), text);
            copy(p->chapter.title, sizeof(p->chapter.title), text);
        }
        if (!strcmp(key,"cover")) copy(p->cover_url,sizeof(p->cover_url),text);
        if (!strcmp(key, "author")) copy(p->book.author, sizeof(p->book.author), text);
        if (!strcmp(key, "readUpdateTime")) p->book.updated = (uint32_t)strtoul(text, NULL, 10);
        if (!strcmp(key, "chapterUid")) {
            if (strlen(text) >= sizeof(p->chapter.uid)) return false;
            copy(p->chapter.uid, sizeof(p->chapter.uid), text);
        }
        if (!strcmp(p->array,"chapterInfos") && !strcmp(key,"level") && type=='n') {
            char* end;long level=strtol(text,&end,10);
            if(!*end && level>=0 && level<=15){p->chapter.level=(uint8_t)level;p->chapter.level_tag=0xa5;}
        }
        if (!strcmp(key, "chapterIdx")) p->chapter.index = (uint32_t)strtoul(text, NULL, 10);
        if (!strcmp(key, "paid")) p->chapter.paid = !strcmp(text, "true") || atoi(text) != 0;
    }
    return true;
}
static bool cancelled(thingsos_weidu_client_t* c) {
    return c->io.cancelled && c->io.cancelled(c->io.context);
}
static void publish(thingsos_weidu_client_t* c, thingsos_weidu_phase_t phase, const char* text) {
    /* State and progress only; text can contain titles, login QR or credentials. */
    if(phase!=c->status.phase || phase==WD_ERROR)
        thingsos_diag_event(DIAG_TASK,1,(unsigned)phase,c->status.completed,0,0);
    c->status.phase = phase;
    c->status.authenticated = thingsos_weidu_session_valid(&c->session);
    copy(c->status.message, sizeof(c->status.message), text);
    ++c->status.serial;
    if (c->io.publish) c->io.publish(c->io.context, &c->status);
}
static thingsos_weidu_error_t request(
    thingsos_weidu_client_t* c, const char* method, const char* endpoint, const char* body,
    const char* referer, const char* name, size_t limit, char* out, size_t cap) {
    if (cancelled(c)) return WD_CANCEL;
    if (!path(out, cap, c->root, name)) return WD_LIMIT;
    thingsos_weidu_error_t e=WD_NETWORK;
    /* Only shelf/chapter/context downloads use this file channel: safe to replay. */
    for(unsigned attempt=0;attempt<3;++attempt) {
        if(cancelled(c))return WD_CANCEL;
        c->io.attempt=attempt;
        e=c->io.http(c->io.context,method,endpoint,body,referer,out,limit,&c->session);
        thingsos_diag_event(DIAG_NET_RETRY,1,attempt+1,3,e,0);
        if((e!=WD_NETWORK && e!=WD_DNS) || attempt==2)break;
        if(c->io.wait_ms)c->io.wait_ms(c->io.context,500u*(attempt+1));
    }
    return e;
}
static thingsos_weidu_error_t json_result_mode(const char* file, parsed_t* p, bool truncate_payloads,const thingsos_weidu_control_t* control) {
    bool ok = json_file(file, fields, p, truncate_payloads,control);
    if(thingsos_weidu_stopped(control))return WD_CANCEL;
    if (p->error == -2012) return WD_AUTH;
    if (p->error == -9001) return WD_LIMIT;
    if (p->error == -9002) return WD_STORAGE;
    return ok && !p->error ? WD_OK : WD_PROTOCOL;
}
static thingsos_weidu_error_t json_result(const char* file, parsed_t* p) {
    return json_result_mode(file, p, false,NULL);
}
static thingsos_weidu_error_t auth_json(
    thingsos_weidu_client_t* c, const char* method, const char* endpoint, const char* body,
    parsed_t* p) {
    if (cancelled(c)) return WD_CANCEL;
    if (!c->io.auth) return WD_PROTOCOL;
    char* data = thingsos_psram_alloc(16385);
    if (!data) return WD_MEMORY;
    size_t bytes = 0;
    thingsos_weidu_error_t e =
        c->io.auth(c->io.context, method, endpoint, body, data, 16385, &bytes, &c->session);
    if (e == WD_OK && bytes > 16384) e = WD_LIMIT;
    if (e == WD_OK) {
        json_t* j = thingsos_psram_calloc(1, sizeof(*j));
        if (!j)
            e = WD_MEMORY;
        else {
            j->memory = data;
            j->length = bytes;
            j->event = fields;
            j->ctx = p;
            bool ok = value(j, "", 0) && space(j) == EOF;
            free(j);
            e = p->error == -2012 ? WD_AUTH : ok && !p->error ? WD_OK : WD_PROTOCOL;
        }
    }
    volatile unsigned char* wipe = (volatile unsigned char*)data;
    for (size_t i = 0; i < 16385; ++i) wipe[i] = 0;
    free(data);
    return e;
}
static bool session_save(thingsos_weidu_client_t* c) {
    return c->io.session(c->io.context, &c->session, true);
}
thingsos_weidu_error_t thingsos_weidu_login(thingsos_weidu_client_t* c) {
    memset(&c->session, 0, sizeof(c->session));
    publish(c, WD_LOGIN, "正在获取登录二维码");
    parsed_t* p = thingsos_psram_calloc(1, sizeof(*p));
    if (!p) return WD_MEMORY;
    /* The first request after Wi-Fi association can lose the TLS/TCP race.
       Retrying here keeps the user on the login page instead of sending them
       back to Wi-Fi settings. */
    thingsos_weidu_error_t e = WD_NETWORK;
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        memset(p, 0, sizeof(*p));
        p->array_depth = -1;
        e = auth_json(c, "GET", "/api/auth/getLoginUid", NULL, p);
        if (e != WD_NETWORK && e != WD_DNS) break;
        if (cancelled(c)) {
            e = WD_CANCEL;
            break;
        }
        if (attempt < 2 && c->io.wait_ms) c->io.wait_ms(c->io.context, 1000u * (attempt + 1));
    }
    if (e != WD_OK || !thingsos_weidu_token(p->uid, 128)) {
        free(p);
        return e == WD_OK ? WD_PROTOCOL : e;
    }
    char uid[128];
    copy(uid, sizeof(uid), p->uid);
    snprintf(c->status.qr, sizeof(c->status.qr), "https://weread.qq.com/web/confirm?uid=%s", uid);
    publish(c, WD_QR, "请用微信扫码并确认登录");
    uint64_t started = c->io.now_ms(c->io.context);
    while (c->io.now_ms(c->io.context) - started < 240000) {
        for (unsigned i = 0; i < 20; ++i) {
            if (cancelled(c)) {
                free(p);
                return WD_CANCEL;
            }
            c->io.wait_ms(c->io.context, 100);
        }
        char endpoint[240];
        snprintf(endpoint, sizeof(endpoint), "/api/auth/getLoginInfo?uid=%s&otp=", uid);
        memset(p, 0, sizeof(*p));
        p->array_depth = -1;
        e = auth_json(c, "GET", endpoint, NULL, p);
        /* A server long-poll timeout or brief connection loss does not
           invalidate an already issued QR. Keep its original expiry. */
        if (e == WD_NETWORK) continue;
        if (e != WD_OK) {
            free(p);
            return e;
        }
        if (p->success) {
            copy(c->session.vid, sizeof(c->session.vid), p->vid);
            copy(c->session.skey, sizeof(c->session.skey), p->token);
            free(p);
            c->status.qr[0] = 0;
            if (!thingsos_weidu_session_valid(&c->session)) return WD_AUTH;
            if (!session_save(c)) return WD_STORAGE;
            return thingsos_weidu_sync(c);
        }
        if (p->logic[0] && strcmp(p->logic, "LOGIN_TIMEOUT")) {
            free(p);
            return WD_AUTH;
        }
    }
    free(p);
    return WD_AUTH;
}
typedef struct {
    char magic[8], account[33];
    uint32_t count;
} shelf_header_t;
thingsos_weidu_error_t thingsos_weidu_renew(thingsos_weidu_client_t* c) {
    if (!c->session.rt[0]) return WD_AUTH;
    parsed_t* p = thingsos_psram_calloc(1, sizeof(*p));
    if (!p) return WD_MEMORY;
    p->array_depth = -1;
    thingsos_weidu_error_t e = auth_json(
        c, "POST", "/web/login/renewal", "{\"rq\":\"%2Fweb%2Fbook%2Fread\",\"ql\":false}", p);
    bool success = p->success;
    free(p);
    if (e != WD_OK || !success || !thingsos_weidu_session_valid(&c->session)) return WD_AUTH;
    return session_save(c) ? WD_OK : WD_STORAGE;
}
typedef struct { uint32_t updated, position; } shelf_order_t;
static int shelf_recent(const void *a,const void *b) {
    const shelf_order_t *x=a,*y=b;
    if(x->updated!=y->updated)return x->updated>y->updated?-1:1;
    return x->position<y->position?-1:x->position>y->position;
}
bool thingsos_weidu_shelf_search(
    const char* root, const thingsos_weidu_session_t* s, size_t start, thingsos_weidu_book_t* books,
    size_t cap, size_t* count, size_t* total, const char* query) {
    *count = *total = 0;
    if (!thingsos_weidu_session_valid(s)) return false;
    char name[640], account[33];
    if (!path(name, sizeof(name), root, "shelf.dat")) return false;
    FILE* f = fopen(name, "rb");
    if (!f) {
        char backup[645];
        snprintf(backup, sizeof(backup), "%s.bak", name);
        if (!rename(backup, name)) f = fopen(name, "rb");
    }
    if (!f) return false;
    /* Each page re-reads the whole shelf; a 16 KiB buffer keeps a
       several-thousand book file to a few hundred SD reads. */
    char* buffer = thingsos_psram_alloc(16 * 1024);
    if (buffer && setvbuf(f, buffer, _IOFBF, 16 * 1024)) {
        free(buffer);
        buffer = NULL;
    }
    shelf_header_t h;
    thingsos_weidu_md5(s->vid, strlen(s->vid), account);
    bool ok = fread(&h, sizeof(h), 1, f) == 1 && !memcmp(h.magic, "WD_SHELF", 8) &&
              !memcmp(h.account, account, 33) && h.count <= WD_SHELF_MAX_RECORDS;
    if (ok) {
        long expected = (long)(sizeof(h) + (size_t)h.count * sizeof(*books));
        ok = !fseek(f, 0, SEEK_END) && ftell(f) == expected;
    }
    if (ok) {
        shelf_order_t *order=h.count?thingsos_psram_alloc((size_t)h.count*sizeof(*order)):NULL;
        if(h.count && !order)ok=false;
        size_t matched=0;
        thingsos_weidu_book_t book;
        if(ok)ok=!fseek(f,sizeof(h),SEEK_SET);
        for(uint32_t i=0;ok && i<h.count;i++) {
            ok=fread(&book,sizeof(book),1,f)==1 &&
                memchr(book.id,0,sizeof(book.id)) && id_ok(book.id) &&
                memchr(book.title,0,sizeof(book.title)) && memchr(book.author,0,sizeof(book.author));
            /* Skip items that are not books (followed 公众号 accounts/articles). */
            if(ok && !shelf_book(book.id))continue;
            if(ok && (!query || !query[0] || strstr(book.title,query)))
                order[matched++]=(shelf_order_t){book.updated,i};
        }
        if(ok) {
            *total=matched;
            *count=start<matched?matched-start:0;
            if(*count>cap)*count=cap;
            if(*count)qsort(order,matched,sizeof(*order),shelf_recent);
            for(size_t i=0;ok && i<*count;i++)
                ok=!fseek(f,(long)(sizeof(h)+(size_t)order[start+i].position*sizeof(*books)),SEEK_SET) && fread(&books[i],sizeof(*books),1,f)==1;
        }
        free(order);
    }
    fclose(f);
    free(buffer);
    if (!ok) *count = *total = 0;
    return ok;
}
bool thingsos_weidu_shelf_page(
    const char* root,const thingsos_weidu_session_t* session,size_t start,
    thingsos_weidu_book_t* books,size_t cap,size_t* count,size_t* total) {
    return thingsos_weidu_shelf_search(root,session,start,books,cap,count,total,NULL);
}
#include "weidu_shelf.inc"
typedef struct {
    char raw[600], tmp[600], final[600];
} sync_paths_scratch_t;
thingsos_weidu_error_t thingsos_weidu_sync(thingsos_weidu_client_t* c) {
    if (!thingsos_weidu_session_valid(&c->session)) return WD_AUTH;
    publish(c, WD_SHELF, "正在同步微读书架");
    sync_paths_scratch_t* sc = thingsos_psram_alloc(sizeof(*sc));
    if (!sc) return WD_MEMORY;
    thingsos_weidu_error_t e = request(
        c, "GET", "/web/shelf/sync", NULL, NULL, "shelf.json", WD_SHELF_MAX_BYTES, sc->raw,
        sizeof(sc->raw));
    if (e != WD_OK) {
        free(sc);
        return e;
    }
    path(sc->tmp, sizeof(sc->tmp), c->root, "shelf.tmp");
    path(sc->final, sizeof(sc->final), c->root, "shelf.dat");
    FILE* out = fopen(sc->tmp, "wb");
    if (!out) {
        free(sc);
        return WD_STORAGE;
    }
    shelf_header_t h = { { 0 }, { 0 }, 0 };
    memcpy(h.magic, "WD_SHELF", 8);
    thingsos_weidu_md5(c->session.vid, strlen(c->session.vid), h.account);
    bool ok = fwrite(&h, sizeof(h), 1, out) == 1;
    parsed_t* p = thingsos_psram_calloc(1, sizeof(*p));
    if (!p) {
        fclose(out);
        remove(sc->tmp);
        free(sc);
        return WD_MEMORY;
    }
    p->array = "books";
    p->array_depth = -1;
    p->output = out;
    p->limit = WD_SHELF_MAX_RECORDS;
    e = ok ? json_result(sc->raw, p) : WD_STORAGE;
    h.count = p->records;
    if (e == WD_OK && !p->array_seen) e = WD_PROTOCOL;
    free(p);
    if (e == WD_OK)
        ok = !fseek(out, 0, SEEK_SET) && fwrite(&h, sizeof(h), 1, out) == 1 &&
             thingsos_weidu_flush(out);
    if (fclose(out)) ok = false;
    if (cancelled(c)) e = WD_CANCEL;
    if (e != WD_OK || !ok || !commit_file(sc->tmp, sc->final)) {
        remove(sc->tmp);
        free(sc);
        return e != WD_OK ? e : WD_STORAGE;
    }
    free(sc);
    if (!session_save(c)) return WD_STORAGE;
    c->status.count = h.count;
    publish(
        c, WD_READY, h.count ? "书架已同步，点选书籍缓存阅读" : "书架为空，请在微信读书中添加书籍");
    return WD_OK;
}
thingsos_weidu_error_t thingsos_weidu_covers(thingsos_weidu_client_t* c,const thingsos_weidu_book_t* books,size_t count) {
    if(!c->io.image || !count || count>9)return WD_OK;
    char* raw=thingsos_psram_alloc(600);if(!raw)return WD_MEMORY;
    if(!path(raw,600,c->root,"shelf.json")){free(raw);return WD_LIMIT;}
    parsed_t* p=thingsos_psram_calloc(1,sizeof(*p));
    if(!p){free(raw);return WD_MEMORY;}
    p->array="books";p->array_depth=-1;p->limit=WD_SHELF_MAX_RECORDS;p->covers=c;p->cover_books=books;p->cover_count=count;
    json_result(raw,p);free(p);free(raw);
    return cancelled(c)?WD_CANCEL:WD_OK;
}
static bool prefix_zip(const char* p) {
    FILE* f = fopen(p, "rb");
    unsigned char b[4];
    bool ok = f && fread(b, 1, 4, f) == 4 && !memcmp(b, "PK\3\4", 4);
    if (f) fclose(f);
    return ok;
}
static bool validate_epub_detail(const char* file,char* detail,size_t capacity,const thingsos_weidu_control_t* control) {
    if(thingsos_weidu_stopped(control))return false;
    if(detail && capacity)snprintf(detail,capacity,"validate-memory");
    thingsos_epub_t* e = thingsos_psram_calloc(1, sizeof(*e));
    if (!e) return false;
    bool ok = thingsos_epub_open(e, file);
    if(!ok && detail && capacity)snprintf(detail,capacity,"validate-open");
    thingsos_chapter_t* chapter = thingsos_psram_calloc(1, sizeof(*chapter));
    if (!chapter) {ok = false;if(detail && capacity)snprintf(detail,capacity,"validate-memory");}
    for (size_t i = 0; ok && i < e->spine_count; ++i) {
        if(thingsos_weidu_stopped(control)){ok=false;break;}
        ok = thingsos_epub_chapter(e, i, chapter);
        if(!ok && detail && capacity)snprintf(detail,capacity,"validate-chapter=%u",(unsigned)i+1);
        thingsos_chapter_close(chapter);
    }
    free(chapter);
    thingsos_epub_close(e);
    free(e);
    return ok;
}
static bool validate_epub(const char* file) {return validate_epub_detail(file,NULL,0,NULL);}
/* Local failures are not server response errors. Preserve downloaded source
   and expose a bounded step/chapter so a retry can run without the network. */
static thingsos_weidu_error_t repair_chapter(thingsos_weidu_client_t*,
    const thingsos_weidu_book_t*,const char*,const char*,unsigned);
static thingsos_weidu_error_t generate_epub(thingsos_weidu_client_t* c,const char* target,const thingsos_weidu_book_t* book,const char* dir,const char* toc,unsigned count) {
    char detail[80],message[160];
    thingsos_weidu_control_t control={c->io.cancelled,c->io.context};
    bool packed=false;
    unsigned repaired[3],repairs=0;
    for(;;) {
        packed=thingsos_weidu_pack_control(target,book,dir,toc,count,detail,sizeof(detail),&control);
        bool valid=packed && validate_epub_detail(target,detail,sizeof(detail),&control);
        if(cancelled(c))return WD_CANCEL;
        if(valid)return WD_OK;
        unsigned chapter=0;
        /* Retry source damage only. Disk writes, allocation, ZIP and budget
           failures cannot be repaired by downloading the same chapter. */
        bool source_error=!packed &&
            (sscanf(detail,"normalize-tag-eof chapter=%u",&chapter)==1 ||
             sscanf(detail,"normalize-entity chapter=%u",&chapter)==1 ||
             sscanf(detail,"unclosed-markup chapter=%u",&chapter)==1 ||
             sscanf(detail,"normalize-open-input chapter=%u",&chapter)==1);
        if(packed)source_error=sscanf(detail,"validate-chapter=%u",&chapter)==1;
        if(!source_error || !chapter || chapter>count || repairs==3)break;
        bool repeated=false;
        for(unsigned i=0;i<repairs;++i)if(repaired[i]==chapter)repeated=true;
        if(repeated)break;
        repaired[repairs++]=chapter;
        snprintf(message,sizeof(message),"正在重新下载第 %u/%u 章并修复离线书",chapter,count);
        publish(c,WD_DOWNLOAD,message);
        thingsos_weidu_error_t e=repair_chapter(c,book,dir,toc,chapter-1);
        if(c->io.finish_network)c->io.finish_network(c->io.context);
        if(e!=WD_OK) {
            if(e!=WD_CANCEL){snprintf(message,sizeof(message),"第 %u 章重新下载失败，已下载章节保留",chapter);publish(c,WD_ERROR,message);}
            return e;
        }
        publish(c,WD_DOWNLOAD,"正在重新生成离线 EPUB");
    }
    snprintf(message,sizeof(message),"离线 EPUB %s失败：%s，已下载章节保留",packed?"校验":"生成",detail);
    publish(c,WD_ERROR,message);
    return packed?WD_PROTOCOL:WD_STORAGE;
}
static thingsos_weidu_error_t install_epub(
    thingsos_weidu_client_t* c, const char* from, const char* temporary, const char* final) {
    if (!validate_epub(from)) return WD_PROTOCOL;
    unsigned char* block = thingsos_psram_alloc(4096);
    if (!block) return WD_MEMORY;
    FILE *in = fopen(from, "rb"), *out = fopen(temporary, "wb");
    size_t n;
    bool ok = in && out;
    while (ok && (n = fread(block, 1, 4096, in)))
        ok = fwrite(block, 1, n, out) == n && !cancelled(c);
    free(block);
    if (in) {
        if (ferror(in)) ok = false;
        fclose(in);
    }
    if (out) {
        if (!thingsos_weidu_flush(out)) ok = false;
        if (fclose(out)) ok = false;
    }
    return ok && commit_file(temporary, final) ? WD_OK : cancelled(c) ? WD_CANCEL : WD_STORAGE;
}
#include "weidu_names.inc"
static thingsos_weidu_error_t download_failure(thingsos_weidu_client_t* c,thingsos_weidu_error_t e,const char* stage) {
    thingsos_diag_event(DIAG_FAILURE,3,(unsigned)c->status.phase,c->status.completed,e,0);
    if(e==WD_PROTOCOL || e==WD_NETWORK || e==WD_DNS || e==WD_STORAGE) {
        char detail[160];
        snprintf(detail,sizeof(detail),"%s：%s，原缓存保留",stage,e==WD_PROTOCOL?"响应格式不兼容":e==WD_DNS?"域名解析失败":e==WD_STORAGE?"存储读写失败":"连接超时或中断，请重试");
        publish(c,WD_ERROR,detail);
    }
    return e;
}
/* Download and EPUB validation nest with VFS/TLS; keep path and shard
   workspace on the heap instead of consuming the worker's internal stack. */
typedef struct {
    char raw[640], toc[600], body[1024], dir[600], final[600], temporary[640];
    char b[160], ch[160], referer[400], ps[128], shard[3][600], decoded[640];
    char marker[700], previous_table[620], pending_toc[640];
} download_workspace_t;
/* Reuse a saved chapter only when the previous table names the same chapter id
   and its text file is still a plausible size; the plain/markup flag comes from
   that table because the payload is not decoded again. */
static bool resume_chapter(const char* previous, const char* dir, unsigned index,
                           thingsos_weidu_chapter_t* chapter) {
    FILE* table = fopen(previous, "rb");
    if (!table) return false;
    thingsos_weidu_chapter_t old;
    bool same = !fseek(table, (long)(index * sizeof(old)), SEEK_SET) &&
                fread(&old, sizeof(old), 1, table) == 1 && !strcmp(old.uid, chapter->uid);
    fclose(table);
    if (!same) return false;
    char file[700];
    snprintf(file, sizeof(file), "%s/chapter-%u.raw", dir, index);
    FILE* raw = fopen(file, "rb");
    if (!raw) return false;
    bool ok = !fseek(raw, 0, SEEK_END);
    long size = ok ? ftell(raw) : 0;
    fclose(raw);
    if (!ok || size <= 0 || size > 512 * 1024) return false;
    chapter->plain = old.plain;
    return true;
}
#include "weidu_stream.inc"
/* Shared chapter fetch for normal downloads and bounded pack recovery. */
static thingsos_weidu_error_t fetch_chapter(thingsos_weidu_client_t* c,
    const thingsos_weidu_book_t* book,download_workspace_t* w,unsigned i,
    bool prefetch_active,unsigned p,bool repair,bool* installed,const char** stage,thingsos_weidu_chapter_t* chapter) {
    thingsos_weidu_control_t control={c->io.cancelled,c->io.context};
    thingsos_weidu_error_t e=WD_OK;
    FILE* f=NULL;
    *installed=false;
    do {
        f = fopen(w->toc, "rb");
        bool ok = f && !fseek(f, (long)(i * sizeof(*chapter)), SEEK_SET) &&
                  fread(chapter, sizeof(*chapter), 1, f) == 1;
        if (f) fclose(f);
        if (!ok) {
            e = WD_STORAGE;
            break;
        }
        if (!thingsos_weidu_encode_id(book->id, w->b) || !thingsos_weidu_encode_id(chapter->uid, w->ch)) {
            e = WD_PROTOCOL;
            break;
        }
        snprintf(w->referer, sizeof(w->referer), "https://weread.qq.com/web/reader/%sk%s", w->b, w->ch);
        *stage="阅读上下文";
        publish(c, WD_DOWNLOAD, chapter->title);
        if (prefetch_active) {
            e = c->io.prefetch_get(c->io.context, p, w->ps, sizeof(w->ps), 60000);
        } else {
            for(unsigned attempt=0;attempt<3;++attempt) {
                if(cancelled(c)){e=WD_CANCEL;break;}
                e=c->io.reader?c->io.reader(c->io.context,w->referer,w->ps,sizeof(w->ps),&c->session):WD_PROTOCOL;
                thingsos_diag_event(DIAG_NET_RETRY,2,attempt+1,3,e,0);
                if((e!=WD_NETWORK && e!=WD_DNS) || attempt==2)break;
                if(c->io.wait_ms)c->io.wait_ms(c->io.context,500u*(attempt+1));
            }
        }
        if (e != WD_OK) break;
        if (!thingsos_weidu_token(w->ps, sizeof(w->ps))) {
            e = WD_ACCESS;
            break;
        }
        *stage="章节请求参数";
        if (!thingsos_weidu_body(
                book->id, chapter->uid, w->ps, c->io.unix_time(c->io.context),
                c->io.random(c->io.context), w->body, sizeof(w->body))) {
            e = WD_PROTOCOL;
            break;
        }
        const char* endpoints[3] = { "/web/book/chapter/e_0", "/web/book/chapter/e_1",
                                     "/web/book/chapter/e_3" };
        *stage="正文主分片";
        e = request(
            c, "POST", endpoints[0], w->body, w->referer, "shard0", 64u * 1024u * 1024u, w->shard[0],
            sizeof(w->shard[0]));
        if (e != WD_OK) break;
        if (prefix_zip(w->shard[0])) {
            if(repair){e=WD_PROTOCOL;break;}
            e = install_epub(c, w->shard[0], w->temporary, w->final);
            *installed=true;
            break;
        }
        f = fopen(w->shard[0], "rb");
        int first=EOF;
        if(f)do { first=fgetc(f); } while(first!=EOF && isspace((unsigned char)first));
        if (f) fclose(f);
        bool plain = first == '{';
        if (plain) {
            *stage="正文类型识别";
            parsed_t* metadata = thingsos_psram_calloc(1, sizeof(*metadata));
            if (!metadata) {
                e = WD_MEMORY;
                break;
            }
            metadata->array_depth = -1;
            /* e_0 text metadata can include an entire chapter in content.
               Scan that payload with bounded storage; fields ignores its 't'
               event. Syntax, bookId and server errors still require validation,
               and the actual text is fetched through t_0/t_1 below. */
            e = json_result_mode(w->shard[0], metadata, true,&control);
            bool match = !strcmp(metadata->book_id, book->id);
            free(metadata);
            if (e != WD_OK || !match) {
                if (e == WD_OK) e = WD_PROTOCOL;
                break;
            }
            endpoints[0] = "/web/book/chapter/t_0";
            endpoints[1] = "/web/book/chapter/t_1";
            e = request(
                c, "POST", endpoints[0], w->body, w->referer, "shard0", 2u * 1024u * 1024u, w->shard[0],
                sizeof(w->shard[0]));
            if (e != WD_OK) break;
        }
        unsigned shards = plain ? 2 : 3;
        *stage="正文补充分片";
        for (unsigned s = 1; s < shards && e == WD_OK; ++s) {
            char name[16];
            snprintf(name, sizeof(name), "shard%u", s);
            e = request(
                c, "POST", endpoints[s], w->body, w->referer, name, 2u * 1024u * 1024u, w->shard[s],
                sizeof(w->shard[s]));
        }
        if (e != WD_OK) break;
        snprintf(w->decoded, sizeof(w->decoded), "%s/chapter-%u.raw.part", w->dir, i);
        const char* files[] = { w->shard[0], w->shard[1], w->shard[2] };
        *stage="正文解码校验";
        e = thingsos_weidu_decode_control(files, shards, w->decoded,&control);
        if (e != WD_OK) break;
        if (prefix_zip(w->decoded)) {
            if(repair){e=WD_PROTOCOL;break;}
            e = install_epub(c, w->decoded, w->temporary, w->final);
            *installed=true;
            break;
        }
        if(repair) {
            const char* reason=NULL;
            bool valid=thingsos_weidu_normalize_control(
                w->decoded,w->temporary,chapter->title,plain,&reason,&control);
            remove(w->temporary);
            if(!valid || cancelled(c)){e=cancelled(c)?WD_CANCEL:WD_PROTOCOL;break;}
        }
        chapter->plain = plain;
        f = fopen(w->toc, "r+b");
        ok = f && !fseek(f, (long)(i * sizeof(*chapter)), SEEK_SET) &&
             fwrite(chapter, sizeof(*chapter), 1, f) == 1;
        if (f) {
            if (fflush(f)) ok = false;
            if (fclose(f)) ok = false;
        }
        if (!ok) {
            e = WD_STORAGE;
            break;
        }
        if (!plain && chapter->paid) {
            f = fopen(w->decoded, "rb");
            bool markup = false;
            int v;
            if (f) {
                while ((v = fgetc(f)) != EOF)
                    if (v == '<') {
                        markup = true;
                        break;
                    }
                fclose(f);
            }
            if (!markup) {
                remove(w->decoded);
                e = WD_ACCESS;
                break;
            }
        }
        snprintf(w->raw,sizeof(w->raw),"%s/chapter-%u.raw",w->dir,i);
        if(cancelled(c)){remove(w->decoded);e=WD_CANCEL;break;}
        if(!commit_file(w->decoded,w->raw)){remove(w->decoded);e=WD_STORAGE;break;}
    } while(false);
    return e;
}
typedef struct {
    thingsos_weidu_client_t* client;
    const char* dir;
    uint64_t last_saved;
    thingsos_weidu_error_t error;
    bool stalled;
    char raw[640], cover[700], normalized[1152], file[640], tmp[650];
} offline_images_t;
static bool offline_cache_image(void* context, char* url, char name[41]) {
    offline_images_t* images = context;
    thingsos_weidu_client_t* c = images->client;
    if (!c->io.image || cancelled(c)) {
        if (cancelled(c)) images->error = WD_CANCEL;
        return false;
    }
    if (!thingsos_weidu_image_cache_key(url, images->normalized, sizeof(images->normalized), name))
        return false;
    if (!path(images->file, sizeof(images->file) - 5, images->dir, name)) return false;
    unsigned width = 0, height = 0;
    if (thingsos_file_image_info(images->file, &width, &height)) {
        if (c->io.now_ms) images->last_saved = c->io.now_ms(c->io.context);
        return true;
    }
    if (images->stalled) return false;
    uint64_t at = c->io.now_ms ? c->io.now_ms(c->io.context) : 0;
    if (at - images->last_saved >= 30000) {
        images->stalled = true;
        return false;
    }
    snprintf(images->tmp, sizeof(images->tmp), "%s.part", images->file);
    thingsos_weidu_error_t error = WD_OK;
    for (unsigned attempt = 0; attempt < 2; ++attempt) {
        if (cancelled(c)) { error = WD_CANCEL; break; }
        error = c->io.image(c->io.context, images->normalized, images->tmp, THINGSOS_MEMBER_BYTES);
        if (cancelled(c)) { error = WD_CANCEL; break; }
        if (error != WD_NETWORK && error != WD_DNS && error != WD_TLS) break;
        if (!attempt && c->io.wait_ms) c->io.wait_ms(c->io.context, 300);
    }
    bool valid = error == WD_OK && thingsos_file_image_info(images->tmp, &width, &height);
    bool saved = valid && !cancelled(c) && commit_file(images->tmp, images->file);
    remove(images->tmp);
    if (cancelled(c) || error == WD_CANCEL) {
        images->error = WD_CANCEL;
        return false;
    }
    if (error == WD_STORAGE || (valid && !saved)) {
        images->error = WD_STORAGE;
        return false;
    }
    if (saved && c->io.now_ms) images->last_saved = c->io.now_ms(c->io.context);
    return saved;
}
static thingsos_weidu_error_t cache_chapter_images(
    thingsos_weidu_client_t* c, const char* book_id, const char* dir, unsigned index,
    const thingsos_weidu_chapter_t* chapter) {
    if (cancelled(c)) return WD_CANCEL;
    if (!c->io.image || chapter->plain) return WD_OK;
    offline_images_t* images = thingsos_psram_calloc(1, sizeof(*images));
    if (!images) return WD_MEMORY;
    unsigned width = 0, height = 0;
    if (!index && thingsos_weidu_cover_chapter(chapter->title) &&
        cover_path(c->root, book_id, images->cover, sizeof(images->cover)) &&
        thingsos_file_image_info(images->cover, &width, &height)) {
        free(images);
        return WD_OK;
    }
    snprintf(images->raw, sizeof(images->raw), "%s/chapter-%u.raw", dir, index);
    images->client = c;
    images->dir = dir;
    images->error = WD_OK;
    images->last_saved = c->io.now_ms ? c->io.now_ms(c->io.context) : 0;
    thingsos_weidu_control_t control = {c->io.cancelled, c->io.context};
    thingsos_weidu_scan_images(images->raw, chapter->plain, &control, offline_cache_image, images);
    thingsos_weidu_error_t e = cancelled(c) ? WD_CANCEL : images->error;
    free(images);
    return e;
}
static thingsos_weidu_error_t repair_chapter(thingsos_weidu_client_t* c,
    const thingsos_weidu_book_t* book,const char* dir,const char* toc,unsigned index) {
    download_workspace_t* w=thingsos_psram_calloc(1,sizeof(*w));
    if(!w)return WD_MEMORY;
    copy(w->dir,sizeof(w->dir),dir);copy(w->toc,sizeof(w->toc),toc);
    /* A chapter repair must never install a server-supplied whole-book ZIP. */
    snprintf(w->temporary,sizeof(w->temporary),"%s/repair.epub.part",dir);
    snprintf(w->final,sizeof(w->final),"%s/repair.epub",dir);
    thingsos_weidu_chapter_t chapter={0};
    bool installed=false;const char* stage="章节修复";
    thingsos_weidu_error_t e=fetch_chapter(c,book,w,index,false,0,true,&installed,&stage,&chapter);
    if(e==WD_OK && !installed)e=cache_chapter_images(c,book->id,dir,index,&chapter);
    remove(w->temporary);remove(w->final);remove(w->decoded);
    free(w);
    return installed?WD_PROTOCOL:e;
}
static thingsos_weidu_error_t download_run(
    thingsos_weidu_client_t* c, const thingsos_weidu_book_t* book, download_workspace_t* w,
    bool online, unsigned wanted, bool retain_network, unsigned amount) {
    thingsos_weidu_control_t control={c->io.cancelled,c->io.context};
    if (!thingsos_weidu_session_valid(&c->session)) return WD_AUTH;
    if (!id_ok(book->id)) return WD_PROTOCOL;
    if (c->io.unix_time(c->io.context) < 1577836800) return WD_CLOCK;
    if(thingsos_weidu_covers(c,book,1)==WD_CANCEL)return WD_CANCEL;
    snprintf(w->dir, sizeof(w->dir), "%s/book-%.64s", c->root, book->id);
    wd_mkdir(w->dir);
    /* Only an interrupted run resumes: the marker survives a failure and is
       cleared on success, so asking to cache again always re-fetches. */
    snprintf(w->marker, sizeof(w->marker), "%s/download.inprogress", w->dir);
    FILE* progress = fopen(w->marker, "rb");
    bool has_stream=thingsos_weidu_stream_path(c->root,book->id,w->raw,sizeof(w->raw)) && stream_exists(w->raw);
    bool resumable = progress != NULL || online || has_stream;
    if (progress)
        fclose(progress);
    else if (!online && (progress = fopen(w->marker, "wb")))
        fclose(progress);
    if(!thingsos_weidu_cache_named(c->output,book->id,book->title,w->final,sizeof(w->final)))return WD_STORAGE;
    snprintf(w->temporary, sizeof(w->temporary), "%s.part", w->final);
    copy(c->status.title, sizeof(c->status.title), book->title);
    c->status.completed = c->status.total = 0;
    const char* stage="章节目录";
    path(w->toc, sizeof(w->toc), w->dir, "toc.dat");
    unsigned count=online?stream_count(w->dir):0;
    thingsos_weidu_error_t e=WD_OK;
    FILE* f=NULL;
    snprintf(w->previous_table,sizeof(w->previous_table),"%s%s",w->toc,count?"":".prev");
    if(!count) {
    publish(c, WD_DOWNLOAD, "正在读取章节目录");
    snprintf(w->body, sizeof(w->body), "{\"bookIds\":[\"%s\"]}", book->id);
    e = request(
        c, "POST", "/web/book/chapterInfos", w->body, NULL, "toc.json", 2u * 1024u * 1024u, w->raw,
        sizeof(w->raw));
    if (e != WD_OK) return download_failure(c,e,stage);
    path(w->toc, sizeof(w->toc), w->dir, "toc.dat");
    /* An interrupted run leaves its chapters on disk. Keep the previous table
       beside the new one: it proves which chapter id each saved file is, so a
       later run can resume instead of downloading the whole book again. */
    snprintf(w->previous_table, sizeof(w->previous_table), "%s.prev", w->toc);
    snprintf(w->pending_toc,sizeof(w->pending_toc),"%s.part",w->toc);
    thingsos_diag_event(DIAG_WEIDU_CACHE,DIAG_WD_TOC_PARSE_START,online,0,0,0);
    f = fopen(w->pending_toc, "wb");
    if (!f) {
        thingsos_diag_event(DIAG_WEIDU_CACHE,DIAG_WD_TOC_COMMIT,online,0,WD_STORAGE,errno);
        return WD_STORAGE;
    }
    parsed_t* p = thingsos_psram_calloc(1, sizeof(*p));
    if (!p) {
        thingsos_diag_event(DIAG_WEIDU_CACHE,DIAG_WD_TOC_PARSE,online,0,WD_MEMORY,0);
        fclose(f);
        return WD_MEMORY;
    }
    p->array = "chapterInfos";
    p->array_depth = -1;
    p->output = f;
    p->limit = THINGSOS_SPINE_LIMIT;
    e = json_result_mode(w->raw, p,false,&control);
    count = p->records;
    if (e == WD_OK && (!p->array_seen || !count)) e = WD_PROTOCOL;
    thingsos_diag_event(DIAG_WEIDU_CACHE,DIAG_WD_TOC_PARSE,online,count,e,p->error);
    free(p);
    thingsos_diag_event(DIAG_WEIDU_CACHE,DIAG_WD_TOC_COMMIT_START,online,count,0,0);
    bool write_failed = fflush(f) || ferror(f);
    int write_error = write_failed ? errno : 0;
    if (fclose(f)) {write_failed = true;write_error = errno;}
    if (write_failed) {
        e = WD_STORAGE;
        thingsos_diag_event(DIAG_WEIDU_CACHE,DIAG_WD_TOC_COMMIT,online,count,e,write_error);
    }
    if (e != WD_OK) {remove(w->pending_toc);return download_failure(c,e,stage);}
    struct stat old_toc;
    if(!stat(w->toc,&old_toc)) {
        remove(w->previous_table);
        if(rename(w->toc,w->previous_table)){
            thingsos_diag_event(DIAG_WEIDU_CACHE,DIAG_WD_TOC_COMMIT,online,count,WD_STORAGE,errno);
            remove(w->pending_toc);return WD_STORAGE;
        }
    }
    if(!commit_file(w->pending_toc,w->toc)) {
        thingsos_diag_event(DIAG_WEIDU_CACHE,DIAG_WD_TOC_COMMIT,online,count,WD_STORAGE,errno);
        rename(w->previous_table,w->toc);remove(w->pending_toc);return WD_STORAGE;
    }
    thingsos_diag_event(DIAG_WEIDU_CACHE,DIAG_WD_TOC_COMMIT,online,count,WD_OK,0);
    }
    if(online && wanted>=count)wanted=0;
    unsigned first=online?wanted:0,end=online && amount<count-wanted?wanted+amount:count;
    c->status.total = end-first;
    thingsos_weidu_chapter_t* chapter = thingsos_psram_calloc(1, sizeof(*chapter));
    if (!chapter) return WD_MEMORY;
    unsigned* pending = thingsos_psram_calloc(count ? count : 1, sizeof(*pending));
    if (!pending) { free(chapter); return WD_MEMORY; }
    unsigned pending_count = 0;
    bool prefetch_active = false;
    /* Resume first so the prefetch workers see only chapters that still need
       a reader token. */
    for (unsigned i = first; i < end; ++i) {
        if (cancelled(c)) { e = WD_CANCEL; goto finish; }
        if(online && has_stream && thingsos_weidu_stream_images_ready(c->root,book->id,i,false)) {
            ++c->status.completed;continue;
        }
        if(online && has_stream && thingsos_weidu_stream_ready(c->root,book->id,i)) {
            /* Legacy and partial chapters retain their text while the image
               pass is retried from the already validated local raw source. */
            snprintf(w->raw,sizeof(w->raw),"%s/chapter-%u.raw",w->dir,i);
            struct stat st;
            if(!stat(w->raw,&st) && st.st_size>0 && st.st_size<=THINGSOS_XML_BYTES) {
                bool upgraded=stream_commit(c,book,w->dir,i);
                if(cancelled(c)){e=WD_CANCEL;goto finish;}
                if(upgraded || thingsos_weidu_stream_ready(c->root,book->id,i)) {
                    ++c->status.completed;continue;
                }
            }
        }
        f = fopen(w->toc, "rb");
        bool ok = f && !fseek(f, (long)(i * sizeof(*chapter)), SEEK_SET) &&
                  fread(chapter, sizeof(*chapter), 1, f) == 1;
        if (f) fclose(f);
        if (!ok) { e = WD_STORAGE; goto finish; }
        if (resumable && resume_chapter(w->previous_table, w->dir, i, chapter)) {
            f = fopen(w->toc, "r+b");
            bool kept = f && !fseek(f, (long)(i * sizeof(*chapter)), SEEK_SET) &&
                        fwrite(chapter, sizeof(*chapter), 1, f) == 1;
            if (f) {
                if (fflush(f)) kept = false;
                if (fclose(f)) kept = false;
            }
            if (!kept) { e = WD_STORAGE; goto finish; }
            if(online && !stream_commit(c,book,w->dir,i)){e=WD_STORAGE;stage="章节缓存整理";goto finish;}
            if(!online){stage="正文图片缓存";e=cache_chapter_images(c,book->id,w->dir,i,chapter);if(e!=WD_OK)goto finish;}
            ++c->status.completed;
            publish(c, WD_DOWNLOAD, chapter->title);
            continue;
        }
        pending[pending_count++] = i;
    }
    if (pending_count && (!online || pending_count>1) && c->io.prefetch_start)
        prefetch_active = c->io.prefetch_start(c->io.context, w->toc, book->id, pending,
                                               pending_count);
    for (unsigned p = 0; p < pending_count; ++p) {
        if (cancelled(c)) {
            e = WD_CANCEL;
            break;
        }
        const unsigned i = pending[p];
        bool installed=false;
        e=fetch_chapter(c,book,w,i,prefetch_active,p,false,&installed,&stage,chapter);
        if(installed){online=false;goto finish;}
        if(e!=WD_OK)break;
        if(online && !stream_commit(c,book,w->dir,i)){e=WD_STORAGE;stage="章节缓存整理";break;}
        if(!online){stage="正文图片缓存";e=cache_chapter_images(c,book->id,w->dir,i,chapter);if(e!=WD_OK)break;}
        ++c->status.completed;
        publish(c, WD_DOWNLOAD, chapter->title);
    }
    if(prefetch_active && c->io.prefetch_stop){c->io.prefetch_stop(c->io.context);prefetch_active=false;}
    if (e == WD_OK && !cancelled(c)) {
        if(!retain_network && c->io.finish_network)c->io.finish_network(c->io.context);
        if(online) {
            if(e==WD_OK && cancelled(c))e=WD_CANCEL;
        } else {
        stage="离线 EPUB 生成";
        publish(c, WD_DOWNLOAD, "正在生成离线 EPUB");
        e=generate_epub(c,w->temporary,book,w->dir,w->toc,count);
        if(e==WD_OK && cancelled(c))
            e = WD_CANCEL;
        else if (e==WD_OK && !commit_file(w->temporary, w->final))
            e = WD_STORAGE;
        }
    }
finish:
    if(cancelled(c))e=WD_CANCEL;
    if (prefetch_active && c->io.prefetch_stop) c->io.prefetch_stop(c->io.context);
    free(pending);
    free(chapter);
    remove(w->temporary);
    if(strcmp(w->previous_table,w->toc))remove(w->previous_table);
    if (e == WD_OK) {
        if(online)thingsos_weidu_stream_path(c->root,book->id,c->status.path,sizeof(c->status.path));
        else copy(c->status.path, sizeof(c->status.path), w->final);
        publish(c, WD_READY, online?"章节已就绪":"缓存完成，可离线阅读");
        if(!online)remove(w->marker);
    }
    return c->status.phase==WD_ERROR && !strcmp(stage,"离线 EPUB 生成")?e:download_failure(c,e,stage);
}
thingsos_weidu_error_t thingsos_weidu_download(
    thingsos_weidu_client_t* c, const thingsos_weidu_book_t* book) {
#ifdef ESP_PLATFORM
    download_workspace_t* w = heap_caps_calloc(1,sizeof(*w),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
#else
    download_workspace_t* w = calloc(1, sizeof(*w));
#endif
    if (!w) return WD_MEMORY;
    thingsos_weidu_error_t e = download_run(c, book, w, false, 0, false, 0);
    free(w);
    return e;
}
thingsos_weidu_error_t thingsos_weidu_stream_fetch(thingsos_weidu_client_t* c,const thingsos_weidu_book_t* book,unsigned index) {
    if(!c || !book || !id_ok(book->id))return WD_PROTOCOL;
    download_workspace_t* w=thingsos_psram_calloc(1,sizeof(*w));
    if(!w)return WD_MEMORY;
    thingsos_weidu_error_t e=download_run(c,book,w,true,index,false,1);
    free(w);return e;
}
thingsos_weidu_error_t thingsos_weidu_stream_window(thingsos_weidu_client_t* c,const thingsos_weidu_book_t* book,unsigned first,unsigned amount) {
    if(!c || !book || !id_ok(book->id) || !amount || amount>3)return WD_PROTOCOL;
    download_workspace_t* w=thingsos_psram_calloc(1,sizeof(*w));
    if(!w)return WD_MEMORY;
    thingsos_weidu_error_t e=download_run(c,book,w,true,first,true,amount);
    if(c->io.finish_network)c->io.finish_network(c->io.context);
    if(e==WD_OK)publish(c,WD_READY,"阅读缓冲已就绪");
    free(w);return e;
}
/* Rebuild already downloaded local data; never treat chapter count as a
   committed EPUB. All source files must exist and the output is validated. */
bool thingsos_weidu_partial(const char* root,const char* id,unsigned* count) {
    if(count)*count=0;
    if(!id_ok(id))return false;
    char file[700];snprintf(file,sizeof(file),"%s/book-%s/toc.dat",root,id);
    FILE* toc=fopen(file,"rb");if(!toc)return false;
    bool ok=!fseek(toc,0,SEEK_END);long bytes=ok?ftell(toc):-1;rewind(toc);
    unsigned n=bytes>0?(unsigned)((size_t)bytes/sizeof(thingsos_weidu_chapter_t)):0;
    ok=ok && n && n<=THINGSOS_SPINE_LIMIT && (size_t)bytes==n*sizeof(thingsos_weidu_chapter_t);
    thingsos_weidu_chapter_t chapter;
    for(unsigned i=0;ok && i<n;++i) {
        ok=fread(&chapter,sizeof(chapter),1,toc)==1 && memchr(chapter.uid,0,sizeof(chapter.uid)) &&
            memchr(chapter.title,0,sizeof(chapter.title)) && id_ok(chapter.uid);
        if(!ok)break;
        snprintf(file,sizeof(file),"%s/book-%s/chapter-%u.raw",root,id,i);
        FILE* raw=fopen(file,"rb");ok=raw && !fseek(raw,0,SEEK_END);
        long size=ok?ftell(raw):0;ok=ok && size>0 && size<=512*1024;
        if(raw)fclose(raw);
    }
    fclose(toc);if(ok && count)*count=n;return ok;
}
thingsos_weidu_error_t thingsos_weidu_restore(thingsos_weidu_client_t* c,const thingsos_weidu_book_t* book) {
    if(!thingsos_weidu_session_valid(&c->session))return WD_AUTH;
    unsigned count=0;if(!thingsos_weidu_partial(c->root,book->id,&count))return WD_STORAGE;
#ifdef ESP_PLATFORM
    download_workspace_t* w=heap_caps_calloc(1,sizeof(*w),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
#else
    download_workspace_t* w=calloc(1,sizeof(*w));
#endif
    if(!w)return WD_MEMORY;
    snprintf(w->dir,sizeof(w->dir),"%s/book-%.64s",c->root,book->id);
    snprintf(w->toc,sizeof(w->toc),"%s/book-%.64s/toc.dat",c->root,book->id);
    if(!thingsos_weidu_cache_named(c->output,book->id,book->title,w->final,sizeof(w->final))){free(w);return WD_STORAGE;}
    snprintf(w->temporary,sizeof(w->temporary),"%s.part",w->final);
    c->status.total=c->status.completed=count;
    copy(c->status.title,sizeof(c->status.title),book->title);
    publish(c,WD_DOWNLOAD,"正在用已下载章节生成离线书");
    if(c->io.finish_network)c->io.finish_network(c->io.context);
    if(c->io.wait_ms)c->io.wait_ms(c->io.context,200);
    thingsos_weidu_error_t e=cancelled(c)?WD_CANCEL:WD_OK;
    if(e==WD_OK)e=generate_epub(c,w->temporary,book,w->dir,w->toc,count);
    if(e==WD_OK && cancelled(c))e=WD_CANCEL;
    if(e==WD_OK && !commit_file(w->temporary,w->final))e=WD_STORAGE;
    remove(w->temporary);
    if(e==WD_OK){copy(c->status.path,sizeof(c->status.path),w->final);publish(c,WD_READY,"缓存完成，可离线阅读");}
    free(w);return c->status.phase==WD_ERROR?e:download_failure(c,e,"离线缓存恢复");
}
const char* thingsos_weidu_error_text(thingsos_weidu_error_t e) {
    static const char* const text[] = { "完成",
                                        "网络请求失败，请检查 WiFi 后重试",
                                        "登录失效，请重新扫码",
                                        "服务响应格式不兼容，原缓存保留",
                                        "本机存储操作失败，请检查存储后重试",
                                        "运行内存不足，请稍后重试",
                                        "响应或章节超出处理预算，原缓存保留",
                                        "操作已取消，原缓存保留",
                                        "请先联网校准时间",
                                        "正文暂不可用，请确认阅读权限后重试",
                                        "HTTPS 安全连接失败，请重试",
                                        "域名解析失败，请稍后重试或切换网络",
                                        "请到微信读书官网完成安全验证后重试",
                                        "平台未确认本次提交，时长不重复补报" };
    return (unsigned)e < sizeof(text) / sizeof(text[0]) ? text[e] : text[WD_PROTOCOL];
}

/* Internal streaming reader shared by the notes adapter. */
bool thingsos_weidu_json_events(const char* file,bool (*event)(void*,const char*,const char*,char,unsigned),void* context){
#ifdef ESP_PLATFORM
    json_t* j=heap_caps_calloc(1,sizeof(*j),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
#else
    json_t* j=calloc(1,sizeof(*j));
#endif
    if(!j)return false;
    j->f=fopen(file,"rb");j->event=event;j->ctx=context;j->truncate_strings=true;
    bool ok=j->f&&value(j,"",0)&&space(j)==EOF&&!ferror(j->f);
    if(j->f)fclose(j->f);
    free(j);return ok;
}

bool thingsos_weidu_json_memory_events(const char* text,size_t length,bool (*event)(void*,const char*,const char*,char,unsigned),void* context){
#ifdef ESP_PLATFORM
    json_t* j=heap_caps_calloc(1,sizeof(*j),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
#else
    json_t* j=calloc(1,sizeof(*j));
#endif
    if(!j)return false;
    j->memory=text;j->length=length;j->event=event;j->ctx=context;
    bool ok=value(j,"",0)&&space(j)==EOF;
    free(j);return ok;
}
