/* SPDX-License-Identifier: Apache-2.0
 * Bounded, account-scoped progress outbox. Duration is deliberately excluded. */
#define WD_PROGRESS_QUEUE_LIMIT 8
typedef struct {uint64_t generation;thingsos_wd_upload_t position;} wd_queue_entry_t;
typedef struct {
    uint32_t magic,count;
    uint64_t generation;
    char owner[33];
    wd_queue_entry_t entries[WD_PROGRESS_QUEUE_LIMIT];
    uint32_t crc;
} wd_queue_t;
static bool queue_path(const char* root,const char* owner,char path[768]) {
    if(!root || !owner || strlen(owner)!=32)return false;
    for(unsigned i=0;i<32;++i)if(!isxdigit((unsigned char)owner[i]))return false;
    return snprintf(path,768,"%s/progress-%s.queue",root,owner)<768;
}
static bool queue_read_file(const char* path,const char* owner,wd_queue_t* q) {
    FILE* f=fopen(path,"rb");if(!f)return false;
    bool ok=fread(q,sizeof(*q),1,f)==1 && fgetc(f)==EOF && !ferror(f);fclose(f);
    if(!ok || q->magic!=0x57515131 || q->count>WD_PROGRESS_QUEUE_LIMIT || memcmp(q->owner,owner,33) ||
       q->crc!=crc32(0,(const Bytef*)q,offsetof(wd_queue_t,crc)))return false;
    for(unsigned i=0;i<q->count;++i) {
        thingsos_wd_upload_t* in=&q->entries[i].position;
        if(!q->entries[i].generation || q->entries[i].generation>q->generation ||
           !memchr(in->id,0,sizeof(in->id)) || !safe(in->id) || in->percent>100 ||
           in->chapter>=THINGSOS_SPINE_LIMIT || in->seconds || in->report)return false;
        for(unsigned j=0;j<i;++j)if(!strcmp(q->entries[j].position.id,in->id))return false;
    }
    return true;
}
static bool queue_read(const char* path,const char* owner,wd_queue_t* q) {
    if(queue_read_file(path,owner,q))return true;
    char backup[800];snprintf(backup,sizeof(backup),"%s.bak",path);
    if(queue_read_file(backup,owner,q))return true;
    /* A corrupt existing outbox must never be silently overwritten. */
    FILE* f=fopen(path,"rb");if(f){fclose(f);return false;}
    f=fopen(backup,"rb");if(f){fclose(f);return false;}
    memset(q,0,sizeof(*q));q->magic=0x57515131;memcpy(q->owner,owner,33);return true;
}
static bool queue_write(const char* path,wd_queue_t* q) {
    q->crc=crc32(0,(const Bytef*)q,offsetof(wd_queue_t,crc));
    char tmp[800];snprintf(tmp,sizeof(tmp),"%s.part",path);FILE* f=fopen(tmp,"wb");
    bool ok=f && fwrite(q,sizeof(*q),1,f)==1 && thingsos_weidu_flush(f);
    if(f && fclose(f))ok=false;
    if(ok)ok=swap_file(tmp,path);
    remove(tmp);
    return ok;
}
bool thingsos_wd_queue_put(const char* root,const char* owner,const thingsos_wd_upload_t* in,uint64_t* generation) {
    char path[768];if(!in || !queue_path(root,owner,path) || !memchr(in->id,0,sizeof(in->id)) ||
        !safe(in->id) || in->percent>100 || in->chapter>=THINGSOS_SPINE_LIMIT)return false;
    wd_queue_t* q=wd_alloc(sizeof(*q));if(!q)return false;
    bool ok=queue_read(path,owner,q);unsigned i=0;
    if(ok) {
        for(;i<q->count;++i)if(!strcmp(q->entries[i].position.id,in->id))break;
        ok=i<WD_PROGRESS_QUEUE_LIMIT && q->generation!=UINT64_MAX;
        if(ok) {
            if(i==q->count)++q->count;
            q->entries[i].position=*in;q->entries[i].position.seconds=0;q->entries[i].position.report=false;
            q->entries[i].generation=++q->generation;*generation=q->generation;
            ok=queue_write(path,q);
        }
    }
    free(q);return ok;
}
bool thingsos_wd_queue_peek(const char* root,const char* owner,thingsos_wd_upload_t* out,uint64_t* generation) {
    char path[768];if(!queue_path(root,owner,path))return false;
    wd_queue_t* q=wd_alloc(sizeof(*q));if(!q)return false;
    bool ok=queue_read(path,owner,q) && q->count;
    if(ok){*out=q->entries[0].position;*generation=q->entries[0].generation;}
    free(q);return ok;
}
bool thingsos_wd_queue_ack(const char* root,const char* owner,const char* id,uint64_t generation) {
    char path[768];if(!queue_path(root,owner,path))return false;
    wd_queue_t* q=wd_alloc(sizeof(*q));if(!q)return false;
    bool ok=queue_read(path,owner,q);
    if(ok)for(unsigned i=0;i<q->count;++i)if(q->entries[i].generation==generation && !strcmp(q->entries[i].position.id,id)) {
        memmove(q->entries+i,q->entries+i+1,(q->count-i-1)*sizeof(q->entries[0]));
        memset(q->entries+--q->count,0,sizeof(q->entries[0]));ok=queue_write(path,q);break;
    }
    free(q);return ok;
}
#define WD_SYNC_STATE_MAGIC 0x57535931u
typedef struct {
    uint32_t magic, version;
    char owner[33], id[64];
    thingsos_wd_sync_t sync;
    uint32_t crc;
} wd_sync_record_t;
static bool sync_path(const char* root, const char* owner, const char* id, char path[768]) {
    if (!root || !owner || strlen(owner) != 32 || !safe(id)) return false;
    for (unsigned i = 0; i < 32; ++i)
        if (!isxdigit((unsigned char)owner[i])) return false;
    return snprintf(path, 768, "%s/book-%s/sync-%s.dat", root, id, owner) < 768;
}
static bool sync_read_file(
    const char* path, const char* owner, const char* id, thingsos_wd_sync_t* out) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    wd_sync_record_t r;
    bool ok = fread(&r, sizeof(r), 1, f) == 1 && fgetc(f) == EOF && !ferror(f);
    fclose(f);
    if (!ok || r.magic != WD_SYNC_STATE_MAGIC || r.version != 1 || memcmp(r.owner, owner, 33) ||
        !memchr(r.id, 0, sizeof(r.id)) || strcmp(r.id, id) ||
        !memchr(r.sync.uploaded_uid, 0, sizeof(r.sync.uploaded_uid)) ||
        !memchr(r.sync.conflict_cloud.uid, 0, sizeof(r.sync.conflict_cloud.uid)) ||
        r.crc != crc32(0, (const Bytef*)&r, (uInt)offsetof(wd_sync_record_t, crc)))
        return false;
    *out = r.sync;
    return true;
}
bool thingsos_wd_sync_load(
    const char* root, const char* owner, const char* id, thingsos_wd_sync_t* out) {
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    char path[768], bak[800];
    if (!sync_path(root, owner, id, path)) return false;
    if (sync_read_file(path, owner, id, out)) return true;
    snprintf(bak, sizeof(bak), "%s.bak", path);
    return sync_read_file(bak, owner, id, out);
}
bool thingsos_wd_sync_save(
    const char* root, const char* owner, const char* id, const thingsos_wd_sync_t* in) {
    char final[768], tmp[800], dir[768];
    if (!in || !sync_path(root, owner, id, final)) return false;
    snprintf(dir, sizeof(dir), "%s", root);
    mkdir_one(dir);
    snprintf(dir, sizeof(dir), "%s/book-%s", root, id);
    mkdir_one(dir);
    wd_sync_record_t* r = wd_alloc(sizeof(*r));
    if (!r) return false;
    r->magic = WD_SYNC_STATE_MAGIC;
    r->version = 1;
    memcpy(r->owner, owner, 33);
    cp(r->id, sizeof(r->id), id);
    r->sync = *in;
    r->crc = crc32(0, (const Bytef*)r, (uInt)offsetof(wd_sync_record_t, crc));
    snprintf(tmp, sizeof(tmp), "%s.part", final);
    FILE* f = fopen(tmp, "wb");
    bool ok = f && fwrite(r, sizeof(*r), 1, f) == 1 && thingsos_weidu_flush(f);
    if (f && fclose(f)) ok = false;
    thingsos_wd_sync_t verify;
    if (ok) ok = sync_read_file(tmp, owner, id, &verify) && swap_file(tmp, final);
    remove(tmp);
    free(r);
    return ok;
}
thingsos_wd_sync_relation_t thingsos_wd_progress_compare(
    const thingsos_wd_progress_t* cloud, const char* local_uid, uint32_t local_offset,
    bool local_mapped, uint32_t local_updated, const thingsos_wd_sync_t* sync) {
    if (!cloud || !cloud->valid || !cloud->updated) return WD_SYNC_NONE;
    if (local_mapped && local_uid && !strcmp(cloud->uid, local_uid) &&
        cloud->offset == local_offset)
        return WD_SYNC_SAME;
    if (sync && sync->uploaded_uid[0] && !strcmp(cloud->uid, sync->uploaded_uid) &&
        cloud->offset == sync->uploaded_offset && !local_mapped &&
        cloud->updated <= sync->uploaded_updated + 60)
        return WD_SYNC_SAME;
    if (sync && cloud->updated <= sync->dismissed_updated) return WD_SYNC_DISMISSED;
    uint32_t latest_local = local_updated;
    if (sync && sync->local_updated > latest_local) latest_local = sync->local_updated;
    return cloud->updated > latest_local ? WD_SYNC_CLOUD_NEWER : WD_SYNC_LOCAL_NEWER;
}
