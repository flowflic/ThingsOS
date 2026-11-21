/* SPDX-License-Identifier: Apache-2.0 */
typedef struct {
    FILE* file;
    size_t bytes, limit;
    unsigned budget_ms;
    unsigned phase;
    thingsos_weidu_error_t error;
    thingsos_weidu_session_t* session;
    bool mp_renewal,stream,extract_reader,reader_ready;
    char* memory;
    const char* image_url;
    char* image_redirect;
    bool image_redirect_ready;
    thingsos_weidu_ps_t *ps,*pc,*token;
    void* transport;
    bool (*cancelled)(void*);
    void* cancel_context;
} transfer_t;
static bool transfer_stopped(const transfer_t* t) {
    return t->cancelled && t->cancelled(t->cancel_context);
}
/* Async body delivery runs on the request owning worker task. */
static void reader_http_data(transfer_t* t,const unsigned char* data,size_t got,int status) {
    if(t->error!=WD_OK || t->reader_ready)return;
    if(transfer_stopped(t)){t->error=WD_CANCEL;return;}
    if(got>t->limit-t->bytes){t->error=WD_LIMIT;return;}
    if(status!=200){t->bytes+=got;return;}
    if(t->extract_reader){
        if(reading_context_active){
            if(t->pc->state!=4 && !thingsos_weidu_field_feed(t->pc,"\"pclts\"",data,got)){t->pc->state=4;t->pc->value[0]=0;}
            if(t->token->state!=4 && !thingsos_weidu_field_feed(t->token,"\"token\"",data,got)){t->error=WD_PROTOCOL;return;}
            if(t->pc->state==4)strlcpy(work->reader_pc,t->pc->value,sizeof(work->reader_pc));
            if(t->token->state==4)strlcpy(work->reader_token,t->token->value,sizeof(work->reader_token));
        }
        if(t->ps->state!=4 && !thingsos_weidu_ps_feed(t->ps,data,got)){t->error=WD_PROTOCOL;return;}
        if(t->ps->state==4) {
            strcpy(t->memory,t->ps->value);
            t->reader_ready=!reading_context_active || (t->pc->state==4 && t->token->state==4);
        }
    }else if(t->memory)memcpy(t->memory+t->bytes,data,got);
    else if(fwrite(data,1,got,t->file)!=got){t->error=WD_STORAGE;return;}
    t->bytes+=got;
}
