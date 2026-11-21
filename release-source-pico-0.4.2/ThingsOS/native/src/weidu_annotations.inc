/* SPDX-License-Identifier: Apache-2.0
 * Independent private thought snapshots and verified, single-attempt writes.
 * Protocol facts: official public chunk 2.8c461fc1.js and the user-referenced
 * weread.koplugin API reference. No implementation code is reused.
 */
static bool personal_path(char out[768],const char* root,const char* id,const char* owner) {
    if(!safe(id) || strlen(owner)!=32)return false;
    for(unsigned i=0;i<32;++i)if(!isxdigit((unsigned char)owner[i]))return false;
    return snprintf(out,768,"%s/book-%s/own-%s.notes",root,id,owner)<768;
}
static bool personal_header(const char* root,const thingsos_weidu_session_t* s,const char* id,
    manifest_t* m,char path[768]) {
    if(!thingsos_weidu_session_valid(s))return false;
    char owner[33];thingsos_weidu_md5(s->vid,strlen(s->vid),owner);
    if(!personal_path(path,root,id,owner))return false;
    FILE* f=fopen(path,"rb");if(!f)return false;
    bool ok=fread(m,sizeof(*m),1,f)==1 && m->magic==0x57504331 && m->version==1 &&
        m->record_size==sizeof(thingsos_wd_personal_t) && !memcmp(m->owner,owner,33) &&
        m->info.count[2]<=WD_NOTE_LIMIT && m->seal==crc32(0,(const Bytef*)m,offsetof(manifest_t,seal)) &&
        !fseek(f,0,SEEK_END) && ftell(f)==(long)(sizeof(*m)+m->info.count[2]*sizeof(thingsos_wd_personal_t));
    fclose(f);return ok;
}
bool thingsos_wd_personal_info(const char* root,const thingsos_weidu_session_t* s,const char* id,unsigned* count) {
    *count=0;manifest_t m;char path[768];if(!personal_header(root,s,id,&m,path))return false;
    *count=m.info.count[2];return true;
}
bool thingsos_wd_personal(const char* root,const thingsos_weidu_session_t* s,const char* id,unsigned index,thingsos_wd_personal_t* out) {
    manifest_t m;char path[768];if(!out || !personal_header(root,s,id,&m,path) || index>=m.info.count[2])return false;
    FILE* f=fopen(path,"rb");bool ok=f && !fseek(f,(long)(sizeof(m)+index*sizeof(*out)),SEEK_SET) && fread(out,sizeof(*out),1,f)==1;
    if(f)fclose(f);
    return ok && out->integrity==crc32(0,(const Bytef*)out,offsetof(thingsos_wd_personal_t,integrity)) &&
        memchr(out->quote,0,sizeof(out->quote)) && memchr(out->review_id,0,sizeof(out->review_id)) &&
        memchr(out->note.text,0,sizeof(out->note.text)) && memchr(out->note.uid,0,sizeof(out->note.uid));
}
typedef struct {
    manifest_t m;
    note_parser_t p;
    thingsos_wd_personal_t record;
    char final[768], tmp[800], response[768], request[320];
} personal_refresh_scratch_t;
thingsos_weidu_error_t thingsos_wd_personal_refresh(thingsos_weidu_client_t* c,const char* id) {
    if(!safe(id))return WD_PROTOCOL;
    if(!thingsos_weidu_session_valid(&c->session))return WD_AUTH;
    personal_refresh_scratch_t* sc=wd_alloc(sizeof(*sc));
    if(!sc)return WD_MEMORY;
    sc->m.magic=0x57504331;sc->m.version=1;sc->m.record_size=sizeof(thingsos_wd_personal_t);
    thingsos_weidu_md5(c->session.vid,strlen(c->session.vid),sc->m.owner);
    if(!personal_path(sc->final,c->root,id,sc->m.owner)){free(sc);return WD_PROTOCOL;}
    snprintf(sc->tmp,sizeof(sc->tmp),"%s.part",sc->final);snprintf(sc->response,sizeof(sc->response),"%s/personal-response.part",c->root);
    FILE* out=fopen(sc->tmp,"wb");if(!out){free(sc);return WD_STORAGE;}
    bool ok=fwrite(&sc->m,sizeof(sc->m),1,out)==1;
    thingsos_weidu_error_t e=ok?WD_OK:WD_STORAGE;
    char cursor[40]="0";bool more=false;unsigned pages=0;
    while(e==WD_OK) {
        if(c->io.cancelled && c->io.cancelled(c->io.context)){e=WD_CANCEL;break;}
        if(++pages>16){e=WD_LIMIT;break;}
        snprintf(sc->request,sizeof(sc->request),"{\"api_name\":\"/review/list/mine\",\"skill_version\":\"1.0.3\",\"bookid\":\"%s\",\"synckey\":%s,\"count\":100}",id,cursor);
        e=c->io.http(c->io.context,"POST","/api/agent/gateway",sc->request,NULL,sc->response,WD_RESPONSE_LIMIT,&c->session);
        if(e!=WD_OK)break;
        memset(&sc->record,0,sizeof(sc->record));
        memset(&sc->p,0,sizeof(sc->p));
        sc->p.personal=&sc->record;sc->p.note=&sc->record.note;sc->p.manifest=&sc->m;sc->p.kind=2;sc->p.out=out;sc->p.container_depth=1;
        bool parsed=thingsos_weidu_json_events(sc->response,note_event,&sc->p);
        if(!parsed || sc->p.error || !sc->p.root || !sc->p.seen || sc->p.record){e=sc->p.error?sc->p.error:WD_PROTOCOL;break;}
        more=sc->p.more;
        if(!more)break;
        if(!sc->p.sync[0] || !strcmp(cursor,sc->p.sync)){e=WD_PROTOCOL;break;}
        strcpy(cursor,sc->p.sync);
        if(sc->m.info.count[2]>=WD_NOTE_LIMIT){sc->m.info.limited[2]=true;break;}
    }
    if(e==WD_OK && c->io.cancelled && c->io.cancelled(c->io.context))e=WD_CANCEL;
    if(e==WD_OK) {
        sc->m.seal=crc32(0,(const Bytef*)&sc->m,offsetof(manifest_t,seal));
        if(fseek(out,0,SEEK_SET) || fwrite(&sc->m,sizeof(sc->m),1,out)!=1 || !thingsos_weidu_flush(out))e=WD_STORAGE;
    }
    if(fclose(out) && e==WD_OK)e=WD_STORAGE;
    if(e==WD_OK && !swap_file(sc->tmp,sc->final))e=WD_STORAGE;
    remove(sc->tmp);remove(sc->response);free(sc);return e;
}
bool thingsos_wd_personal_find(const char* root,const thingsos_weidu_session_t* s,const char* id,const char* uid,uint32_t begin,uint32_t end,thingsos_wd_personal_t* out) {
    manifest_t m;char path[768];if(!out || !uid || begin>=end || !personal_header(root,s,id,&m,path))return false;
    FILE* f=fopen(path,"rb");if(!f || fseek(f,sizeof(m),SEEK_SET)){if(f)fclose(f);return false;}
    bool found=false;
    for(unsigned i=0;i<m.info.count[2];++i) {
        if(fread(out,sizeof(*out),1,f)!=1 || out->integrity!=crc32(0,(const Bytef*)out,offsetof(thingsos_wd_personal_t,integrity)) ||
            !memchr(out->note.uid,0,sizeof(out->note.uid)) || !memchr(out->note.text,0,sizeof(out->note.text)) || !memchr(out->quote,0,sizeof(out->quote)))break;
        if(out->note.range && !strcmp(out->note.uid,uid) && out->note.begin==begin && out->note.length==end-begin){found=true;break;}
    }
    fclose(f);return found;
}
static bool annotation_json(char* out,size_t cap,const char* text) {
    size_t used=0;if(cap<3)return false;out[used++]='"';
    for(const unsigned char* p=(const unsigned char*)text;*p;++p) {
        if(used+7>=cap)return false;
        if(*p=='"' || *p=='\\'){out[used++]='\\';out[used++]=(char)*p;}
        else if(*p<32){snprintf(out+used,7,"\\u%04x",*p);used+=6;}
        else out[used++]=(char)*p;
    }
    out[used++]='"';out[used]=0;return true;
}
static bool annotation_base64(const char* text,char* out,size_t cap) {
    const char* alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t n=strlen(text),at=0;if(((n+2)/3)*4+1>cap)return false;
    for(size_t i=0;i<n;i+=3) {
        uint32_t v=(uint32_t)(unsigned char)text[i]<<16;
        if(i+1<n)v|=(uint32_t)(unsigned char)text[i+1]<<8;
        if(i+2<n)v|=(unsigned char)text[i+2];
        out[at++]=alphabet[v>>18];out[at++]=alphabet[(v>>12)&63];
        out[at++]=i+1<n?alphabet[(v>>6)&63]:'=';out[at++]=i+2<n?alphabet[v&63]:'=';
    }
    out[at]=0;return true;
}
typedef struct {
    const thingsos_annotation_t* note;
    char id[96],matched_id[96],uid[64],range[48],quote[1536];
    unsigned array_depth,record_depth,records;
    bool root,seen,in_array,record,matched;
    thingsos_weidu_error_t error;
} annotation_lookup_t;
static bool annotation_lookup_event(void* context,const char* key,const char* text,char type,unsigned depth) {
    annotation_lookup_t* p=context;
    if(!depth && type=='{')p->root=true;
    if(!strcmp(key,"errcode") || !strcmp(key,"errCode"))if(strcmp(text,"0")){p->error=!strcmp(text,"-2012")?WD_AUTH:WD_ACCESS;return false;}
    if(depth==1 && !strcmp(key,"updated") && type=='['){p->seen=p->in_array=true;p->array_depth=depth;return true;}
    if(p->in_array && type=='{' && depth==p->array_depth+1) {
        p->record=true;p->record_depth=depth;p->id[0]=p->uid[0]=p->range[0]=p->quote[0]=0;
        if(++p->records>WD_NOTE_LIMIT){p->error=WD_LIMIT;return false;}
    }
    if(p->record && type=='}' && depth==p->record_depth) {
        char want[48];snprintf(want,sizeof(want),"%u-%u",(unsigned)p->note->source_begin,(unsigned)p->note->source_end);
        if(!p->matched && !strcmp(p->uid,p->note->uid) && !strcmp(p->range,want) && !strcmp(p->quote,p->note->quote) && p->id[0]) {
            p->matched=true;strcpy(p->matched_id,p->id);
        }
        p->record=false;
    }
    if(p->record && (type=='s' || type=='n')) {
        if(!strcmp(key,"bookmarkId"))cp(p->id,sizeof(p->id),text);
        else if(!strcmp(key,"chapterUid"))cp(p->uid,sizeof(p->uid),text);
        else if(!strcmp(key,"range"))cp(p->range,sizeof(p->range),text);
        else if(!strcmp(key,"markText"))cp(p->quote,sizeof(p->quote),text);
    }
    if(type==']' && p->in_array && depth==p->array_depth)p->in_array=false;
    return true;
}
typedef struct {char path[768], body[256];} annotation_lookup_scratch_t;
static thingsos_weidu_error_t annotation_lookup(thingsos_weidu_client_t* c,const char* id,thingsos_annotation_t* n,bool* found) {
    *found=false;
    annotation_lookup_scratch_t* sc=wd_alloc(sizeof(*sc));
    if(!sc)return WD_MEMORY;
    snprintf(sc->path,sizeof(sc->path),"%s/annotation-lookup.part",c->root);
    snprintf(sc->body,sizeof(sc->body),"{\"api_name\":\"/book/bookmarklist\",\"skill_version\":\"1.0.3\",\"bookId\":\"%s\"}",id);
    thingsos_weidu_error_t e=c->io.http(c->io.context,"POST","/api/agent/gateway",sc->body,NULL,sc->path,WD_RESPONSE_LIMIT,&c->session);
    if(e==WD_OK) {
        annotation_lookup_t* p=wd_alloc(sizeof(*p));if(!p){remove(sc->path);free(sc);return WD_MEMORY;}
        memset(p,0,sizeof(*p));p->note=n;
        bool ok=thingsos_weidu_json_events(sc->path,annotation_lookup_event,p);
        if(p->error)e=p->error;
        else if(!ok || !p->root || !p->seen)e=WD_PROTOCOL;
        else if(p->matched){cp(n->bookmark_id,sizeof(n->bookmark_id),p->matched_id);*found=true;}
        free(p);
    }
    remove(sc->path);free(sc);return e;
}
typedef struct {char id[96];bool root,success;thingsos_weidu_error_t error;} annotation_ack_t;
typedef struct {char path[768];annotation_ack_t ack;} annotation_post_scratch_t;
static bool annotation_ack_event(void* context,const char* key,const char* text,char type,unsigned depth) {
    annotation_ack_t* p=context;if(!depth && type=='{')p->root=true;
    if(!strcmp(key,"errcode") || !strcmp(key,"errCode"))if(strcmp(text,"0")){p->error=!strcmp(text,"-2012")?WD_AUTH:WD_ACCESS;return false;}
    if(!strcmp(key,"reviewId") || !strcmp(key,"bookmarkId")) {
        if(type!='s' || !text[0] || strlen(text)>=sizeof(p->id))return false;
        strcpy(p->id,text);
    }
    if(!strcmp(key,"succ"))p->success=!strcmp(text,"1") || !strcmp(text,"true");
    return true;
}
static thingsos_weidu_error_t annotation_post(thingsos_weidu_client_t* c,const char* endpoint,const char* body,char id[96],bool editing) {
    if(c->io.cancelled && c->io.cancelled(c->io.context))return WD_CANCEL;
    annotation_post_scratch_t* sc=wd_alloc(sizeof(*sc));
    if(!sc)return WD_MEMORY;
    snprintf(sc->path,sizeof(sc->path),"%s/annotation-ack.part",c->root);
    /* Never retry a write here, even on DNS/network/authentication failures. */
    thingsos_weidu_error_t e=c->io.http(c->io.context,"POST",endpoint,body,NULL,sc->path,16384,&c->session);
    if(e==WD_OK) {
        bool parsed=thingsos_weidu_json_events(sc->path,annotation_ack_event,&sc->ack);
        if(sc->ack.error)e=sc->ack.error;
        else if(parsed && sc->ack.root && (sc->ack.id[0] || (editing && sc->ack.success && id[0]))) {
            if(sc->ack.id[0])strcpy(id,sc->ack.id);
        } else e=WD_UNCONFIRMED;
    }
    remove(sc->path);free(sc);return e;
}
typedef struct {manifest_t m;char path[768];thingsos_wd_personal_t p;} personal_match_scratch_t;
static thingsos_weidu_error_t annotation_personal_match(thingsos_weidu_client_t* c,const char* id,thingsos_annotation_t* n,bool* found,bool* owns) {
    *found=*owns=false;thingsos_weidu_error_t e=thingsos_wd_personal_refresh(c,id);if(e!=WD_OK)return e;
    personal_match_scratch_t* sc=wd_alloc(sizeof(*sc));if(!sc)return WD_MEMORY;
    if(!personal_header(c->root,&c->session,id,&sc->m,sc->path)){free(sc);return WD_STORAGE;}
    for(unsigned i=0;i<sc->m.info.count[2];++i) {
        if(!thingsos_wd_personal(c->root,&c->session,id,i,&sc->p)){e=WD_STORAGE;break;}
        if(strcmp(sc->p.note.uid,n->uid) || !sc->p.note.range || sc->p.note.begin!=n->source_begin ||
           sc->p.note.length!=n->source_end-n->source_begin || strcmp(sc->p.quote,n->quote))continue;
        if(n->review_id[0] && !strcmp(n->review_id,sc->p.review_id))*owns=true;
        if(n->thought[0] && !strcmp(sc->p.note.text,n->thought) && sc->p.review_id[0]) {
            cp(n->review_id,sizeof(n->review_id),sc->p.review_id);*found=true;*owns=true;break;
        }
    }
    bool limited=sc->m.info.limited[2];
    free(sc);if(e==WD_OK && limited && !*found)e=WD_LIMIT;return e;
}
typedef struct {char review[600], body[640];} remove_review_scratch_t;
static thingsos_weidu_error_t annotation_remove_review(thingsos_weidu_client_t* c,const char* id,thingsos_annotation_t* n,unsigned* state) {
    if(!n->review_id[0])return WD_OK;
    bool found=false,owns=false;
    thingsos_weidu_error_t e=annotation_personal_match(c,id,n,&found,&owns);
    if(e!=WD_OK)return e;
    if(!owns){n->review_id[0]=0;*state=THINGSOS_NOTE_SYNCED;return WD_OK;}
    if(n->sync==THINGSOS_NOTE_UNCERTAIN || n->sync==THINGSOS_NOTE_SENDING){*state=THINGSOS_NOTE_UNCERTAIN;return WD_UNCONFIRMED;}
    remove_review_scratch_t* sc=wd_alloc(sizeof(*sc));if(!sc)return WD_MEMORY;
    if(!annotation_json(sc->review,sizeof(sc->review),n->review_id)){free(sc);return WD_PROTOCOL;}
    snprintf(sc->body,sizeof(sc->body),"{\"reviewId\":%s}",sc->review);
    *state=THINGSOS_NOTE_UNCERTAIN;
    e=annotation_post(c,"/web/review/delete",sc->body,n->review_id,true);free(sc);if(e!=WD_OK)return e;
    e=annotation_personal_match(c,id,n,&found,&owns);
    if(e==WD_OK && !owns){n->review_id[0]=0;*state=THINGSOS_NOTE_SYNCED;return WD_OK;}
    return e==WD_OK?WD_UNCONFIRMED:e;
}
static bool annotation_source_current(thingsos_weidu_client_t* c,const char* id,const thingsos_annotation_t* n) {
    char* file=wd_alloc(768);
    if(!file)return false;
    bool cached=thingsos_weidu_cached(c->output,id,file,768);
    if(!cached){free(file);return false;}
    thingsos_epub_t* e=wd_alloc(sizeof(*e));thingsos_chapter_t* ch=wd_alloc(sizeof(*ch));
    if(!e || !ch){free(file);free(e);free(ch);return false;}
    bool ok=thingsos_epub_open(e,file);
    free(file);
    if(ok){thingsos_epub_navigation(e);ok=thingsos_epub_chapter(e,n->chapter,ch);}
    size_t a=n->begin,b=n->end;
    ok=ok && a<b && b<=ch->bytes && n->text_crc==crc32(0,(const Bytef*)ch->text,ch->bytes) &&
        b-a==strlen(n->quote) && !memcmp(ch->text+a,n->quote,b-a);
    thingsos_wd_map_t map={0};uint32_t begin,end;
    if(ok)ok=thingsos_wd_map(c->root,id,n->chapter,ch,&map) && thingsos_wd_source(&map,a,&begin) &&
        thingsos_wd_source(&map,b,&end) && begin==n->source_begin && end==n->source_end;
    thingsos_wd_map_free(&map);thingsos_chapter_close(ch);thingsos_epub_close(e);free(ch);free(e);return ok;
}
typedef struct {thingsos_weidu_chapter_t toc;char bookmark[600], body[768];} push_delete_scratch_t;
thingsos_weidu_error_t thingsos_wd_annotation_push(thingsos_weidu_client_t* c,const char* id,const char* owner,thingsos_annotation_t* n,unsigned* state) {
    if(!state)return WD_PROTOCOL;
    *state=THINGSOS_NOTE_FAILED;
    if(!c || !safe(id) || !n || !n->id || !n->quote[0] || !memchr(n->quote,0,sizeof(n->quote)) ||
       !memchr(n->thought,0,sizeof(n->thought)) || !memchr(n->review_id,0,sizeof(n->review_id)) ||
       !memchr(n->bookmark_id,0,sizeof(n->bookmark_id)) || !memchr(n->uid,0,sizeof(n->uid)) || !safe(n->uid) || n->source_begin>=n->source_end)return WD_PROTOCOL;
    if(!thingsos_weidu_session_valid(&c->session))return WD_AUTH;
    uint32_t numeric_uid;if(!decimal(n->uid,&numeric_uid))return WD_PROTOCOL;
    char account[33];thingsos_weidu_md5(c->session.vid,strlen(c->session.vid),account);
    if(!owner || strcmp(owner,account))return WD_AUTH;
    push_delete_scratch_t* sc=wd_alloc(sizeof(*sc));if(!sc)return WD_MEMORY;
    if(!thingsos_wd_toc(c->root,id,n->chapter,&sc->toc) || strcmp(sc->toc.uid,n->uid)){free(sc);return WD_PROTOCOL;}
    unsigned toc_index=sc->toc.index;
    if(!annotation_source_current(c,id,n)){free(sc);return WD_PROTOCOL;}
    /* An explicit local deletion keeps the review ID until private listing
       proves its absence. Lost write responses must never be replayed. */
    if(n->deleted) {
        if(n->thought[0]){free(sc);return WD_PROTOCOL;}
        if(n->sync==THINGSOS_NOTE_UNCERTAIN && !n->review_id[0] && !n->bookmark_id[0]) {
            *state=THINGSOS_NOTE_UNCERTAIN;free(sc);return WD_UNCONFIRMED;
        }
        thingsos_weidu_error_t e=annotation_remove_review(c,id,n,state);if(e!=WD_OK){free(sc);return e;}
        bool present=false;e=annotation_lookup(c,id,n,&present);if(e!=WD_OK){free(sc);return e;}
        if(present) {
            if(n->sync==THINGSOS_NOTE_UNCERTAIN || n->sync==THINGSOS_NOTE_SENDING){*state=THINGSOS_NOTE_UNCERTAIN;free(sc);return WD_UNCONFIRMED;}
            if(!annotation_json(sc->bookmark,sizeof(sc->bookmark),n->bookmark_id)){free(sc);return WD_PROTOCOL;}
            snprintf(sc->body,sizeof(sc->body),"{\"bookId\":\"%s\",\"bookmarkId\":%s}",id,sc->bookmark);
            *state=THINGSOS_NOTE_UNCERTAIN;e=annotation_post(c,"/web/book/removeBookmark",sc->body,n->bookmark_id,true);
            free(sc);sc=NULL;
            if(e!=WD_OK)return e;
            e=annotation_lookup(c,id,n,&present);if(e!=WD_OK)return e;
            if(present)return WD_UNCONFIRMED;
        }
        free(sc);
        /* Both remote removals are already verified. A failed local snapshot
           refresh is safely retryable; it must not leave an uncertain write. */
        n->bookmark_id[0]=0;*state=THINGSOS_NOTE_FAILED;
        e=thingsos_wd_notes_refresh(c,id);if(e==WD_OK)*state=THINGSOS_NOTE_SYNCED;
        return e;
    }
    free(sc);
    if(!n->thought[0] && n->review_id[0])return annotation_remove_review(c,id,n,state);
    bool bookmark=false,thought=false,owns=false;
    thingsos_weidu_error_t e=annotation_lookup(c,id,n,&bookmark);if(e!=WD_OK)return e;
    if(n->thought[0]){e=annotation_personal_match(c,id,n,&thought,&owns);if(e!=WD_OK)return e;}
    if(bookmark && (!n->thought[0] || thought)){*state=THINGSOS_NOTE_SYNCED;return WD_OK;}
    if(n->sync==THINGSOS_NOTE_UNCERTAIN || n->sync==THINGSOS_NOTE_SENDING){*state=THINGSOS_NOTE_UNCERTAIN;return WD_UNCONFIRMED;}
    char* body=wd_alloc(8192);char* quoted=wd_alloc(6148);char* encoded=wd_alloc(1400);
    if(!body || !quoted || !encoded){free(body);free(quoted);free(encoded);return WD_MEMORY;}
    if(!bookmark) {
        if(!annotation_base64(n->quote,encoded,1400)){e=WD_LIMIT;goto done;}
        int bytes=snprintf(body,8192,"{\"bookId\":\"%s\",\"chapterUid\":%s,\"chapterIdx\":%u,\"bookVersion\":0,\"type\":1,\"style\":1,\"colorStyle\":0,\"range\":\"%u-%u\",\"markText\":\"%s\"}",id,n->uid,toc_index,(unsigned)n->source_begin,(unsigned)n->source_end,encoded);
        uint32_t uid;if(!decimal(n->uid,&uid) || bytes<0 || bytes>=8192){e=WD_PROTOCOL;goto done;}
        *state=THINGSOS_NOTE_UNCERTAIN;
        e=annotation_post(c,"/web/book/addBookmark",body,n->bookmark_id,false);if(e!=WD_OK)goto done;
    }
    if(n->thought[0] && !thought) {
        if(!annotation_json(quoted,6148,n->thought)){e=WD_LIMIT;goto done;}
        int bytes;
        if(n->review_id[0]) {
            if(!owns){e=WD_UNCONFIRMED;*state=THINGSOS_NOTE_UNCERTAIN;goto done;}
            if(!annotation_json(encoded,600,n->review_id)){e=WD_PROTOCOL;goto done;}
            bytes=snprintf(body,8192,"{\"reviewId\":%s,\"content\":%s,\"isPrivate\":1}",encoded,quoted);
        } else {
            char* abstract=wd_alloc(6148);if(!abstract){e=WD_MEMORY;goto done;}
            bool ok=annotation_json(abstract,6148,n->quote);
            bytes=ok?snprintf(body,8192,"{\"type\":1,\"bookId\":\"%s\",\"chapterUid\":%s,\"bookVersion\":0,\"range\":\"%u-%u\",\"content\":%s,\"abstract\":%s,\"isPrivate\":1}",id,n->uid,(unsigned)n->source_begin,(unsigned)n->source_end,quoted,abstract):-1;
            free(abstract);
        }
        if(bytes<0 || bytes>=8192){e=WD_LIMIT;goto done;}
        *state=THINGSOS_NOTE_UNCERTAIN;
        e=annotation_post(c,n->review_id[0]?"/web/review/edit":"/web/review/add",body,n->review_id,n->review_id[0]!=0);
        if(e!=WD_OK)goto done;
    }
    e=annotation_lookup(c,id,n,&bookmark);
    if(e==WD_OK && n->thought[0])e=annotation_personal_match(c,id,n,&thought,&owns);
    if(e==WD_OK && bookmark && (!n->thought[0] || thought))*state=THINGSOS_NOTE_SYNCED;
    else {if(e==WD_OK)e=WD_UNCONFIRMED;*state=THINGSOS_NOTE_UNCERTAIN;}
done:
    if(e==WD_AUTH || e==WD_ACCESS)*state=THINGSOS_NOTE_FAILED;
    free(body);free(quoted);free(encoded);return e;
}
