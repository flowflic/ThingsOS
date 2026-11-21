/* SPDX-License-Identifier: Apache-2.0
 * Human-readable cache filenames, with private ID associations and legacy lookup.
 */
typedef struct {uint32_t magic;char id[64],name[192];uint32_t seal;} cache_name_t;
typedef struct {uint64_t key;char id[64];} cache_name_memo_t;
static cache_name_memo_t* cache_name_memo;
static unsigned cache_name_next;
static uint64_t cache_name_hash(const char* text) {
    uint64_t h=1469598103934665603ULL;for(;*text;++text)h=(h^(unsigned char)*text)*1099511628211ULL;return h?h:1;
}
static uint32_t cache_name_seal(const cache_name_t* n) {
    uint32_t h=2166136261u;const unsigned char* p=(const unsigned char*)n;
    for(size_t i=0;i<offsetof(cache_name_t,seal);++i)h=(h^p[i])*16777619u;
    return h;
}
static bool cache_name_valid(const char* name) {
    size_t n=strlen(name);return n>12 && n<192 && !strncmp(name,"微读-",strlen("微读-")) &&
        !strcmp(name+n-5,".epub") && !strpbrk(name,"/\\:\r\n");
}
static bool cache_name_read(const char* file,cache_name_t* n) {
    FILE* f=fopen(file,"rb");
    if(!f){char backup[800];snprintf(backup,sizeof(backup),"%s.bak",file);if(!rename(backup,file))f=fopen(file,"rb");}
    bool ok=f && fread(n,sizeof(*n),1,f)==1 && fgetc(f)==EOF;if(f)fclose(f);
    return ok && n->magic==0x57444e31 && memchr(n->id,0,sizeof(n->id)) && id_ok(n->id) &&
        memchr(n->name,0,sizeof(n->name)) && cache_name_valid(n->name) && n->seal==cache_name_seal(n);
}
static void cache_name_remember(const char* file,const char* id) {
    if(!cache_name_memo)cache_name_memo=thingsos_psram_calloc(512,sizeof(*cache_name_memo));
    if(!cache_name_memo)return;
    uint64_t key=cache_name_hash(file);
    unsigned at=512;for(unsigned i=0;i<512;++i)if(cache_name_memo[i].key==key){at=i;break;}
    if(at==512)at=cache_name_next++%512;
    cache_name_memo[at].key=key;copy(cache_name_memo[at].id,sizeof(cache_name_memo[at].id),id);
}
bool thingsos_weidu_cache_register(const char* file,const char* id) {
    if(!file || !id_ok(id) || strlen(id)>=64)return false;
    const char* base=strrchr(file,'/');if(!base || !cache_name_valid(base+1) || (size_t)(base-file)>500)return false;
    char dir[640],target[768],tmp[780];snprintf(dir,sizeof(dir),"%.*s/.weidu-names",(int)(base-file),file);wd_mkdir(dir);
    cache_name_t n={0};n.magic=0x57444e31;copy(n.id,sizeof(n.id),id);copy(n.name,sizeof(n.name),base+1);n.seal=cache_name_seal(&n);
    /* Both associations are committed before a rename: a power loss leaves
       either the old file or a fully resolvable new filename. */
    for(unsigned pass=0;pass<2;++pass) {
        if(!pass)snprintf(target,sizeof(target),"%s/n-%016llx.map",dir,(unsigned long long)cache_name_hash(n.name));
        else snprintf(target,sizeof(target),"%s/i-%s.map",dir,id);
        snprintf(tmp,sizeof(tmp),"%s.part",target);FILE* f=fopen(tmp,"wb");
        bool ok=f && fwrite(&n,sizeof(n),1,f)==1 && thingsos_weidu_flush(f);if(f && fclose(f))ok=false;
        if(!ok || !commit_file(tmp,target)){remove(tmp);return false;}
    }
    cache_name_remember(file,id);return true;
}
bool thingsos_weidu_cache_id(const char* file,char* id,size_t cap) {
    if(!file || !id || !cap)return false;
    id[0]=0;
    const char* name=strrchr(file,'/');name=name?name+1:file;
    size_t prefix=strlen("微读-"),n=strlen(name);
    if(n<=prefix+5 || strncmp(name,"微读-",prefix) || strcmp(name+n-5,".epub"))return false;
    uint64_t key=cache_name_hash(file);
    if(cache_name_memo)for(unsigned i=0;i<512;++i)if(cache_name_memo[i].key==key) {
        if(strlen(cache_name_memo[i].id)>=cap)return false;
        strcpy(id,cache_name_memo[i].id);return true;
    }
    if(name!=file && (size_t)(name-file)<510) {
        char map[768];cache_name_t record;
        snprintf(map,sizeof(map),"%.*s.weidu-names/n-%016llx.map",(int)(name-file),file,(unsigned long long)cache_name_hash(name));
        if(cache_name_read(map,&record) && !strcmp(record.name,name)) {
            if(strlen(record.id)>=cap)return false;
            strcpy(id,record.id);cache_name_remember(file,id);return true;
        }
    }
    n-=prefix+5;if(n>=cap || n>=64)return false;
    memcpy(id,name+prefix,n);id[n]=0;
    if(!id_ok(id)){id[0]=0;return false;}cache_name_remember(file,id);return true;
}
static bool cache_file_exists(const char* file) {
    FILE* f=fopen(file,"rb");if(!f)return false;fclose(f);return true;
}
bool thingsos_weidu_cache_identity(const char* file,char* out,size_t cap) {
    if(file && thingsos_weidu_stream_identity(file,out,cap))return true;
    char id[64];const char* slash=file?strrchr(file,'/'):NULL;
    if(!slash || !thingsos_weidu_cache_id(file,id,sizeof(id)))return false;
    int n=snprintf(out,cap,"%.*s/微读-%s.epub",(int)(slash-file),file,id);
    return n>0 && (size_t)n<cap;
}
static bool cache_recover_file(const char* file) {
    if(cache_file_exists(file))return true;
    char backup[768];snprintf(backup,sizeof(backup),"%s.bak",file);
    return !rename(backup,file) && cache_file_exists(file);
}
bool thingsos_weidu_cached(const char* output,const char* id,char* p,size_t cap) {
    if(!output || !id_ok(id) || strlen(id)>=64)return false;
    char map[768];cache_name_t n;snprintf(map,sizeof(map),"%s/.weidu-names/i-%s.map",output,id);
    if(cache_name_read(map,&n) && !strcmp(n.id,id)) {
        int bytes=snprintf(p,cap,"%s/%s",output,n.name);char owner[64];
        if(bytes>0 && (size_t)bytes<cap && thingsos_weidu_cache_id(p,owner,sizeof(owner)) && !strcmp(owner,id) && cache_recover_file(p))return true;
    }
    int bytes=snprintf(p,cap,"%s/微读-%s.epub",output,id);
    return bytes>0 && (size_t)bytes<cap && cache_recover_file(p);
}
bool thingsos_weidu_cache_named(const char* output,const char* id,const char* title,char* out,size_t cap) {
    if(!output || !id_ok(id) || strlen(id)>=64 || !title)return false;
    char clean[121],name[192],prior[640],owner[64];copy(clean,sizeof(clean),title);
    for(unsigned char* p=(unsigned char*)clean;*p;++p)if(*p<32 || strchr("/\\:*?\"<>|",*p))*p='_';
    size_t bytes=strlen(clean);while(bytes && (clean[bytes-1]==' ' || clean[bytes-1]=='.'))clean[--bytes]=0;
    if(!clean[0])copy(clean,sizeof(clean),"未命名书籍");
    bool old=thingsos_weidu_cached(output,id,prior,sizeof(prior));
    snprintf(name,sizeof(name),"微读-%s.epub",clean);
    for(unsigned attempt=0;attempt<2;++attempt) {
        int n=snprintf(out,cap,"%s/%s",output,name);if(n<0 || (size_t)n>=cap)return false;
        if(!cache_file_exists(out) || (thingsos_weidu_cache_id(out,owner,sizeof(owner)) && !strcmp(owner,id)))break;
        if(attempt)return false;
        copy(clean,80,title);for(unsigned char* p=(unsigned char*)clean;*p;++p)if(*p<32 || strchr("/\\:*?\"<>|",*p))*p='_';
        snprintf(name,sizeof(name),"微读-%s (%s).epub",clean,id);
    }
    if(!thingsos_weidu_cache_register(out,id))return false;
    if(old && strcmp(prior,out) && !cache_file_exists(out) && rename(prior,out)) {
        thingsos_weidu_cache_register(prior,id);return false;
    }
    return true;
}
unsigned thingsos_weidu_cache_names_migrate(const char* output) {
    if(cache_name_memo)memset(cache_name_memo,0,512*sizeof(*cache_name_memo));
    thingsos_browser_t* list=thingsos_psram_calloc(1,sizeof(*list));if(!list)return 0;
    unsigned renamed=0;
    if(thingsos_browser_scan(list,output,"/sdcard"))for(size_t i=0;i<list->count;++i) {
        const char* name=list->entries[i].name;char id[64],old[640],next[640],legacy[100];
        if(list->entries[i].directory || !thingsos_weidu_cache_id(name,id,sizeof(id)))continue;
        snprintf(legacy,sizeof(legacy),"微读-%s.epub",id);if(strcmp(legacy,name))continue;
        snprintf(old,sizeof(old),"%s/%s",output,name);
        char mapped[64];if(thingsos_weidu_cache_id(old,mapped,sizeof(mapped)) && strcmp(mapped,id))continue;
        thingsos_epub_t* e=thingsos_psram_calloc(1,sizeof(*e));if(!e)break;
        if(thingsos_epub_open(e,old)) {
            char title[160];copy(title,sizeof(title),e->title);thingsos_epub_close(e);
            if(thingsos_weidu_cache_named(output,id,title,next,sizeof(next)) && strcmp(old,next))++renamed;
        }
        free(e);
    }
    free(list);return renamed;
}
