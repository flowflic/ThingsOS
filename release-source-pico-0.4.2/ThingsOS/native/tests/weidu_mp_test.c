/* SPDX-License-Identifier: Apache-2.0 */
#include "thingsos/weidu_mp.h"
#include "thingsos/markdown.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <direct.h>
/* The article directory is asserted below; a silent creation failure must fail
   the test instead of only surfacing as a missing file. */
static void make_dir(const char* path){
    if(_mkdir(path) && errno!=EEXIST){fprintf(stderr,"MP test: cannot create %s\n",path);exit(1);}
}
#define CHECK(x) do{if(!(x)){fprintf(stderr,"MP test %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static unsigned mode,renewals,content_calls,image_calls;
static bool cancelled;
static bool cancel_fn(void* p){(void)p;return cancelled;}
static unsigned read_checks;
static bool cancel_read(void* p){(void)p;return ++read_checks>=3;}
static bool session_fn(void* p,thingsos_weidu_session_t* s,bool save){(void)p;(void)s;(void)save;return true;}
static void text_file(const char* name,const char* text){FILE* f=fopen(name,"wb");CHECK(f && fputs(text,f)>=0 && !fclose(f));}
static thingsos_weidu_error_t auth_fn(void* ctx,const char* m,const char* path,const char* body,char* out,size_t cap,size_t* bytes,thingsos_weidu_session_t* s){
    (void)ctx;(void)m;(void)body;(void)s;CHECK(!strcmp(path,"/web/login/renewal") && cap>11);strcpy(out,"{\"succ\":1}");*bytes=strlen(out);++renewals;return WD_OK;
}
static thingsos_weidu_error_t http_fn(void* ctx,const char* method,const char* path,const char* body,const char* ref,const char* dest,size_t limit,thingsos_weidu_session_t* s){
    (void)ctx;(void)method;(void)body;(void)ref;(void)s;CHECK(limit<=6u*1024u*1024u);
    if(mode==1)return WD_NETWORK;
    if(mode==2){text_file(dest,"{\"errCode\":-2041}");return WD_OK;}
    if(mode==3){text_file(dest,"{\"reviews\":[");return WD_OK;}
    if(mode==4){text_file(dest,"{\"reviews\":[]}");return WD_OK;}
    if(mode==5){
        /* Real MP responses can omit title entirely; retain an openable row. */
        text_file(dest,"{\"reviews\":[{\"createTime\":1787134583,\"subReviews\":[{\"reviewId\":\"MP_WXS_123_WR_missing_title\",\"review\":{\"mpInfo\":{\"originalId\":\"original_missing_title\"}}}]}]}");return WD_OK;
    }
    if(mode==6){
        text_file(dest,"{\"reviews\":[{\"createTime\":1787134583.5,\"subReviews\":[{\"review\":{\"reviewId\":\"MP_WXS_123_WR_alias\",\"mpInfo\":{\"articleTitle\":\"嵌套文章标题\",\"originalId\":\"original_alias\"}}}]}]}");return WD_OK;
    }
    if(mode==7){
        /* Image-heavy article: far more pictures than a chapter can hold. */
        ++content_calls;
        FILE* f=fopen(dest,"wb");CHECK(f);
        CHECK(fputs("<html><body><div id='js_content'>",f)>=0);
        for(unsigned i=0;i<70;++i)CHECK(fprintf(f,"<p><img src='https://mmbiz.qpic.cn/many-%u/%s?wx_fmt=jpeg'>图 %u</p>",i,i?"640":"0",i)>=0);
        CHECK(fputs("<p>末尾文字</p></div></body></html>",f)>=0 && !fclose(f));
        return WD_OK;
    }
    if(strstr(path,"/mp/content")){
        ++content_calls;
        if(strstr(path,"reviewId=MP_"))text_file(dest,"");
        else text_file(dest,"<html><head><style>BAD_STYLE</style></head><body>PAGE CHROME<div class='rich_media_content' id='js_content'><p>文章正文 &amp; &#x4e2d;&#25991;</p><img src='placeholder' data-src='https://mmbiz.qpic.cn/sample-a/640?wx_fmt=png&amp;foo=bar'><div>内层段落</div><script>BAD_SCRIPT</script><style>BAD_STYLE</style><img src='//mmbiz.qpic.cn/sample-b/640?wx_fmt=png'><img data-src='https://evil.example/file'><h2>小标题</h2><p><strong>加粗内容</strong>和<em>斜体内容</em> <code>a_b*c</code> 末尾</p></div>FOOTER</body></html>");
        return WD_OK;
    }
    if(!strcmp(path,"/api/agent/gateway")){
        CHECK(body && strstr(body,"/shelf/sync"));
        text_file(dest,"{\"books\":[{\"bookId\":\"123\",\"title\":\"普通书\"},{\"bookId\":\"MP_WXS_123\",\"title\":\"测试公众号\"}],\"mp\":[{\"bookInfo\":{\"bookId\":\"MP_WXS_456\",\"title\":\"另一个公众号\"}}]}");return WD_OK;
    }
    CHECK(strstr(path,"/web/mp/articles?bookId=MP_WXS_123&maxIdx=0&count=10"));
    /* Production review ids are ~88 characters; the parser and the record layout
       must carry them intact. */
    static const char* long_id=
        "MP_WXS_2398602260_WR11BEBQPBq09ArDajEDqFAH8a48ZNFIv6aQBbIH2I2830hCAkJCHbApO0UPCfKGnKGboGrr4Nt";
    FILE* f=fopen(dest,"wb");CHECK(f);
    /* Production shape: reviews[] wrappers (title/createTime) around a review
       object that carries the id. */
    fputs("{\"reviews\":[",f);
    for(unsigned i=0;i<15;++i)
        fprintf(f,"%s{\"createTime\":%u,\"subCount\":1,\"title\":\"文章 %u\",\"subReviews\":[{\"reviewId\":\"%s%u\",\"review\":{\"reviewId\":\"%s%u\",\"mpInfo\":{\"originalId\":\"a%u\"}}}]}",
                i?",":"",1700000000u+i,i,long_id,i,long_id,i,i);
    fputs("],\"clearAll\":0}",f);CHECK(!fclose(f));return WD_OK;
}
static thingsos_weidu_error_t image_fn(void* ctx,const char* url,const char* dest,size_t limit){
    (void)ctx;(void)url;(void)dest;(void)limit;++image_calls;return WD_NETWORK;
}
int main(int argc,char** argv){
    CHECK(argc==2);char root[512],output[512],source[600],dest[600];
    snprintf(root,sizeof(root),"%s/mp-tests",argv[1]);make_dir(root);
    snprintf(output,sizeof(output),"%s/articles",root);make_dir(output);
    thingsos_weidu_client_t* c=calloc(1,sizeof(*c));CHECK(c);strcpy(c->root,root);strcpy(c->session.vid,"100");strcpy(c->session.skey,"test-key");strcpy(c->session.rt,"test-refresh");
    c->io.http=http_fn;c->io.image=image_fn;c->io.auth=auth_fn;c->io.session=session_fn;c->io.cancelled=cancel_fn;
    CHECK(thingsos_mp_account_id("MP_WXS_123") && !thingsos_mp_account_id("MP_WXS_../x") && !thingsos_mp_account_id("123"));
    CHECK(thingsos_mp_refresh(c,NULL)==WD_OK);
    thingsos_weidu_book_t rows[10],account;size_t n,total;
    CHECK(thingsos_mp_page(root,&c->session,NULL,0,rows,10,&n,&total) && n==2 && total==2);account=rows[0];CHECK(!strcmp(account.id,"MP_WXS_123"));
    thingsos_weidu_error_t refresh_error=thingsos_mp_refresh(c,&account);
    if(refresh_error!=WD_OK || renewals!=1)fprintf(stderr,"mp refresh=%d renewals=%u\n",(int)refresh_error,renewals);
    CHECK(refresh_error==WD_OK && renewals==1);
    CHECK(thingsos_mp_page(root,&c->session,account.id,0,rows,10,&n,&total) && n==10 && total==10);
    if(strcmp(rows[0].author,"a14")||strcmp(rows[9].author,"a5"))fprintf(stderr,"rows: [0]=%s/%s u=%u [9]=%s/%s\n",rows[0].title,rows[0].author,rows[0].updated,rows[9].title,rows[9].author);
    CHECK(!strcmp(rows[0].author,"a14") && !strcmp(rows[9].author,"a5"));
    CHECK(!strcmp(rows[0].title,"文章 14") && !strcmp(rows[9].title,"文章 5"));
    for(unsigned failure=1;failure<=3;++failure){mode=failure;thingsos_weidu_error_t error=thingsos_mp_refresh(c,&account);CHECK(error==(failure==1?WD_NETWORK:failure==2?WD_VERIFY:WD_PROTOCOL));CHECK(thingsos_mp_page(root,&c->session,account.id,0,rows,10,&n,&total) && n==10);}
    mode=0;cancelled=true;CHECK(thingsos_mp_refresh(c,&account)==WD_CANCEL);cancelled=false;
    /* Images are ignored even if the image transport would fail. Minimal
       Markdown is preserved without image sidecars or decoding work. */
    CHECK(thingsos_mp_download(c,&account,&rows[0],output)==WD_OK);
    CHECK(content_calls==2 && image_calls==0);
    CHECK(thingsos_mp_cached(output,&c->session,&account,&rows[0],dest,sizeof(dest)));
    CHECK(thingsos_mp_download(c,&account,&rows[0],output)==WD_OK && content_calls==4 && image_calls==0);
    CHECK(strstr(dest,"/articles/") && !strstr(dest,"/books/"));
    char text[2048]={0};FILE* f=fopen(dest,"rb");CHECK(f);fread(text,1,sizeof(text)-1,f);fclose(f);
    CHECK(strstr(text,"文章正文 & 中文") && strstr(text,"内层段落") && strstr(text,"末尾") && !strstr(text,"BAD_") && !strstr(text,"FOOTER") && !strstr(text,"PAGE CHROME"));
    CHECK(strstr(text,"## 小标题") && strstr(text,"**加粗内容**") && strstr(text,"*斜体内容*") && strstr(text,"`a_b*c`"));
    CHECK(!strstr(text,"![") && !strstr(text,"mmbiz.qpic.cn") && !strstr(text,".img"));
    thingsos_chapter_t* chapter=calloc(1,sizeof(*chapter));CHECK(chapter);chapter->text=text;chapter->bytes=strlen(text);
    CHECK(thingsos_markdown_path(dest) && thingsos_markdown_prepare(dest,chapter) && !chapter->image_count && chapter->markdown_index);
    free(chapter->markdown_index);free(chapter);
    /* A failed refresh must leave the previously committed Markdown readable. */
    mode=1;CHECK(thingsos_mp_download(c,&account,&rows[0],output)==WD_NETWORK);mode=0;
    char retained[2048]={0};f=fopen(dest,"rb");CHECK(f);fread(retained,1,sizeof(retained)-1,f);fclose(f);CHECK(!strcmp(text,retained));
    strcpy(c->session.vid,"101");CHECK(!thingsos_mp_page(root,&c->session,account.id,0,rows,10,&n,&total));CHECK(!thingsos_mp_cached(output,&c->session,&account,&rows[0],dest,sizeof(dest)));strcpy(c->session.vid,"100");
    snprintf(source,sizeof(source),"%s/source.html",root);snprintf(dest,sizeof(dest),"%s/text.part",root);
    text_file(source,"<div id='js_content'><div>正文</div><p>未闭合");CHECK(!thingsos_mp_text(source,dest,"title",NULL,NULL));
    text_file(source,"<script>var s=\"<div id='js_content'>fake</div>\"</script>");CHECK(!thingsos_mp_text(source,dest,"title",NULL,NULL));
    /* '<' in raw script/style text is not an HTML tag. */
    text_file(source,"<script>if(a < b){let s=\"quoted \\\" value\";}</script><style>.x:before{content:'<';}</style><div id='js_content'><p>真实正文</p></div>FOOTER");
    CHECK(thingsos_mp_text(source,dest,"title",NULL,NULL));
    /* Raw closing markers and comments may cross SD block boundaries. */
    FILE* large=fopen(source,"wb");CHECK(large);
    CHECK(fputs("<script>",large)>=0);
    for(unsigned i=0;i<700000;++i)CHECK(fputs("a<b;",large)>=0);
    CHECK(fputs("</ScRiPt><style>.a{content:'<';}</STYLE><!-- <div id='js_content'>fake > </div> --><div id='js_content'><p>跨块真实正文</p><script>if(a < b){}</script><p>正文末尾</p></div>FOOTER",large)>=0 && !fclose(large));
    CHECK(thingsos_mp_text(source,dest,"title",NULL,NULL));
    memset(text,0,sizeof(text));f=fopen(dest,"rb");CHECK(f);fread(text,1,sizeof(text)-1,f);fclose(f);
    CHECK(strstr(text,"跨块真实正文") && strstr(text,"正文末尾") && !strstr(text,"fake") && !strstr(text,"FOOTER") && !strstr(text,"a<b"));
    read_checks=0;CHECK(!thingsos_mp_text(source,dest,"title",cancel_read,NULL) && read_checks==3);
    f=fopen(dest,"rb");CHECK(!f);
    mode=5;CHECK(thingsos_mp_refresh(c,&account)==WD_OK);
    CHECK(thingsos_mp_page(root,&c->session,account.id,0,rows,10,&n,&total) && n==1);
    CHECK(rows[0].title[0] && !strcmp(rows[0].id,"MP_WXS_123_WR_missing_title"));
    CHECK(rows[0].updated==1787134583 && !strcmp(rows[0].author,"original_missing_title"));
    mode=6;CHECK(thingsos_mp_refresh(c,&account)==WD_OK);
    CHECK(thingsos_mp_page(root,&c->session,account.id,0,rows,10,&n,&total) && n==1);
    CHECK(!strcmp(rows[0].title,"嵌套文章标题") && rows[0].updated==1787134583);
    CHECK(!strcmp(rows[0].author,"original_alias"));
    mode=4;CHECK(thingsos_mp_refresh(c,&account)==WD_OK);CHECK(thingsos_mp_page(root,&c->session,account.id,0,rows,10,&n,&total) && n==0);
    /* Even an image-heavy article keeps all text and makes zero image calls. */
    thingsos_weidu_book_t long_article={0};
    strcpy(long_article.id,"MP_WXS_123_WR_many_images");
    strcpy(long_article.title,"长文");
    /* Fixtures persist between runs: drop this article and its pictures so the
       call count reflects a first download rather than a warm cache. */
    char owner[33],dir[640],article_file[700],index_file[712];
    thingsos_weidu_md5(c->session.vid,strlen(c->session.vid),owner);
    snprintf(dir,sizeof(dir),"%s/%s-%s",output,owner,account.id);
    char digest[33];thingsos_weidu_md5(long_article.id,strlen(long_article.id),digest);
    snprintf(article_file,sizeof(article_file),"%s/%s.md",dir,digest);
    snprintf(index_file,sizeof(index_file),"%s.images",article_file);
    remove(article_file);remove(index_file);
    for(unsigned i=0;i<70;++i){
        char many[96],key[1100],image_file[768];snprintf(many,sizeof(many),"https://mmbiz.qpic.cn/many-%u/640?wx_fmt=jpeg",i);
        snprintf(key,sizeof(key),"v3:%s",many);
        thingsos_weidu_md5(key,strlen(key),digest);
        snprintf(image_file,sizeof(image_file),"%s/%s.img",dir,digest);remove(image_file);
    }
    mode=7;image_calls=0;
    CHECK(thingsos_mp_download(c,&account,&long_article,output)==WD_OK);
    CHECK(image_calls==0);
    CHECK(thingsos_mp_cached(output,&c->session,&account,&long_article,dest,sizeof(dest)));
    char long_text[8192]={0};f=fopen(dest,"rb");CHECK(f);fread(long_text,1,sizeof(long_text)-1,f);fclose(f);
    unsigned markers=0;for(char* p=long_text;(p=strstr(p,"!["));++p)++markers;
    CHECK(markers==0 && strstr(long_text,"末尾文字") && strstr(long_text,"图 69"));
    mode=0;
    free(c);puts("MP accounts, latest ten, snapshot retention, account isolation and streaming body checks passed");return 0;
}
