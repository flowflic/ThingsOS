// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define THINGSOS_IMA_MAX_BYTES (200u*1024u*1024u)
typedef enum { IMA_IDLE,IMA_CHECKING,IMA_DUPLICATE,IMA_TRANSFERRING,IMA_ADDING,IMA_UPLOADED,IMA_FAILED,IMA_UNCERTAIN,IMA_CANCELLED } thingsos_ima_phase_t;
typedef struct {
    thingsos_ima_phase_t phase;
    uint64_t bytes,modified;
    unsigned percent;
    char path[512],name[256],kb[160],kb_name[192],media[160],message[192];
} thingsos_ima_job_t;
typedef struct {
    void* context;
    /* Returned JSON is owned by caller (free). NULL means transport uncertain. */
    char* (*rpc)(void*,const char* method,const char* json);
    bool (*put)(void*,const char* host,const char* raw_path,const char* escaped_path,
                const char* auth,const char* token,uint64_t bytes);
    bool (*sha1)(const void*,size_t,unsigned char[20]);
    bool (*hmac)(const void*,size_t,const void*,size_t,unsigned char[20]);
    bool (*cancelled)(void*);
    /* ADDING must be durable BEFORE add_knowledge is sent. false aborts. */
    bool (*changed)(void*,const thingsos_ima_job_t*);
} thingsos_ima_ops_t;
bool thingsos_ima_recording_path(const char*);
bool thingsos_ima_preflight(uint64_t bytes,uint64_t duration_ms);
bool thingsos_ima_escape(const char*,char*,size_t,bool slash);
bool thingsos_ima_sign(const thingsos_ima_ops_t*,const char* sid,const char* secret,
    uint64_t start,uint64_t end,const char* host,const char* path,uint64_t bytes,char*,size_t);
/* keep_copy requires a user-approved unique remote name, checked again. */
void thingsos_ima_execute(thingsos_ima_job_t*,const thingsos_ima_ops_t*);
/* Only read back an uncertain add; never blindly repeat it. */
void thingsos_ima_reconcile(thingsos_ima_job_t*,const thingsos_ima_ops_t*);
