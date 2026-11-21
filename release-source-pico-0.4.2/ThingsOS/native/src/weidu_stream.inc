/* SPDX-License-Identifier: Apache-2.0
 * A local chapter source. It contains no login/session material and performs
 * no network I/O on the reader task. All publication uses temporary files.
 */
typedef struct {char magic[8];thingsos_weidu_book_t book;char output[512];uint32_t seal;} stream_header_t;
static uint32_t stream_seal(const stream_header_t* h) {
    const unsigned char* p=(const unsigned char*)h;uint32_t n=2166136261u;
    for(size_t i=0;i<offsetof(stream_header_t,seal);++i)n=(n^p[i])*16777619u;
    return n;
}
static bool stream_header(const char* file,stream_header_t* h) {
    FILE* f=fopen(file,"rb");bool ok=f && fread(h,sizeof(*h),1,f)==1 && fgetc(f)==EOF;
    if(f)fclose(f);
    return ok && !memcmp(h->magic,"TOSWRD1",8) && h->seal==stream_seal(h) &&
        memchr(h->book.id,0,sizeof(h->book.id)) && id_ok(h->book.id) &&
        memchr(h->book.title,0,sizeof(h->book.title)) && memchr(h->book.author,0,sizeof(h->book.author)) &&
        memchr(h->output,0,sizeof(h->output)) && h->output[0];
}
static bool stream_exists(const char* file) {
    stream_header_t header;return stream_header(file,&header);
}
static unsigned stream_count(const char* dir) {
    char file[640];snprintf(file,sizeof(file),"%s/toc.dat",dir);
    FILE* f=fopen(file,"rb");long bytes=-1;
    if(f){if(!fseek(f,0,SEEK_END))bytes=ftell(f);fclose(f);}
    return bytes>0 && (size_t)bytes%sizeof(thingsos_weidu_chapter_t)==0 &&
        (size_t)bytes/sizeof(thingsos_weidu_chapter_t)<=THINGSOS_SPINE_LIMIT?
        (unsigned)((size_t)bytes/sizeof(thingsos_weidu_chapter_t)):0;
}
static bool stream_row(const char* dir,unsigned index,thingsos_weidu_chapter_t* row) {
    char file[640];snprintf(file,sizeof(file),"%s/toc.dat",dir);FILE* f=fopen(file,"rb");
    bool ok=f && !fseek(f,(long)(index*sizeof(*row)),SEEK_SET) && fread(row,sizeof(*row),1,f)==1;
    if(f)fclose(f);
    return ok && memchr(row->uid,0,sizeof(row->uid)) && id_ok(row->uid) && memchr(row->title,0,sizeof(row->title));
}
bool thingsos_weidu_cover_chapter(const char* title) {
    return !strcmp(title,"封面") || !strcmp(title,"Cover") || !strcmp(title,"cover");
}
bool thingsos_weidu_stream_needs_cover(const char* root,const char* id) {
    char file[700];
    if(!cover_path(root,id,file,sizeof(file)))return false;
    unsigned width,height;
    if(thingsos_file_image_info(file,&width,&height))return false;
    snprintf(file,sizeof(file),"%s/book-%s",root,id);
    thingsos_weidu_chapter_t row;
    return stream_row(file,0,&row) && thingsos_weidu_cover_chapter(row.title);
}
static bool stream_xml_path(const char* dir,unsigned index,const char* uid,const char* suffix,char* file,size_t cap) {
    int n=snprintf(file,cap,"%s/online-%u-%s%s.xhtml",dir,index,uid,suffix);
    return n>0 && (size_t)n<cap;
}
static bool stream_find_xml(const char* dir,unsigned index,bool partial,bool legacy,char* file,size_t cap) {
    thingsos_weidu_chapter_t row;if(!stream_row(dir,index,&row))return false;
    const char* suffixes[]={"-img","-partial",""};
    for(unsigned i=0;i<(legacy?3:partial?2:1);++i) {
        struct stat st;
        if(stream_xml_path(dir,index,row.uid,suffixes[i],file,cap) && !stat(file,&st) &&
           st.st_size>0 && st.st_size<=THINGSOS_XML_BYTES)return true;
    }
    /* Keep a usable old image pass if the FAT replacement was interrupted. */
    if(partial && stream_xml_path(dir,index,row.uid,"-partial",file,cap)) {
        size_t n=strlen(file);
        struct stat st;
        if(n+4<cap) {
            memcpy(file+n,".bak",5);
            if(!stat(file,&st) && st.st_size>0 && st.st_size<=THINGSOS_XML_BYTES)return true;
        }
    }
    return false;
}
bool thingsos_weidu_stream_image_path(const thingsos_epub_t* e,const char* name,char* file,size_t cap) {
    if(!e->weidu_online || strlen(name)!=40 || memcmp(name,"img-",4) || strcmp(name+36,".bin"))return false;
    for(unsigned i=4;i<36;++i)if(!strchr("0123456789abcdef",name[i]))return false;
    return path(file,cap,e->online_dir,name);
}
typedef struct {
    thingsos_weidu_client_t* client;
    const char* dir;
    uint64_t started,last_saved;
    unsigned attempts;
    bool retry;
    char normalized[1152],file[640],tmp[650];
} stream_images_t;
bool thingsos_weidu_image_cache_key(char* url,char* normalized,size_t normalized_cap,char name[41]) {
    if(!url || !normalized || normalized_cap<16 || !name)return false;
    if(!strncmp(url,"//",2)) {
        size_t len=strlen(url);
        if(len+7>normalized_cap)return false;
        memmove(url+6,url,len+1);memcpy(url,"https:",6);
    }
    /* Decode a query separator, reject controls, and discard the URL fragment. */
    char* out=url;
    for(const char* p=url;*p && *p!='#';++p) {
        if((unsigned char)*p<=32 || (unsigned char)*p==127 || strchr("\\\"'<> ",*p))return false;
        if(!strncmp(p,"&amp;",5) || !strncmp(p,"&#38;",5)){*out++='&';p+=4;}
        else *out++=*p;
    }
    *out=0;
    if(!thingsos_weidu_cover_url(url,normalized,normalized_cap))return false;
    char digest[33];thingsos_weidu_md5(normalized,strlen(normalized),digest);
    snprintf(name,41,"img-%s.bin",digest);
    return true;
}
static bool stream_cache_image(void* context,char* url,char name[41]) {
    stream_images_t* images=context;
    thingsos_weidu_client_t* c=images->client;
    if(!c->io.image || cancelled(c))return false;
    if(!thingsos_weidu_image_cache_key(url,images->normalized,sizeof(images->normalized),name))return false;
    if(!path(images->file,sizeof(images->file)-4,images->dir,name))return false;
    unsigned width,height;
    if(thingsos_file_image_info(images->file,&width,&height))return true;
    uint64_t at=c->io.now_ms?c->io.now_ms(c->io.context):0;
    /* Give image-heavy chapters time to make progress, while bounding a stalled
       download and the total time in this optional cache pass. */
    if(images->attempts>=THINGSOS_IMAGE_LIMIT || at-images->last_saved>=15000 || at-images->started>=90000) {
        images->retry=true;return false;
    }
    ++images->attempts;
    snprintf(images->tmp,sizeof(images->tmp),"%s.part",images->file);
    thingsos_weidu_error_t error=c->io.image(c->io.context,images->normalized,images->tmp,THINGSOS_MEMBER_BYTES);
    bool valid=error==WD_OK && thingsos_file_image_info(images->tmp,&width,&height);
    bool saved=valid && !cancelled(c) && commit_file(images->tmp,images->file);
    remove(images->tmp);
    if(saved && c->io.now_ms)images->last_saved=c->io.now_ms(c->io.context);
    if(!saved && (valid || (error!=WD_OK && error!=WD_LIMIT && error!=WD_PROTOCOL)))images->retry=true;
    return saved;
}
bool thingsos_weidu_stream_path(const char* root,const char* id,char* file,size_t cap) {
    if(!id_ok(id))return false;
    int n=snprintf(file,cap,"%s/book-%s/微读-%s.epub",root,id,id);
    return n>0 && (size_t)n<cap;
}
bool thingsos_weidu_stream_book(const char* file,thingsos_weidu_book_t* book) {
    stream_header_t h;if(!stream_header(file,&h))return false;*book=h.book;return true;
}
bool thingsos_weidu_stream_identity(const char* file,char* out,size_t cap) {
    stream_header_t h;if(!stream_header(file,&h))return false;
    int n=snprintf(out,cap,"%s/微读-%s.epub",h.output,h.book.id);return n>0 && (size_t)n<cap;
}
static bool stream_commit(thingsos_weidu_client_t* c,const thingsos_weidu_book_t* book,const char* dir,unsigned index) {
    /* Keep nested SD/normalization work off the 16 KiB network task stack. */
    typedef struct {
        char raw[640],xml[700],tmp[720],descriptor[700];
        thingsos_weidu_chapter_t row;
        stream_header_t header;
        stream_images_t images;
    } workspace_t;
    workspace_t* w=thingsos_psram_calloc(1,sizeof(*w));if(!w)return false;
    bool ok=stream_row(dir,index,&w->row) && stream_xml_path(dir,index,w->row.uid,"-img",w->xml,sizeof(w->xml));
    if(!ok){free(w);return false;}
    snprintf(w->raw,sizeof(w->raw),"%s/chapter-%u.raw",dir,index);snprintf(w->tmp,sizeof(w->tmp),"%s.part",w->xml);
    const char* reason="normalize";
    thingsos_weidu_control_t control={c->io.cancelled,c->io.context};
    w->images.client=c;w->images.dir=dir;
    w->images.started=w->images.last_saved=c->io.now_ms?c->io.now_ms(c->io.context):0;
    ok=thingsos_weidu_normalize_images(w->raw,w->tmp,w->row.title,w->row.plain,&reason,&control,stream_cache_image,&w->images) &&
       !thingsos_weidu_stopped(&control);
    if(ok && w->images.retry)ok=stream_xml_path(dir,index,w->row.uid,"-partial",w->xml,sizeof(w->xml));
    if(ok)ok=commit_file(w->tmp,w->xml);
    if(ok)ok=thingsos_weidu_stream_path(c->root,book->id,w->descriptor,sizeof(w->descriptor));
    if(ok) {
        stream_header_t* h=&w->header;memcpy(h->magic,"TOSWRD1",8);h->book=*book;copy(h->output,sizeof(h->output),c->output);h->seal=stream_seal(h);
        snprintf(w->tmp,sizeof(w->tmp),"%s.part",w->descriptor);FILE* f=fopen(w->tmp,"wb");
        ok=f && fwrite(h,sizeof(*h),1,f)==1 && thingsos_weidu_flush(f);if(f && fclose(f))ok=false;
        if(ok)ok=commit_file(w->tmp,w->descriptor);
    }
    if(!ok)remove(w->tmp);
    free(w);return ok;
}
bool thingsos_weidu_stream_ready(const char* root,const char* id,unsigned index) {
    if(!id_ok(id))return false;
    char dir[600],xml[700];snprintf(dir,sizeof(dir),"%s/book-%s",root,id);
    return stream_find_xml(dir,index,true,true,xml,sizeof(xml));
}
bool thingsos_weidu_stream_images_ready(const char* root,const char* id,unsigned index,bool accept_partial) {
    if(!id_ok(id))return false;
    char dir[600],xml[700];snprintf(dir,sizeof(dir),"%s/book-%s",root,id);
    return stream_find_xml(dir,index,accept_partial,false,xml,sizeof(xml));
}
unsigned thingsos_weidu_stream_count(const char* root,const char* id) {
    if(!id_ok(id))return 0;
    char dir[600];snprintf(dir,sizeof(dir),"%s/book-%s",root,id);return stream_count(dir);
}
bool thingsos_weidu_stream_open(thingsos_epub_t* e,const char* file) {
    stream_header_t h;if(!stream_header(file,&h))return false;
    const char* slash=strrchr(file,'/');if(!slash || (size_t)(slash-file)>=sizeof(e->online_dir))return false;
    snprintf(e->online_dir,sizeof(e->online_dir),"%.*s",(int)(slash-file),file);
    unsigned count=stream_count(e->online_dir);if(!count)return false;
    if(!thingsos_epub_reserve_chapters(e,count))return false;
    e->items=thingsos_psram_calloc(1,sizeof(*e->items));
    if(!e->items){thingsos_epub_close(e);return false;}
    char toc[640];snprintf(toc,sizeof(toc),"%s/toc.dat",e->online_dir);FILE* f=fopen(toc,"rb");
    if(!f){thingsos_epub_close(e);return false;}
    for(unsigned i=0;i<count;++i) {
        thingsos_weidu_chapter_t row;
        if(fread(&row,sizeof(row),1,f)!=1 || !memchr(row.uid,0,sizeof(row.uid)) || !id_ok(row.uid) || !memchr(row.title,0,sizeof(row.title))) {
            fclose(f);thingsos_epub_close(e);return false;
        }
        copy(e->chapter_names[i],sizeof(e->chapter_names[i]),row.title);e->chapter_sources[i]=i;
        e->chapter_levels[i]=row.level_tag==0xa5 && row.level<=15?row.level:thingsos_toc_number_level(row.title);
    }
    fclose(f);
    copy(e->title,sizeof(e->title),h.book.title);copy(e->author,sizeof(e->author),h.book.author);
    e->item_count=1;e->spine_count=count;e->weidu_online=e->weidu_cache=true;
    e->navigation_loaded=e->navigation_nested=true;e->titles_next=count;return true;
}
bool thingsos_weidu_stream_xml(const thingsos_epub_t* e,unsigned index,unsigned char** out,size_t* size) {
    char xml[700];if(!stream_find_xml(e->online_dir,index,true,true,xml,sizeof(xml)))return false;
    FILE* f=fopen(xml,"rb");long bytes=-1;
    if(f && !fseek(f,0,SEEK_END))bytes=ftell(f);
    bool ok=bytes>0 && bytes<=THINGSOS_XML_BYTES && !fseek(f,0,SEEK_SET);
    unsigned char* p=ok?thingsos_psram_alloc((size_t)bytes+1):NULL;
    ok=p && fread(p,1,(size_t)bytes,f)==(size_t)bytes;if(f)fclose(f);
    if(!ok){free(p);return false;}p[bytes]=0;*out=p;*size=(size_t)bytes;return true;
}
