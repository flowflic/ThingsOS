/* SPDX-License-Identifier: Apache-2.0 */
#include "thingsos/weidu_reading.h"
#include <direct.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "thingsos/graphics.h"
#include "thingsos/reader_ui.h"
#define CHECK(x)                                                         \
    do {                                                                 \
        if (!(x)) {                                                      \
            fprintf(stderr, "reading test line %d: %s\n", __LINE__, #x); \
            exit(1);                                                     \
        }                                                                \
    } while (0)
extern void thingsos_wd_host_block_limit(size_t);
extern unsigned thingsos_wd_host_lines_cache_hits(void);
extern unsigned thingsos_wd_host_map_cache_hits(void);
static unsigned mode, requests, reviews, writes;
static const char* calibration_response;
static uint32_t custom_offset;
static const char* thought_response;
static bool cancel_value;
static void text_file(const char*,const char*);
static bool annotation_test,annotation_bookmark,annotation_thought;
static unsigned annotation_posts,annotation_failure;
static uint32_t annotation_begin,annotation_end;
static const char* annotation_content="a thought";
static char annotation_server_content[128];
static thingsos_weidu_error_t annotation_http(const char* endpoint,const char* body,const char* dest,size_t limit) {
    char response[4096];
    if(!strncmp(endpoint,"/web/book/bestbookmarks?",24)) {
        if(annotation_failure==7)return WD_NETWORK;
        CHECK(limit==WD_RESPONSE_LIMIT);strcpy(response,"{\"items\":[]}");
    } else if(!strncmp(endpoint,"/web/review/list?",17)) {
        CHECK(limit==WD_RESPONSE_LIMIT);strcpy(response,"{\"reviews\":[],\"hasMore\":false}");
    } else if(!strcmp(endpoint,"/api/agent/gateway")) {
        CHECK(limit==WD_RESPONSE_LIMIT);
        if(strstr(body,"/book/bookmarklist")) {
            if(annotation_bookmark)snprintf(response,sizeof(response),"{\"updated\":[{\"bookmarkId\":\"b1\",\"chapterUid\":1,\"range\":\"%u-%u\",\"markText\":\"Text\"}]}",annotation_begin,annotation_end);
            else strcpy(response,"{\"updated\":[]}");
        } else {
            CHECK(strstr(body,"/review/list/mine") && strstr(body,"\"bookid\":\"55\""));
            if(annotation_failure==5)return WD_NETWORK;
            if(annotation_thought)snprintf(response,sizeof(response),"{\"reviews\":[{\"review\":{\"reviewId\":\"r1\",\"chapterUid\":1,\"range\":\"%u-%u\",\"abstract\":\"Text\",\"content\":\"%s\"}}],\"hasMore\":false}",annotation_begin,annotation_end,annotation_server_content);
            else strcpy(response,"{\"reviews\":[],\"hasMore\":false}");
        }
    } else {
        CHECK(limit==16384);++annotation_posts;
        if(annotation_failure==1)return WD_NETWORK;
        if(annotation_failure==2){text_file(dest,"{}");return WD_OK;}
        if(annotation_failure==3){text_file(dest,"{\"errcode\":-2012}");return WD_OK;}
        cJSON* json=cJSON_Parse(body);CHECK(json);
        if(!strcmp(endpoint,"/web/book/addBookmark")) {
            CHECK(!strcmp(cJSON_GetObjectItem(json,"markText")->valuestring,"VGV4dA=="));
            annotation_bookmark=true;strcpy(response,"{\"bookmarkId\":\"b1\"}");
        } else if(!strcmp(endpoint,"/web/book/removeBookmark")) {
            CHECK(!strcmp(cJSON_GetObjectItem(json,"bookmarkId")->valuestring,"b1"));
            CHECK(!strcmp(cJSON_GetObjectItem(json,"bookId")->valuestring,"55"));
            annotation_bookmark=false;strcpy(response,"{\"succ\":1}");
            if(annotation_failure==6)annotation_failure=7;
        } else if(!strcmp(endpoint,"/web/review/delete")) {
            CHECK(!strcmp(cJSON_GetObjectItem(json,"reviewId")->valuestring,"r1"));
            annotation_thought=false;strcpy(response,"{\"succ\":1}");
        } else {
            CHECK(cJSON_GetObjectItem(json,"isPrivate")->valueint==1);
            if(!strcmp(endpoint,"/web/review/add"))CHECK(cJSON_GetObjectItem(json,"type")->valueint==1);
            else CHECK(!strcmp(endpoint,"/web/review/edit"));
            CHECK(!strcmp(cJSON_GetObjectItem(json,"content")->valuestring,annotation_content));
            annotation_thought=true;strcpy(annotation_server_content,annotation_content);strcpy(response,"{\"reviewId\":\"r1\"}");
            if(annotation_failure==4)annotation_failure=5;
        }
        cJSON_Delete(json);
    }
    text_file(dest,response);return WD_OK;
}
static bool thought_network_fail,thought_cancel_during_wait;
static unsigned thought_dns_failures,thought_waits;
static void thought_wait_fn(void* p,unsigned ms) {
    (void)p;CHECK(ms==500);++thought_waits;
    if(thought_cancel_during_wait)cancel_value=true;
}
static uint32_t clock_value = 1792281600u;
static uint32_t clock_fn(void* p) {
    (void)p;
    return clock_value;
}
static uint32_t random_fn(void* p) {
    (void)p;
    return 123;
}
static bool cancel_fn(void* p) {
    (void)p;
    return cancel_value;
}
static void text_file(const char* path, const char* text) {
    FILE* f = fopen(path, "wb");
    CHECK(f && fwrite(text, 1, strlen(text), f) == strlen(text) && !fclose(f));
}
static thingsos_weidu_error_t http_fn(
    void* p, const char* method, const char* endpoint, const char* body, const char* ref,
    const char* dest, size_t limit, thingsos_weidu_session_t* session) {
    (void)p;
    (void)method;
    (void)body;
    (void)ref;
    (void)session;
    if(annotation_test)return annotation_http(endpoint,body,dest,limit);
    CHECK(limit == WD_RESPONSE_LIMIT);
    ++requests;
    if(!strcmp(endpoint,"/web/book/readReviews")){
        CHECK(!strcmp(method,"POST") && body && strstr(body,"\"range\":\"3-7\"") && strstr(body,"\"count\":10"));
        if(thought_network_fail)return WD_NETWORK;
        if(thought_dns_failures){--thought_dns_failures;return WD_DNS;}
        text_file(dest,thought_response);return WD_OK;
    }
    if (strstr(endpoint, "bestbookmarks")) CHECK(strstr(endpoint, "count=500"));
    bool personal=!strcmp(endpoint,"/api/agent/gateway");
    if(personal)CHECK(!strcmp(method,"POST") && body && strstr(body,"\"api_name\":\"/book/bookmarklist\"") && strstr(body,"\"skill_version\":\"1.0.3\"") && strstr(body,"\"bookId\":"));
    if(mode==11) {
        if(!strstr(endpoint,"bestbookmarks")){text_file(dest,"{}");return WD_OK;}
        FILE *in=fopen(calibration_response,"rb"),*out=fopen(dest,"wb");CHECK(in&&out);
        char block[1024];size_t n;while((n=fread(block,1,sizeof(block),in)))CHECK(fwrite(block,1,n,out)==n);
        CHECK(!ferror(in));fclose(in);CHECK(!fclose(out));return WD_OK;
    }
    if (mode == 1 && personal) return WD_NETWORK;
    if (mode == 12 && personal) {
        text_file(dest,"{\"errcode\":-2010}");return WD_OK;
    }
    if (mode == 2) {
        text_file(dest, "{}");
        return WD_OK;
    }
    if (mode == 3) {
        text_file(dest, "{\"errcode\":-2012}");
        return WD_OK;
    }
    if (mode == 4) {
        text_file(dest, "{\"items\":[");
        return WD_OK;
    }
    if ((mode == 9 || mode == 10) && strstr(endpoint, "bestbookmarks")) {
        text_file(dest, mode == 10 ? "{\"bestBookMarks\":{\"unexpected\":[]}}" :
            "{\"bestBookMarks\":{\"synckey\":1792315377,\"totalCount\":430,"
            "\"chapters\":[{\"chapterUid\":1,\"title\":\"Chapter\"}],"
            "\"items\":[{\"chapterUid\":1,\"markText\":\"Text\",\"range\":\"3-7\","
            "\"totalCount\":8,\"users\":[{\"name\":\"Reader\"}]}]}}");
        return WD_OK;
    }
    if (mode == 5 && strstr(endpoint, "bestbookmarks")) {
        FILE* f = fopen(dest, "wb");
        CHECK(f);
        fputs("{\"items\":[", f);
        for (unsigned i = 0; i < WD_NOTE_LIMIT + 7; ++i)
            fprintf(
                f, "%s{\"chapterUid\":1,\"markText\":\"Text\",\"range\":\"3-7\"}", i ? "," : "");
        fputs("]}", f);
        CHECK(!fclose(f));
        return WD_OK;
    }
    if (mode == 8 && (strstr(endpoint, "bestbookmarks") || personal)) {
        char data[512];
        snprintf(
            data, sizeof(data),
            "{\"%s\":[{\"chapterUid\":1,\"markText\":\"Text\",\"range\":\"%u-%u\"}]}",
            strstr(endpoint, "bestbookmarks") ? "items" : "updated", (unsigned)custom_offset, (unsigned)custom_offset + 4);
        text_file(dest, data);
        return WD_OK;
    }
    if (mode == 6 && strstr(endpoint, "bestbookmarks")) {
        FILE* f = fopen(dest, "wb");
        CHECK(f);
        fputs("{\"items\":[{\"chapterUid\":\"1\",\"markText\":\"", f);
        for (unsigned i = 0; i < 5000; ++i) fputc('a', f);
        fputs("\"}]}", f);
        CHECK(!fclose(f));
        return WD_OK;
    }
    if (strstr(endpoint, "bestbookmarks"))
        text_file(
            dest,
            "{\"items\":[{\"chapterUid\":1,\"markText\":\"Text\",\"range\":\"3-7\",\"totalCount\":"
            "8}]}");
    else if (personal)
        text_file(
            dest, "{\"updated\":[{\"chapterUid\":\"1\",\"markText\":\"Text\",\"range\":\"3-7\"}]}");
    else {
        CHECK(strstr(endpoint, "listType=3") && strstr(endpoint, "listMode=2"));
        ++reviews;
        if (mode == 7) {
            text_file(dest, "{\"reviews\":[],\"reviewsHasMore\":true,\"synckey\":0}");
            return WD_OK;
        }
        FILE* f = fopen(dest, "wb");
        CHECK(f);
        fputs("{\"reviews\":[", f);
        unsigned start = strstr(endpoint, "maxIdx=40")   ? 40
                         : strstr(endpoint, "maxIdx=20") ? 20
                                                         : 0;
        for (unsigned i = 1; i <= 20; ++i)
            fprintf(
                f,
                "%s{\"idx\":%u,\"author\":{\"name\":\"Author\"},\"review\":{\"content\":\"Review\","
                "\"chapterUid\":1}}",
                i == 1 ? "" : ",", start + i);
        fprintf(
            f, "],\"reviewsHasMore\":true,\"synckey\":\"1234567890123456789\",\"maxIdx\":%u}",
            start + 20);
        CHECK(!fclose(f));
    }
    return WD_OK;
}
static thingsos_weidu_error_t auth_fn(
    void* p, const char* method, const char* path, const char* body, char* out, size_t cap,
    size_t* n, thingsos_weidu_session_t* s) {
    (void)p;
    (void)s;
    (void)path;
    CHECK(cap > 100);
    if (!strcmp(method, "POST")) {
        ++writes;
        cJSON* j = cJSON_Parse(body);
        CHECK(j);
        CHECK(cJSON_IsString(cJSON_GetObjectItem(j, "s")));
        cJSON* rt = cJSON_GetObjectItem(j, "rt");
        if (writes == 1)
            CHECK(!rt);
        else
            CHECK(rt && rt->valueint == 17);
        cJSON_Delete(j);
        if ((mode == 9 && writes == 2) || (mode==16 && writes==3)) return WD_NETWORK;
        strcpy(out, mode==17?"{}":mode==13?"{\"synckey\":1792281600}":mode==14?"{\"data\":{\"succ\":1}}":"{\"succ\":1}");
    } else if(!strcmp(path,"/web/getConfig"))
        strcpy(out,mode==15?"{\"token\":\"\"}":"{\"token\":\"reader-context-token\"}");
    else
        strcpy(
            out,
            "{\"book\":{\"chapterUid\":\"1\",\"chapterOffset\":8,\"progress\":5,\"updateTime\":"
            "1792281600}}");
    *n = strlen(out);
    return WD_OK;
}
static thingsos_weidu_error_t reader_fn(
    void* p, const char* ref, char* ps, size_t cap, thingsos_weidu_session_t* s) {
    (void)p;
    (void)ref;
    (void)cap;
    (void)s;
    strcpy(ps, "1234567890abcdef");
    return WD_OK;
}
static thingsos_weidu_error_t reader_info_fn(
    void* p, const char* ref, char ps[128], char pc[128], char token[128],
    thingsos_weidu_session_t* session) {
    CHECK(strstr(ref, "https://weread.qq.com/web/reader/"));
    strcpy(pc, "reader-context-pc");
    strcpy(token, "reader-context-token");
    return reader_fn(p, ref, ps, 128, session);
}
static thingsos_weidu_error_t exchange_fn(
    void* p, const char* method, const char* endpoint, const char* body, const char* ref, char* out,
    size_t cap, size_t* n, thingsos_weidu_session_t* session) {
    CHECK(strstr(ref, "https://weread.qq.com/web/reader/"));
    if(!strcmp(method,"GET"))return auth_fn(p,method,endpoint,body,out,cap,n,session);
    cJSON* j = cJSON_Parse(body);
    CHECK(j && !strcmp(cJSON_GetObjectItem(j, "pc")->valuestring, "reader-context-pc"));
    cJSON* sg = cJSON_GetObjectItem(j, "sg");
    if (sg && clock_value==1792281600)
        CHECK(!strcmp(
            sg->valuestring, "39264260a2b489f104a6676fbb63391cdb3020378f316ae9efdbfed4555be149"));
    cJSON_Delete(j);
    return auth_fn(p, method, endpoint, body, out, cap, n, session);
}
/* Opt-in local device fixture, never shipped with account/book data. */
static int calibrate(const char* root, const char* id, const char* epub_path, const char* response) {
    thingsos_epub_t* epub = calloc(1, sizeof(*epub));
    thingsos_chapter_t* chapter = calloc(1, sizeof(*chapter));
    thingsos_wd_note_t* note = calloc(1, sizeof(*note));
    CHECK(epub && chapter && note && thingsos_epub_open(epub, epub_path));
    thingsos_weidu_session_t session={0};strcpy(session.vid,"100");strcpy(session.skey,"fixture");
    if(response) {
        thingsos_weidu_client_t* c=calloc(1,sizeof(*c));CHECK(c);
        strcpy(c->root,root);c->session=session;c->io.http=http_fn;c->io.unix_time=clock_fn;
        calibration_response=response;mode=11;CHECK(thingsos_wd_notes_refresh(c,id)==WD_OK);free(c);
    }
    thingsos_wd_notes_info_t info;CHECK(thingsos_wd_notes_info(root,&session,id,&info));
    unsigned total=0, mapped=0;
    for(unsigned i=0;i<info.count[0];++i) {
        CHECK(thingsos_wd_note(root,&session,id,0,i,note));
        int index=thingsos_wd_chapter(root,id,note->uid);CHECK(index>=0);
        CHECK(thingsos_epub_chapter(epub,(unsigned)index,chapter));
        thingsos_wd_map_t map={0};
        bool ok=thingsos_wd_map(root,id,(unsigned)index,chapter,&map);
        size_t a,b;bool hit=ok&&thingsos_wd_map_range(&map,chapter,note,&a,&b);
        thingsos_text_underline_t lines[WD_DRAW_LIMIT];size_t count,available;
        CHECK(ok&&thingsos_wd_lines(root,&session,id,note->uid,&map,chapter,lines,WD_DRAW_LIMIT,&count,&available)&&count>0);
        printf("CALIBRATION chapter=%d map=%d interval=%d checkpoints=%zu\n",index,ok,hit,map.count);
        ++total;if(hit)++mapped;
        thingsos_wd_map_free(&map);thingsos_chapter_close(chapter);
    }
    thingsos_epub_close(epub);free(note);free(chapter);free(epub);
    printf("CALIBRATION exact intervals %u/%u\n",mapped,total);
    CHECK(total && mapped==total);return 0;
}
static int audit_inline_notes(const char* root,const char* id,const char* output) {
    char dir[768],toc[800];snprintf(dir,sizeof(dir),"%s/book-%s",root,id);snprintf(toc,sizeof(toc),"%s/toc.dat",dir);
    FILE* f=fopen(toc,"rb");CHECK(f && !fseek(f,0,SEEK_END));long bytes=ftell(f);CHECK(bytes>0 && bytes%sizeof(thingsos_weidu_chapter_t)==0);fclose(f);
    thingsos_weidu_book_t book={0};snprintf(book.id,sizeof(book.id),"%s",id);strcpy(book.title,"Local diagnostic");
    CHECK(thingsos_weidu_pack(output,&book,dir,toc,(unsigned)(bytes/sizeof(thingsos_weidu_chapter_t))));
    thingsos_epub_t* epub=calloc(1,sizeof(*epub));thingsos_chapter_t* chapter=calloc(1,sizeof(*chapter));CHECK(epub && chapter && thingsos_epub_open(epub,output));
    unsigned total=0;
    for(unsigned i=0;i<epub->spine_count;++i) {
        CHECK(thingsos_epub_chapter(epub,i,chapter));thingsos_wd_map_t map={0};
        bool mapped=thingsos_wd_map(root,id,i,chapter,&map);
        size_t resolved=0;for(size_t j=0;j<chapter->footnote_count;++j)if(chapter->footnotes[j].note_bytes)++resolved;
        printf("AUDIT chapter=%u refs=%zu resolved=%zu notes_bytes=%zu mapped=%d\n",i,chapter->footnote_count,resolved,chapter->notes_bytes,mapped);
        CHECK(mapped && resolved==chapter->footnote_count);total+=(unsigned)resolved;
        thingsos_wd_map_free(&map);thingsos_chapter_close(chapter);
    }
    printf("AUDIT total_notes=%u\n",total);CHECK(total);thingsos_epub_close(epub);free(chapter);free(epub);return 0;
}
extern bool thingsos_weidu_pack(const char*,const thingsos_weidu_book_t*,const char*,const char*,unsigned);
static void navigation_test(const char* root) {
    char dir[700],toc[740],raw[740],file[740],cache[740];
    snprintf(dir,sizeof(dir),"%s/book-9901",root);_mkdir(dir);
    snprintf(toc,sizeof(toc),"%s/toc.dat",dir);snprintf(raw,sizeof(raw),"%s/chapter-0.raw",dir);
    snprintf(cache,sizeof(cache),"%s/navigation.cache",dir);remove(cache);
    snprintf(file,sizeof(file),"%s/微读-9901.epub",root);
    thingsos_weidu_chapter_t record={0};strcpy(record.uid,"1");strcpy(record.title,"One");
    FILE* f=fopen(toc,"wb");CHECK(f && fwrite(&record,sizeof(record),1,f)==1 && !fclose(f));
    const char* html="<html><body><h1>One</h1><h2 id=\"s1\"><span>Center &amp; work</span></h2><p>body one</p>"
        "<h3 class=\"thirdTitle sigil_not_in_toc\">Attachment</h3><p>body two</p>"
        "<h2 id=\"s2\">Village</h2><p>body three</p></body></html>";
    f=fopen(raw,"wb");CHECK(f && fwrite(html,1,strlen(html),f)==strlen(html) && !fclose(f));
    thingsos_weidu_book_t book={0};strcpy(book.id,"9901");strcpy(book.title,"Sections");
    CHECK(thingsos_weidu_pack(file,&book,dir,toc,1));
    thingsos_epub_t* e=calloc(1,sizeof(*e));CHECK(e && thingsos_epub_open(e,file));thingsos_epub_navigation(e);
    bool ready=false;
    CHECK(e->spine_count==1 && thingsos_wd_navigation_step(root,"9901",e,&ready) && !ready);
    CHECK(e->section_toc_count==1 && e->section_toc_next==0); /* roots, no raw scan */
    CHECK(thingsos_wd_navigation_step(root,"9901",e,&ready) && ready);
    CHECK(e->spine_count==1 && e->section_toc_count==3 && e->section_toc[1].level==1);
    CHECK(!strcmp(e->section_toc[1].title,"Center & work") && !strcmp(e->section_toc[2].title,"Village"));
    thingsos_chapter_t* c=calloc(1,sizeof(*c));CHECK(c && thingsos_epub_chapter(e,0,c));
    CHECK(!strncmp(c->text+e->section_toc[1].offset,"Center & work",13));
    CHECK(!strncmp(c->text+e->section_toc[2].offset,"Village",7));
    thingsos_chapter_close(c);free(c);thingsos_epub_close(e);
    /* Warm navigation must work without reopening/scanning raw chapters. */
    char saved[760];snprintf(saved,sizeof(saved),"%s.saved",raw);remove(saved);CHECK(!rename(raw,saved));
    CHECK(thingsos_epub_open(e,file));thingsos_epub_navigation(e);
    CHECK(thingsos_wd_navigation(root,"9901",e) && e->section_toc_count==3);thingsos_epub_close(e);
    CHECK(!rename(saved,raw));
    f=fopen(cache,"r+b");CHECK(f && !fseek(f,-1,SEEK_END) && fputc(0xff,f)!=EOF && !fclose(f));
    CHECK(thingsos_epub_open(e,file));thingsos_epub_navigation(e);
    CHECK(thingsos_wd_navigation(root,"9901",e) && e->section_toc_count==3);thingsos_epub_close(e);
    /* A replacement EPUB with the same book id must invalidate the old index. */
    const char* changed="<html><body><h1>One</h1><h2>New section</h2><p>changed body</p></body></html>";
    f=fopen(raw,"wb");CHECK(f && fwrite(changed,1,strlen(changed),f)==strlen(changed) && !fclose(f));
    CHECK(thingsos_weidu_pack(file,&book,dir,toc,1));CHECK(thingsos_epub_open(e,file));thingsos_epub_navigation(e);
    CHECK(thingsos_wd_navigation(root,"9901",e) && e->section_toc_count==2 && !strcmp(e->section_toc[1].title,"New section"));
    thingsos_epub_close(e);
    /* Two chapters require two separate discovery slices; all roots exist first. */
    strcpy(record.uid,"2");strcpy(record.title,"Two");
    f=fopen(toc,"ab");CHECK(f && fwrite(&record,sizeof(record),1,f)==1 && !fclose(f));
    snprintf(raw,sizeof(raw),"%s/chapter-1.raw",dir);
    const char* second="<html><body><h1>Two</h1><p>second chapter</p></body></html>";
    f=fopen(raw,"wb");CHECK(f && fwrite(second,1,strlen(second),f)==strlen(second) && !fclose(f));
    CHECK(thingsos_weidu_pack(file,&book,dir,toc,2));remove(cache);
    CHECK(thingsos_epub_open(e,file));thingsos_epub_navigation(e);
    CHECK(thingsos_wd_navigation_step(root,"9901",e,&ready) && !ready && e->section_toc_count==2);
    CHECK(thingsos_wd_navigation_step(root,"9901",e,&ready) && !ready && e->section_toc_next==1);
    CHECK(e->section_toc_count==3 && e->section_toc[2].chapter==1);
    CHECK(thingsos_wd_navigation_step(root,"9901",e,&ready) && ready && e->section_toc_next==2);
    CHECK(e->spine_count==2 && e->chapter_sources[1]==1);
    thingsos_epub_close(e);free(e);
}
static int audit_navigation(const char* root,const char* id,const char* file) {
    thingsos_epub_t* e=calloc(1,sizeof(*e));CHECK(e && thingsos_epub_open(e,file));
    thingsos_epub_navigation(e);e->weidu_cache=true;
    CHECK(thingsos_wd_navigation(root,id,e));size_t sections=0;
    for(size_t i=0;i<e->section_toc_count;++i) {
        const thingsos_toc_entry_t* n=&e->section_toc[i];
        printf("NAV %u %u %u %s\n",n->chapter,n->offset,n->level,n->title);sections+=n->offset!=0;
        thingsos_chapter_t* c=calloc(1,sizeof(*c));CHECK(c && thingsos_epub_chapter(e,n->chapter,c));
        CHECK(n->offset<c->bytes);thingsos_chapter_close(c);free(c);
    }
    printf("NAV chapters=%zu entries=%zu sections=%zu\n",e->spine_count,e->section_toc_count,sections);
    CHECK(sections);thingsos_epub_close(e);free(e);return 0;
}
int main(int argc, char** argv) {
    if(argc==5 && !strcmp(argv[1],"--audit-navigation"))return audit_navigation(argv[2],argv[3],argv[4]);
    if(argc==5 && !strcmp(argv[1],"--audit-inline-notes"))return audit_inline_notes(argv[2],argv[3],argv[4]);
    if(argc==4||argc==5)return calibrate(argv[1],argv[2],argv[3],argc==5?argv[4]:NULL);
    CHECK(argc == 2);
    char key_header[64];
    const char* key_json="{\"apikey\":\"wrk-test-value\",\"expireTime\":1900000000}";
    CHECK(thingsos_wd_key_header(key_json,strlen(key_json),key_header,sizeof(key_header)));
    CHECK(!strcmp(key_header,"Bearer wrk-test-value"));
    const char* bad_keys[]={"{}","{\"apikey\":\"short\"}","{\"apikey\":\"wrk-test\\r\\nInjected\"}",
        "{\"apikey\":\"wrk-test-value\",\"errcode\":-2012}",
        "{\"apikey\":\"wrk-test-value\",\"apikey\":\"wrk-second-value\"}",
        "{\"data\":{\"apikey\":\"wrk-test-value\"}}","{\"apikey\":\"wrk-test-value\""};
    for(unsigned i=0;i<sizeof(bad_keys)/sizeof(bad_keys[0]);++i) {
        strcpy(key_header,"sentinel");
        CHECK(!thingsos_wd_key_header(bad_keys[i],strlen(bad_keys[i]),key_header,sizeof(key_header)) && !key_header[0]);
    }
    CHECK(!thingsos_wd_key_header(key_json,strlen(key_json),key_header,12) && !key_header[0]);
    char root[640], dir[700], tocpath[750], rawpath[750], epubpath[750];
    snprintf(root, sizeof(root), "%s/reading-tests", argv[1]);
    _mkdir(root);navigation_test(root);
    _mkdir(root);
    snprintf(dir, sizeof(dir), "%s/book-55", root);
    _mkdir(dir);
    thingsos_weidu_client_t* c = calloc(1, sizeof(*c));
    CHECK(c);
    strcpy(c->root, root);
    strcpy(c->session.vid, "100");
    strcpy(c->session.skey, "secret-key");
    strcpy(c->session.rt, "secret-refresh");
    c->io.http = http_fn;
    c->io.auth = auth_fn;
    c->io.reader = reader_fn;
    c->io.unix_time = clock_fn;
    c->io.random = random_fn;
    c->io.cancelled = cancel_fn;
    thingsos_wd_notes_info_t info;
    thingsos_wd_note_t* note = calloc(1, sizeof(*note));
    CHECK(note);
    thingsos_wd_thought_query_t tq={0};strcpy(tq.id,"55");strcpy(tq.uid,"1");tq.begin=3;tq.end=7;
    thingsos_wd_thought_info_t ti;
    thought_response="{\"reviews\":[{\"range\":\"3-7\",\"maxIdx\":10,\"hasMore\":true,\"totalCount\":15,\"synckey\":\"1234567890123456789\",\"pageReviews\":[{\"reviewId\":\"r1\",\"review\":{\"chapterUid\":1,\"content\":\"A reader thought\",\"author\":{\"name\":\"Reader\"}},\"comments\":[{\"content\":\"Must not replace thought\"}]}]}]}";
    CHECK(thingsos_wd_thought_refresh(c,&tq)==WD_OK);
    CHECK(thingsos_wd_thought_info(root,&c->session,&tq,&ti) && ti.count==1 && ti.more && ti.next_idx==10 && !strcmp(ti.sync,"1234567890123456789"));
    CHECK(thingsos_wd_thought(root,&c->session,&tq,0,note) && !strcmp(note->text,"A reader thought") && !strcmp(note->author,"Reader"));
    CHECK(!thingsos_wd_thought(root,&c->session,&tq,1,note));
    const char* thought_first_response=thought_response;
    tq.max_idx=10;strcpy(tq.sync,"1234567890123456789");
    thought_response="{\"reviews\":[{\"range\":\"3-7\",\"maxIdx\":11,\"hasMore\":false,\"pageReviews\":[{\"review\":{\"content\":\"Second page thought\",\"chapterUid\":1},\"author\":{\"name\":\"Next reader\"}}]}]}";
    CHECK(thingsos_wd_thought_refresh(c,&tq)==WD_OK && thingsos_wd_thought(root,&c->session,&tq,0,note) && !strcmp(note->text,"Second page thought"));
    tq.max_idx=0;tq.sync[0]=0;thought_response=thought_first_response;
    CHECK(thingsos_wd_thought(root,&c->session,&tq,0,note) && !strcmp(note->text,"A reader thought"));

    thingsos_weidu_session_t other=c->session;strcpy(other.vid,"999");CHECK(!thingsos_wd_thought_info(root,&other,&tq,&ti));
    tq.begin=4;CHECK(!thingsos_wd_thought_info(root,&c->session,&tq,&ti));tq.begin=3;
    unsigned thought_before=requests;
    thought_network_fail=true;CHECK(thingsos_wd_thought_refresh(c,&tq)==WD_NETWORK);thought_network_fail=false;
    CHECK(requests==thought_before+1); /* Ambiguous transport errors are never replayed. */
    c->io.wait_ms=thought_wait_fn;thought_dns_failures=1;thought_before=requests;
    CHECK(thingsos_wd_thought_refresh(c,&tq)==WD_OK && requests==thought_before+2 && thought_waits==1);
    thought_dns_failures=3;thought_before=requests;
    CHECK(thingsos_wd_thought_refresh(c,&tq)==WD_DNS && requests==thought_before+2 && thought_waits==2);
    CHECK(thingsos_wd_thought(root,&c->session,&tq,0,note) && !strcmp(note->text,"A reader thought"));
    thought_dns_failures=1;thought_cancel_during_wait=true;thought_before=requests;
    CHECK(thingsos_wd_thought_refresh(c,&tq)==WD_CANCEL && requests==thought_before+1);
    thought_cancel_during_wait=cancel_value=false;thought_dns_failures=0;c->io.wait_ms=NULL;
    CHECK(thingsos_wd_thought(root,&c->session,&tq,0,note) && !strcmp(note->text,"A reader thought"));
    const char* invalid_thoughts[]={"{}","{\"errCode\":-2010}",
        "{\"reviews\":[{\"range\":\"8-9\",\"hasMore\":false,\"pageReviews\":[]}]}",
        "{\"reviews\":[{\"range\":\"3-7\",\"hasMore\":true,\"maxIdx\":0,\"pageReviews\":[]}]}",
        "{\"reviews\":[{\"range\":\"3-7\",\"hasMore\":false,\"pageReviews\":[{\"review\":{\"chapterUid\":2,\"content\":\"Wrong chapter\"}}]}]}",
        "{\"reviews\":[{\"range\":\"3-7\",\"hasMore\":false,\"pageReviews\":[]},{}]}"};
    for(unsigned j=0;j<sizeof(invalid_thoughts)/sizeof(invalid_thoughts[0]);++j){thought_response=invalid_thoughts[j];CHECK(thingsos_wd_thought_refresh(c,&tq)==(j==1?WD_AUTH:WD_PROTOCOL));CHECK(thingsos_wd_thought(root,&c->session,&tq,0,note) && !strcmp(note->text,"A reader thought"));}
    thought_response="{\"reviews\":[{\"range\":\"3-7\",\"hasMore\":false,\"maxIdx\":0,\"pageReviews\":[]}]}";
    cancel_value=true;CHECK(thingsos_wd_thought_refresh(c,&tq)==WD_CANCEL);cancel_value=false;
    CHECK(thingsos_wd_thought(root,&c->session,&tq,0,note));
    CHECK(thingsos_wd_thought_refresh(c,&tq)==WD_OK && thingsos_wd_thought_info(root,&c->session,&tq,&ti) && !ti.count && !ti.more);
    mode = 0;
    CHECK(thingsos_wd_notes_refresh(c, "55") == WD_OK && reviews == 3);
    CHECK(thingsos_wd_notes_info(root, &c->session, "55", &info));
    CHECK(info.count[0] == 1 && info.count[1] == 1 && info.count[2] == 50 && info.limited[2]);
    thingsos_wd_notes_info_t view;
    CHECK(thingsos_wd_notes_view(root,&c->session,"55",&view) && !memcmp(&view,&info,sizeof(view)));
    char manifest_path[768],record_path[768];uint32_t header[4];
    snprintf(manifest_path,sizeof(manifest_path),"%s/notes.manifest",dir);
    FILE* manifest_file=fopen(manifest_path,"rb");CHECK(manifest_file && fread(header,sizeof(header),1,manifest_file)==1);fclose(manifest_file);
    snprintf(record_path,sizeof(record_path),"%s/notes-%u-0.dat",dir,(unsigned)header[3]);
    FILE* corrupt_file=fopen(record_path,"r+b");CHECK(corrupt_file && !fseek(corrupt_file,(long)offsetof(thingsos_wd_note_t,text),SEEK_SET));
    int original_byte=fgetc(corrupt_file);CHECK(original_byte!=EOF && !fseek(corrupt_file,-1,SEEK_CUR));
    CHECK(fputc(original_byte^1,corrupt_file)!=EOF && !fclose(corrupt_file));
    CHECK(thingsos_wd_notes_view(root,&c->session,"55",&view));
    CHECK(!thingsos_wd_note(root,&c->session,"55",0,0,note)); /* Fast directory lookup never permits corrupt text. */
    corrupt_file=fopen(record_path,"r+b");CHECK(corrupt_file && !fseek(corrupt_file,(long)offsetof(thingsos_wd_note_t,text),SEEK_SET));
    CHECK(fputc(original_byte,corrupt_file)!=EOF && !fclose(corrupt_file));
    CHECK(
        thingsos_wd_note(root, &c->session, "55", 0, 0, note) && note->range && note->begin == 3 &&
        note->length == 4 && note->heat == 8);
    CHECK(
        thingsos_wd_note(root, &c->session, "55", 2, 0, note) && !strcmp(note->author, "Author") &&
        !strcmp(note->text, "Review"));
    mode = 9;
    CHECK(thingsos_wd_notes_refresh(c, "55") == WD_OK);
    CHECK(thingsos_wd_notes_info(root,&c->session,"55",&info)&&info.total[0]==430&&info.limited[0]);
    CHECK(thingsos_wd_note(root, &c->session, "55", 0, 0, note) &&
          !strcmp(note->text, "Text") && note->heat == 8 && note->begin == 3);
    mode = 10;
    CHECK(thingsos_wd_notes_refresh(c, "55") == WD_PROTOCOL);
    CHECK(thingsos_wd_notes_info(root, &c->session, "55", &info) && info.count[0] == 1);
    for (unsigned failure = 1; failure <= 4; ++failure) {
        mode = failure;
        if (mode == 2) continue;
        CHECK(thingsos_wd_notes_refresh(c, "55") != WD_OK);
        CHECK(thingsos_wd_notes_info(root, &c->session, "55", &info) && info.count[0] == 1);
    }
    mode=12;CHECK(thingsos_wd_notes_refresh(c,"55")==WD_ACCESS);
    CHECK(thingsos_weidu_session_valid(&c->session));
    cancel_value = true;
    CHECK(thingsos_wd_notes_refresh(c, "55") == WD_CANCEL);
    cancel_value = false;
    strcpy(c->session.vid, "101");
    CHECK(!thingsos_wd_notes_info(root, &c->session, "55", &info));
    CHECK(!thingsos_wd_notes_view(root,&c->session,"55",&view));
    strcpy(c->session.vid, "100");
    mode = 5;
    CHECK(
        thingsos_wd_notes_refresh(c, "55") == WD_OK &&
        thingsos_wd_notes_info(root, &c->session, "55", &info));
    CHECK(info.count[0] == WD_NOTE_LIMIT && info.total[0] == WD_NOTE_LIMIT + 7 && info.limited[0]);
    mode = 6;
    CHECK(
        thingsos_wd_notes_refresh(c, "55") == WD_OK &&
        thingsos_wd_note(root, &c->session, "55", 0, 0, note) && note->truncated &&
        strlen(note->text) == 4095);
    char blocked[768];
    snprintf(blocked, sizeof(blocked), "%s/notes.manifest.part", dir);
    remove(blocked);
    _mkdir(blocked);
    mode = 0;
    CHECK(thingsos_wd_notes_refresh(c, "55") == WD_STORAGE);
    CHECK(thingsos_wd_note(root, &c->session, "55", 0, 0, note) && note->truncated);
    CHECK(!_rmdir(blocked));
    mode = 7;
    CHECK(thingsos_wd_notes_refresh(c, "55") == WD_PROTOCOL);
    CHECK(thingsos_wd_note(root, &c->session, "55", 0, 0, note) && note->truncated);
    mode = 2;
    CHECK(
        thingsos_wd_notes_refresh(c, "55") == WD_OK &&
        thingsos_wd_notes_info(root, &c->session, "55", &info) && !info.count[0] && !info.count[2]);
    unsigned before = requests;
    clock_value = 1;
    CHECK(thingsos_wd_notes_refresh(c, "55") == WD_CLOCK && requests == before);
    clock_value = 1792281600u;
    thingsos_wd_progress_t progress;
    CHECK(thingsos_wd_progress_parse("{\"errcode\":-2012}", &progress) == WD_AUTH);
    CHECK(thingsos_wd_progress_parse("{}", &progress) == WD_OK && !progress.valid);
    CHECK(
        thingsos_wd_progress_parse(
            "{\"book\":{\"chapterUid\":\"1\",\"chapterOffset\":0,\"progress\":100,\"readingTime\":321}}", &progress) ==
            WD_OK &&
        progress.valid && !progress.offset && progress.reading_seconds_valid && progress.reading_seconds==321);
    CHECK(thingsos_wd_progress_parse("{\"errcode\":\"-2012\"}", &progress) == WD_AUTH);
    CHECK(
        thingsos_wd_progress_parse(
            "{\"chapterUid\":1,\"chapterOffset\":0,\"progress\":-1}", &progress) == WD_OK &&
        !progress.valid);
    CHECK(
        thingsos_wd_progress_parse(
            "{\"chapterUid\":1,\"chapterOffset\":0,\"progress\":1.75}", &progress) == WD_OK &&
        progress.valid && progress.percent == 1);
    CHECK(thingsos_wd_pull(c, "55", &progress) == WD_OK && progress.valid && progress.offset == 8);
    thingsos_weidu_chapter_t ch = { 0 };
    strcpy(ch.uid, "1");
    strcpy(ch.title, "Chapter");
    ch.index = 1;
    snprintf(tocpath, sizeof(tocpath), "%s/toc.dat", dir);
    FILE* f = fopen(tocpath, "wb");
    CHECK(f && fwrite(&ch, sizeof(ch), 1, f) == 1 && !fclose(f));
    snprintf(rawpath, sizeof(rawpath), "%s/chapter-0.raw", dir);
    snprintf(epubpath, sizeof(epubpath), "%s/微读-55.epub", root);
    const char* raw =
        "<html><head><title>hidden</title></head><body><p>Text &amp; 中文😀 "
        "Text</p><p>Next</p></body></html>";
    text_file(rawpath, raw);
    thingsos_weidu_book_t book = { 0 };
    strcpy(book.id, "55");
    strcpy(book.title, "Book");
    CHECK(thingsos_weidu_pack(epubpath, &book, dir, tocpath, 1));
    thingsos_epub_t* epub = calloc(1, sizeof(*epub));
    thingsos_chapter_t* chapter = calloc(1, sizeof(*chapter));
    CHECK(epub && chapter);
    CHECK(thingsos_epub_open(epub, epubpath) && thingsos_epub_chapter(epub, 0, chapter));
    thingsos_wd_map_t map = { 0 };
    CHECK(thingsos_wd_map(root, "55", 0, chapter, &map));
    size_t local = (size_t)(strstr(chapter->text, "Text") - chapter->text);
    uint32_t source;
    CHECK(
        thingsos_wd_source(&map, local, &source) &&
        source == (uint32_t)(strstr(raw, "Text") - raw));
    unsigned map_reuses=thingsos_wd_host_map_cache_hits();
    thingsos_wd_map_free(&map);
    CHECK(thingsos_wd_map(root,"55",0,chapter,&map) && thingsos_wd_host_map_cache_hits()==map_reuses+1);
    custom_offset = source;
    /* Writes are single-attempt and need a fresh owner/text/range check plus
       readback. A timed-out POST is reconciled rather than replayed. */
    { annotation_test=true;annotation_begin=source;annotation_end=source+4;
    strcpy(c->output,root);
    thingsos_annotation_t an={0};an.id=1;an.begin=(uint32_t)local;an.end=(uint32_t)local+4;
    an.text_crc=thingsos_annotation_crc(chapter->text,chapter->bytes);an.source_begin=source;an.source_end=source+4;
    an.sync=THINGSOS_NOTE_PENDING;strcpy(an.uid,"1");strcpy(an.quote,"Text");strcpy(an.thought,annotation_content);
    thingsos_annotation_t original_an=an;char owner[33];thingsos_weidu_md5(c->session.vid,strlen(c->session.vid),owner);
    unsigned sync_state=0,network_before=requests;
    CHECK(thingsos_wd_annotation_push(c,"55","wrong-account",&an,&sync_state)==WD_AUTH && !annotation_posts && requests==network_before);
    ++an.text_crc;CHECK(thingsos_wd_annotation_push(c,"55",owner,&an,&sync_state)==WD_PROTOCOL && !annotation_posts);an=original_an;
    CHECK(thingsos_wd_annotation_push(c,"55",owner,&an,&sync_state)==WD_OK && sync_state==THINGSOS_NOTE_SYNCED && annotation_posts==2);
    CHECK(!strcmp(an.bookmark_id,"b1") && !strcmp(an.review_id,"r1"));
    CHECK(thingsos_wd_annotation_push(c,"55",owner,&an,&sync_state)==WD_OK && annotation_posts==2);
    annotation_content="edited thought";strcpy(an.thought,annotation_content);
    CHECK(thingsos_wd_annotation_push(c,"55",owner,&an,&sync_state)==WD_OK && sync_state==THINGSOS_NOTE_SYNCED && annotation_posts==3);
    thingsos_wd_personal_t* own=calloc(1,sizeof(*own));unsigned own_count=0;CHECK(own);
    CHECK(thingsos_wd_personal_info(root,&c->session,"55",&own_count) && own_count==1);
    CHECK(thingsos_wd_personal_find(root,&c->session,"55","1",source,source+4,own) && !strcmp(own->note.text,annotation_content));
    annotation_failure=5;CHECK(thingsos_wd_personal_refresh(c,"55")==WD_NETWORK);
    CHECK(thingsos_wd_personal_info(root,&c->session,"55",&own_count) && own_count==1);free(own);
    annotation_failure=0;an.thought[0]=0;
    unsigned delete_before=annotation_posts;
    CHECK(thingsos_wd_annotation_push(c,"55",owner,&an,&sync_state)==WD_OK && sync_state==THINGSOS_NOTE_SYNCED);
    CHECK(annotation_posts==delete_before+1 && !an.review_id[0] && annotation_bookmark && !annotation_thought);
    CHECK(thingsos_wd_annotation_push(c,"55",owner,&an,&sync_state)==WD_OK && annotation_posts==delete_before+1);
    an=original_an;strcpy(an.review_id,"r1");an.thought[0]=0;annotation_thought=true;an.sync=THINGSOS_NOTE_UNCERTAIN;
    CHECK(thingsos_wd_annotation_push(c,"55",owner,&an,&sync_state)==WD_UNCONFIRMED && annotation_posts==delete_before+1);
    annotation_thought=false;
    CHECK(thingsos_wd_annotation_push(c,"55",owner,&an,&sync_state)==WD_OK && !an.review_id[0] && annotation_posts==delete_before+1);
    an=original_an;an.deleted=1;an.thought[0]=0;strcpy(an.review_id,"r1");strcpy(an.bookmark_id,"b1");
    annotation_bookmark=annotation_thought=true;unsigned remove_before=annotation_posts;
    CHECK(thingsos_wd_annotation_push(c,"55",owner,&an,&sync_state)==WD_OK && sync_state==THINGSOS_NOTE_SYNCED);
    CHECK(!annotation_bookmark && !annotation_thought && !an.review_id[0] && !an.bookmark_id[0] && annotation_posts==remove_before+2);
    an=original_an;an.deleted=1;an.thought[0]=0;strcpy(an.review_id,"r1");strcpy(an.bookmark_id,"b1");
    annotation_bookmark=annotation_thought=true;annotation_failure=6;unsigned retry_before=annotation_posts;
    CHECK(thingsos_wd_annotation_push(c,"55",owner,&an,&sync_state)==WD_NETWORK && sync_state==THINGSOS_NOTE_FAILED);
    CHECK(!an.review_id[0] && !an.bookmark_id[0] && annotation_posts==retry_before+2);
    an.sync=sync_state;annotation_failure=0;
    CHECK(thingsos_wd_annotation_push(c,"55",owner,&an,&sync_state)==WD_OK && sync_state==THINGSOS_NOTE_SYNCED && annotation_posts==retry_before+2);
    for(unsigned failure=1;failure<=3;++failure) {
        annotation_failure=failure;annotation_bookmark=annotation_thought=false;an=original_an;
        unsigned before=annotation_posts;
        thingsos_weidu_error_t expected=failure==1?WD_NETWORK:failure==2?WD_UNCONFIRMED:WD_AUTH;
        CHECK(thingsos_wd_annotation_push(c,"55",owner,&an,&sync_state)==expected && annotation_posts==before+1);
        CHECK(sync_state==(failure==3?THINGSOS_NOTE_FAILED:THINGSOS_NOTE_UNCERTAIN));
        an.sync=THINGSOS_NOTE_UNCERTAIN;annotation_failure=0;
        CHECK(thingsos_wd_annotation_push(c,"55",owner,&an,&sync_state)==WD_UNCONFIRMED && annotation_posts==before+1);
    }
    annotation_content="a thought";annotation_failure=4;annotation_bookmark=annotation_thought=false;an=original_an;
    unsigned partial_before=annotation_posts;
    CHECK(thingsos_wd_annotation_push(c,"55",owner,&an,&sync_state)==WD_NETWORK && sync_state==THINGSOS_NOTE_UNCERTAIN && annotation_posts==partial_before+2);
    CHECK(an.bookmark_id[0] && an.review_id[0]);an.sync=sync_state;annotation_failure=0;
    CHECK(thingsos_wd_annotation_push(c,"55",owner,&an,&sync_state)==WD_OK && sync_state==THINGSOS_NOTE_SYNCED && annotation_posts==partial_before+2);
    annotation_test=false; }
    mode = 8;
    CHECK(thingsos_wd_notes_refresh(c, "55") == WD_OK);
    thingsos_text_underline_t intervals[WD_DRAW_LIMIT];
    size_t count, total;
    CHECK(
        thingsos_wd_lines(
            root, &c->session, "55", "1", &map, chapter, intervals, WD_DRAW_LIMIT, &count,
            &total) &&
        count == 2 && total == 2 && intervals[0].mine && !intervals[1].mine);
    unsigned cached_reads=thingsos_wd_host_lines_cache_hits();
    CHECK(thingsos_wd_lines(root,&c->session,"55","1",&map,chapter,intervals,WD_DRAW_LIMIT,&count,&total));
    CHECK(count==2 && total==2 && thingsos_wd_host_lines_cache_hits()==cached_reads+1);
    char interval_cache[800];snprintf(interval_cache,sizeof(interval_cache),"%s/lines-1.cache",dir);
    FILE* damaged=fopen(interval_cache,"r+b");CHECK(damaged && !fseek(damaged,-1,SEEK_END));
    CHECK(fputc(0xff,damaged)!=EOF && !fclose(damaged));
    cached_reads=thingsos_wd_host_lines_cache_hits();
    CHECK(thingsos_wd_lines(root,&c->session,"55","1",&map,chapter,intervals,WD_DRAW_LIMIT,&count,&total) && count==2);
    CHECK(thingsos_wd_host_lines_cache_hits()==cached_reads); /* CRC failure falls back to verified records. */
    char saved_letter=chapter->text[local];chapter->text[local]='X';
    CHECK(thingsos_wd_lines(root,&c->session,"55","1",&map,chapter,intervals,WD_DRAW_LIMIT,&count,&total));
    CHECK(thingsos_wd_host_lines_cache_hits()==cached_reads);
    chapter->text[local]=saved_letter;
    CHECK(thingsos_wd_lines(root,&c->session,"55","1",&map,chapter,intervals,WD_DRAW_LIMIT,&count,&total) && count==2);
    thingsos_weidu_session_t other_reader=c->session;strcpy(other_reader.vid,"999");
    CHECK(!thingsos_wd_lines(root,&other_reader,"55","1",&map,chapter,intervals,WD_DRAW_LIMIT,&count,&total));
    CHECK(thingsos_wd_line_note(root,&c->session,"55","1",&map,chapter,local,local+4,note));
    CHECK(note->range && note->length==4 && !strcmp(note->text,"Text"));
    CHECK(!thingsos_wd_line_note(root,&c->session,"55","1",&map,chapter,local+1,local+4,note));
    memset(note, 0, sizeof(*note));
    note->begin = source;
    note->length = 4;
    note->range = true;
    strcpy(note->text, "Text");
    size_t a, b;
    CHECK(thingsos_wd_map_range(&map, chapter, note, &a, &b) && a == local && b == local + 4);
    note->begin++;
    CHECK(!thingsos_wd_map_range(&map, chapter, note, &a, &b));
    note->begin--;
    strcpy(note->text, "Other");
    CHECK(!thingsos_wd_map_range(&map, chapter, note, &a, &b));
    size_t after = (size_t)(strstr(chapter->text, " Text") + 1 - chapter->text);
    CHECK(thingsos_wd_source(&map, after, &source));
    CHECK(
        source == (uint32_t)(strstr(raw, " Text") + 1 - raw) -
                      6); /* two CJK UTF-8 widths and astral UTF-16 */
    size_t back;
    CHECK(thingsos_wd_local(&map, source, &back) && back == after);
    chapter->text[local] = 'X';
    thingsos_wd_map_free(&map);
    CHECK(!thingsos_wd_map(root, "55", 0, chapter, &map));
    chapter->text[local] = 'T';
    CHECK(thingsos_wd_map(root, "55", 0, chapter, &map));
    thingsos_wd_upload_t in = { 0 };
    strcpy(in.id, "55");
    in.offset = source;
    in.percent = 8;
    in.seconds = 17;
    in.report = true;
    char body[4096];
    CHECK(thingsos_wd_read_body(&in, &ch, "1234567890abcdef", 1792281600, 123, body, sizeof(body)));
    cJSON* j = cJSON_Parse(body);
    CHECK(
        j && cJSON_GetObjectItem(j, "rt")->valueint == 17 &&
        strlen(cJSON_GetObjectItem(j, "sg")->valuestring) == 64);
    CHECK(!strcmp(
        cJSON_GetObjectItem(j, "sg")->valuestring,
        "6fc26ffa9e5bc7a6a0a109fa84428f92da741574480b1ffad4d60f796d7d1f6d"));
    CHECK(!strcmp(cJSON_GetObjectItem(j, "appId")->valuestring, "wb143h2031264381"));
    cJSON_Delete(j);
    mode = 0;
    writes = 0;
    thingsos_weidu_error_t duration;
    CHECK(thingsos_wd_upload(c, &in, &duration) == WD_OK && duration == WD_OK && writes == 2);
    mode=13;writes=0;
    CHECK(thingsos_wd_upload(c,&in,&duration)==WD_OK && duration==WD_UNCONFIRMED && writes==2);
    mode=14;writes=0;
    CHECK(thingsos_wd_upload(c,&in,&duration)==WD_PROTOCOL && duration==WD_CANCEL && writes==1);
    mode=15;writes=0;
    CHECK(thingsos_wd_upload(c,&in,&duration)==WD_OK && duration==WD_PROTOCOL && writes==1);
    mode=0;
    mode = 9;
    writes = 0;
    CHECK(thingsos_wd_upload(c, &in, &duration) == WD_OK && duration == WD_NETWORK && writes == 2);
    in.report = false;
    writes = 0;
    CHECK(thingsos_wd_upload(c, &in, &duration) == WD_OK && writes == 1 && duration == WD_CANCEL);
    clock_value = 1;
    writes = 0;
    CHECK(thingsos_wd_upload(c, &in, &duration) == WD_CLOCK && !writes);
    clock_value = 1792281600;
    c->io.reader_info = reader_info_fn;
    c->io.exchange = exchange_fn;
    mode = 0;
    writes = 0;
    in.report = true;
    CHECK(thingsos_wd_upload(c, &in, &duration) == WD_OK && duration == WD_OK && writes == 2);
    thingsos_weidu_ps_t token_parser = { 0 };
    thingsos_wd_read_session_t read_session={0};
    writes=0;clock_value=1792281600;
    CHECK(thingsos_wd_begin(c,&in,&read_session)==WD_OK && read_session.entered && writes==1);
    clock_value+=17;in.seconds=999;
    CHECK(thingsos_wd_upload_session(c,&in,&duration,&read_session)==WD_OK && duration==WD_OK && writes==2 && read_session.last_seconds==17);
    CHECK(!read_session.entered && read_session.consumed);
    CHECK(thingsos_wd_upload_session(c,&in,&duration,&read_session)==WD_CANCEL && duration==WD_CANCEL && writes==2);
    mode=9;writes=0;clock_value=1792281600;
    CHECK(thingsos_wd_begin(c,&in,&read_session)==WD_OK && writes==1);clock_value+=17;
    CHECK(thingsos_wd_upload_session(c,&in,&duration,&read_session)==WD_NETWORK && duration==WD_NETWORK && writes==2);
    CHECK(thingsos_wd_upload_session(c,&in,&duration,&read_session)==WD_CANCEL && writes==2);
    mode=0;clock_value=1792281600;in.seconds=17;
    writes=0;CHECK(thingsos_wd_begin(c,&in,&read_session)==WD_OK && writes==1);
    clock_value+=17;
    CHECK(thingsos_wd_upload_interval(c,&in,&duration,&read_session,false)==WD_OK && duration==WD_OK && writes==2 && read_session.entered);
    CHECK(thingsos_wd_upload_interval(c,&in,&duration,&read_session,false)==WD_OK && duration==WD_CANCEL && writes==2);
    clock_value+=17;mode=16;
    CHECK(thingsos_wd_upload_interval(c,&in,&duration,&read_session,false)==WD_NETWORK && duration==WD_NETWORK && writes==3);
    CHECK(thingsos_wd_upload_interval(c,&in,&duration,&read_session,false)==WD_OK && duration==WD_CANCEL && writes==3);
    clock_value+=17;mode=17;
    CHECK(thingsos_wd_upload_interval(c,&in,&duration,&read_session,false)==WD_UNCONFIRMED && duration==WD_UNCONFIRMED && writes==4);
    CHECK(thingsos_wd_upload_interval(c,&in,&duration,&read_session,false)==WD_OK && duration==WD_CANCEL && writes==4);
    clock_value+=17;mode=0;
    CHECK(thingsos_wd_upload_session(c,&in,&duration,&read_session)==WD_OK && duration==WD_OK && writes==5);
    CHECK(thingsos_wd_upload_session(c,&in,&duration,&read_session)==WD_CANCEL && writes==5);
    clock_value=1792281600;
    const char* context = "{\"reader\":{\"token\":\"reader-context-token\"}}";
    for (size_t i = 0; i < strlen(context); ++i)
        CHECK(thingsos_weidu_field_feed(
            &token_parser, "\"token\"", (const unsigned char*)context + i, 1));
    CHECK(token_parser.state == 4 && !strcmp(token_parser.value, "reader-context-token"));
    char owner[33];
    thingsos_weidu_md5("100", 3, owner);
    in.report = true;
    in.seconds = 17;
    CHECK(thingsos_wd_pending_save(root, owner, &in));
    thingsos_wd_upload_t pending;
    CHECK(
        thingsos_wd_pending_load(root, owner, &pending) && !pending.report && !pending.seconds &&
        pending.offset == in.offset);
    CHECK(!thingsos_wd_pending_load(root, "00000000000000000000000000000000", &pending));
    thingsos_wd_pending_clear(root);
    CHECK(!thingsos_wd_pending_load(root, owner, &pending));
    {
        char queue[800];snprintf(queue,sizeof(queue),"%s/progress-%s.queue",root,owner);remove(queue);
        char backup[820];snprintf(backup,sizeof(backup),"%s.bak",queue);remove(backup);
        uint64_t first,second,newer,peek;
        in.report=true;in.seconds=17;
        CHECK(thingsos_wd_queue_put(root,owner,&in,&first));
        thingsos_wd_upload_t other=in;strcpy(other.id,"56");
        CHECK(thingsos_wd_queue_put(root,owner,&other,&second));
        in.offset=in.offset?in.offset-1:0; /* backwards reading replaces the old position too */
        CHECK(thingsos_wd_queue_put(root,owner,&in,&newer) && newer>second);
        CHECK(thingsos_wd_queue_ack(root,owner,in.id,first)); /* stale completion */
        CHECK(thingsos_wd_queue_peek(root,owner,&pending,&peek) && peek==newer && pending.offset==in.offset);
        CHECK(!pending.report && !pending.seconds);
        CHECK(!thingsos_wd_queue_peek(root,"00000000000000000000000000000000",&pending,&peek));
        CHECK(thingsos_wd_queue_ack(root,owner,in.id,newer));
        CHECK(thingsos_wd_queue_peek(root,owner,&pending,&peek) && !strcmp(pending.id,"56") && peek==second);
        CHECK(thingsos_wd_queue_ack(root,owner,"56",second));
        CHECK(!thingsos_wd_queue_peek(root,owner,&pending,&peek));
        for(unsigned i=0;i<8;++i){snprintf(other.id,sizeof(other.id),"q%u",i);CHECK(thingsos_wd_queue_put(root,owner,&other,&peek));}
        strcpy(other.id,"overflow");CHECK(!thingsos_wd_queue_put(root,owner,&other,&peek));
        CHECK(thingsos_wd_queue_peek(root,owner,&pending,&peek) && !strcmp(pending.id,"q0"));
        FILE* broken=fopen(queue,"wb");CHECK(broken && fputc(1,broken)!=EOF && !fclose(broken));
        CHECK(thingsos_wd_queue_peek(root,owner,&pending,&peek)); /* last valid backup */
        broken=fopen(backup,"wb");CHECK(broken && fputc(1,broken)!=EOF && !fclose(broken));
        CHECK(!thingsos_wd_queue_put(root,owner,&other,&peek)); /* preserve corrupt original */
        remove(queue);remove(backup);
    }
    unsigned selection;
    CHECK(thingsos_reader_menu_hit(500, 1150, &selection) == THINGSOS_MENU_MORE);
    CHECK(thingsos_reader_menu_hit_weidu(300, 1150, &selection) == THINGSOS_MENU_NOTES);
    /* Overlay pixels and pagination: mine takes precedence over dotted intervals. */
    char fontpath[750];
    snprintf(fontpath, sizeof(fontpath), "%s/default.ttf", argv[1]);
    thingsos_font_t font = { 0 };
    if (!thingsos_font_file(&font, fontpath)) {
        snprintf(
            fontpath, sizeof(fontpath), "%s/../../native/firmware/main/assets/default.ttf",
            argv[1]);
        CHECK(thingsos_font_file(&font, fontpath));
    }
    thingsos_font_size(&font, 32);
    unsigned char* pixels = calloc(684 * 1216, 1);
    CHECK(pixels);
    memset(pixels, 255, 684 * 1216);
    thingsos_canvas_t canvas = { pixels, 684, 1216 };
    size_t end = thingsos_text_page_layout(
        &font, &canvas, chapter, local, 36, 90, 612, 1040, 145, 0, true, NULL, NULL, NULL);
    thingsos_text_underline_t lines[2] = { { local, local + 4, false },
                                           { local, local + 4, true } };
    chapter->underlines = lines;
    chapter->underline_count = 2;
    CHECK(
        thingsos_text_page_layout(
            &font, &canvas, chapter, local, 36, 90, 612, 1040, 145, 0, true, NULL, NULL, NULL) == end);
    for (unsigned x = 36; x < 65; ++x) CHECK(pixels[132 * 684 + x] == 0);
    size_t string_end = thingsos_text_string_page(
        &font, &canvas, chapter->text, chapter->bytes, local, 36, 90, 612, 1040, 145);
    chapter->underlines = NULL;
    chapter->underline_count = 0;
    CHECK(
        string_end ==
        thingsos_text_page_layout(
            &font, &canvas, chapter, local, 36, 90, 612, 1040, 145, 0, false, NULL, NULL, NULL));
    {
        /* A wrapped highlight gets separate row boxes, never a rectangle across
           the empty remainder of adjacent lines. Footnote targets stay separate. */
        char text[]="First highlighted line and a second line plus body text.";
        thingsos_text_underline_t marked={0,36,false};
        thingsos_chapter_t view={0};view.text=text;view.bytes=strlen(text);view.underlines=&marked;view.underline_count=1;
        thingsos_text_hits_t hit={0};
        size_t finish=thingsos_text_page_layout(&font,NULL,&view,0,36,90,160,1040,145,0,true,NULL,NULL,&hit);
        CHECK(finish>0 && hit.line_count>1 && !hit.count);
        for(size_t k=0;k<hit.line_count;++k){CHECK(hit.lines[k].begin==0 && hit.lines[k].end==36 && hit.lines[k].y>=90 && hit.lines[k].y+hit.lines[k].h<=1130);if(k)CHECK(hit.lines[k].y>hit.lines[k-1].y);}
        marked.mine=true;memset(&hit,0,sizeof(hit));
        thingsos_text_page_layout(&font,NULL,&view,0,36,90,160,1040,145,0,true,NULL,NULL,&hit);CHECK(hit.line_count>1 && hit.glyph_count);
    }
    CHECK(thingsos_wd_memory_ready(34 * 1024, 17 * 1024, 576 * 1024, 64 * 1024));
    CHECK(!thingsos_wd_memory_ready(33 * 1024, 17 * 1024, 576 * 1024, 64 * 1024));
    CHECK(!thingsos_wd_memory_ready(34 * 1024, 16 * 1024, 576 * 1024, 64 * 1024));
    CHECK(!thingsos_wd_memory_ready(34 * 1024, 17 * 1024, 575 * 1024, 64 * 1024));
    CHECK(!thingsos_wd_memory_ready(34 * 1024, 17 * 1024, 576 * 1024, 63 * 1024));
    thingsos_wd_host_block_limit(2048);
    writes = 0;
    CHECK(thingsos_wd_upload(c, &in, &duration) == WD_MEMORY && !writes);
    thingsos_wd_host_block_limit(SIZE_MAX);
    thingsos_wd_map_free(&map);
    thingsos_chapter_close(chapter);
    ch.plain = true;
    f = fopen(tocpath, "wb");
    CHECK(f && fwrite(&ch, sizeof(ch), 1, f) == 1 && !fclose(f));
    size_t body_bytes = 300000;
    chapter->bytes = strlen(ch.title) + 1 + body_bytes;
    chapter->text = malloc(chapter->bytes + 1);
    CHECK(chapter->text);
    memcpy(chapter->text, ch.title, strlen(ch.title));
    chapter->text[strlen(ch.title)] = '\n';
    memset(chapter->text + strlen(ch.title) + 1, 'A', body_bytes);
    chapter->text[chapter->bytes] = 0;
    f = fopen(rawpath, "wb");
    CHECK(
        f && fwrite(chapter->text + strlen(ch.title) + 1, 1, body_bytes, f) == body_bytes &&
        !fclose(f));
    CHECK(thingsos_wd_map(root, "55", 0, chapter, &map));
    CHECK(map.capacity * 8 <= 65536 && map.count < 3000);
    printf(
        "MEMORY: source=300000 B, stream=4096 B, index live=%zu B, checkpoints=%zu\n",
        map.capacity * 8, map.count);
    thingsos_wd_map_free(&map);
    thingsos_wd_host_block_limit(4096);
    CHECK(
        !thingsos_wd_map(root, "55", 0, chapter, &map) && !map.source &&
        chapter->bytes == body_bytes + 8);
    thingsos_wd_host_block_limit(SIZE_MAX);
    /* Historical packing retained head text/whitespace. Match that exact
       complete chapter without rewriting it or accepting changed content. */
    thingsos_chapter_close(chapter);thingsos_epub_close(epub);
    ch.plain=false;f=fopen(tocpath,"wb");CHECK(f&&fwrite(&ch,sizeof(ch),1,f)==1&&!fclose(f));
    text_file(rawpath,"<html>legacy\r\n  <body><p>Text</p>\r\n  <p>Next</p></body></html>");
    CHECK(thingsos_weidu_pack(epubpath,&book,dir,tocpath,1));
    const char* legacy="<html><head>legacy\r\n  </head><body><p>Text</p>\r\n  <p>Next</p></body></html>";
    text_file(rawpath,legacy);
    CHECK(thingsos_epub_open(epub,epubpath)&&thingsos_epub_chapter(epub,0,chapter));
    CHECK(thingsos_wd_map(root,"55",0,chapter,&map));
    memset(note,0,sizeof(*note));note->range=true;note->begin=(uint32_t)(strstr(legacy,"Text")-legacy);
    note->length=(uint32_t)(strstr(legacy,"Next")+4-legacy)-note->begin;strcpy(note->text,"TextNext");
    CHECK(thingsos_wd_map_range(&map,chapter,note,&a,&b));
    strcpy(note->text,"TextNexx");CHECK(!thingsos_wd_map_range(&map,chapter,note,&a,&b));
    thingsos_wd_map_free(&map);
    /* A WeRead chapter that links to a note keeps its anchors through packing, so
       the cached book offers the same tap-to-read footnote as a local EPUB. */
    {
        char fn_raw[750], fn_toc[750], fn_epub[750];
        snprintf(fn_raw, sizeof(fn_raw), "%s/chapter-0.raw", dir);
        snprintf(fn_toc, sizeof(fn_toc), "%s/footnote-toc.dat", dir);
        snprintf(fn_epub, sizeof(fn_epub), "%s/微读-57.epub", root);
        ch.plain = false;
        f = fopen(fn_toc, "wb");
        CHECK(f && fwrite(&ch, sizeof(ch), 1, f) == 1 && !fclose(f));
        text_file(
            fn_raw, "<html><body><p>正文<a href=\"#ft1\" id=\"r1\"><sup>1</sup></a>后文</p>"
                    "<div id=\"ft1\"><p>注解正文</p></div></body></html>");
        CHECK(thingsos_weidu_pack(fn_epub, &book, dir, fn_toc, 1));
        CHECK(thingsos_epub_open(epub, fn_epub) && thingsos_epub_chapter(epub, 0, chapter));
        CHECK(chapter->footnote_count == 1 && chapter->footnotes && chapter->notes);
        CHECK(chapter->footnotes[0].end - chapter->footnotes[0].begin == 1 &&
              chapter->text[chapter->footnotes[0].begin] == '1');
        CHECK(chapter->footnotes[0].note_bytes > 0);
        char note_text[128];
        size_t note_bytes = chapter->footnotes[0].note_bytes < sizeof(note_text) - 1
                                ? chapter->footnotes[0].note_bytes
                                : sizeof(note_text) - 1;
        memcpy(note_text, chapter->notes + chapter->footnotes[0].note_offset, note_bytes);
        note_text[note_bytes] = 0;
        CHECK(strstr(note_text, "注解正文"));
        CHECK(thingsos_wd_map(root,"55",0,chapter,&map));thingsos_wd_map_free(&map);
        thingsos_chapter_close(chapter);thingsos_epub_close(epub);
        text_file(fn_raw,"<p>Body<a role=\"doc-noteref\" href=\"#ft1\"><img src=\"note.png\"/></a>After<a role=\"doc-noteref\" href=\"#ft1\"/></p><aside id=\"ft1\">NOTE_BODY</aside>");
        CHECK(thingsos_weidu_pack(fn_epub,&book,dir,fn_toc,1));
        CHECK(thingsos_epub_open(epub,fn_epub) && thingsos_epub_chapter(epub,0,chapter) && chapter->footnote_count==2);
        CHECK(thingsos_wd_map(root,"55",0,chapter,&map));thingsos_wd_map_free(&map);
        thingsos_chapter_close(chapter);thingsos_epub_close(epub);
        f=fopen(fn_raw,"wb");CHECK(f);
        CHECK(fputs("<p>Before<img alt='注解 &amp; \"quoted\" ",f)>=0);
        for(unsigned i=0;i<1000;++i)CHECK(fputs("长注",f)>=0);
        CHECK(fputs("' class='qqreader-footnote' src='note.png'/>After<span class=\"reader_footer_note\" data-wr-footernote=\"Second &lt;note&gt;\"></span>End</p>",f)>=0 && !fclose(f));
        CHECK(thingsos_weidu_pack(fn_epub,&book,dir,fn_toc,1));
        CHECK(thingsos_epub_open(epub,fn_epub) && thingsos_epub_chapter(epub,0,chapter));
        CHECK(chapter->footnote_count==2 && chapter->image_count==0 && chapter->footnotes[0].note_bytes>4096);
        CHECK(strstr(chapter->notes,"注解 & \"quoted\"") && strstr(chapter->notes+chapter->footnotes[1].note_offset,"Second <note>"));
        CHECK(thingsos_wd_map(root,"55",0,chapter,&map));thingsos_wd_map_free(&map);
        thingsos_chapter_close(chapter);thingsos_epub_close(epub);
    }
    /* Decoder refills may split a UTF-8 scalar, entity, or quoted tag. Verify
       the full text/source map for each split and reject truncated UTF-8. */
    for (unsigned padding = 480; padding < 530; ++padding) {
        ch.plain = false;
        f = fopen(tocpath, "wb");
        CHECK(f && fwrite(&ch, sizeof(ch), 1, f) == 1 && !fclose(f));
        f = fopen(rawpath, "wb");
        CHECK(f && fputs("<p>", f) >= 0);
        for (unsigned i = 0; i < padding; ++i) CHECK(fputc('a', f) != EOF);
        CHECK(fputs("中文😀&amp;<span title='a>b'>尾文</span></p>", f) >= 0 && !fclose(f));
        CHECK(thingsos_weidu_pack(epubpath, &book, dir, tocpath, 1));
        CHECK(thingsos_epub_open(epub, epubpath) && thingsos_epub_chapter(epub, 0, chapter));
        CHECK(strstr(chapter->text, "中文😀&尾文"));
        CHECK(thingsos_wd_map(root, "55", 0, chapter, &map));
        uint32_t source_offset = 0;
        CHECK(thingsos_wd_source(&map, (size_t)(strstr(chapter->text, "中文") - chapter->text), &source_offset));
        CHECK(source_offset == padding + 3);
        thingsos_wd_map_free(&map);
        f = fopen(rawpath, "ab");
        CHECK(f && fputc(0xf0, f) != EOF && fputc(0x9f, f) != EOF && !fclose(f));
        CHECK(!thingsos_wd_map(root, "55", 0, chapter, &map));
        thingsos_wd_map_free(&map);
        thingsos_chapter_close(chapter);
        thingsos_epub_close(epub);
    }
    free(pixels);
    thingsos_font_close(&font);
    thingsos_wd_map_free(&map);
    thingsos_chapter_close(chapter);
    thingsos_epub_close(epub);
    free(chapter);
    free(epub);
    free(note);
    free(c);
    puts(
        "PASS: transactional notes, account isolation, pagination cursors, limits, empty objects, "
        "source UTF-16 mapping, safe omission, separate progress/time, no retry, overlay/page "
        "identity");
    return 0;
}
