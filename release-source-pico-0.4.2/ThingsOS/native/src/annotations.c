/* SPDX-License-Identifier: Apache-2.0
 * Independent account-scoped reading notes. A verified snapshot is replaced
 * only after its complete successor has been written and flushed.
 */
#include "thingsos/annotations.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#include <io.h>
#define MKDIR(p) _mkdir(p)
#define SYNC(f) (_commit(_fileno(f))==0)
#else
#include <unistd.h>
#define MKDIR(p) mkdir(p,0700)
#define SYNC(f) (fsync(fileno(f))==0)
#endif
typedef struct {
    uint32_t magic,version,count,next_id;
    uint64_t book;
    char owner[33];
    uint32_t payload_crc,seal;
} note_header_t;
uint32_t thingsos_annotation_crc(const void* raw,size_t bytes) {
    const unsigned char* p=raw;uint32_t crc=0xffffffff;
    for(size_t i=0;i<bytes;++i){crc^=p[i];for(unsigned b=0;b<8;++b)crc=(crc>>1)^(0xedb88320u& (0u-(crc&1u)));}
    return ~crc;
}
static bool owner_ok(const char* s) {
    if(!s)return false;
    size_t n=strlen(s);if(n!=0 && n!=32)return false;
    for(size_t i=0;i<n;++i)if(!((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f')))return false;
    return true;
}
static void filename(const thingsos_annotations_t* s,char out[640]) {
    snprintf(out,640,"%s/%016llx-%s.notes",s->root,(unsigned long long)s->book,s->owner[0]?s->owner:"local");
}
static bool valid_note(const thingsos_annotation_t* n) {
    return n->id && n->begin<n->end && n->sync<=THINGSOS_NOTE_UNCERTAIN && n->deleted<=1 &&
        memchr(n->quote,0,sizeof(n->quote)) && n->quote[0] &&
        memchr(n->thought,0,sizeof(n->thought)) && memchr(n->uid,0,sizeof(n->uid)) &&
        memchr(n->bookmark_id,0,sizeof(n->bookmark_id)) && memchr(n->review_id,0,sizeof(n->review_id));
}
static bool read_snapshot(thingsos_annotations_t* s,const char* file) {
    FILE* f=fopen(file,"rb");if(!f)return false;
    note_header_t h;bool ok=fread(&h,sizeof(h),1,f)==1 && h.magic==0x414e4f54 && (h.version==1 || h.version==2) &&
        h.book==s->book && !memcmp(h.owner,s->owner,sizeof(h.owner)) && h.count<=THINGSOS_ANNOTATION_LIMIT && h.next_id &&
        h.seal==thingsos_annotation_crc(&h,offsetof(note_header_t,seal));
    size_t stride=ok && h.version==1?offsetof(thingsos_annotation_t,deleted):sizeof(*s->notes);
    if(ok)ok=fread(s->notes,stride,h.count,f)==h.count && fgetc(f)==EOF && !ferror(f) &&
        h.payload_crc==thingsos_annotation_crc(s->notes,h.count*stride);
    if(ok && h.version==1)for(unsigned i=h.count;i-->0;) {
        memmove(s->notes+i,(unsigned char*)s->notes+i*stride,stride);s->notes[i].deleted=0;
    }
    for(unsigned i=0;ok && i<h.count;++i) {
        ok=valid_note(&s->notes[i]) && s->notes[i].id<h.next_id;
        for(unsigned j=0;ok && j<i;++j)if(s->notes[j].id==s->notes[i].id)ok=false;
    }
    fclose(f);if(ok){s->count=h.count;s->next_id=h.next_id;}return ok;
}
bool thingsos_annotations_load(thingsos_annotations_t* s,const char* root,uint64_t book,const char* owner) {
    if(!s || !root || strlen(root)>=sizeof(s->root) || !book || !owner_ok(owner))return false;
    memset(s,0,sizeof(*s));s->book=book;s->next_id=1;strcpy(s->root,root);strcpy(s->owner,owner);
    char path[640],bak[650];filename(s,path);snprintf(bak,sizeof(bak),"%s.bak",path);
    FILE* probe=fopen(path,"rb");bool exists=probe!=NULL;if(probe)fclose(probe);
    if(!read_snapshot(s,path)) {
        probe=fopen(bak,"rb");bool backup=probe!=NULL;if(probe)fclose(probe);
        if(read_snapshot(s,bak))s->recovered=true;
        else if(exists || backup){memset(s->notes,0,sizeof(s->notes));return false;}
    }
    /* A reset while a POST was in flight is not evidence it failed. Never
       blindly replay such a write; reconcile with the server first. */
    for(unsigned i=0;i<s->count;++i)if(s->notes[i].sync==THINGSOS_NOTE_SENDING)s->notes[i].sync=THINGSOS_NOTE_UNCERTAIN;
    return true;
}
static bool save(thingsos_annotations_t* s) {
    MKDIR(s->root);
    char path[640],tmp[650],bak[650];filename(s,path);snprintf(tmp,sizeof(tmp),"%s.part",path);snprintf(bak,sizeof(bak),"%s.bak",path);
    note_header_t h={0};h.magic=0x414e4f54;h.version=2;h.book=s->book;h.count=s->count;h.next_id=s->next_id;
    memcpy(h.owner,s->owner,sizeof(h.owner));h.payload_crc=thingsos_annotation_crc(s->notes,s->count*sizeof(*s->notes));
    h.seal=thingsos_annotation_crc(&h,offsetof(note_header_t,seal));
    FILE* f=fopen(tmp,"wb");bool ok=f && fwrite(&h,sizeof(h),1,f)==1 &&
        fwrite(s->notes,sizeof(*s->notes),s->count,f)==s->count && !fflush(f) && SYNC(f);
    if(f && fclose(f))ok=false;
    if(!ok){remove(tmp);return false;}
    f=fopen(path,"rb");bool old=f!=NULL;if(f)fclose(f);
    /* When recovering, the backup is the last known good snapshot. */
    if(old && !s->recovered){remove(bak);if(rename(path,bak)){remove(tmp);return false;}}
    else if(old && remove(path)){remove(tmp);return false;}
    if(rename(tmp,path)){if(old && !s->recovered)rename(bak,path);remove(tmp);return false;}
    s->recovered=false;return true;
}
bool thingsos_annotations_put(thingsos_annotations_t* s,thingsos_annotation_t* note) {
    if(!s || !note)return false;
    unsigned at=s->count;
    for(unsigned i=0;i<s->count;++i)if(s->notes[i].id==note->id){at=i;break;}
    bool added=at==s->count;
    if(added && (note->id || s->count==THINGSOS_ANNOTATION_LIMIT || s->next_id==UINT32_MAX))return false;
    thingsos_annotation_t old=s->notes[at<THINGSOS_ANNOTATION_LIMIT?at:0];
    uint32_t id=note->id;
    if(added){note->id=s->next_id++;++s->count;}
    if(!valid_note(note)){if(added){--s->count;--s->next_id;note->id=id;}return false;}
    s->notes[at]=*note;
    if(save(s))return true;
    s->notes[at]=old;if(added){--s->count;--s->next_id;note->id=id;}return false;
}
bool thingsos_annotations_remove(thingsos_annotations_t* s,uint32_t id) {
    if(!s)return false;
    for(unsigned i=0;i<s->count;++i)if(s->notes[i].id==id) {
        thingsos_annotation_t old=s->notes[i];
        memmove(s->notes+i,s->notes+i+1,(s->count-i-1)*sizeof(*s->notes));--s->count;
        if(save(s))return true;
        memmove(s->notes+i+1,s->notes+i,(s->count-i)*sizeof(*s->notes));s->notes[i]=old;++s->count;return false;
    }
    return false;
}
