/* SPDX-License-Identifier: Apache-2.0 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "thingsos/content.h"
#define CHECK(x) do {if(!(x)){fprintf(stderr,"EPUB limits line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static thingsos_epub_t* epub;
static void read_chapter(size_t index) {
    thingsos_chapter_t* chapter=calloc(1,sizeof(*chapter));CHECK(chapter);
    CHECK(thingsos_epub_chapter(epub,index,chapter) && chapter->bytes);
    thingsos_chapter_close(chapter);free(chapter);
}
static void sample_book(const char* path) {
    CHECK(thingsos_epub_open(epub,path));
    size_t original=epub->spine_count;
    CHECK(epub->chapter_capacity==original && epub->item_count==2631 && original==2512 && epub->zip.count==2577);
    thingsos_epub_navigation_fast(epub);
    CHECK(epub->chapter_capacity>=epub->spine_count && epub->spine_count>=original);
    for(size_t i=0;i<epub->spine_count;++i)read_chapter(i);
    printf("Sample opened and all %zu chapters decoded; resources=%zu ZIP=%zu\n",epub->spine_count,epub->item_count,epub->zip.count);
    thingsos_epub_close(epub);
}
int main(int argc,char** argv) {
    CHECK(argc>=2 && THINGSOS_ZIP_ENTRIES==10240 && THINGSOS_SPINE_LIMIT==10240);
    epub=calloc(1,sizeof(*epub));CHECK(epub);
    if(argc==3 && !strcmp(argv[1],"--sample")){sample_book(argv[2]);free(epub);return 0;}
    const char* good[]={"zip-limit.epub","manifest-limit.epub","spine-limit.epub","duplicate-css.epub"};
    for(unsigned i=0;i<4;++i){
        char path[768];snprintf(path,sizeof(path),"%s/%s",argv[1],good[i]);
        CHECK(thingsos_epub_open(epub,path));
        CHECK(epub->chapter_capacity==epub->spine_count);
        if(i==0)CHECK(epub->zip.count==10240);
        if(i==1)CHECK(epub->item_count==10240 && epub->spine_count==1);
        if(i==2)CHECK(epub->spine_count==10240);
        read_chapter(0);read_chapter(epub->spine_count-1);
        thingsos_epub_close(epub);CHECK(!epub->chapter_names && !epub->chapter_fragments && !epub->chapter_capacity);
    }
    const char* bad[]={"zip-over.epub","manifest-over.epub","spine-over.epub","duplicate-chapter.epub","duplicate-mixed.epub"};
    for(unsigned i=0;i<5;++i){
        char path[768];snprintf(path,sizeof(path),"%s/%s",argv[1],bad[i]);
        CHECK(!thingsos_epub_open(epub,path));
        CHECK(!epub->zip.file && !epub->items && !epub->chapter_names && !epub->chapter_fragments);
    }
    char path[768];
    snprintf(path,sizeof(path),"%s/large-toc.epub",argv[1]);
    CHECK(thingsos_epub_open(epub,path));
    read_chapter(0);
    thingsos_epub_close(epub);
    snprintf(path,sizeof(path),"%s/large-chapter.epub",argv[1]);
    CHECK(thingsos_epub_open(epub,path));
    thingsos_chapter_t* chapter=calloc(1,sizeof(*chapter));CHECK(chapter);
    CHECK(!thingsos_epub_chapter(epub,0,chapter));free(chapter);
    thingsos_epub_close(epub);
    CHECK(thingsos_epub_reserve_chapters(epub,2));strcpy(epub->chapter_names[0],"preserved");strcpy(epub->chapter_fragments[1],"section");
    CHECK(thingsos_epub_reserve_chapters(epub,4));CHECK(!strcmp(epub->chapter_names[0],"preserved") && !strcmp(epub->chapter_fragments[1],"section"));
    CHECK(!thingsos_epub_reserve_chapters(epub,10241) && !strcmp(epub->chapter_names[0],"preserved"));
    thingsos_epub_close(epub);free(epub);
    puts("10240 ZIP/manifest/spine boundaries and stylesheet compatibility passed");return 0;
}
