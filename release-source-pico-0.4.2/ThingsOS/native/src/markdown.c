// SPDX-License-Identifier: Apache-2.0
#include "thingsos/markdown.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif
#define MD_BLOCK 4096u
#define MD_MAGIC 0x4d445332u
#define MD_VERSION 1u
typedef struct {uint32_t magic,version,bytes,count;} md_header_t;
typedef struct {uint16_t flags[MD_BLOCK];char reference[1024];char io[4096];unsigned scans;} md_work_t;
static void* allocate(size_t n) {
#ifdef ESP_PLATFORM
    return heap_caps_calloc(1,n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
#else
    return calloc(1,n);
#endif
}
bool thingsos_markdown_path(const char* path) {
    const char* p=path?strrchr(path,'.'):NULL;
    if(!p)return false;
    char ext[10];size_t n=strlen(p);if(n>=sizeof(ext))return false;
    for(size_t i=0;i<=n;++i)ext[i]=(char)tolower((unsigned char)p[i]);
    return !strcmp(ext,".md") || !strcmp(ext,".markdown");
}
bool thingsos_markdown_image_path(const char* source,const char* ref,char out[256]) {
    if(!source || !ref || !ref[0] || ref[0]=='/' || strlen(ref)>200)return false;
    const char* segment=ref;
    for(const char* p=ref;;++p) {
        if(*p && ((unsigned char)*p<32 || strchr("\\:#?%\"<>|",*p)))return false;
        if(!*p || *p=='/') {
            size_t n=(size_t)(p-segment);
            if(!n || (n==1 && segment[0]=='.') || (n==2 && !strncmp(segment,"..",2)))return false;
            if(!*p)break;
            segment=p+1;
        }
    }
    const char* slash=strrchr(source,'/');if(!slash)return false;
    size_t n=(size_t)(slash-source)+1;
    if(n+strlen(ref)>=256)return false;
    memcpy(out,source,n);strcpy(out+n,ref);return true;
}
static void mark(md_work_t* w,size_t a,size_t b,unsigned flag) {
    for(size_t i=a;i<b;++i)w->flags[i]|=(uint16_t)flag;
}
static bool escaped(const char* s,size_t p) {
    size_t n=0;while(p && s[--p]=='\\')++n;return (n&1)!=0;
}
/* Intentionally bounded Markdown subset: inline markup is paired within one
   line, at most eight levels deep. Unmatched delimiters remain visible. */
static void inline_parse(md_work_t* w,const char* s,size_t a,size_t b,unsigned depth,
                         const char* path,size_t base,thingsos_chapter_t* ch) {
    if(depth==8)return;
    for(size_t i=a;i<b;) {
        if(s[i]=='\\' && i+1<b && ispunct((unsigned char)s[i+1])) {
            mark(w,i,i+1,MD_HIDE);i+=2;continue;
        }
        bool image=s[i]=='!' && i+1<b && s[i+1]=='[';
        if(image || s[i]=='[') {
            size_t label=i+1+image,close=label;
            while(close<b && (s[close]!=']' || escaped(s,close))){if(!w->scans){return;}--w->scans;++close;}
            size_t end=close+2;
            if(close+1<b && s[close+1]=='(') {
                unsigned nested=1;
                while(end<b && nested) {if(!w->scans){return;}--w->scans;if(!escaped(s,end)){if(s[end]=='(')++nested;else if(s[end]==')')--nested;}if(nested)++end;}
                size_t len=end-(close+2);
                if(end<b && len && len<sizeof(w->reference)) {
                    memcpy(w->reference,s+close+2,len);w->reference[len]=0;
                    if(image) {
                        if(ch->image_count<THINGSOS_IMAGE_LIMIT) {
                            thingsos_inline_t* img=ch->images+ch->image_count;
                            if(thingsos_markdown_image_path(path,w->reference,img->path)) {
                                img->offset=base+i;size_t alt=close-label;if(alt>=sizeof(img->alt))alt=sizeof(img->alt)-1;
                                memcpy(img->alt,s+label,alt);img->alt[alt]=0;++ch->image_count;
                                mark(w,i,end+1,MD_HIDE);i=end+1;continue;
                            }
                        }
                        /* Remote/unsafe images become readable alt text. No
                           network requests or arbitrary filesystem traversal. */
                    }
                    mark(w,i,label,MD_HIDE);mark(w,close,end+1,MD_HIDE);
                    if(!image)mark(w,label,close,MD_LINK);
                    inline_parse(w,s,label,close,depth+1,path,base,ch);i=end+1;continue;
                }
            }
        }
        if(s[i]=='`' || s[i]=='*' || s[i]=='_') {
            char token=s[i];size_t width=1;
            while(i+width<b && s[i+width]==token)++width;
            if(width>3 || (token=='_' && i>a && isalnum((unsigned char)s[i-1]))) {i+=width;continue;}
            size_t end=i+width;
            for(;end+width<=b;++end) {
                if(!w->scans){return;}--w->scans;
                if(s[end]!=token || escaped(s,end) || (end && s[end-1]==token))continue;
                size_t run=1;while(end+run<b && s[end+run]==token)++run;
                if(run>=width && run<=3){end+=run-width;break;}
            }
            if(end+width<=b && end>i+width &&
               (token=='`' || (!isspace((unsigned char)s[i+width]) && !isspace((unsigned char)s[end-1])))) {
                unsigned flag=token=='`'?MD_CODE:width==1?MD_ITALIC:width==2?MD_BOLD:MD_BOLD|MD_ITALIC;
                mark(w,i,i+width,MD_HIDE);mark(w,end,end+width,MD_HIDE);mark(w,i+width,end,flag);
                if(token!='`')inline_parse(w,s,i+width,end,depth+1,path,base,ch);
                i=end+width;continue;
            }
            i+=width;continue;
        }
        ++i;
    }
}
bool thingsos_markdown_prepare(const char* path,thingsos_chapter_t* ch) {
    if(!path || !ch || !ch->text || ch->bytes>THINGSOS_XML_BYTES || ch->markdown_index)return false;
    size_t pn=strlen(path);if(pn>700)return false;
    const char* slash=strrchr(path,'/');if(!slash)return false;
    uint64_t hash=14695981039346656037ULL;for(size_t i=0;i<pn;++i)hash=(hash^(unsigned char)path[i])*1099511628211ULL;
    char* names=allocate((pn+80)*2);md_work_t* w=allocate(sizeof(*w));
    if(!names || !w){free(names);free(w);return false;}
    char* temp=names+pn+80;snprintf(names,pn+80,"%.*s/.md-%016llx.idx",(int)(slash-path),path,(unsigned long long)hash);size_t index_len=strlen(names);memcpy(temp,names,index_len);memcpy(temp+index_len,".part",6);
    FILE* f=fopen(temp,"wb");bool ok=f!=NULL;
    md_header_t h={MD_MAGIC,MD_VERSION,(uint32_t)ch->bytes,0};
    if(ok){setvbuf(f,w->io,_IOFBF,sizeof(w->io));ok=fwrite(&h,sizeof(h),1,f)==1;}
    bool fenced=false,continuation=false;char fence=0;size_t fence_width=0;
    ch->image_count=0;ch->heading=false;
    for(size_t base=0;ok && base<ch->bytes;) {
        size_t n=0;while(n<MD_BLOCK && base+n<ch->bytes && ch->text[base+n]!='\n')++n;
        if(n<MD_BLOCK && base+n<ch->bytes)++n;
        const char* s=ch->text+base;memset(w->flags,0,n*sizeof(w->flags[0]));
        size_t first=0;while(first<n && first<3 && s[first]==' ')++first;
        size_t run=0;if(first<n && (s[first]=='`'||s[first]=='~'))while(first+run<n && s[first+run]==s[first])++run;
        bool fence_line=!continuation && run>=3 && (!fenced || (s[first]==fence && run>=fence_width));
        if(fenced && fence_line) {
            size_t tail=first+run;while(tail<n && isspace((unsigned char)s[tail]))++tail;
            fence_line=tail==n;
        }
        if(fence_line) {
            mark(w,0,n,MD_HIDE);if(!fenced){fence=s[first];fence_width=run;}fenced=!fenced;
        } else if(fenced)mark(w,0,n,MD_CODE);
        else if(!continuation && (n<MD_BLOCK || s[n-1]=='\n' || base+n==ch->bytes)) {
            size_t a=first,k=a;
            while(k<n && s[k]=='#')++k;
            if(k>a && k-a<=6 && k<n && isspace((unsigned char)s[k])) {
                mark(w,0,k+1,MD_HIDE);mark(w,k+1,n,k-a==1?MD_H1|MD_BOLD:k-a==2?MD_H2|MD_BOLD:MD_H3|MD_BOLD);a=k+1;
            } else if(a<n && s[a]=='>') {
                k=a+1;if(k<n && s[k]==' ')++k;mark(w,0,k,MD_HIDE);mark(w,k,n,MD_QUOTE);a=k;
            } else {
                k=a;while(k<n && isdigit((unsigned char)s[k]))++k;
                bool list=(a+1<n && strchr("-*+",s[a]) && s[a+1]==' ') ||
                    (k>a && k+1<n && (s[k]=='.'||s[k]==')') && s[k+1]==' ');
                if(list)mark(w,0,n,MD_LIST);
            }
            w->scans=MD_BLOCK*16;inline_parse(w,s,a,n,0,path,base,ch);
        }
        for(size_t a=0;ok && a<n;) {
            size_t end=a+1;while(end<n && w->flags[end]==w->flags[a])++end;
            thingsos_md_run_t rec={(uint32_t)(base+a),(uint32_t)(base+end),w->flags[a]};
            ok=fwrite(&rec,sizeof(rec),1,f)==1;++h.count;a=end;
        }
        continuation=n && s[n-1]!='\n' && base+n<ch->bytes;base+=n;
    }
    if(ok)ok=!fseek(f,0,SEEK_SET) && fwrite(&h,sizeof(h),1,f)==1 && !fflush(f);
    if(f && fclose(f))ok=false;
    if(ok){remove(names);ok=!rename(temp,names);}
    if(ok){ch->markdown_index=names;names=NULL;}
    else {remove(temp);ch->image_count=0;}
    free(names);free(w);return ok;
}
void thingsos_markdown_cursor_close(thingsos_md_cursor_t* c) {
    if(c){if(c->file)fclose(c->file);free(c);}
}
thingsos_md_cursor_t* thingsos_markdown_cursor(const thingsos_chapter_t* ch,size_t start) {
    if(!ch || !ch->markdown_index)return NULL;
    thingsos_md_cursor_t* c=allocate(sizeof(*c));if(!c)return NULL;
    c->file=fopen(ch->markdown_index,"rb");md_header_t h;
    if(c->file)setvbuf(c->file,NULL,_IONBF,0);
    if(!c->file || fread(&h,sizeof(h),1,c->file)!=1 || h.magic!=MD_MAGIC || h.version!=MD_VERSION ||
       h.bytes!=ch->bytes || h.count>ch->bytes || fseek(c->file,0,SEEK_END) ||
       ftell(c->file)!=(long)(sizeof(h)+h.count*sizeof(c->runs[0])))goto fail;
    c->count=h.count;c->bytes=h.bytes;
    uint32_t lo=0,hi=h.count;
    while(lo<hi) {
        uint32_t mid=lo+(hi-lo)/2;thingsos_md_run_t rec;
        if(fseek(c->file,(long)(sizeof(h)+mid*sizeof(rec)),SEEK_SET) || fread(&rec,sizeof(rec),1,c->file)!=1)goto fail;
        if(rec.end<=start)lo=mid+1;else hi=mid;
    }
    c->next=lo;
    if(fseek(c->file,(long)(sizeof(h)+lo*sizeof(c->runs[0])),SEEK_SET))goto fail;
    return c;
fail:thingsos_markdown_cursor_close(c);return NULL;
}
bool thingsos_markdown_style(thingsos_md_cursor_t* c,size_t offset,thingsos_md_run_t* out) {
    if(!c || !out || offset>=c->bytes)return false;
    for(;;) {
        if(c->at==c->used) {
            uint32_t n=c->count-c->next;if(n>128)n=128;if(!n)return false;
            if(fread(c->runs,sizeof(c->runs[0]),n,c->file)!=n)return false;
            c->next+=n;c->used=n;c->at=0;
        }
        thingsos_md_run_t* r=c->runs+c->at;
        if(r->begin>=r->end || r->end>c->bytes || (r->flags&~1023u))return false;
        if(offset>=r->end){++c->at;continue;}
        if(offset<r->begin)return false;
        *out=*r;return true;
    }
}
