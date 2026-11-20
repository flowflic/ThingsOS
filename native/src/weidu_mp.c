/* SPDX-License-Identifier: Apache-2.0 */
#include "thingsos/weidu_mp.h"
#include "zlib.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <stddef.h>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "esp_log.h"
#endif
#ifdef _WIN32
#include <direct.h>
#define mp_mkdir(p) _mkdir(p)
#else
#define mp_mkdir(p) mkdir(p,0775)
#endif
extern bool thingsos_weidu_json_events(const char*,bool (*)(void*,const char*,const char*,char,unsigned),void*);
static void* mp_alloc(size_t n) {
#ifdef ESP_PLATFORM
    return heap_caps_calloc(1,n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
#else
    return calloc(1,n);
#endif
}
static void cp(char* out,size_t cap,const char* s) {
    size_t n=strlen(s);if(n>=cap){n=cap-1;while(n && ((unsigned char)s[n]&0xc0)==0x80)--n;}
    memcpy(out,s,n);out[n]=0;
}
static bool safe_len(const char* s,size_t cap) {
    if(!s || !*s || strlen(s)>=cap)return false;
    for(;*s;++s)if(!isalnum((unsigned char)*s) && *s!='_' && *s!='-')return false;
    return true;
}
/* Account ids stay short; article ids are long and only need to fit the field. */
static bool safe(const char* s) { return safe_len(s,64); }
static bool safe_id(const char* s) { return safe_len(s,sizeof(((thingsos_weidu_book_t*)0)->id)); }
bool thingsos_mp_account_id(const char* s){return safe(s) && !strncmp(s,"MP_WXS_",7) && s[7];}
static bool stopped(thingsos_weidu_client_t* c){return c->io.cancelled && c->io.cancelled(c->io.context);}
static bool replace(const char* tmp,const char* final) {
    char bak[800];int n=snprintf(bak,sizeof(bak),"%s.bak",final);if(n<0 || (size_t)n>=sizeof(bak))return false;
    FILE* f=fopen(final,"rb");bool old=f!=NULL;if(f)fclose(f);
    if(old){remove(bak);if(rename(final,bak))return false;}
    if(rename(tmp,final)){if(old)rename(bak,final);return false;}
    if(old)remove(bak);
    return true;
}
typedef struct {uint32_t magic,version,count,crc;char owner[33];} mp_header_t;
typedef struct {thingsos_weidu_book_t book;uint32_t crc;} mp_row_t;
static bool cache_path(const char* root,const char* account,char* path,size_t cap) {
    if(account && !thingsos_mp_account_id(account))return false;
    int n=snprintf(path,cap,"%s/mp-%s.dat",root,account?account:"accounts");return n>0 && (size_t)n<cap;
}
static bool row_valid(const mp_row_t* r) {
    return memchr(r->book.id,0,sizeof(r->book.id)) && safe_id(r->book.id) &&
        memchr(r->book.title,0,sizeof(r->book.title)) && memchr(r->book.author,0,sizeof(r->book.author)) &&
        r->crc==crc32(0,(const Bytef*)&r->book,sizeof(r->book));
}
bool thingsos_mp_page(const char* root,const thingsos_weidu_session_t* s,const char* account,
    size_t start,thingsos_weidu_book_t* books,size_t cap,size_t* count,size_t* total) {
    *count=*total=0;if(!thingsos_weidu_session_valid(s))return false;
    char path[704],owner[33];if(!cache_path(root,account,path,sizeof(path)))return false;
    FILE* f=fopen(path,"rb");if(!f){char bak[710];snprintf(bak,sizeof(bak),"%s.bak",path);f=fopen(bak,"rb");}
    if(!f)return false;
    mp_header_t h;thingsos_weidu_md5(s->vid,strlen(s->vid),owner);
    bool ok=fread(&h,sizeof(h),1,f)==1 && h.magic==0x4d505731 && h.version==1 && h.count<=(account?10u:4096u) &&
        !memcmp(h.owner,owner,33) && h.crc==crc32(0,(Bytef*)&h+offsetof(mp_header_t,owner),sizeof(h)-offsetof(mp_header_t,owner));
    long bytes=ok?(long)(sizeof(h)+(size_t)h.count*sizeof(mp_row_t)):0;
    ok=ok && !fseek(f,0,SEEK_END) && ftell(f)==bytes;
    if(ok){*total=h.count;*count=start<h.count?h.count-start:0;if(*count>cap)*count=cap;}
    mp_row_t r;
    if(ok && *count)ok=!fseek(f,(long)(sizeof(h)+start*sizeof(r)),SEEK_SET);
    for(size_t i=0;ok && i<*count;++i){ok=fread(&r,sizeof(r),1,f)==1 && row_valid(&r);if(ok)books[i]=r.book;}
    fclose(f);if(!ok)*count=*total=0;return ok;
}
typedef struct {
    thingsos_weidu_book_t current,latest[10];
    unsigned count,record_depth,array_depth;
    bool in_record,seen,articles;
    unsigned records,dropped_id,dropped_title;
    const char* fallback_title;
    int error;FILE* out;thingsos_weidu_client_t* client;
} mp_parse_t;
static bool append(mp_parse_t* p) {
    thingsos_weidu_book_t* b=&p->current;
    if(!safe_id(b->id)){++p->dropped_id;return true;}
    if(p->articles){
        /* Last resort: name the row after the account rather than dropping it,
           so the list still opens the article. */
        if(!b->title[0])cp(b->title,sizeof(b->title),
                           p->fallback_title && p->fallback_title[0]?"公众号文章":"文章");
        if(!b->title[0]){++p->dropped_title;return true;}
        ++p->records;
        for(unsigned i=0;i<p->count;++i)if(!strcmp(p->latest[i].id,b->id) ||
            (b->author[0] && !strcmp(p->latest[i].author,b->author)))return true;
        unsigned pos=0;while(pos<p->count && p->latest[pos].updated>=b->updated)++pos;
        if(pos<10){unsigned end=p->count<10?p->count++:9;for(unsigned i=end;i>pos;--i)p->latest[i]=p->latest[i-1];p->latest[pos]=*b;}
    }else if(thingsos_mp_account_id(b->id)){
        if(p->count>=4096){p->error=WD_LIMIT;return false;}
        mp_row_t row={.book=*b};row.crc=crc32(0,(Bytef*)&row.book,sizeof(row.book));
        if(fwrite(&row,sizeof(row),1,p->out)!=1){p->error=WD_STORAGE;return false;}++p->count;
    }
    return true;
}
static bool mp_event(void* raw,const char* key,const char* text,char type,unsigned depth) {
    mp_parse_t* p=raw;if(stopped(p->client)){p->error=WD_CANCEL;return false;}
    if((!strcmp(key,"errcode") || !strcmp(key,"errCode")) && (type=='n' || type=='s')) {
        long e=strtol(text,NULL,10);if(e){p->error=e==-2041?WD_VERIFY:e==-2012?WD_ACCESS:WD_PROTOCOL;return false;}
    }
    /* The outermost article array defines the records; an article may nest its
       own "subReviews", whose fields belong to the same record. */
    if(type=='['){
        bool article_array=p->articles?
            (!strcmp(key,"reviews") || !strcmp(key,"subReviews") || !strcmp(key,"articles") ||
             !strcmp(key,"reviewList")):
            (!strcmp(key,"books") || !strcmp(key,"mp"));
        if(article_array){p->seen=true;if(!p->array_depth)p->array_depth=depth;}
    }
    if(type=='{' && p->array_depth && depth==p->array_depth+1){p->in_record=true;p->record_depth=depth;memset(&p->current,0,sizeof(p->current));}
    if(type=='}' && p->in_record && depth==p->record_depth){p->in_record=false;return append(p);}
    if(type==']' && depth==p->array_depth)p->array_depth=0;
    if(!p->in_record || (type!='s' && type!='n'))return true;
    thingsos_weidu_book_t* t=&p->current;
    bool article_id=p->articles && (!strcmp(key,"reviewId") || !strcmp(key,"reviewID") ||
                                    !strcmp(key,"review_id") || !strcmp(key,"id"));
    if(article_id || (!p->articles && !strcmp(key,"bookId"))){
        /* One odd id must not abort the whole list: keep the characters that are
           safe for paths and URLs, and drop only that single record if none are
           left. */
        char cleaned[sizeof(t->id)];
        size_t n=0;
        for(const char* q=text;*q && n+1<sizeof(cleaned);++q)
            if(isalnum((unsigned char)*q) || *q=='_' || *q=='-')cleaned[n++]=*q;
        cleaned[n]=0;
        if(!n)return true;
        cp(t->id,sizeof(t->id),cleaned);
    }else if(!strcmp(key,"title") || (!p->articles && !strcmp(key,"name"))){
        if(!t->title[0])cp(t->title,sizeof(t->title),text);
    }else if(!strcmp(key,"articleTitle") || !strcmp(key,"article_title")){
        if(!t->title[0])cp(t->title,sizeof(t->title),text);
    }else if(!strcmp(key,p->articles?"originalId":"author")){
        /* Optional: a long article URL is truncated, never fatal. */
        if(!t->author[0])cp(t->author,sizeof(t->author),text);
    }else if(!strcmp(key,p->articles?"createTime":"readUpdateTime")){
        /* Some payloads carry fractional seconds; take the integer part. */
        char* end;unsigned long n=strtoul(text,&end,10);
        if(end!=text && n<=UINT32_MAX && !t->updated)t->updated=(uint32_t)n;
    }
    return true;
}
thingsos_weidu_error_t thingsos_mp_refresh(thingsos_weidu_client_t* c,const thingsos_weidu_book_t* account) {
    if(stopped(c))return WD_CANCEL;
    if(!thingsos_weidu_session_valid(&c->session))return WD_AUTH;
    if(account && !thingsos_mp_account_id(account->id))return WD_PROTOCOL;
    char final[704],tmp[712],raw[704],endpoint[256];
    if(!cache_path(c->root,account?account->id:NULL,final,sizeof(final)))return WD_LIMIT;
    snprintf(tmp,sizeof(tmp),"%s.part",final);snprintf(raw,sizeof(raw),"%s/mp-response.part",c->root);
    thingsos_weidu_error_t e;
    if(account){
        /* Renewal obtains the short-lived MP ticket inside the device transport. */
        e=thingsos_weidu_renew(c);if(e!=WD_OK)return e;
        snprintf(endpoint,sizeof(endpoint),"/web/mp/articles?bookId=%s&maxIdx=0&count=10",account->id);
        e=c->io.http(c->io.context,"GET",endpoint,NULL,NULL,raw,2u*1024u*1024u,&c->session);
    }else e=c->io.http(c->io.context,"POST","/api/agent/gateway","{\"api_name\":\"/shelf/sync\",\"skill_version\":\"1.0.3\"}",NULL,raw,4u*1024u*1024u,&c->session);
    if(e!=WD_OK){remove(raw);return e;}
#ifdef ESP_PLATFORM
    /* Diagnostic for format drift: the head of the payload names the arrays and
       keys the server currently sends. */
    {
        char head[241]={0};
        FILE* h=fopen(raw,"rb");
        size_t got=h?fread(head,1,sizeof(head)-1,h):0;
        if(h)fclose(h);
        for(size_t i=0;i<got;++i)
            if((unsigned char)head[i]<32)head[i]=' ';
        head[got]=0;
        ESP_LOGI("ThingsOS_mp","account=%s payload=%u head=%s",account?account->id:"*",
            (unsigned)got,head);
    }
#endif
    FILE* out=fopen(tmp,"wb");if(!out){remove(raw);return WD_STORAGE;}
    mp_header_t h={.magic=0x4d505731,.version=1};thingsos_weidu_md5(c->session.vid,strlen(c->session.vid),h.owner);
    mp_parse_t* p=mp_alloc(sizeof(*p));if(!p){fclose(out);remove(tmp);remove(raw);return WD_MEMORY;}
    p->articles=account!=NULL;p->out=out;p->client=c;
    p->fallback_title=account?account->title:"";
    bool ok=fwrite(&h,sizeof(h),1,out)==1 && thingsos_weidu_json_events(raw,mp_event,p) && p->seen;
    if(ok && p->articles)for(unsigned i=0;ok && i<p->count;++i){mp_row_t row={.book=p->latest[i]};row.crc=crc32(0,(Bytef*)&row.book,sizeof(row.book));ok=fwrite(&row,sizeof(row),1,out)==1;}
    h.count=p->count;e=p->error?(thingsos_weidu_error_t)p->error:ok?WD_OK:WD_PROTOCOL;
#ifdef ESP_PLATFORM
    ESP_LOGI("ThingsOS_mp","parse articles=%d records=%u seen=%d error=%d ok=%d kept=%u no_id=%u no_title=%u",
        p->articles,p->count,p->seen,p->error,ok,p->records,p->dropped_id,p->dropped_title);
#endif
    free(p);remove(raw);
    h.crc=crc32(0,(Bytef*)&h+offsetof(mp_header_t,owner),sizeof(h)-offsetof(mp_header_t,owner));
    /* Count is covered separately by exact file length and bounded before use. */
    if(e==WD_OK)ok=!fseek(out,0,SEEK_SET) && fwrite(&h,sizeof(h),1,out)==1 && thingsos_weidu_flush(out);
    if(fclose(out))ok=false;
    if(stopped(c))e=WD_CANCEL;
    if(e==WD_OK && (!ok || !replace(tmp,final)))e=WD_STORAGE;
    remove(tmp);return e;
}
static bool article_path(const char* output,const thingsos_weidu_session_t* s,const thingsos_weidu_book_t* account,
    const thingsos_weidu_book_t* article,char* out,size_t cap,bool make) {
    if(!thingsos_weidu_session_valid(s) || !thingsos_mp_account_id(account->id) || !safe_id(article->id))return false;
    char owner[33],id[33],dir[704];thingsos_weidu_md5(s->vid,strlen(s->vid),owner);thingsos_weidu_md5(article->id,strlen(article->id),id);
    if(make)mp_mkdir(output);
    int n=snprintf(dir,sizeof(dir),"%s/%s-%s",output,owner,account->id);if(n<0 || (size_t)n>=sizeof(dir))return false;
    if(make)mp_mkdir(dir);
    n=snprintf(out,cap,"%s/%s.md",dir,id);return n>0 && (size_t)n<cap;
}
bool thingsos_mp_cached(const char* output,const thingsos_weidu_session_t* s,const thingsos_weidu_book_t* account,
    const thingsos_weidu_book_t* article,char* out,size_t cap) {
    if(!article_path(output,s,account,article,out,cap,false))return false;
    FILE* f=fopen(out,"rb");if(!f)return false;
    bool ok=!fseek(f,0,SEEK_END);long bytes=ok?ftell(f):-1;fclose(f);
    /* A committed Markdown article is independent of old image sidecars. */
    return ok && bytes>0 && bytes<=1024*1024;
}
static bool put_cp(FILE* out,unsigned long v) {
    if(!v || v>0x10ffff || (v>=0xd800 && v<=0xdfff))return false;
    unsigned char b[4];unsigned n=v<128?1:v<2048?2:v<65536?3:4;
    for(unsigned i=n-1;i>0;--i){b[i]=(unsigned char)(128|(v&63));v>>=6;}b[0]=(unsigned char)((n==1?0:n==2?192:n==3?224:240)|v);return fwrite(b,1,n,out)==n;
}
typedef struct {
    FILE* file;
    bool (*cancel)(void*);
    void* context;
    size_t next,used;
    bool cancelled;
    unsigned char data[4096];
    char tag[2048];
} mp_input_t;
/* Avoid the stdio lock/dispatch on every byte of multi-megabyte script blocks.
   Cancellation is checked at every bounded SD read, including inside tags. */
static int mp_get(mp_input_t* in) {
    if(in->next==in->used){
        if(in->cancel && in->cancel(in->context)){in->cancelled=true;return EOF;}
        in->used=fread(in->data,1,sizeof(in->data),in->file);in->next=0;
        if(!in->used)return EOF;
    }
    return in->data[in->next++];
}
static bool mp_until(mp_input_t* in,const char* end) {
    size_t n=0;int c;
    while((c=mp_get(in))!=EOF){
        while(n && c!=(unsigned char)end[n])--n;
        if(c==(unsigned char)end[n])++n;
        if(!end[n])return true;
    }
    return false;
}
/* HTML script/style contents are raw text, not recursively parsed markup.
   JavaScript comparisons and quoted '<' must never swallow </script>. */
static bool mp_raw_end(mp_input_t* in,const char* name) {
    char end[20];snprintf(end,sizeof(end),"</%s",name);size_t n=0;int c;
    while((c=mp_get(in))!=EOF){
        c=tolower((unsigned char)c);
        n=c==(unsigned char)end[n]?n+1:c=='<'?1:0;
        if(!end[n]){
            c=mp_get(in);
            if(c=='>')return true;
            if(c=='/' || isspace((unsigned char)c))return mp_until(in,">");
            n=c=='<'?1:0;
        }
    }
    return false;
}
static bool md_character(FILE* out,int c,bool markdown,bool code) {
    if(markdown && !code && c<128 && strchr("\\`*_{}[]()#+!>~-",c) && fputc('\\',out)==EOF)return false;
    return fputc(c,out)!=EOF;
}
static bool mp_text_body(const char* source,const char* dest,const char* title,bool (*cancel)(void*),void* ctx,bool markdown) {
    FILE* in=fopen(source,"rb");FILE* out=fopen(dest,"wb");if(!in || !out){if(in)fclose(in);if(out)fclose(out);return false;}
    mp_input_t* input=mp_alloc(sizeof(*input)+4096);if(!input){fclose(in);fclose(out);remove(dest);return false;}
    input->file=in;input->cancel=cancel;input->context=ctx;
    setvbuf(out,(char*)(input+1),_IOFBF,4096);
    bool body=false,done=false,code=false,inline_code=false,ok=true;
    if(markdown)ok=fputs("# ",out)!=EOF;
    for(const unsigned char* t=(const unsigned char*)title;ok && *t;++t)ok=md_character(out,*t,markdown,false);
    ok=ok && fputs("\n\n",out)!=EOF;unsigned divs=0,steps=0;size_t chars=0;
    int c;
    while(ok && !done && (c=mp_get(input))!=EOF){
        if(!(++steps%2048) && ((cancel && cancel(ctx)) || ftell(out)>1024*1024)){ok=false;break;}
        if(c=='<'){
            char* tag=input->tag;size_t n=0;int q=0;
            c=mp_get(input);
            if(c=='!'){
                int a=mp_get(input),b=mp_get(input);
                if(a=='-' && b=='-'){ok=mp_until(input,"-->");continue;}
                if(b==EOF){ok=false;break;}
                tag[n++]='!';tag[n++]=(char)a;
                c=b;
            }
            for(;c!=EOF;c=mp_get(input)){if(q){if(c==q)q=0;}else if(c=='\'' || c=='"')q=c;else if(c=='>')break;if(n+1<sizeof(input->tag))tag[n++]=(char)c;}
            if(c==EOF){ok=false;break;}tag[n]=0;
            char name[24];size_t k=0;const char* t=tag;while(isspace((unsigned char)*t))++t;bool close=*t=='/';if(close)++t;
            while(isalnum((unsigned char)*t) && k+1<sizeof(name))name[k++]=(char)tolower((unsigned char)*t++);
            name[k]=0;
            if(!close && (!strcmp(name,"script") || !strcmp(name,"style"))){ok=mp_raw_end(input,name);continue;}
            if(!body){if(!close && !strcmp(name,"div") &&
                (strstr(tag,"id=\"js_content\"") || strstr(tag,"id='js_content'") || strstr(tag,"rich_media_content"))){body=true;divs=1;}continue;}
            /* Article images are intentionally ignored: no anchors, URL cache,
               decode arena or download requests. Text Markdown stays intact. */
            if(!strcmp(name,"img"))continue;
            if(!strcmp(name,"div")){if(close){if(!--divs)done=true;}else ++divs;}
            if(markdown) {
                if(!strcmp(name,"pre")){ok=fputs(close?"\n```\n":"\n```\n",out)!=EOF;code=!close;}
                else if(!strcmp(name,"code")){if(!code){ok=fputc('`',out)!=EOF;inline_code=!close;}}
                else if(!code && (!strcmp(name,"strong") || !strcmp(name,"b")))ok=fputs("**",out)!=EOF;
                else if(!code && (!strcmp(name,"em") || !strcmp(name,"i")))ok=fputc('*',out)!=EOF;
                else if(!code && name[0]=='h' && name[1]>='1' && name[1]<='6' && !name[2]) {
                    ok=fputs("\n\n",out)!=EOF;
                    if(!close){for(int i=0;i<name[1]-'0';++i)ok=ok && fputc('#',out)!=EOF;ok=ok && fputc(' ',out)!=EOF;}
                } else if(!code && !strcmp(name,"li"))ok=fputs(close?"\n":"\n- ",out)!=EOF;
                else if(!code && !strcmp(name,"blockquote"))ok=fputs(close?"\n":"\n> ",out)!=EOF;
                else if(!strcmp(name,"div") || !strcmp(name,"p") || !strcmp(name,"br") || !strcmp(name,"section"))ok=fputs(code?"\n":"\n\n",out)!=EOF;
            } else if(!strcmp(name,"div") || !strcmp(name,"p") || !strcmp(name,"br") || !strcmp(name,"section") || !strcmp(name,"li") || (name[0]=='h' && isdigit((unsigned char)name[1])))ok=fputc('\n',out)!=EOF;
            continue;
        }
        if(!body)continue;
        if(c=='&'){
            char ent[32];size_t n=0;int e;
            while((e=mp_get(input))!=EOF && e!=';' && n+1<sizeof(ent) && !isspace((unsigned char)e) && e!='<')ent[n++]=(char)e;
            ent[n]=0;
            if(e==';'){
                const char* v=!strcmp(ent,"amp")?"&":!strcmp(ent,"lt")?"<":!strcmp(ent,"gt")?">":!strcmp(ent,"quot")?"\"":!strcmp(ent,"apos")?"'":!strcmp(ent,"nbsp")?" ":NULL;
                if(v)ok=md_character(out,(unsigned char)*v,markdown,code || inline_code);
                else if(ent[0]=='#'){bool hex=ent[1]=='x'||ent[1]=='X';char* end;unsigned long num=strtoul(ent+1+hex,&end,hex?16:10);ok=!*end && (num<128?md_character(out,(int)num,markdown,code || inline_code):put_cp(out,num));}
                else ok=fprintf(out,"&%s;",ent)>0;
            }else{ok=fprintf(out,"&%s",ent)>0;if(e!=EOF)--input->next;}
            ++chars;
        }else if(c>=32 || c=='\n' || c=='\t'){ok=md_character(out,c,markdown,code || inline_code);if(!isspace((unsigned char)c))++chars;}
    }
    ok=ok && body && done && chars>0 && !input->cancelled && !ferror(in) && ftell(out)<=1024*1024 && (!cancel || !cancel(ctx)) && thingsos_weidu_flush(out);
#ifdef ESP_PLATFORM
    ESP_LOGI("ThingsOS_mp","body found=%d closed=%d text_units=%u markdown=%d images=disabled ok=%d",
        body,done,(unsigned)chars,markdown,ok);
#endif
    if(fclose(in))ok=false;
    if(fclose(out))ok=false;
    free(input);
    if(!ok)remove(dest);
    return ok;
}
bool thingsos_mp_text(const char* source,const char* dest,const char* title,bool (*cancel)(void*),void* ctx) {
    return mp_text_body(source,dest,title,cancel,ctx,true);
}
thingsos_weidu_error_t thingsos_mp_download(thingsos_weidu_client_t* c,const thingsos_weidu_book_t* account,const thingsos_weidu_book_t* article,const char* output) {
    char final[768],tmp[780],raw[704],url[256];
    if(!article_path(output,&c->session,account,article,final,sizeof(final),true))return WD_PROTOCOL;
    snprintf(tmp,sizeof(tmp),"%s.part",final);snprintf(raw,sizeof(raw),"%s/mp-html.part",c->root);
    thingsos_weidu_error_t e=WD_PROTOCOL;
    for(unsigned attempt=0;attempt<2;++attempt){
        const char* id=attempt?article->author:article->id;
        if(!safe_id(id) || (attempt && !strcmp(id,article->id)))continue;
        snprintf(url,sizeof(url),"/web/mp/content?reviewId=%s",id);
        e=c->io.http(c->io.context,"GET",url,NULL,NULL,raw,6u*1024u*1024u,&c->session);
        if(e==WD_OK){
            uint64_t started=c->io.now_ms?c->io.now_ms(c->io.context):0;
            e=mp_text_body(raw,tmp,article->title,c->io.cancelled,c->io.context,true)?WD_OK:WD_PROTOCOL;
#ifdef ESP_PLATFORM
            ESP_LOGI("ThingsOS_mp","extract attempt=%u elapsed_ms=%u error=%d",attempt,
                (unsigned)(c->io.now_ms?c->io.now_ms(c->io.context)-started:0),e);
#else
            (void)started;
#endif
        }
        if(e!=WD_OK){char head[161]={0};FILE* hf=fopen(raw,"rb");size_t got=hf?fread(head,1,sizeof(head)-1,hf):0;if(hf)fclose(hf);for(size_t k=0;k<got;++k)if((unsigned char)head[k]<32)head[k]=' ';
#ifdef ESP_PLATFORM
            ESP_LOGI("ThingsOS_mp","content reject attempt=%u bytes=%u head=%s",attempt,(unsigned)got,head);
#endif
        }
        remove(raw);if(stopped(c)){e=WD_CANCEL;break;}if(e==WD_OK)break;
    }
    if(stopped(c))e=WD_CANCEL;
    if(e==WD_OK && !replace(tmp,final))e=WD_STORAGE;
    /* Keep the previous committed article if parsing, cancellation or storage fails. */
    remove(tmp);if(e==WD_OK)cp(c->status.path,sizeof(c->status.path),final);return e;
}
