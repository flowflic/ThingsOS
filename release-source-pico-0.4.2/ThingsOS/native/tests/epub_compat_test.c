// SPDX-License-Identifier: Apache-2.0
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "thingsos/content.h"

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "EPUB compatibility line %d: %s\n", __LINE__, #x); exit(1); } } while (0)

static void accepted(const char* root,const char* name,size_t items,size_t chapters) {
    char path[1024];snprintf(path,sizeof(path),"%s/%s",root,name);
    thingsos_epub_t* e=calloc(1,sizeof(*e));CHECK(e);
    printf("Open %s: expected items=%u chapters=%u\n",name,(unsigned)items,(unsigned)chapters);
    CHECK(thingsos_epub_open(e,path));
    CHECK(e->item_count==items && e->spine_count==chapters);
    thingsos_epub_navigation_fast(e);
    CHECK(e->spine_count==chapters);
    size_t probes[]={0,chapters>512?512:chapters-1,chapters-1};
    for(unsigned i=0;i<3;++i) {
        thingsos_chapter_t* chapter=calloc(1,sizeof(*chapter));CHECK(chapter);
        CHECK(thingsos_epub_chapter(e,probes[i],chapter));
        char marker[40];snprintf(marker,sizeof(marker),"EPUB_COMPAT_%04u",(unsigned)probes[i]);
        CHECK(chapter->bytes && strstr(chapter->text,marker));
        thingsos_chapter_close(chapter);free(chapter);
    }
    thingsos_epub_close(e);
    CHECK(!e->zip.file && !e->zip.entries && !e->items && !e->spine_count);
    free(e);
}

static void rejected(const char* root,const char* name) {
    char path[1024];snprintf(path,sizeof(path),"%s/%s",root,name);
    thingsos_epub_t* e=calloc(1,sizeof(*e));CHECK(e);
    CHECK(!thingsos_epub_open(e,path));
    CHECK(!e->zip.file && !e->zip.entries && !e->items && !e->spine_count);
    thingsos_epub_close(e);free(e);
}

int main(int argc,char** argv) {
    CHECK(argc==3);
    const char* root=argv[1];
    if(!strcmp(argv[2],"mime")) {
        const char* good[]={"mime-exact.epub","mime-lf.epub","mime-crlf.epub","mime-spaces.epub"};
        const char* bad[]={"mime-wrong.epub","mime-suffix.epub","mime-leading.epub","mime-nul.epub","mime-tab.epub","mime-empty.epub","mime-oversize.epub"};
        for(unsigned i=0;i<sizeof(good)/sizeof(*good);++i)accepted(root,good[i],1,1);
        for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);++i)rejected(root,bad[i]);
    } else if(!strcmp(argv[2],"compilation")) {
        accepted(root,"compilation-576-572.epub",576,572);
        accepted(root,"compilation-576-572-crlf.epub",576,572);
    } else if(!strcmp(argv[2],"bounds")) {
        accepted(root,"capacity-512.epub",512,512);
        accepted(root,"capacity-1024.epub",1024,1024);
        rejected(root,"capacity-manifest-over.epub");
        rejected(root,"capacity-spine-over.epub");
    } else CHECK(0);
    printf("PASS EPUB %s; epub state=%u bytes, manifest capacity=%u bytes\n",argv[2],
        (unsigned)sizeof(thingsos_epub_t),(unsigned)(THINGSOS_SPINE_LIMIT*sizeof(thingsos_epub_item_t)));
    return 0;
}
