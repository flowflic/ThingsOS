/* SPDX-License-Identifier: Apache-2.0 */
#include "thingsos/content.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#define make_dir(p) _mkdir(p)
#else
#include <sys/stat.h>
#define make_dir(p) mkdir(p,0775)
#endif
#define CHECK(x) do{if(!(x)){fprintf(stderr,"TXT line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void compare_book(const char* source,const char* expected,const char* index,unsigned encoding) {
    thingsos_epub_t* e=calloc(1,sizeof(*e));thingsos_chapter_t* c=calloc(1,sizeof(*c));CHECK(e&&c);
    CHECK(thingsos_txt_open(e,source,index));CHECK(e->plain_text&&e->text_encoding==encoding);
    FILE* oracle=fopen(expected,"rb");CHECK(oracle);size_t total=0,max=0,count=e->spine_count;
    for(unsigned i=0;i<e->spine_count;++i) {
        CHECK(thingsos_epub_chapter(e,i,c));CHECK(c->bytes<=3u*THINGSOS_TXT_SEGMENT);
        CHECK(c->text[c->bytes]==0);if(c->bytes>max)max=c->bytes;
        for(size_t pos=0;pos<c->bytes;) {
            unsigned char buf[4096];size_t n=c->bytes-pos;if(n>sizeof(buf))n=sizeof(buf);
            CHECK(fread(buf,1,n,oracle)==n&&!memcmp(buf,c->text+pos,n));pos+=n;
        }
        if(encoding==1)CHECK(c->source_start==e->spine[i]);
        else CHECK(!c->source_start&&e->chapter_sources[i]==i);
        total+=c->bytes;thingsos_chapter_close(c);
    }
    CHECK(fgetc(oracle)==EOF&&!ferror(oracle));fclose(oracle);
    thingsos_epub_close(e);
    CHECK(thingsos_txt_open(e,source,index)&&e->spine_count==count);
    CHECK(thingsos_epub_chapter(e,count-1,c));thingsos_chapter_close(c);thingsos_epub_close(e);
    FILE* bad=fopen(index,"wb");CHECK(bad);CHECK(fputs("interrupted disposable index",bad)>=0);fclose(bad);
    CHECK(thingsos_txt_open(e,source,index)&&e->spine_count==count);thingsos_epub_close(e);
    printf("PASS: TXT %s segments=%u decoded=%llu max_segment=%u; full oracle, reopen, corrupt-index rebuild\n",
           source,(unsigned)count,(unsigned long long)total,(unsigned)max);
    free(e);free(c);
}
int main(int argc,char** argv) {
    CHECK(argc==2||argc==6);make_dir(argv[1]);
    char source[640],expected[640],index[640];
    snprintf(source,sizeof(source),"%s/txt-boundary.txt",argv[1]);
    snprintf(expected,sizeof(expected),"%s/txt-boundary.utf8",argv[1]);
    snprintf(index,sizeof(index),"%s/txt-boundary.idx",argv[1]);
    FILE* raw=fopen(source,"wb"),*utf8=fopen(expected,"wb");CHECK(raw&&utf8);
    /* One very long line forces a multibyte boundary split, then CRLF. */
    fputc('A',raw);fputc('A',utf8);
    for(unsigned i=0;i<200000;++i){CHECK(fwrite("\xd0\xde\xd5\xe6",1,4,raw)==4);CHECK(fwrite("\xe4\xbf\xae\xe7\x9c\x9f",1,6,utf8)==6);}
    fputs("\r\nEND\r\n",raw);fputs("\r\nEND\r\n",utf8);CHECK(!fclose(raw)&&!fclose(utf8));
    compare_book(source,expected,index,2);
    snprintf(source,sizeof(source),"%s/txt-utf8.txt",argv[1]);raw=fopen(source,"wb");CHECK(raw);
    for(unsigned i=0;i<100000;++i)CHECK(fwrite("\xe4\xbf\xae\xf0\x9f\x98\x80",1,7,raw)==7);
    CHECK(!fclose(raw));compare_book(source,source,index,1);
    raw=fopen(source,"wb");CHECK(raw);CHECK(fwrite("\xef\xbb\xbf\xed\xa0\x80",1,6,raw)==6);fclose(raw);
    thingsos_epub_t* e=calloc(1,sizeof(*e));CHECK(e);CHECK(!thingsos_txt_open(e,source,index));free(e);
    if(argc==6) {
        snprintf(index,sizeof(index),"%s/xianni.idx",argv[1]);compare_book(argv[2],argv[3],index,2);
        snprintf(index,sizeof(index),"%s/xiuzhen.idx",argv[1]);compare_book(argv[4],argv[5],index,2);
    }
    puts("PASS: bounded TXT UTF-8/GBK decoding, character boundaries and no source rewriting");return 0;
}
