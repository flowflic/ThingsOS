/* SPDX-License-Identifier: Apache-2.0 */
#include "thingsos/weidu.h"
#include "thingsos/weidu_reading.h"
#include "thingsos/weidu_stream.h"
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "thingsos/content.h"
#include "thingsos/graphics.h"
#include "thingsos/manage.h"
#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#define MKDIR(p) mkdir(p, 0775)
#endif
#define CHECK(x)                                                       \
    do {                                                               \
        if (!(x)) {                                                    \
            fprintf(stderr, "Weidu test line %d: %s\n", __LINE__, #x); \
            exit(1);                                                   \
        }                                                              \
    } while (0)
typedef struct {
    uint64_t now;
    bool cancel;
    unsigned qr, saves, requests;
    unsigned poll_failures, poll_calls, uid_failures, toc_failures, reader_failures;
    unsigned shards, reader_calls, chapters, fail_chapter,network_finishes,link_waits;
    unsigned prefetch_starts,prefetch_stops,prefetch_count,prefetch_indices[3];
    int mode;
    bool bundle,notes_fail;
    unsigned note_requests;
    unsigned inline_requests,inline_delay_ms;
    bool inline_fail,inline_cancel;
    thingsos_weidu_session_t saved;
    char fixture[512];
    const char* primary_fixture;
    const char* corrupt_repair_path;
} mock_t;
static bool write_text(const char* name, const char* text) {
    FILE* f = fopen(name, "wb");
    if (!f) return false;
    bool ok = fwrite(text, 1, strlen(text), f) == strlen(text);
    return fclose(f) == 0 && ok;
}
static void write_shard(const char* destination, const char* body) {
    char md5[33];
    thingsos_weidu_md5(body, strlen(body), md5);
    FILE* f = fopen(destination, "wb");
    CHECK(f);
    CHECK(fwrite(md5, 1, 32, f) == 32 && fwrite(body, 1, strlen(body), f) == strlen(body));
    CHECK(!fclose(f));
}
static thingsos_weidu_error_t public_image(void* raw,const char* url,const char* file,size_t limit) {
    mock_t* m=raw;
    static const unsigned char png[]={
            0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,0,0,0,13,0x49,0x48,0x44,0x52,
            0,0,0,4,0,0,0,2,8,0,0,0,0,0x5a,0xc3,0x22,0xbf,0,0,0,15,0x49,0x44,0x41,0x54,
            0x78,0x9c,0x63,0x60,0x70,0x68,0xf8,0xcf,0,0x22,0,0x0e,0x43,3,0x7f,0x22,0x2e,0x46,0x2f,
            0,0,0,0,0x49,0x45,0x4e,0x44,0xae,0x42,0x60,0x82};
    if(limit==THINGSOS_MEMBER_BYTES) {
        CHECK(!strcmp(url,"https://res.weread.qq.com/figure.png?x=1&y=2") ||
              !strcmp(url,"https://res.weread.qq.com/retry.png") || strstr(url,"https://res.weread.qq.com/comic-page-"));
        ++m->inline_requests;
        m->now+=m->inline_delay_ms;
        if(m->inline_fail && strstr(url,"retry.png"))return WD_NETWORK;
        FILE* out=fopen(file,"wb");CHECK(out && fwrite(png,1,sizeof(png),out)==sizeof(png) && !fclose(out));
        if(m->inline_cancel){m->cancel=true;return WD_CANCEL;}
        return WD_OK;
    }
    CHECK(strstr(url,"https://cdn.weread.qq.com/") && limit==256u*1024u);
    ++m->requests;
    FILE* out=fopen(file,"wb");CHECK(out && fwrite(png,1,sizeof(png),out)==sizeof(png) && !fclose(out));
    return WD_OK;
}
static bool store(void* raw, thingsos_weidu_session_t* s, bool save) {
    mock_t* m = raw;
    if (save) {
        m->saved = *s;
        ++m->saves;
        return true;
    }
    *s = m->saved;
    return thingsos_weidu_session_valid(s);
}
static bool cancelled(void* raw) {
    return ((mock_t*)raw)->cancel;
}
static uint64_t clock_ms(void* raw) {
    return ((mock_t*)raw)->now;
}
static uint32_t unix_time(void* raw) {
    return ((mock_t*)raw)->bundle?WD_SYNC_MIN_TIME+100:1700000000;
}
static uint32_t random_value(void* raw) {
    (void)raw;
    return 9;
}
static void wait_ms(void* raw, unsigned ms) {
    ((mock_t*)raw)->now += ms;
}
static void finish_network(void* raw){++((mock_t*)raw)->network_finishes;}
static void wait_link(void* raw,unsigned max_ms){CHECK(max_ms>=3000);++((mock_t*)raw)->link_waits;}
static bool window_prefetch_start(void* raw,const char* toc,const char* id,const unsigned* indices,unsigned count) {
    mock_t* m=raw;CHECK(toc && strstr(toc,"toc.dat") && !strcmp(id,"1234567890") && count<=3);
    ++m->prefetch_starts;m->prefetch_count=count;
    for(unsigned i=0;i<count;++i)m->prefetch_indices[i]=indices[i];
    return true;
}
static thingsos_weidu_error_t window_prefetch_get(void* raw,unsigned sequence,char* ps,size_t cap,unsigned timeout) {
    mock_t* m=raw;CHECK(sequence<m->prefetch_count && cap>=128 && timeout==60000);
    strcpy(ps,"wiretoken");return WD_OK;
}
static void window_prefetch_stop(void* raw){++((mock_t*)raw)->prefetch_stops;}
static void publish(void* raw, const thingsos_weidu_status_t* s) {
    mock_t* m = raw;
    if(m->corrupt_repair_path && !strcmp(s->message,"正在重新生成离线 EPUB"))
    {
        CHECK(write_text(m->corrupt_repair_path,"<p class=broken\">damaged"));
        char toc[700];strcpy(toc,m->corrupt_repair_path);strcpy(strrchr(toc,'/')+1,"toc.dat");
        FILE* f=fopen(toc,"r+b");thingsos_weidu_chapter_t ch;
        CHECK(f && !fseek(f,48*sizeof(ch),SEEK_SET) && fread(&ch,sizeof(ch),1,f)==1);
        ch.plain=false;CHECK(!fseek(f,48*sizeof(ch),SEEK_SET) && fwrite(&ch,sizeof(ch),1,f)==1 && !fclose(f));
    }
    if (s->phase == WD_QR) {
        ++m->qr;
        CHECK(strstr(s->qr, "https://weread.qq.com/web/confirm?uid="));
    }
    CHECK(!strstr(s->message, "secret") && !strstr(s->qr, "secret"));
}
static thingsos_weidu_error_t http(
    void* raw, const char* method, const char* endpoint, const char* body, const char* referer,
    const char* destination, size_t limit, thingsos_weidu_session_t* session) {
    mock_t* m = raw;
    ++m->requests;
    if(strstr(endpoint,"bestbookmarks") || !strcmp(endpoint,"/api/agent/gateway") || strstr(endpoint,"/web/review/list")) {
        ++m->note_requests;
        if(m->notes_fail)return WD_NETWORK;
        CHECK(write_text(destination,strstr(endpoint,"bestbookmarks")?"{\"items\":[{\"chapterUid\":1,\"markText\":\"Popular\",\"range\":\"0-4\"}]}":
            !strcmp(endpoint,"/api/agent/gateway")?"{\"updated\":[{\"chapterUid\":1,\"markText\":\"Mine\",\"range\":\"0-4\"}]}":
            "{\"reviews\":[{\"review\":{\"content\":\"Review\",\"chapterUid\":1}}],\"reviewsHasMore\":0}"));
        return WD_OK;
    }
    CHECK(!strstr(endpoint, "/api/auth/") && strcmp(endpoint, "/web/login/renewal"));
    CHECK(!strstr(endpoint, "/web/reader/"));
    size_t allowed;
    if (!strcmp(endpoint, "/web/book/chapter/e_0")) allowed = 64u * 1024u * 1024u;
    else if (!strcmp(endpoint, "/web/shelf/sync")) allowed = 24u * 1024u * 1024u;
    else allowed = 2u * 1024u * 1024u;
    CHECK(limit <= allowed);
    (void)referer;
    if (m->cancel) return WD_CANCEL;
    if (!strcmp(endpoint, "/api/auth/getLoginUid"))
        CHECK(write_text(destination, "{\"uid\":\"public-uid\"}"));
    else if (strstr(endpoint, "/api/auth/getLoginInfo?")) {
        CHECK(write_text(
            destination,
            "{\"succeed\":true,\"data\":{\"webLoginVid\":123,\"accessToken\":\"secret\"}}"));
        strcpy(session->rt, "refresh-secret");
    } else if (!strcmp(endpoint, "/web/login/renewal")) {
        CHECK(!strcmp(method, "POST") && strstr(body, "ql"));
        strcpy(session->skey, "renewed-secret");
        CHECK(write_text(destination, "{\"succ\":true}"));
    } else if (!strcmp(endpoint, "/web/shelf/sync")) {
        if (m->mode == 1) return WD_NETWORK;
        if (m->mode == 2)
            CHECK(write_text(
                destination, "{\"books\":[{\"bookId\":\"1\",\"title\":\"bad\"}],\"broken\":}"));
        else if (m->mode == 3)
            CHECK(write_text(destination, "{\"errcode\":-2012}"));
        else if (m->mode == 7)
            CHECK(write_text(destination, "{\"books\":[{\"bookId\":\"../escape\"}]}"));
        else if (m->mode == 8) {
            FILE* f = fopen(destination, "wb");
            CHECK(f);
            fputs("{\"books\":[", f);
            for (unsigned i = 0; i < 20001; ++i)
                fprintf(f, "%s{\"bookId\":\"%u\",\"title\":\"book\"}", i ? "," : "", i);
            fputs("]}", f);
            CHECK(!fclose(f));
        } else if (m->mode == 15) {
            FILE* f = fopen(destination, "wb");
            CHECK(f);
            fputs("{\"books\":[", f);
            for (unsigned i = 0; i < 5000; ++i)
                fprintf(f, "%s{\"bookId\":\"%u\",\"title\":\"book %u\",\"author\":\"a\",\"readUpdateTime\":%u}",
                        i ? "," : "", 100000u + i, i, i);
            fputs("]}", f);
            CHECK(!fclose(f));
        } else if(m->mode==11)CHECK(write_text(destination,
            "{\"books\":[{\"bookId\":\"3\",\"title\":\"Tie first\",\"readUpdateTime\":5},{\"bookId\":\"4\",\"title\":\"Tie second\",\"readUpdateTime\":5},{\"bookId\":\"2\",\"title\":\"Old\",\"readUpdateTime\":1},{\"bookId\":\"1234567890\",\"title\":\"New\",\"readUpdateTime\":2}]}"));
        else
            CHECK(write_text(
                destination,
                "{\"books\":[{\"bookId\":\"2\",\"title\":\"Second\",\"author\":\"B\",\"cover\":\"https://cdn.weread.qq.com/test.jpg\",\"readUpdateTime\":1},"
                "{\"bookId\":\"1234567890\",\"title\":\"A \\u4e2d\\u6587 \\uD83D\\uDE00\",\"author\":\"author\",\"readUpdateTime\":2}]}"));
    } else if (!strcmp(endpoint, "/web/book/chapterInfos")) {
        if(m->mode==12 && m->toc_failures++<2)return WD_NETWORK;
        CHECK(!strcmp(method, "POST") && strstr(body, m->primary_fixture?"491760":"1234567890"));
        if(m->mode==25)CHECK(write_text(destination,"{\"chapterInfos\":[{\"chapterUid\":1,\"title\":\"partial\"}],\"broken\":}"));
        else if(m->mode==14) {
            FILE* toc=fopen(destination,"wb");CHECK(toc);
            fputs("{\"data\":[{\"bookId\":\"1234567890\",\"chapterInfos\":[",toc);
            for(unsigned i=0;i<49;++i) {
                char title[64];snprintf(title,sizeof(title),"Chapter %u",i+1);
                if(i<3)strcpy(title,i==0?"Part A":i==1?"Section B":"Detail C");
                fprintf(toc,"%s{\"chapterUid\":%u,\"chapterIdx\":%u,\"title\":\"%s\",\"level\":%u,\"paid\":0}",i?",":"",i+1,i+1,title,i<3?i+1:1);
            }
            fputs("]}]}",toc);CHECK(!fclose(toc));
        }
        else if(m->mode==9)CHECK(write_text(destination,
            "{\"data\":[{\"bookId\":\"1234567890\",\"updated\":[{\"chapterUid\":1,\"chapterIdx\":1,\"title\":\"Updated schema\",\"paid\":0}]}]}"));
        else if(m->chapters>1) {
            FILE* toc=fopen(destination,"wb");CHECK(toc);
            fputs("{\"data\":[{\"bookId\":\"1234567890\",\"chapterInfos\":[",toc);
            for(unsigned i=0;i<m->chapters;++i)fprintf(toc,"%s{\"chapterUid\":%u,\"chapterIdx\":%u,\"title\":\"Chapter %u\",\"paid\":0}",i?",":"",i+1,i+1,i+1);
            fputs("]}]}",toc);CHECK(!fclose(toc));
        }
        else CHECK(write_text(
            destination,
            "{\"data\":[{\"bookId\":\"1234567890\",\"chapterInfos\":[{\"chapterUid\":1,"
            "\"chapterIdx\":1,\"title\":\"Chapter one\",\"paid\":0}]}]}"));
    } else if (strstr(endpoint, "/web/reader/"))
        CHECK(write_text(
            destination, "<html><script>var reader={\"psvts\":\"wiretoken\"}</script></html>"));
    else if (!strcmp(endpoint, "/web/book/chapter/e_0")) {
        CHECK(body && strstr(body, "\"s\":") && !strcmp(method, "POST"));
        if(m->mode>=16 && m->mode<=24) {
            FILE* f=fopen(destination,"wb");CHECK(f);
            CHECK(fputs("{\"content\":\"",f)>=0);
            /* Long ignored payloads must still be scanned after the 4 KiB
               prefix, including JSON escapes and surrogate pairs. */
            for(unsigned i=0;i<8000;++i)CHECK(fputs("正文\\n\\uD83D\\uDE00",f)>=0);
            if(m->mode==19)CHECK(fputs("\\q",f)>=0); /* Invalid escape beyond prefix. */
            if(m->mode!=20)CHECK(fputs("\"",f)>=0); /* Unterminated payload. */
            if(m->mode!=18)CHECK(fprintf(f,",\"bookId\":\"%s\"",m->mode==17?"wrong":"1234567890")>0);
            if(m->mode==21)CHECK(fputs(",\"errcode\":-2012",f)>=0);
            if(m->mode==22)CHECK(fputs(",\"errcode\":-1234",f)>=0);
            if(m->mode==24) {
                CHECK(fputs(",\"bookId\":\"",f)>=0);
                for(unsigned i=0;i<5000;++i)CHECK(fputc('1',f)!=EOF);
                CHECK(fputc('"',f)!=EOF);
            }
            CHECK(fputc('}',f)!=EOF);
            if(m->mode==23)CHECK(fputs(" trailing",f)>=0);
            CHECK(!fclose(f));
        } else if (m->mode == 4 || m->mode == 6 || m->mode == 10 || m->mode == 14)
            CHECK(write_text(destination, " \n\t{\"bookId\":\"1234567890\"}"));
        else if (m->mode == 5) {
            m->cancel = true;
            return WD_CANCEL;
        } else {
            FILE *in = fopen(m->primary_fixture?m->primary_fixture:m->fixture, "rb"), *out = fopen(destination, "wb");
            CHECK(in && out);
            char block[4096];
            size_t n;
            while ((n = fread(block, 1, sizeof(block), in))) CHECK(fwrite(block, 1, n, out) == n);
            CHECK(!ferror(in));
            fclose(in);
            CHECK(!fclose(out));
        }
    } else if (!strcmp(endpoint, "/web/book/chapter/t_0")) {
        ++m->shards;
        write_shard(destination, "xVsSG");
        if (m->mode == 6) {
            FILE* f = fopen(destination, "r+b");
            CHECK(f);
            CHECK(!fseek(f,33,SEEK_SET) && fputc('?', f) != EOF);
            fclose(f);
        } else if(m->mode==10) {
            FILE* f=fopen(destination,"r+b");CHECK(f);
            CHECK(fwrite("opaque-prefix-0123456789012345678",1,32,f)==32);
            CHECK(!fclose(f));
        }
    } else if (!strcmp(endpoint, "/web/book/chapter/t_1"))
        write_shard(destination, "bG8=");
    else {
        fprintf(stderr, "Unexpected mock endpoint %s\n", endpoint);
        return WD_PROTOCOL;
    }
    return WD_OK;
}
static thingsos_weidu_error_t auth(
    void* raw, const char* method, const char* endpoint, const char* body, char* data, size_t cap,
    size_t* bytes, thingsos_weidu_session_t* session) {
    mock_t* m = raw;
    ++m->requests;
    const char* response;
    if (!strcmp(endpoint,"/web/shelf/add")) {
        CHECK(!strcmp(method,"POST") && strstr(body,"\"bookIds\":[\"999\"]"));
        if(m->mode==30)return WD_NETWORK;
        response=m->mode==31?"{\"succ\":0}":m->mode==32?"{\"other\":1}":"{\"succ\":1}";
    } else if (!strcmp(endpoint, "/api/auth/getLoginUid")) {
        if (m->uid_failures) {
            --m->uid_failures;
            return WD_NETWORK;
        }
        response = "{\"uid\":\"public-uid\"}";
    } else if (strstr(endpoint, "/api/auth/getLoginInfo?")) {
        ++m->poll_calls;
        if(m->poll_failures) {
            --m->poll_failures;
            m->now+=20000;
            return WD_NETWORK;
        }
        response = "{\"succeed\":true,\"data\":{\"webLoginVid\":123,\"accessToken\":\"secret\"}}";
        strcpy(session->rt, "refresh-secret");
    } else if (!strcmp(endpoint, "/web/login/renewal")) {
        CHECK(!strcmp(method, "POST") && strstr(body, "ql"));
        strcpy(session->skey, "renewed-secret");
        response = "{\"succ\":true}";
    } else
        return WD_PROTOCOL;
    *bytes = strlen(response);
    CHECK(*bytes < cap);
    memcpy(data, response, *bytes + 1);
    return WD_OK;
}
typedef struct {unsigned calls;thingsos_weidu_error_t first,last;} read_retry_t;
static thingsos_weidu_error_t retry_read(thingsos_weidu_client_t* c,void* raw){
    (void)c;read_retry_t* r=raw;return ++r->calls==1?r->first:r->last;
}
static unsigned char* bytes(const char* path, size_t* n) {
    FILE* f = fopen(path, "rb");
    CHECK(f && !fseek(f, 0, SEEK_END));
    long size = ftell(f);
    CHECK(size >= 0);
    *n = (size_t)size;
    rewind(f);
    unsigned char* data = malloc(*n + 1);
    CHECK(data && fread(data, 1, *n, f) == *n);
    fclose(f);
    data[*n] = 0;
    return data;
}
static thingsos_weidu_error_t reader(
    void* raw, const char* referer, char* token, size_t cap, thingsos_weidu_session_t* s) {
    mock_t* m=raw;
    if(m->mode==13 && m->reader_failures++<2)return WD_NETWORK;
    ++m->reader_calls;
    if(m->fail_chapter && m->reader_calls==m->fail_chapter) {
        m->fail_chapter=0;
        return WD_PROTOCOL;
    }
    (void)s;
    CHECK(!strncmp(referer, "https://weread.qq.com/web/reader/", 31) && cap >= 128);
    const char* html = "<html><script>{\"psvts\": \"wiretoken\"}</script></html>";
    thingsos_weidu_ps_t p = { 0 };
    for (size_t i = 0; i < strlen(html); ++i)
        CHECK(thingsos_weidu_ps_feed(&p, (const unsigned char*)html + i, 1));
    CHECK(p.state == 4);
    CHECK(p.offset == 35);
    strcpy(token, p.value);
    return WD_OK;
}
static void cache_names_test(const char* root) {
    char dir[640],legacy[768],named[768],again[768],source[640],id[64];
    snprintf(dir,sizeof(dir),"%s/cache-names-test",root);MKDIR(dir);
    snprintf(source,sizeof(source),"%s/valid.epub",root);size_t size;unsigned char* raw=bytes(source,&size);
    snprintf(legacy,sizeof(legacy),"%s/微读-77.epub",dir);
    FILE* f=fopen(legacy,"wb");CHECK(f && fwrite(raw,1,size,f)==size && !fclose(f));
    thingsos_path_mapper(thingsos_weidu_cache_identity);uint64_t before=thingsos_path_id(legacy);
    CHECK(thingsos_weidu_cache_names_migrate(dir)>=1);
    CHECK(thingsos_weidu_cached(dir,"77",named,sizeof(named)) && strcmp(named,legacy));
    CHECK(strstr(named,"山野来信") && thingsos_weidu_cache_id(named,id,sizeof(id)) && !strcmp(id,"77"));
    CHECK(thingsos_path_id(named)==before);size_t after;unsigned char* copy=bytes(named,&after);CHECK(after==size && !memcmp(copy,raw,size));free(copy);
    CHECK(thingsos_weidu_cache_named(dir,"78","同名书",named,sizeof(named)));
    f=fopen(named,"wb");CHECK(f && fwrite(raw,1,size,f)==size && !fclose(f));
    CHECK(thingsos_weidu_cache_named(dir,"79","同名书",again,sizeof(again)) && strcmp(named,again));
    f=fopen(again,"wb");CHECK(f && fputs("second",f)>=0 && !fclose(f));
    CHECK(thingsos_weidu_cache_id(again,id,sizeof(id)) && !strcmp(id,"79"));
    CHECK(thingsos_weidu_cached(dir,"78",source,sizeof(source)) && !strcmp(source,named));
    uint64_t stable=thingsos_path_id(named);
    CHECK(thingsos_weidu_cache_named(dir,"78","重命名 / : 测试",again,sizeof(again)) && strcmp(named,again));
    CHECK(!strstr(strrchr(again,'/')+1,"/") && !strchr(strrchr(again,'/')+1,':') && thingsos_path_id(again)==stable);
    CHECK(!remove(again) && !thingsos_weidu_cached(dir,"78",named,sizeof(named)));
    thingsos_path_mapper(NULL);free(raw);
}
typedef struct {unsigned calls,after;} cancel_limit_t;
static bool stop_after(void* context){cancel_limit_t* s=context;return ++s->calls>=s->after;}
static void cache_pipeline_cancel_test(const char* root) {
    char dir[640],raw[700],xml[700],toc[700],packed[700];
    snprintf(dir,sizeof(dir),"%s/cancel-pipeline",root);MKDIR(dir);
    snprintf(raw,sizeof(raw),"%s/chapter-0.raw",dir);snprintf(xml,sizeof(xml),"%s/chapter.xhtml",dir);
    snprintf(toc,sizeof(toc),"%s/toc.dat",dir);snprintf(packed,sizeof(packed),"%s/candidate.epub.part",dir);
    const char* input="A&<B>\"C\t\r\n正文\n";
    const char* escaped="A&amp;&lt;B&gt;&quot;C\t</p><p>正文</p><p>";
    FILE* f=fopen(raw,"wb");CHECK(f);for(unsigned i=0;i<3000;++i)CHECK(fwrite(input,1,strlen(input),f)==strlen(input));CHECK(!fclose(f));
    const char* reason;CHECK(thingsos_weidu_normalize(raw,xml,"Title",true,&reason));
    const char* header="<?xml version=\"1.0\" encoding=\"UTF-8\"?><html xmlns=\"http://www.w3.org/1999/xhtml\"><body><h1>Title</h1><p>";
    char block[256];f=fopen(xml,"rb");CHECK(f && fread(block,1,strlen(header),f)==strlen(header) && !memcmp(block,header,strlen(header)));
    for(unsigned i=0;i<3000;++i)CHECK(fread(block,1,strlen(escaped),f)==strlen(escaped) && !memcmp(block,escaped,strlen(escaped)));
    const char* tail="</p></body></html>";CHECK(fread(block,1,strlen(tail),f)==strlen(tail) && !memcmp(block,tail,strlen(tail)) && fgetc(f)==EOF && !fclose(f));
    cancel_limit_t state={0,3};thingsos_weidu_control_t control={stop_after,&state};
    CHECK(!thingsos_weidu_normalize_control(raw,xml,"Title",true,&reason,&control) && !strcmp(reason,"cancelled"));
    CHECK(!(f=fopen(xml,"rb")));
    thingsos_weidu_chapter_t row={0};strcpy(row.uid,"1");strcpy(row.title,"Title");row.plain=true;
    f=fopen(toc,"wb");CHECK(f && fwrite(&row,sizeof(row),1,f)==1 && !fclose(f));
    thingsos_weidu_book_t book={0};strcpy(book.id,"9");strcpy(book.title,"Title");
    state=(cancel_limit_t){0,12};char detail[96];
    CHECK(!thingsos_weidu_pack_control(packed,&book,dir,toc,1,detail,sizeof(detail),&control) && !(f=fopen(packed,"rb")));
    char shards[2][700];const char* paths[2]={shards[0],shards[1]};
    for(unsigned i=0;i<2;++i){
        snprintf(shards[i],sizeof(shards[i]),"%s/shard%u",dir,i);f=fopen(shards[i],"wb");CHECK(f);
        CHECK(fwrite("01234567890123456789012345678901",1,32,f)==32);if(!i)CHECK(fputc('X',f)!=EOF);
        for(unsigned j=0;j<50000;++j)CHECK(fputc('A',f)!=EOF);CHECK(!fclose(f));
    }
    state=(cancel_limit_t){0,18};CHECK(thingsos_weidu_decode_control(paths,2,xml,&control)==WD_CANCEL && !(f=fopen(xml,"rb")));
}
static void catalog_results(void) {
    thingsos_weidu_book_t books[2];unsigned count;
    const char* nested="{\"data\":{\"books\":[{\"title\":\"外层\",\"bookInfo\":{\"bookId\":123,\"title\":\"\\u7403\\u5f62\\u95ea\\u7535\",\"author\":\"刘慈欣\"}},"
        "{\"bookId\":\"123\",\"title\":\"重复\"},{\"bookId\":\"invalid\",\"title\":\"无效\"},{\"bookId\":\"456\",\"title\":\"三体\"},{\"bookId\":789,\"title\":\"超出容量\"}]}}";
    CHECK(thingsos_weidu_catalog_results(nested,strlen(nested),books,2,&count)==WD_OK && count==2);
    CHECK(!strcmp(books[0].id,"123") && !strcmp(books[0].title,"球形闪电") && !strcmp(books[0].author,"刘慈欣"));
    CHECK(!strcmp(books[1].id,"456") && !strcmp(books[1].title,"三体"));
    const char* covers="{\"books\":[{\"bookId\":\"3002309559\",\"cover\":\"https://cdn.weread.qq.com/other.jpg\"},"
        "{\"cover\":\"https://cdn.weread.qq.com/outer.jpg\",\"bookInfo\":{\"cover\":\"https://cdn.weread.qq.com/target.jpg\",\"bookId\":\"25504039\"}},"
        "{\"bookId\":\"33\",\"cover\":\"https://qq.com.example.test/bad.jpg\"}]}";
    char cover_url[640];
    CHECK(thingsos_weidu_catalog_cover(covers,strlen(covers),"25504039",cover_url,sizeof(cover_url)));
    CHECK(!strcmp(cover_url,"https://cdn.weread.qq.com/target.jpg"));
    CHECK(!thingsos_weidu_catalog_cover(covers,strlen(covers),"999",cover_url,sizeof(cover_url)));
    CHECK(!thingsos_weidu_catalog_cover(covers,strlen(covers),"33",cover_url,sizeof(cover_url)));
    const char* empty="{\"data\":{\"books\":[{\"bookId\":1,\"title\":\"wrapped\"}]},\"books\":[]}";
    CHECK(thingsos_weidu_catalog_results(empty,strlen(empty),books,2,&count)==WD_OK && !count);
    const char* denied="{\"errcode\":-2010,\"misc\":{\"books\":[]}}";
    CHECK(thingsos_weidu_catalog_results(denied,strlen(denied),books,2,&count)==WD_PROTOCOL && !count);
    char* large=malloc(46000);CHECK(large);
    const char* prefix="{\"ignored\":\"";
    strcpy(large,prefix);memset(large+strlen(prefix),'x',45000);
    strcpy(large+strlen(prefix)+45000,"\",\"books\":[{\"bookId\":\"42\",\"title\":\"球形闪电\"}]}");
    size_t bytes=strlen(large);
    CHECK(thingsos_weidu_catalog_results(large,bytes,books,2,&count)==WD_OK && count==1 && !strcmp(books[0].id,"42"));
    CHECK(thingsos_weidu_catalog_results(large,bytes-1,books,2,&count)==WD_PROTOCOL && !count);
    free(large);
}
int main(int argc, char** argv) {
    catalog_results();
    char public_cover[640];
    CHECK(thingsos_weidu_cover_url("http://wfqqreader-1252317822.image.myqcloud.com/cover.jpg",public_cover,sizeof(public_cover)));
    CHECK(!strncmp(public_cover,"https://",8));
    CHECK(thingsos_weidu_cover_url("https://cdn.weread.qq.com/cover.jpg",public_cover,sizeof(public_cover)));
    CHECK(!thingsos_weidu_cover_url("https://qq.com.evil.test/a.jpg",public_cover,sizeof(public_cover)));
    CHECK(!thingsos_weidu_cover_url("https://qq.com@evil.test/a.jpg",public_cover,sizeof(public_cover)));
    CHECK(!thingsos_weidu_cover_url("file:///a.jpg",public_cover,sizeof(public_cover)));
    CHECK(!thingsos_weidu_cover_url("https://qq.com/a\r\nCookie: bad",public_cover,sizeof(public_cover)));
    const char* image_origin="https://cdn.weread.qq.com/books/cover.jpg?old=1";
    CHECK(thingsos_weidu_image_redirect_url(image_origin,"https://img.qpic.cn/new.jpg?sig=x%2Fy&v=1",public_cover,sizeof(public_cover)));
    CHECK(!strcmp(public_cover,"https://img.qpic.cn/new.jpg?sig=x%2Fy&v=1"));
    CHECK(thingsos_weidu_image_redirect_url(image_origin,"//img.qpic.cn/new.jpg",public_cover,sizeof(public_cover)));
    CHECK(!strcmp(public_cover,"https://img.qpic.cn/new.jpg"));
    CHECK(thingsos_weidu_image_redirect_url(image_origin,"/new.jpg",public_cover,sizeof(public_cover)));
    CHECK(!strcmp(public_cover,"https://cdn.weread.qq.com/new.jpg"));
    CHECK(thingsos_weidu_image_redirect_url(image_origin,"new.jpg",public_cover,sizeof(public_cover)));
    CHECK(!strcmp(public_cover,"https://cdn.weread.qq.com/books/new.jpg"));
    CHECK(thingsos_weidu_image_redirect_url(image_origin,"?new=1",public_cover,sizeof(public_cover)));
    CHECK(!strcmp(public_cover,"https://cdn.weread.qq.com/books/cover.jpg?new=1"));
    CHECK(!thingsos_weidu_image_redirect_url(image_origin,"http://img.qpic.cn/a.jpg",public_cover,sizeof(public_cover)));
    CHECK(!thingsos_weidu_image_redirect_url(image_origin,"//qq.com.evil.test/a.jpg",public_cover,sizeof(public_cover)));
    CHECK(!thingsos_weidu_image_redirect_url(image_origin,"https://qq.com@evil.test/a.jpg",public_cover,sizeof(public_cover)));
    CHECK(!thingsos_weidu_image_redirect_url(image_origin,"/a.jpg\r\nCookie: bad",public_cover,sizeof(public_cover)));
    CHECK(!thingsos_weidu_image_redirect_url(image_origin,"/a.jpg",public_cover,20));
    const char* comic_cdn="https://wrco-40036.sh.gfp.tencent-cloud.com/CB_3300016974/Figure-0005-0001.jpg?sig=x%2Fy&v=1";
    CHECK(thingsos_weidu_image_redirect_url("https://res.weread.qq.com/figure.jpg",comic_cdn,public_cover,sizeof(public_cover)));
    CHECK(!strcmp(public_cover,comic_cdn));
    CHECK(!thingsos_weidu_cover_url(comic_cdn,public_cover,sizeof(public_cover)));
    CHECK(!thingsos_weidu_image_redirect_url(image_origin,"https://other.sh.gfp.tencent-cloud.com/figure.jpg",public_cover,sizeof(public_cover)));
    CHECK(argc == 2 || argc == 3);
    char hash[33], encoded[160], sign[17], request[1024];
    char cache_id[64];
    CHECK(thingsos_weidu_cache_id("/sdcard/books/微读-123_ab.epub",cache_id,sizeof(cache_id)) && !strcmp(cache_id,"123_ab"));
    CHECK(!thingsos_weidu_cache_id("/sdcard/books/微读-123.epub.part",cache_id,sizeof(cache_id)));
    CHECK(!thingsos_weidu_cache_id("/sdcard/books/微读-123.epub.bak",cache_id,sizeof(cache_id)));
    CHECK(!thingsos_weidu_cache_id("/sdcard/books/微读-123.invalid.epub",cache_id,sizeof(cache_id)));
    CHECK(!thingsos_weidu_cache_id("/sdcard/books/local.epub",cache_id,sizeof(cache_id)));
    thingsos_weidu_md5("", 0, hash);
    CHECK(!strcmp(hash, "d41d8cd98f00b204e9800998ecf8427e"));
    thingsos_weidu_md5("abc", 3, hash);
    CHECK(!strcmp(hash, "900150983cd24fb0d6963f7d28e17f72"));
    CHECK(
        thingsos_weidu_encode_id("1234567890", encoded) &&
        !strcmp(encoded, "e80329f0775bcd15g0109e5"));
    thingsos_weidu_sign(
        "b=abc&c=def&ct=1700000000&pc=ghi&prevChapter=false&ps=jkl&r=81&sc=1&st=0", sign);
    CHECK(!strcmp(sign, "784d746a"));
    CHECK(
        thingsos_weidu_body(
            "1234567890", "1", "wiretoken", 1700000000, 9, request, sizeof(request)) &&
        strstr(request, "\"r\":81"));
    CHECK(!thingsos_weidu_body("1", "1", "bad\"token", 1700000000, 9, request, sizeof(request)));
    CHECK(!thingsos_weidu_body("1", "1", "ok", 0, 9, request, sizeof(request)));
    mock_t m = { .poll_failures = 1, .uid_failures = 1 };
    snprintf(m.fixture, sizeof(m.fixture), "%s/valid.epub", argv[1]);
    /* An AP disconnect mid-body is an mbedTLS socket error without
       verification flags: replayable network loss, not a TLS failure. */
    CHECK(thingsos_weidu_transport_error(76,0)==WD_NETWORK);   /* NET_RECV_FAILED (diag 0.2.2) */
    CHECK(thingsos_weidu_transport_error(-0x4C,0)==WD_NETWORK);
    CHECK(thingsos_weidu_transport_error(0x4E,0)==WD_NETWORK);  /* NET_SEND_FAILED */
    CHECK(thingsos_weidu_transport_error(0x50,0)==WD_NETWORK);  /* NET_CONN_RESET */
    CHECK(thingsos_weidu_transport_error(0x7280,0)==WD_NETWORK);/* SSL_CONN_EOF */
    CHECK(thingsos_weidu_transport_error(0,0)==WD_NETWORK);
    CHECK(thingsos_weidu_transport_error(76,8)==WD_TLS);        /* verification flags win */
    CHECK(thingsos_weidu_transport_error(0x2700,0)==WD_TLS);    /* X509 verify failed */
    CHECK(thingsos_weidu_transport_error(-0x7780,0)==WD_TLS);   /* fatal alert */
    CHECK(strstr(thingsos_weidu_link_lost_text(),"WiFi 连接中断"));
    thingsos_weidu_client_t* c = calloc(1, sizeof(*c));
    CHECK(c);
    snprintf(c->root, sizeof(c->root), "%s/weidu-test", argv[1]);
    MKDIR(c->root);
    snprintf(c->output, sizeof(c->output), "%s/weidu-output", argv[1]);
    MKDIR(c->output);
    c->io = (thingsos_weidu_io_t){ &m,           http,    store,   cancelled, clock_ms, unix_time,
                                   random_value, wait_ms, publish, auth,      reader };
    CHECK(thingsos_weidu_login(c) == WD_OK && m.qr == 1 && m.saves >= 2 && c->status.count == 2);
    CHECK(m.poll_calls == 2); /* Same QR survives one failed confirmation poll. */
    CHECK(thingsos_weidu_session_valid(&c->session));
    bool renewed;unsigned auth_requests=m.requests;
    read_retry_t rr={0,WD_AUTH,WD_OK};
    CHECK(thingsos_wd_authenticated_read(c,false,retry_read,&rr,&renewed)==WD_OK && renewed && rr.calls==2 && m.requests==auth_requests+1);
    auth_requests=m.requests;rr=(read_retry_t){0,WD_OK,WD_OK};
    CHECK(thingsos_wd_authenticated_read(c,true,retry_read,&rr,&renewed)==WD_OK && renewed && rr.calls==1 && m.requests==auth_requests+1);
    auth_requests=m.requests;rr=(read_retry_t){0,WD_AUTH,WD_AUTH};
    CHECK(thingsos_wd_authenticated_read(c,false,retry_read,&rr,&renewed)==WD_AUTH && renewed && rr.calls==2 && m.requests==auth_requests+1);
    auth_requests=m.requests;rr=(read_retry_t){0,WD_NETWORK,WD_OK};
    CHECK(thingsos_wd_authenticated_read(c,false,retry_read,&rr,&renewed)==WD_NETWORK && !renewed && rr.calls==1 && m.requests==auth_requests);
    m.cancel=true;rr.calls=0;CHECK(thingsos_wd_authenticated_read(c,true,retry_read,&rr,&renewed)==WD_CANCEL && !rr.calls && m.requests==auth_requests);m.cancel=false;
    char saved_refresh[sizeof(c->session.rt)];strcpy(saved_refresh,c->session.rt);c->session.rt[0]=0;rr.calls=0;
    CHECK(thingsos_wd_authenticated_read(c,true,retry_read,&rr,&renewed)==WD_AUTH && !rr.calls && m.requests==auth_requests);strcpy(c->session.rt,saved_refresh);

    thingsos_weidu_book_t books[4];
    size_t count, total;
    CHECK(
        thingsos_weidu_shelf_page(c->root, &c->session, 0, books, 4, &count, &total) &&
        count == 2 && total == 2 && strstr(books[0].title, "中文"));
    char cover_dir[600];snprintf(cover_dir,sizeof(cover_dir),"%s/covers",c->root);MKDIR(cover_dir);
    char cover_file[700];snprintf(cover_file,sizeof(cover_file),"%s/covers/2.jpg",c->root);
    CHECK(write_text(cover_file,"invalid cover"));c->io.image=public_image;
    CHECK(thingsos_weidu_covers(c,books,count)==WD_OK);
    FILE* image_file=fopen(cover_file,"rb");CHECK(image_file);fclose(image_file);
    unsigned cover_w,cover_h;
    CHECK(thingsos_file_image_info(cover_file,&cover_w,&cover_h) && cover_w==4 && cover_h==2);
    unsigned requests_after_cover=m.requests;
    CHECK(thingsos_weidu_covers(c,books,count)==WD_OK && m.requests==requests_after_cover);
    c->io.image=NULL;
    thingsos_weidu_session_t other = c->session;
    strcpy(other.vid, "999");
    CHECK(!thingsos_weidu_shelf_page(c->root, &other, 0, books, 4, &count, &total));
    for (int mode = 1; mode <= 3; ++mode) {
        m.mode = mode;
        CHECK(
            thingsos_weidu_sync(c) == (mode == 1   ? WD_NETWORK
                                       : mode == 2 ? WD_PROTOCOL
                                                   : WD_AUTH));
        CHECK(
            thingsos_weidu_shelf_page(c->root, &c->session, 0, books, 4, &count, &total) &&
            total == 2);
    }
    m.mode = 7;
    CHECK(thingsos_weidu_sync(c) == WD_PROTOCOL);
    m.mode = 8;
    CHECK(thingsos_weidu_sync(c) == WD_LIMIT);
    CHECK(
        thingsos_weidu_shelf_page(c->root, &c->session, 0, books, 4, &count, &total) && total == 2);
    /* A several-thousand-book shelf must sync and page instead of reporting
       "over budget" and leaving the shelf empty. */
    thingsos_weidu_book_t large[4];
    size_t large_count, large_total;
    m.mode = 15;
    CHECK(thingsos_weidu_sync(c) == WD_OK && c->status.count == 5000);
    CHECK(
        thingsos_weidu_shelf_page(c->root, &c->session, 0, large, 4, &large_count, &large_total) &&
        large_total == 5000 && large_count == 4 && strstr(large[0].title, "book 4999"));
    m.mode = 0;
    CHECK(thingsos_weidu_renew(c) == WD_OK && !strcmp(c->session.skey, "renewed-secret"));
    CHECK(thingsos_weidu_download(c, &books[0]) == WD_OK);
    char cached[512];
    CHECK(thingsos_weidu_cached(c->output, books[0].id, cached, sizeof(cached)));
    size_t old_size;
    unsigned char* old = bytes(cached, &old_size);
    m.mode = 5;
    CHECK(thingsos_weidu_download(c, &books[0]) == WD_CANCEL);
    m.cancel = false;
    size_t new_size;
    unsigned char* new_data = bytes(cached, &new_size);
    CHECK(new_size == old_size && !memcmp(old, new_data, old_size));
    free(old);
    free(new_data);
    m.mode = 4;
    CHECK(thingsos_weidu_download(c, &books[0]) == WD_OK);
    m.bundle=true;thingsos_weidu_error_t notes_error;thingsos_wd_notes_info_t notes_info;
    CHECK(thingsos_wd_cache_bundle(c,&books[0],false,&notes_error)==WD_OK && notes_error==WD_OK && m.note_requests==3);
    CHECK(thingsos_wd_notes_info(c->root,&c->session,books[0].id,&notes_info) && notes_info.count[0]==1 && notes_info.count[1]==1 && notes_info.count[2]==1);
    m.notes_fail=true;
    CHECK(thingsos_wd_cache_bundle(c,&books[0],false,&notes_error)==WD_OK && notes_error==WD_NETWORK);
    CHECK(thingsos_weidu_cached(c->output,books[0].id,cached,sizeof(cached)) && c->status.phase==WD_READY);
    CHECK(strstr(c->status.message,"未完成") && thingsos_wd_notes_info(c->root,&c->session,books[0].id,&notes_info) && notes_info.count[1]==1);
    m.notes_fail=false;
    CHECK(thingsos_wd_cache_bundle(c,&books[0],true,&notes_error)==WD_OK && notes_error==WD_OK);
    unsigned before_failed_bundle=m.note_requests;m.mode=6;
    CHECK(thingsos_wd_cache_bundle(c,&books[0],false,&notes_error)==WD_PROTOCOL && notes_error==WD_CANCEL && m.note_requests==before_failed_bundle);
    m.bundle=false;m.mode=4;
    /* Each replay after a link loss first waits for Wi-Fi to come back. */
    c->io.wait_link=wait_link;m.link_waits=0;
    m.mode=12;CHECK(thingsos_weidu_download(c,&books[0])==WD_OK && m.toc_failures==3 && m.link_waits==2);
    m.link_waits=0;
    m.mode=13;CHECK(thingsos_weidu_download(c,&books[0])==WD_OK && m.reader_failures==3 && m.link_waits==2);
    c->io.wait_link=NULL;
    /* An interrupted download keeps the chapters it already saved; the retry
       resumes from them instead of fetching the book from chapter one again. */
    /* Start from an empty chapter directory so the run really downloads. */
    char resume_dir[700],resume_file[760];
    snprintf(resume_dir,sizeof(resume_dir),"%s/book-%s",c->root,books[0].id);
    for(unsigned i=0;i<4;++i){snprintf(resume_file,sizeof(resume_file),"%s/chapter-%u.raw",resume_dir,i);remove(resume_file);}
    snprintf(resume_file,sizeof(resume_file),"%s/toc.dat",resume_dir);remove(resume_file);
    snprintf(resume_file,sizeof(resume_file),"%s/toc.dat.prev",resume_dir);remove(resume_file);
    snprintf(resume_file,sizeof(resume_file),"%s/download.inprogress",resume_dir);remove(resume_file);
    m.mode=4;m.cancel=false;m.chapters=3;m.reader_calls=0;m.fail_chapter=2;m.shards=0;
    CHECK(thingsos_weidu_download(c,&books[0])==WD_PROTOCOL);
    CHECK(m.shards==1 && m.reader_calls==2);
    CHECK(thingsos_weidu_download(c,&books[0])==WD_OK);
    CHECK(m.shards==3 && m.reader_calls==4);
    CHECK(thingsos_weidu_cached(c->output,books[0].id,cached,sizeof(cached)));
    m.chapters=0;
    m.mode=9;CHECK(thingsos_weidu_download(c,&books[0])==WD_OK);
    m.mode=10;CHECK(thingsos_weidu_download(c,&books[0])==WD_OK);
    old = bytes(cached, &old_size);
    m.mode = 6;
    CHECK(thingsos_weidu_download(c, &books[0]) == WD_PROTOCOL);
    CHECK(strstr(c->status.message,"正文解码校验"));
    new_data = bytes(cached, &new_size);
    CHECK(new_size == old_size && !memcmp(old, new_data, old_size));
    free(old);
    free(new_data);
    thingsos_epub_t* epub = calloc(1, sizeof(*epub));
    thingsos_chapter_t* chapter = calloc(1, sizeof(*chapter));
    CHECK(epub && chapter);
    CHECK(
        thingsos_epub_open(epub, cached) && thingsos_epub_chapter(epub, 0, chapter) &&
        strstr(chapter->text, "Hello"));
    thingsos_chapter_close(chapter);
    thingsos_epub_close(epub);
    free(chapter);
    free(epub);
    /* Reproduce the e_0 metadata failure with >47 KiB content. Rejected
       responses must not request text shards or replace the existing EPUB. */
    char metadata_marker[700];
    snprintf(metadata_marker,sizeof(metadata_marker),"%s/book-%s/download.inprogress",c->root,books[0].id);
    for(int mode=16;mode<=24;++mode) {
        remove(metadata_marker); /* Each case must fetch, rather than resume. */
        m.mode=mode;
        unsigned before=m.shards;
        old=bytes(cached,&old_size);
        thingsos_weidu_error_t expected=mode==16?WD_OK:mode==21?WD_AUTH:WD_PROTOCOL;
        thingsos_weidu_error_t result=thingsos_weidu_download(c,&books[0]);
        if(result!=expected)fprintf(stderr,"Metadata mode %d: error %d expected %d, stage %s\n",mode,result,expected,c->status.message);
        CHECK(result==expected);
        CHECK(m.shards==before+(mode==16?1:0));
        if(mode!=16) {
            if(mode!=21)CHECK(strstr(c->status.message,"正文类型识别"));
            new_data=bytes(cached,&new_size);
            CHECK(new_size==old_size && !memcmp(old,new_data,old_size));
            free(new_data);
        }
        free(old);
    }
    remove(metadata_marker);
    /* Optional private device response; keep book content out of test fixtures. */
    if(argc>2) {
        thingsos_weidu_book_t actual=books[0];strcpy(actual.id,"491760");
        m.mode=0;m.primary_fixture=argv[2];unsigned before=m.shards;
        CHECK(thingsos_weidu_download(c,&actual)==WD_OK && m.shards==before+1);
        m.primary_fixture=NULL;
    }
    m.mode=14;CHECK(thingsos_weidu_download(c,&books[0])==WD_OK);
    CHECK(c->status.completed==49 && c->status.total==49);
    thingsos_epub_t* many=calloc(1,sizeof(*many));CHECK(many);
    CHECK(thingsos_epub_open(many,c->status.path) && many->spine_count==49);
    thingsos_epub_navigation(many);
    CHECK(!strcmp(many->chapter_names[0],"Part A") && !strcmp(many->chapter_names[48],"Chapter 49"));
    CHECK(sizeof(thingsos_weidu_chapter_t)==264 && many->chapter_levels[0]==0 && many->chapter_levels[1]==1 && many->chapter_levels[2]==2 && many->chapter_levels[3]==0);
    thingsos_epub_close(many);free(many);
    unsigned partial_count=0,requests_before_restore=m.requests;
    CHECK(thingsos_weidu_partial(c->root,books[0].id,&partial_count) && partial_count==49);
    CHECK(thingsos_weidu_restore(c,&books[0])==WD_OK && m.requests==requests_before_restore);
    char missing_part[700];snprintf(missing_part,sizeof(missing_part),"%s/book-%s/chapter-48.raw",c->root,books[0].id);
    /* Force the bulk chapter payload to end exactly at a 512-byte stdio/SD
       boundary, then write the central directory and reopen the result. */
    FILE* boundary=fopen(c->status.path,"rb");CHECK(boundary && !fseek(boundary,-6,SEEK_END));
    unsigned char cd[4];CHECK(fread(cd,1,4,boundary)==4 && !fclose(boundary));
    uint32_t directory=(uint32_t)cd[0]|((uint32_t)cd[1]<<8)|((uint32_t)cd[2]<<16)|((uint32_t)cd[3]<<24);
    unsigned pad=512-directory%512;
    boundary=fopen(missing_part,"ab");CHECK(boundary);
    for(unsigned i=0;i<pad;++i)CHECK(fputc(' ',boundary)!=EOF);
    CHECK(!fclose(boundary));
    CHECK(thingsos_weidu_restore(c,&books[0])==WD_OK && m.requests==requests_before_restore);
    boundary=fopen(c->status.path,"rb");CHECK(boundary && !fseek(boundary,-6,SEEK_END));
    CHECK(fread(cd,1,4,boundary)==4 && !fclose(boundary));
    uint32_t aligned_directory=(uint32_t)cd[0]|((uint32_t)cd[1]<<8)|((uint32_t)cd[2]<<16)|((uint32_t)cd[3]<<24);
    CHECK(aligned_directory==directory+pad && aligned_directory%512==0);
    /* Corrupt one saved markup chapter, then repair only that chapter. */
    char repair_toc[700];snprintf(repair_toc,sizeof(repair_toc),"%s/book-%s/toc.dat",c->root,books[0].id);
    thingsos_weidu_chapter_t damaged;
    boundary=fopen(repair_toc,"r+b");CHECK(boundary && !fseek(boundary,48*sizeof(damaged),SEEK_SET));
    CHECK(fread(&damaged,sizeof(damaged),1,boundary)==1);damaged.plain=false;
    CHECK(!fseek(boundary,48*sizeof(damaged),SEEK_SET) && fwrite(&damaged,sizeof(damaged),1,boundary)==1 && !fclose(boundary));
    boundary=fopen(missing_part,"wb");CHECK(boundary && fputs("<p class=broken\">damaged",boundary)>=0 && !fclose(boundary));
    m.mode=6;unsigned repair_readers=m.reader_calls;
    CHECK(thingsos_weidu_restore(c,&books[0])!=WD_OK);
    CHECK(m.reader_calls==repair_readers+1);
    CHECK(thingsos_weidu_cached(c->output,books[0].id,cached,sizeof(cached)));
    boundary=fopen(missing_part,"rb");CHECK(boundary && fgetc(boundary)=='<' && !fclose(boundary));
    m.mode=4;repair_readers=m.reader_calls;unsigned repair_shards=m.shards;
    CHECK(thingsos_weidu_restore(c,&books[0])==WD_OK);
    CHECK(m.reader_calls==repair_readers+1 && m.shards==repair_shards+1);
    CHECK(c->status.completed==49 && c->status.total==49);
    /* A fresh but still broken response must not loop on the same chapter. */
    boundary=fopen(repair_toc,"r+b");CHECK(boundary && !fseek(boundary,48*sizeof(damaged),SEEK_SET));
    CHECK(fwrite(&damaged,sizeof(damaged),1,boundary)==1 && !fclose(boundary));
    CHECK(write_text(missing_part,"<p class=broken\">damaged"));
    m.corrupt_repair_path=missing_part;repair_readers=m.reader_calls;
    CHECK(thingsos_weidu_restore(c,&books[0])!=WD_OK && m.reader_calls==repair_readers+1);
    CHECK(strstr(c->status.message,"normalize-tag-eof chapter=49"));
    m.corrupt_repair_path=NULL;
    /* Interrupted whole-book downloads use the same recovery path. */
    char repair_marker[700];snprintf(repair_marker,sizeof(repair_marker),"%s/book-%s/download.inprogress",c->root,books[0].id);
    CHECK(write_text(repair_marker,""));repair_readers=m.reader_calls;m.mode=14;
    CHECK(thingsos_weidu_download(c,&books[0])==WD_OK && m.reader_calls==repair_readers+1);
    CHECK(!remove(missing_part));
    CHECK(!thingsos_weidu_partial(c->root,books[0].id,&partial_count));
    CHECK(thingsos_weidu_restore(c,&books[0])==WD_STORAGE);
    CHECK(thingsos_weidu_cached(c->output,books[0].id,cached,sizeof(cached)));

    /* Open chapter four of a six-chapter remote book without fetching the
       other chapters or producing a whole-book EPUB. */
    thingsos_weidu_client_t* stream=calloc(1,sizeof(*stream));CHECK(stream);*stream=*c;
    snprintf(stream->root,sizeof(stream->root),"%s/online-source",argv[1]);MKDIR(stream->root);
    snprintf(stream->output,sizeof(stream->output),"%s/online-books",argv[1]);MKDIR(stream->output);
    char stream_dir[640],stream_file[700];snprintf(stream_dir,sizeof(stream_dir),"%s/book-%s",stream->root,books[0].id);MKDIR(stream_dir);
    snprintf(stream_file,sizeof(stream_file),"%s/toc.dat",stream_dir);remove(stream_file);
    for(unsigned i=0;i<6;++i){
        snprintf(stream_file,sizeof(stream_file),"%s/chapter-%u.raw",stream_dir,i);remove(stream_file);
        snprintf(stream_file,sizeof(stream_file),"%s/online-%u-%u.xhtml",stream_dir,i,i+1);remove(stream_file);
        snprintf(stream_file,sizeof(stream_file),"%s/online-%u-%u-img.xhtml",stream_dir,i,i+1);remove(stream_file);
        snprintf(stream_file,sizeof(stream_file),"%s/online-%u-%u-partial.xhtml",stream_dir,i,i+1);remove(stream_file);
    }
    m.mode=4;m.chapters=6;unsigned reader_before=m.reader_calls,shards_before=m.shards;
    CHECK(thingsos_weidu_stream_fetch(stream,&books[0],3)==WD_OK);
    CHECK(m.reader_calls==reader_before+1 && m.shards==shards_before+1 && stream->status.completed==1);
    CHECK(!thingsos_weidu_cached(stream->output,books[0].id,stream_file,sizeof(stream_file)));
    CHECK(thingsos_weidu_stream_path(stream->root,books[0].id,stream_file,sizeof(stream_file)));
    thingsos_epub_t* remote=calloc(1,sizeof(*remote));thingsos_chapter_t* remote_text=calloc(1,sizeof(*remote_text));CHECK(remote && remote_text);
    CHECK(thingsos_epub_open(remote,stream_file) && remote->weidu_online && remote->spine_count==6);
    CHECK(thingsos_epub_chapter(remote,3,remote_text) && strstr(remote_text->text,"Hello"));thingsos_chapter_close(remote_text);
    CHECK(!thingsos_epub_chapter(remote,0,remote_text));
    unsigned before_reopen=m.requests;
    CHECK(thingsos_weidu_stream_fetch(stream,&books[0],3)==WD_OK && m.requests==before_reopen);
    CHECK(thingsos_weidu_stream_ready(stream->root,books[0].id,3));
    char online_identity[700],offline_identity[700];
    CHECK(thingsos_weidu_cache_identity(stream_file,online_identity,sizeof(online_identity)));
    snprintf(offline_identity,sizeof(offline_identity),"%s/微读-%s.epub",stream->output,books[0].id);CHECK(!strcmp(online_identity,offline_identity));
    m.mode=6;CHECK(thingsos_weidu_stream_fetch(stream,&books[0],2)==WD_PROTOCOL);
    CHECK(!thingsos_weidu_stream_ready(stream->root,books[0].id,2));
    CHECK(thingsos_epub_chapter(remote,3,remote_text) && strstr(remote_text->text,"Hello"));thingsos_chapter_close(remote_text);
    m.mode=4;CHECK(thingsos_weidu_stream_fetch(stream,&books[0],1)==WD_OK);
    CHECK(thingsos_epub_chapter(remote,1,remote_text));thingsos_chapter_close(remote_text);
    CHECK(thingsos_weidu_stream_fetch(stream,&books[0],0)==WD_OK);
    m.mode=25;CHECK(thingsos_weidu_download(stream,&books[0])==WD_PROTOCOL);
    CHECK(thingsos_weidu_stream_count(stream->root,books[0].id)==6);
    CHECK(thingsos_epub_chapter(remote,3,remote_text));thingsos_chapter_close(remote_text);

    /* Online parts expose their cached internal headings without a ZIP spine.
       Missing parts retain roots; discovery must not initiate network requests. */
    char nav_raw[700],nav_xml[700];
    snprintf(nav_raw,sizeof(nav_raw),"%s/chapter-3.raw",stream_dir);
    snprintf(nav_xml,sizeof(nav_xml),"%s/online-3-4-img.xhtml",stream_dir);
    CHECK(write_text(nav_raw,"<html><body><h1>Internal chapter</h1><p>Section body</p></body></html>"));
    char nav_toc[700];snprintf(nav_toc,sizeof(nav_toc),"%s/toc.dat",stream_dir);
    FILE* nav_index=fopen(nav_toc,"r+b");thingsos_weidu_chapter_t nav_row;
    CHECK(nav_index && !fseek(nav_index,3*sizeof(nav_row),SEEK_SET) && fread(&nav_row,sizeof(nav_row),1,nav_index)==1);
    nav_row.plain=false;
    CHECK(!fseek(nav_index,3*sizeof(nav_row),SEEK_SET) && fwrite(&nav_row,sizeof(nav_row),1,nav_index)==1 && !fclose(nav_index));
    const char* nav_reason=NULL;
    CHECK(thingsos_weidu_normalize(nav_raw,nav_xml,remote->chapter_names[3],false,&nav_reason));
    unsigned nav_requests=m.requests;
    CHECK(thingsos_wd_navigation(stream->root,books[0].id,remote));
    CHECK(m.requests==nav_requests && remote->section_toc_count==7 && remote->spine_count==6);
    const thingsos_toc_entry_t* section=&remote->section_toc[4];
    CHECK(section->chapter==3 && section->offset && section->level==remote->chapter_levels[3]+1);
    CHECK(!strcmp(section->title,"Internal chapter"));
    CHECK(thingsos_epub_chapter(remote,section->chapter,remote_text));
    CHECK(!strncmp(remote_text->text+section->offset,"Internal chapter",16));
    thingsos_chapter_close(remote_text);
    thingsos_epub_close(remote);free(remote);free(remote_text);
    /* A three-chapter job preserves partial successes, reuses the channel,
       skips ready chapters on retry, and clamps at the end of the book. */
    snprintf(stream->root,sizeof(stream->root),"%s/online-window-source",argv[1]);MKDIR(stream->root);
    snprintf(stream_dir,sizeof(stream_dir),"%s/book-%s",stream->root,books[0].id);MKDIR(stream_dir);
    snprintf(stream_file,sizeof(stream_file),"%s/toc.dat",stream_dir);remove(stream_file);
    for(unsigned i=0;i<6;++i){
        snprintf(stream_file,sizeof(stream_file),"%s/chapter-%u.raw",stream_dir,i);remove(stream_file);
        snprintf(stream_file,sizeof(stream_file),"%s/online-%u-%u.xhtml",stream_dir,i,i+1);remove(stream_file);
        snprintf(stream_file,sizeof(stream_file),"%s/online-%u-%u-img.xhtml",stream_dir,i,i+1);remove(stream_file);
        snprintf(stream_file,sizeof(stream_file),"%s/online-%u-%u-partial.xhtml",stream_dir,i,i+1);remove(stream_file);
    }
    stream->io.finish_network=finish_network;m.mode=4;
    unsigned finishes=m.network_finishes;m.fail_chapter=m.reader_calls+2;
    CHECK(thingsos_weidu_stream_window(stream,&books[0],0,3)==WD_PROTOCOL);
    CHECK(thingsos_weidu_stream_ready(stream->root,books[0].id,0) && !thingsos_weidu_stream_ready(stream->root,books[0].id,1));
    CHECK(m.network_finishes==finishes+1);reader_before=m.reader_calls;
    CHECK(thingsos_weidu_stream_window(stream,&books[0],0,3)==WD_OK && m.reader_calls==reader_before+2);
    CHECK(stream->status.total==3 && stream->status.completed==3 && m.network_finishes==finishes+2);
    reader_before=m.reader_calls;
    CHECK(thingsos_weidu_stream_window(stream,&books[0],0,3)==WD_OK && m.reader_calls==reader_before);
    CHECK(thingsos_weidu_stream_window(stream,&books[0],5,3)==WD_OK && stream->status.total==1 && stream->status.completed==1);
    CHECK(thingsos_weidu_stream_window(stream,&books[0],0,0)==WD_PROTOCOL);
    m.cancel=true;CHECK(thingsos_weidu_stream_window(stream,&books[0],3,2)==WD_CANCEL);m.cancel=false;
    stream->io.prefetch_start=window_prefetch_start;stream->io.prefetch_get=window_prefetch_get;stream->io.prefetch_stop=window_prefetch_stop;
    CHECK(thingsos_weidu_stream_window(stream,&books[0],3,3)==WD_OK);
    CHECK(m.prefetch_starts==1 && m.prefetch_stops==1 && m.prefetch_count==2 && m.prefetch_indices[0]==3 && m.prefetch_indices[1]==4);
    CHECK(thingsos_weidu_stream_ready(stream->root,books[0].id,3) && thingsos_weidu_stream_ready(stream->root,books[0].id,4));

    /* Rebuild an existing chapter from its saved raw markup. Failed images
       leave readable text; a retry fetches only the missing image. */
    char legacy[700],art[700],image_digest[33],first_art[700],second_art[700];
    snprintf(legacy,sizeof(legacy),"%s/online-4-5.xhtml",stream_dir);
    snprintf(art,sizeof(art),"%s/online-4-5-img.xhtml",stream_dir);
    CHECK(!rename(art,legacy));
    snprintf(stream_file,sizeof(stream_file),"%s/chapter-4.raw",stream_dir);
    const char* first_url="https://res.weread.qq.com/figure.png?x=1&y=2";
    const char* second_url="https://res.weread.qq.com/retry.png";
    thingsos_weidu_md5(first_url,strlen(first_url),image_digest);snprintf(first_art,sizeof(first_art),"%s/img-%s.bin",stream_dir,image_digest);remove(first_art);
    thingsos_weidu_md5(second_url,strlen(second_url),image_digest);snprintf(second_art,sizeof(second_art),"%s/img-%s.bin",stream_dir,image_digest);remove(second_art);
    CHECK(write_text(stream_file,"<p>Before</p><img data-src='//res.weread.qq.com/figure.png?x=1&amp;y=2' src='placeholder' alt='Figure'/>"
        "<img src='https://res.weread.qq.com/figure.png?x=1&amp;y=2'/><p>After</p>"
        "<img src='https://res.weread.qq.com/retry.png'/><img src='https://evil.example/ignore.png'/>"
        "<img class='qqreader-footnote' alt='Note' src='https://res.weread.qq.com/icon.png'/>"));
    snprintf(art,sizeof(art),"%s/toc.dat",stream_dir);
    FILE* toc=fopen(art,"r+b");thingsos_weidu_chapter_t row;
    CHECK(toc && !fseek(toc,4*sizeof(row),SEEK_SET) && fread(&row,sizeof(row),1,toc)==1);
    row.plain=false;
    CHECK(!fseek(toc,4*sizeof(row),SEEK_SET) && fwrite(&row,sizeof(row),1,toc)==1 && !fclose(toc));
    stream->io.image=public_image;
    CHECK(thingsos_weidu_stream_ready(stream->root,books[0].id,4));
    CHECK(!thingsos_weidu_stream_images_ready(stream->root,books[0].id,4,true));
    reader_before=m.reader_calls;m.inline_cancel=true;
    CHECK(thingsos_weidu_stream_fetch(stream,&books[0],4)==WD_CANCEL);
    CHECK(thingsos_weidu_stream_ready(stream->root,books[0].id,4) && !thingsos_weidu_stream_images_ready(stream->root,books[0].id,4,true));
    FILE* interrupted=fopen(first_art,"rb");CHECK(!interrupted);
    m.inline_cancel=m.cancel=false;m.inline_fail=true;
    CHECK(thingsos_weidu_stream_fetch(stream,&books[0],4)==WD_OK && m.reader_calls==reader_before && m.inline_requests==3);
    CHECK(thingsos_weidu_stream_images_ready(stream->root,books[0].id,4,true) && !thingsos_weidu_stream_images_ready(stream->root,books[0].id,4,false));
    remote=calloc(1,sizeof(*remote));remote_text=calloc(1,sizeof(*remote_text));CHECK(remote && remote_text);
    CHECK(thingsos_epub_open(remote,stream->status.path));
    CHECK(thingsos_epub_chapter(remote,4,remote_text) && strstr(remote_text->text,"Before") && strstr(remote_text->text,"After"));
    CHECK(remote_text->image_count==2 && remote_text->images[0].width==4 && remote_text->images[0].height==2);
    CHECK(!strcmp(remote_text->images[0].path,remote_text->images[1].path));
    CHECK(!thingsos_weidu_stream_image_path(remote,"../unrelated.bin",art,sizeof(art)));
    thingsos_chapter_close(remote_text);
    m.inline_fail=false;
    CHECK(thingsos_weidu_stream_fetch(stream,&books[0],4)==WD_OK && m.reader_calls==reader_before && m.inline_requests==4);
    CHECK(thingsos_weidu_stream_images_ready(stream->root,books[0].id,4,false));
    CHECK(thingsos_epub_chapter(remote,4,remote_text) && remote_text->image_count==3 && remote_text->footnote_count==1);
    thingsos_chapter_close(remote_text);thingsos_epub_close(remote);free(remote);free(remote_text);

    /* A cached comic chapter can take more than 15 seconds while every page succeeds. */
    snprintf(stream->root,sizeof(stream->root),"%s/online-comic-source",argv[1]);MKDIR(stream->root);
    snprintf(stream_dir,sizeof(stream_dir),"%s/book-%s",stream->root,books[0].id);MKDIR(stream_dir);
    snprintf(art,sizeof(art),"%s/online-0-1-img.xhtml",stream_dir);
    snprintf(legacy,sizeof(legacy),"%s/online-0-1.xhtml",stream_dir);
    remove(art);remove(legacy);
    CHECK(thingsos_weidu_stream_fetch(stream,&books[0],0)==WD_OK && !rename(art,legacy));
    snprintf(art,sizeof(art),"%s/toc.dat",stream_dir);
    toc=fopen(art,"r+b");CHECK(toc && fread(&row,sizeof(row),1,toc)==1);
    row.plain=false;
    CHECK(!fseek(toc,0,SEEK_SET) && fwrite(&row,sizeof(row),1,toc)==1 && !fclose(toc));
    snprintf(stream_file,sizeof(stream_file),"%s/chapter-0.raw",stream_dir);
    FILE* comic=fopen(stream_file,"wb");CHECK(comic);
    for(unsigned i=0;i<31;++i) {
        char url[100];snprintf(url,sizeof(url),"https://res.weread.qq.com/comic-page-%u.jpg",i);
        CHECK(fprintf(comic,"<img src=\"%s\"/>",url)>0);
        thingsos_weidu_md5(url,strlen(url),image_digest);
        snprintf(art,sizeof(art),"%s/img-%s.bin",stream_dir,image_digest);remove(art);
    }
    CHECK(!fclose(comic));
    m.inline_delay_ms=1100;
    unsigned comic_requests=m.inline_requests;uint64_t comic_started=m.now;
    CHECK(thingsos_weidu_stream_fetch(stream,&books[0],0)==WD_OK);
    CHECK(m.inline_requests-comic_requests==31 && m.now-comic_started>=34100);
    CHECK(thingsos_weidu_stream_images_ready(stream->root,books[0].id,0,false));
    m.inline_delay_ms=0;
    remote=calloc(1,sizeof(*remote));remote_text=calloc(1,sizeof(*remote_text));CHECK(remote && remote_text);
    CHECK(thingsos_epub_open(remote,stream->status.path));
    CHECK(thingsos_epub_chapter(remote,0,remote_text) && remote_text->image_count==31);
    thingsos_chapter_close(remote_text);thingsos_epub_close(remote);free(remote);free(remote_text);
    free(stream);m.chapters=0;

    /* Regression for the actual 1548-chapter book: the old packer rejected
       this after every network request had already completed. */
    m.mode=4;m.chapters=1548;
    CHECK(thingsos_weidu_download(c,&books[0])==WD_OK && c->status.completed==1548);
    remote=calloc(1,sizeof(*remote));remote_text=calloc(1,sizeof(*remote_text));CHECK(remote && remote_text);
    CHECK(thingsos_epub_open(remote,c->status.path) && remote->spine_count==1548);
    CHECK(thingsos_epub_chapter(remote,1547,remote_text) && strstr(remote_text->text,"Hello"));
    thingsos_chapter_close(remote_text);thingsos_epub_close(remote);free(remote_text);free(remote);m.chapters=0;

    CHECK(!thingsos_weidu_cached(c->output, "../escape", cached, sizeof(cached)));
    m.mode=11;CHECK(thingsos_weidu_sync(c)==WD_OK);
    CHECK(thingsos_weidu_shelf_page(c->root,&c->session,0,books,2,&count,&total) && count==2 && total==4);
    CHECK(!strcmp(books[0].id,"3") && !strcmp(books[1].id,"4"));
    CHECK(thingsos_weidu_shelf_page(c->root,&c->session,2,books,2,&count,&total) && count==2);
    CHECK(!strcmp(books[0].id,"1234567890") && !strcmp(books[1].id,"2"));
    CHECK(thingsos_weidu_shelf_search(c->root,&c->session,0,books,1,&count,&total,"Tie") && count==1 && total==2 && !strcmp(books[0].id,"3"));
    CHECK(thingsos_weidu_shelf_search(c->root,&c->session,1,books,1,&count,&total,"Tie") && count==1 && total==2 && !strcmp(books[0].id,"4"));
    CHECK(thingsos_weidu_shelf_search(c->root,&c->session,2,books,1,&count,&total,"Tie") && count==0 && total==2);
    CHECK(thingsos_weidu_shelf_search(c->root,&c->session,0,NULL,0,&count,&total,"absent") && count==0 && total==0);
    CHECK(thingsos_weidu_shelf_search(c->root,&c->session,0,NULL,0,&count,&total,"") && count==0 && total==4);
    thingsos_weidu_book_t added={0};strcpy(added.id,"999");strcpy(added.title,"Online only");
    CHECK(!thingsos_weidu_shelf_has(c->root,&c->session,added.id));
    for(int mode=30;mode<=32;++mode) {
        m.mode=mode;CHECK(thingsos_weidu_shelf_add(c,&added)!=WD_OK);
        CHECK(!thingsos_weidu_shelf_has(c->root,&c->session,added.id));
        CHECK(thingsos_weidu_shelf_page(c->root,&c->session,0,NULL,0,&count,&total) && total==4);
    }
    m.mode=4;CHECK(thingsos_weidu_shelf_add(c,&added)==WD_OK);
    CHECK(thingsos_weidu_shelf_has(c->root,&c->session,added.id));
    CHECK(thingsos_weidu_shelf_search(c->root,&c->session,0,books,2,&count,&total,"Online") && total==1 && !strcmp(books[0].id,"999"));
    unsigned before_add=m.requests;
    CHECK(thingsos_weidu_shelf_add(c,&added)==WD_OK && m.requests==before_add);
    CHECK(thingsos_weidu_shelf_page(c->root,&c->session,0,NULL,0,&count,&total) && total==5);
    m.poll_failures=100;
    unsigned saved_before=m.saves, polls_before=m.poll_calls;
    uint64_t login_started=m.now;
    CHECK(thingsos_weidu_login(c)==WD_AUTH && m.qr==2 && m.saves==saved_before);
    CHECK(m.now-login_started>=240000 && m.now-login_started<265000);
    CHECK(m.poll_calls-polls_before<15);
    m.cancel=true;polls_before=m.poll_calls;
    CHECK(thingsos_weidu_login(c)==WD_CANCEL && m.poll_calls==polls_before);
    strcpy(c->session.skey, "bad\r\nCookie");
    CHECK(!thingsos_weidu_session_valid(&c->session));
    /* Block-based position checks must still reject oversized normalized text
       and remove the uncommitted EPUB, without changing the cached book. */
    char limit_dir[700],limit_toc[720],limit_raw[720],limit_epub[720];
    snprintf(limit_dir,sizeof(limit_dir),"%s/pack-limit",argv[1]);MKDIR(limit_dir);
    snprintf(limit_toc,sizeof(limit_toc),"%s/toc.dat",limit_dir);
    snprintf(limit_raw,sizeof(limit_raw),"%s/chapter-0.raw",limit_dir);
    snprintf(limit_epub,sizeof(limit_epub),"%s/limit.epub",limit_dir);
    thingsos_weidu_chapter_t limit_chapter={0};strcpy(limit_chapter.title,"Large");limit_chapter.plain=true;
    FILE* limit_file=fopen(limit_toc,"wb");CHECK(limit_file && fwrite(&limit_chapter,sizeof(limit_chapter),1,limit_file)==1 && !fclose(limit_file));
    limit_file=fopen(limit_raw,"wb");CHECK(limit_file);char block[4096];memset(block,'A',sizeof(block));
    for(unsigned i=0;i<130;++i)CHECK(fwrite(block,1,sizeof(block),limit_file)==sizeof(block));
    CHECK(!fclose(limit_file));
    char pack_detail[96];
    CHECK(!thingsos_weidu_pack_detail(limit_epub,&books[0],limit_dir,limit_toc,1,pack_detail,sizeof(pack_detail)));
    CHECK(strstr(pack_detail,"chapter-limit chapter=1"));
    limit_file=fopen(limit_epub,"rb");CHECK(!limit_file);
    /* Long inline attributes must not require a tag-sized allocation or leak
       attribute text into the book. Delimiters inside quotes are not markup;
       self-closing and nested skipped tags still need their true tail. */
    limit_chapter.plain=false;
    limit_file=fopen(limit_toc,"wb");CHECK(limit_file && fwrite(&limit_chapter,sizeof(limit_chapter),1,limit_file)==1 && !fclose(limit_file));
    const char* long_tag_heads[]={"<div data-note=\"", "<head data-note=\"", "<script data-note=\"", "<nav data-note=\""};
    const char* long_tag_tails[]={
        "\">VISIBLE_LONG</div>",
        "\">HIDDEN_HEAD</head><p>VISIBLE_LONG</p>",
        "\"/><p>VISIBLE_LONG</p>",
        "\">HIDDEN_NAV<nav data-url=\"https://example/\">HIDDEN_INNER</nav>HIDDEN_TAIL</nav><p>VISIBLE_LONG</p>"
    };
    for(unsigned variant=0;variant<5;++variant) {
        limit_file=fopen(limit_raw,"wb");CHECK(limit_file);
        unsigned source=variant<4?variant:0;
        CHECK(fputs(long_tag_heads[source],limit_file)>=0);
        CHECK(fputs("ATTRIBUTE_ONLY > <p> / '",limit_file)>=0);
        for(unsigned i=0;i<4;++i)CHECK(fwrite(block,1,sizeof(block),limit_file)==sizeof(block));
        if(variant<4)CHECK(fputs(long_tag_tails[variant],limit_file)>=0);
        CHECK(!fclose(limit_file));
        bool packed=thingsos_weidu_pack_detail(limit_epub,&books[0],limit_dir,limit_toc,1,pack_detail,sizeof(pack_detail));
        if(variant==4) {
            CHECK(!packed && strstr(pack_detail,"normalize-tag-eof chapter=1"));
            limit_file=fopen(limit_epub,"rb");CHECK(!limit_file);
        } else {
            CHECK(packed && !pack_detail[0]);
            thingsos_epub_t* long_epub=calloc(1,sizeof(*long_epub));
            thingsos_chapter_t* long_text=calloc(1,sizeof(*long_text));CHECK(long_epub && long_text);
            CHECK(thingsos_epub_open(long_epub,limit_epub) && thingsos_epub_chapter(long_epub,0,long_text));
            CHECK(strstr(long_text->text,"VISIBLE_LONG") && !strstr(long_text->text,"ATTRIBUTE_ONLY") && !strstr(long_text->text,"HIDDEN_"));
            thingsos_chapter_close(long_text);thingsos_epub_close(long_epub);free(long_text);free(long_epub);
        }
    }
    /* Footnote compatibility must survive the actual raw -> EPUB normalizer. */
    const char* note_sources[]={
        "<p>Text <a href = \"#fn1\"><sup>1</sup></a></p><p><a id=\"fn1\"></a>PACKED_NOTE_BODY</p>",
        "<p>Text <a class=\"a-long-attribute-before-the-link-0123456789012345678901234567890123456789\" href=\"#fn1\"><sup>1</sup></a></p><aside><span id=\"fn1\">PACKED_NOTE_BODY</span></aside>",
    };
    for(unsigned variant=0;variant<2;++variant) {
        limit_chapter.plain=false;limit_file=fopen(limit_toc,"wb");CHECK(limit_file && fwrite(&limit_chapter,sizeof(limit_chapter),1,limit_file)==1 && !fclose(limit_file));
        limit_file=fopen(limit_raw,"wb");CHECK(limit_file && fputs(note_sources[variant],limit_file)>=0 && !fclose(limit_file));
        CHECK(thingsos_weidu_pack(limit_epub,&books[0],limit_dir,limit_toc,1));
        thingsos_epub_t* ne=calloc(1,sizeof(*ne));thingsos_chapter_t* nc=calloc(1,sizeof(*nc));CHECK(ne && nc && thingsos_epub_open(ne,limit_epub) && thingsos_epub_chapter(ne,0,nc));
        CHECK(nc->footnote_count==1 && nc->footnotes[0].note_bytes && strstr(nc->notes+nc->footnotes[0].note_offset,"PACKED_NOTE_BODY"));
        thingsos_chapter_close(nc);thingsos_epub_close(ne);free(nc);free(ne);
    }
    /* Real cache shape: synthetic heading + source book title + split heading.
       Only this exact leading repetition may be hidden; body mentions survive. */
    static const char* const header_sources[]={
        "<html><head><title>HEAD_ONLY</title></head><body><h1>Split<br/>Heading</h1><p>Body mentions Split Heading.</p></body></html>",
        "Book\nSplit\nHeading\nBody mentions Split Heading.",
        "Split Heading is mentioned in this sentence.\nBody.",
    };
    thingsos_weidu_book_t header_book=books[0];strcpy(header_book.title,"Book");
    strcpy(limit_chapter.title,"Split Heading");
    snprintf(limit_epub,sizeof(limit_epub),"%s/微读-header.epub",limit_dir);
    for(unsigned variant=0;variant<3;++variant) {
        limit_chapter.plain=variant!=0;
        limit_file=fopen(limit_toc,"wb");CHECK(limit_file);
        CHECK(fwrite(&limit_chapter,sizeof(limit_chapter),1,limit_file)==1 && !fclose(limit_file));
        limit_file=fopen(limit_raw,"wb");CHECK(limit_file);
        CHECK(fwrite(header_sources[variant],1,strlen(header_sources[variant]),limit_file)==strlen(header_sources[variant]) && !fclose(limit_file));
        CHECK(thingsos_weidu_pack(limit_epub,&header_book,limit_dir,limit_toc,1));
        thingsos_epub_t* header=calloc(1,sizeof(*header));thingsos_chapter_t* text=calloc(1,sizeof(*text));CHECK(header && text);
        CHECK(thingsos_epub_open(header,limit_epub));thingsos_epub_navigation(header);
        CHECK(thingsos_epub_chapter(header,0,text) && !strstr(text->text,"HEAD_ONLY"));
        if((text->hidden_end>text->hidden_start)!=(variant<2))fprintf(stderr,"header variant=%u opf=%s title=%s heading=%zu..%zu cache=%d text=[%s]\n",variant,header->opf,header->chapter_names[0],text->heading_start,text->heading_end,header->weidu_cache,text->text);
        CHECK((text->hidden_end>text->hidden_start)==(variant<2));
        CHECK(strstr(text->text+text->hidden_end,"Body"));
        size_t saved_bytes=text->bytes;thingsos_chapter_close(text);
        header->weidu_cache=false;CHECK(thingsos_epub_chapter(header,0,text));
        CHECK(!text->hidden_end && text->bytes==saved_bytes);
        thingsos_chapter_close(text);thingsos_epub_close(header);free(text);free(header);
    }
    /* Cover chapter normalization formerly discarded every img and packed no
       resource. Check both the old-cache fixture and the new self-contained EPUB. */
    char covers[720],cover_image[740],valid_book[720];
    snprintf(covers,sizeof(covers),"%s/covers",argv[1]);MKDIR(covers);
    snprintf(cover_image,sizeof(cover_image),"%s/55.jpg",covers);remove(cover_image);
    strcpy(header_book.id,"55");strcpy(limit_chapter.title,"封面");limit_chapter.plain=false;
    limit_file=fopen(limit_toc,"wb");CHECK(limit_file && fwrite(&limit_chapter,sizeof(limit_chapter),1,limit_file)==1 && !fclose(limit_file));
    CHECK(write_text(limit_raw,"<html><body><img src=\"https://cdn.weread.qq.com/cover.jpg\"/></body></html>"));
    snprintf(limit_epub,sizeof(limit_epub),"%s/微读-55.epub",limit_dir);
    CHECK(thingsos_weidu_pack(limit_epub,&header_book,limit_dir,limit_toc,1));
    thingsos_epub_t* cover_epub=calloc(1,sizeof(*cover_epub));CHECK(cover_epub);
    snprintf(valid_book,sizeof(valid_book),"%s/valid.epub",argv[1]);CHECK(thingsos_epub_open(cover_epub,valid_book));
    unsigned char* image_data=NULL;size_t image_bytes=0;
    CHECK(thingsos_zip_read(&cover_epub->zip,cover_epub->cover,THINGSOS_MEMBER_BYTES,&image_data,&image_bytes));
    thingsos_epub_close(cover_epub);
    limit_file=fopen(cover_image,"wb");CHECK(limit_file && fwrite(image_data,1,image_bytes,limit_file)==image_bytes && !fclose(limit_file));free(image_data);
    snprintf(limit_epub,sizeof(limit_epub),"%s/微读-56.epub",limit_dir);
    CHECK(thingsos_weidu_pack(limit_epub,&header_book,limit_dir,limit_toc,1));
    CHECK(thingsos_epub_open(cover_epub,limit_epub));thingsos_epub_navigation(cover_epub);
    CHECK(cover_epub->spine_count==1 && !strcmp(cover_epub->cover,"OPS/cover.jpg"));
    thingsos_chapter_t* cover_chapter=calloc(1,sizeof(*cover_chapter));CHECK(cover_chapter);
    CHECK(thingsos_epub_chapter(cover_epub,0,cover_chapter) && cover_chapter->image_count==1 && cover_chapter->cover_image==1 && !strcmp(cover_chapter->images[0].path,"OPS/cover.jpg"));
    CHECK(thingsos_zip_read(&cover_epub->zip,cover_epub->cover,THINGSOS_MEMBER_BYTES,&image_data,&image_bytes));
    unsigned char* gray=NULL;unsigned iw=0,ih=0;CHECK(thingsos_decode_image(image_data,image_bytes,&gray,&iw,&ih) && iw && ih);
    free(gray);free(image_data);thingsos_chapter_close(cover_chapter);free(cover_chapter);thingsos_epub_close(cover_epub);free(cover_epub);
    /* Full offline book caching must download, deduplicate, and embed chapter
       images into the synthesized EPUB, and retain them across offline restore. */
    {
        thingsos_weidu_client_t img_client=*c;
        strcpy(img_client.session.vid,"123");strcpy(img_client.session.skey,"secret");strcpy(img_client.session.rt,"refresh-secret");
        thingsos_weidu_book_t img_book={0};
        strcpy(img_book.id,"1234567890");strcpy(img_book.title,"Image Book");strcpy(img_book.author,"Author");
        snprintf(img_client.root,sizeof(img_client.root),"%s/offline-img-source",argv[1]);MKDIR(img_client.root);
        snprintf(img_client.output,sizeof(img_client.output),"%s/offline-img-books",argv[1]);MKDIR(img_client.output);
        img_client.io.image=public_image;
        char dir[640],file[700],digest[33],img1[700],img2[700];
        snprintf(dir,sizeof(dir),"%s/book-%s",img_client.root,img_book.id);MKDIR(dir);
        const char* url1="https://res.weread.qq.com/figure.png?x=1&y=2";
        const char* url2="https://res.weread.qq.com/retry.png";
        thingsos_weidu_md5(url1,strlen(url1),digest);snprintf(img1,sizeof(img1),"%s/img-%s.bin",dir,digest);remove(img1);
        thingsos_weidu_md5(url2,strlen(url2),digest);snprintf(img2,sizeof(img2),"%s/img-%s.bin",dir,digest);remove(img2);
        thingsos_weidu_chapter_t rows[2]={0};
        strcpy(rows[0].uid,"1");strcpy(rows[0].title,"Chapter 1");rows[0].index=1;rows[0].plain=false;
        strcpy(rows[1].uid,"2");strcpy(rows[1].title,"Chapter 2");rows[1].index=2;rows[1].plain=false;
        snprintf(file,sizeof(file),"%s/toc.dat",dir);
        FILE* tf=fopen(file,"wb");CHECK(tf && fwrite(rows,sizeof(rows[0]),2,tf)==2 && !fclose(tf));
        snprintf(file,sizeof(file),"%s/chapter-0.raw",dir);
        CHECK(write_text(file,"<p>Before</p><img data-src='//res.weread.qq.com/figure.png?x=1&amp;y=2' src='placeholder' alt='Fig1'/>"
            "<img src='https://res.weread.qq.com/figure.png?x=1&y=2'/>"
            "<img src='https://res.weread.qq.com/retry.png'/>"
            "<img src='https://evil.example/ignore.png'/>"
            "<img class='qqreader-footnote' alt='Note' src='https://res.weread.qq.com/icon.png'/><p>After</p>"));
        snprintf(file,sizeof(file),"%s/chapter-1.raw",dir);
        CHECK(write_text(file,"<p>Second chapter</p><img src='https://res.weread.qq.com/figure.png?x=1&amp;y=2'/>"));
        snprintf(file,sizeof(file),"%s/download.inprogress",dir);
        CHECK(write_text(file,""));

        m.mode=4;m.chapters=2;m.inline_cancel=true;m.inline_fail=false;m.cancel=false;
        unsigned before_inline=m.inline_requests;
        CHECK(thingsos_weidu_download(&img_client,&img_book)==WD_CANCEL);
        CHECK(m.inline_requests==before_inline+1);
        CHECK(!fopen(img1,"rb"));

        m.inline_cancel=false;m.cancel=false;m.inline_fail=true;
        before_inline=m.inline_requests;
        CHECK(thingsos_weidu_download(&img_client,&img_book)==WD_OK);
        /* figure.png cached once and reused across 3 img tags; retry.png failed (retried twice). */
        CHECK(m.inline_requests-before_inline==3);
        thingsos_epub_t* off=calloc(1,sizeof(*off));
        thingsos_chapter_t* ch=calloc(1,sizeof(*ch));
        CHECK(off && ch);
        CHECK(thingsos_epub_open(off,img_client.status.path) && !off->weidu_online && off->spine_count==2);
        CHECK(thingsos_epub_chapter(off,0,ch) && ch->image_count==2 && ch->footnote_count==1);
        CHECK(!strcmp(ch->images[0].path,ch->images[1].path) && ch->images[0].width==4 && ch->images[0].height==2);
        thingsos_chapter_close(ch);
        CHECK(thingsos_epub_chapter(off,1,ch) && ch->image_count==1 && ch->images[0].width==4 && ch->images[0].height==2);
        unsigned char* raw_img=NULL;size_t raw_bytes=0;unsigned char* dec_gray=NULL;unsigned dw=0,dh=0;
        CHECK(thingsos_zip_read(&off->zip,ch->images[0].path,THINGSOS_MEMBER_BYTES,&raw_img,&raw_bytes));
        CHECK(thingsos_decode_image(raw_img,raw_bytes,&dec_gray,&dw,&dh) && dw==4 && dh==2);
        free(dec_gray);free(raw_img);thingsos_chapter_close(ch);thingsos_epub_close(off);

        /* Re-run full download when retry.png recovers: both images end up packed. */
        m.inline_fail=false;
        snprintf(file,sizeof(file),"%s/download.inprogress",dir);
        CHECK(write_text(file,""));
        before_inline=m.inline_requests;
        CHECK(thingsos_weidu_download(&img_client,&img_book)==WD_OK);
        CHECK(m.inline_requests-before_inline==1);
        CHECK(thingsos_epub_open(off,img_client.status.path));
        CHECK(thingsos_epub_chapter(off,0,ch) && ch->image_count==3 && ch->footnote_count==1);
        thingsos_chapter_close(ch);thingsos_epub_close(off);

        /* Offline restore with io.image == NULL must retain all packed images. */
        img_client.io.image=NULL;
        unsigned req_before=m.requests;
        CHECK(thingsos_weidu_restore(&img_client,&img_book)==WD_OK && m.requests==req_before);
        CHECK(thingsos_epub_open(off,img_client.status.path));
        CHECK(thingsos_epub_chapter(off,0,ch) && ch->image_count==3 && ch->footnote_count==1);
        CHECK(thingsos_zip_read(&off->zip,ch->images[2].path,THINGSOS_MEMBER_BYTES,&raw_img,&raw_bytes));
        CHECK(thingsos_decode_image(raw_img,raw_bytes,&dec_gray,&dw,&dh) && dw==4 && dh==2);
        free(dec_gray);free(raw_img);thingsos_chapter_close(ch);thingsos_epub_close(off);
        free(ch);free(off);
        m.chapters=0;
    }
    free(c);cache_names_test(argv[1]);cache_pipeline_cancel_test(argv[1]);
    puts(
        "PASS: independent QR/session/renewal, streaming shelf, account binding, atomic failure "
        "retention, chapter wire format and readable EPUB");
    return 0;
}
