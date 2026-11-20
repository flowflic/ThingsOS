/* SPDX-License-Identifier: Apache-2.0 */
#include "thingsos/annotations.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"annotations line %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(void) {
    const char* root="annotation-store-test";
    const char* file="annotation-store-test/000000000000007b-local.notes";
    const char* bak="annotation-store-test/000000000000007b-local.notes.bak";
    remove(file);remove(bak);
    thingsos_annotations_t* s=calloc(1,sizeof(*s));thingsos_annotations_t* t=calloc(1,sizeof(*t));CHECK(s&&t);
    CHECK(thingsos_annotation_crc("123456789",9)==0xcbf43926);
    CHECK(thingsos_annotations_load(s,root,123,"") && !s->count);
    thingsos_annotation_t n={0};n.begin=2;n.end=8;strcpy(n.quote,"sample");strcpy(n.thought,"first");
    CHECK(thingsos_annotations_put(s,&n) && n.id==1 && s->count==1);
    CHECK(thingsos_annotations_load(t,root,123,"") && t->count==1 && !strcmp(t->notes[0].thought,"first"));
    strcpy(n.thought,"edited");n.sync=THINGSOS_NOTE_SENDING;
    CHECK(thingsos_annotations_put(s,&n));
    CHECK(thingsos_annotations_load(t,root,123,"") && t->notes[0].sync==THINGSOS_NOTE_UNCERTAIN);
    CHECK(thingsos_annotations_load(t,root,123,"0123456789abcdef0123456789abcdef") && !t->count);
    CHECK(thingsos_annotations_load(t,root,124,"") && !t->count);
    CHECK(!thingsos_annotations_load(t,root,123,"invalid-account"));
    /* A corrupt latest snapshot recovers the previous verified payload. */
    FILE* f=fopen(file,"wb");CHECK(f);fputs("broken",f);fclose(f);
    CHECK(thingsos_annotations_load(t,root,123,"") && t->recovered && !strcmp(t->notes[0].thought,"first"));
    n=t->notes[0];strcpy(n.thought,"recovered");CHECK(thingsos_annotations_put(t,&n));
    CHECK(thingsos_annotations_load(s,root,123,"") && !s->recovered && !strcmp(s->notes[0].thought,"recovered"));
    /* A failed snapshot replacement rolls back both caller ID and RAM. */
    char original_root[512];strcpy(original_root,s->root);strcpy(s->root,"missing-parent-for-notes/child");
    thingsos_annotation_t second=n;second.id=0;CHECK(!thingsos_annotations_put(s,&second) && !second.id && s->count==1 && s->next_id==2);
    CHECK(!thingsos_annotations_remove(s,n.id) && s->count==1 && s->notes[0].id==n.id);
    strcpy(s->root,original_root);
    CHECK(thingsos_annotations_remove(s,n.id) && !s->count);
    CHECK(thingsos_annotations_load(t,root,123,"") && !t->count);
    n.id=0;n.end=n.begin;CHECK(!thingsos_annotations_put(t,&n) && !n.id && !t->count);
    /* A real packed v1 payload migrates without changing either note. */
    typedef struct {uint32_t magic,version,count,next_id;uint64_t book;char owner[33];uint32_t payload_crc,seal;} old_header_t;
    old_header_t h={0};h.magic=0x414e4f54;h.version=1;h.count=2;h.next_id=3;h.book=123;
    unsigned char payload[2*offsetof(thingsos_annotation_t,deleted)];
    thingsos_annotation_t legacy={0};legacy.id=1;legacy.begin=2;legacy.end=8;strcpy(legacy.quote,"sample");strcpy(legacy.thought,"legacy one");
    memcpy(payload,&legacy,sizeof(payload)/2);legacy.id=2;strcpy(legacy.thought,"legacy two");
    memcpy(payload+sizeof(payload)/2,&legacy,sizeof(payload)/2);
    h.payload_crc=thingsos_annotation_crc(payload,sizeof(payload));h.seal=thingsos_annotation_crc(&h,offsetof(old_header_t,seal));
    f=fopen(file,"wb");CHECK(f && fwrite(&h,sizeof(h),1,f)==1 && fwrite(payload,sizeof(payload),1,f)==1 && !fclose(f));
    CHECK(thingsos_annotations_load(t,root,123,"") && t->count==2 && !t->notes[0].deleted && !t->notes[1].deleted);
    CHECK(!strcmp(t->notes[0].thought,"legacy one") && !strcmp(t->notes[1].thought,"legacy two"));
    legacy=t->notes[1];legacy.deleted=1;CHECK(thingsos_annotations_put(t,&legacy));
    CHECK(thingsos_annotations_load(s,root,123,"") && s->count==2 && s->notes[1].deleted==1);
    remove(file);remove(bak);free(s);free(t);puts("annotation migration, deletion persistence, recovery, isolation and rollback passed");return 0;
}
