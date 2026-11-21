/* SPDX-License-Identifier: Apache-2.0 */
static bool shelf_header_read(FILE* f,const thingsos_weidu_session_t* s,shelf_header_t* h) {
    char account[33];thingsos_weidu_md5(s->vid,strlen(s->vid),account);
    return f && fread(h,sizeof(*h),1,f)==1 && !memcmp(h->magic,"WD_SHELF",8) &&
        !memcmp(h->account,account,33) && h->count<=WD_SHELF_MAX_RECORDS &&
        !fseek(f,0,SEEK_END) && ftell(f)==(long)(sizeof(*h)+(size_t)h->count*sizeof(thingsos_weidu_book_t)) &&
        !fseek(f,sizeof(*h),SEEK_SET);
}
bool thingsos_weidu_shelf_membership(const char* root,const thingsos_weidu_session_t* s,const thingsos_weidu_book_t* books,size_t count,bool* members) {
    if(!s || !thingsos_weidu_session_valid(s) || count>20 || (count && (!books || !members)))return false;
    for(size_t i=0;i<count;++i)members[i]=false;
    char file[640];if(!path(file,sizeof(file),root,"shelf.dat"))return false;
    FILE* f=fopen(file,"rb");shelf_header_t h;bool ok=shelf_header_read(f,s,&h);
    if(ok) {
        thingsos_weidu_book_t b;
        for(unsigned i=0;ok && i<h.count;++i) {
            ok=fread(&b,sizeof(b),1,f)==1 && memchr(b.id,0,sizeof(b.id));
            for(size_t j=0;ok && j<count;++j)if(!strcmp(b.id,books[j].id))members[j]=true;
        }
    }
    if(f)fclose(f);
    return ok;
}
bool thingsos_weidu_shelf_has(const char* root,const thingsos_weidu_session_t* s,const char* id) {
    if(!id_ok(id))return false;
    thingsos_weidu_book_t b={0};copy(b.id,sizeof(b.id),id);bool member=false;
    return thingsos_weidu_shelf_membership(root,s,&b,1,&member) && member;
}
/* Commit local membership only after the remote write is acknowledged. */
static bool shelf_remember(thingsos_weidu_client_t* c,const thingsos_weidu_book_t* book) {
    char final[640],tmp[650];if(!path(final,sizeof(final),c->root,"shelf.dat"))return false;
    snprintf(tmp,sizeof(tmp),"%s.add.part",final);
    FILE* in=fopen(final,"rb");shelf_header_t h={0};
    if(in && !shelf_header_read(in,&c->session,&h)){fclose(in);return false;}
    if(!in){memcpy(h.magic,"WD_SHELF",8);thingsos_weidu_md5(c->session.vid,strlen(c->session.vid),h.account);}
    if(h.count>=WD_SHELF_MAX_RECORDS){if(in)fclose(in);return false;}
    unsigned old=h.count++;FILE* out=fopen(tmp,"wb");
    bool ok=out && fwrite(&h,sizeof(h),1,out)==1;
    thingsos_weidu_book_t row;
    for(unsigned i=0;ok && i<old;++i) {
        ok=fread(&row,sizeof(row),1,in)==1 && fwrite(&row,sizeof(row),1,out)==1;
    }
    row=*book;row.updated=(uint32_t)c->io.unix_time(c->io.context);
    if(ok)ok=fwrite(&row,sizeof(row),1,out)==1 && thingsos_weidu_flush(out);
    if(in)fclose(in);
    if(out && fclose(out))ok=false;
    if(ok)ok=commit_file(tmp,final);
    if(!ok)remove(tmp);
    return ok;
}
thingsos_weidu_error_t thingsos_weidu_shelf_add(thingsos_weidu_client_t* c,const thingsos_weidu_book_t* book) {
    if(!c || !book || !id_ok(book->id))return WD_PROTOCOL;
    if(!thingsos_weidu_session_valid(&c->session))return WD_AUTH;
    if(cancelled(c))return WD_CANCEL;
    if(thingsos_weidu_shelf_has(c->root,&c->session,book->id)) {
        publish(c,WD_READY,"已在书架");return WD_OK;
    }
    char body[160];snprintf(body,sizeof(body),"{\"bookIds\":[\"%s\"]}",book->id);
    parsed_t* p=thingsos_psram_calloc(1,sizeof(*p));if(!p)return WD_MEMORY;
    p->array_depth=-1;
    thingsos_weidu_error_t e=auth_json(c,"POST","/web/shelf/add",body,p);
    if(e==WD_OK && !p->success)e=WD_PROTOCOL;
    free(p);
    if(e!=WD_OK)return e;
    /* Do not lose acknowledged membership if Cancel arrived after the reply. */
    if(!shelf_remember(c,book)) {
        publish(c,WD_ERROR,"已加入云端书架，本地同步未完成，请点同步");return WD_STORAGE;
    }
    publish(c,WD_READY,"已加入书架");return WD_OK;
}
