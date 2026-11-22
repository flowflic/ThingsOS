// SPDX-License-Identifier: Apache-2.0
#include "thingsos/ima_protocol.h"
#include "cJSON.h"
#include <windows.h>
#include <bcrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"IMA test line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static bool digest(const void* k,size_t kn,const void* p,size_t n,unsigned char out[20]){BCRYPT_ALG_HANDLE alg=NULL;BCRYPT_HASH_HANDLE hash=NULL;bool ok=BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA1_ALGORITHM,NULL,k?BCRYPT_ALG_HANDLE_HMAC_FLAG:0)>=0;if(ok)ok=BCryptCreateHash(alg,&hash,NULL,0,(PUCHAR)k,(ULONG)kn,0)>=0;if(ok)ok=BCryptHashData(hash,(PUCHAR)p,(ULONG)n,0)>=0&&BCryptFinishHash(hash,out,20,0)>=0;if(hash)BCryptDestroyHash(hash);if(alg)BCryptCloseAlgorithmProvider(alg,0);return ok;}
static bool sha(const void* p,size_t n,unsigned char out[20]){return digest(NULL,0,p,n,out);}
static bool mac(const void* k,size_t kn,const void* p,size_t n,unsigned char out[20]){return digest(k,kn,p,n,out);}
static bool duplicate,put_fail,add_lost,search_empty,invalid_credential,cancel,durable_fail,check_error;
static unsigned calls,put_calls,adds,searches,credential_case;static thingsos_ima_job_t* current;
static char* answer(const char* s){char* p=malloc(strlen(s)+1);CHECK(p);strcpy(p,s);return p;}
static const char* string(cJSON* o,const char* k){cJSON* v=cJSON_GetObjectItem(o,k);CHECK(cJSON_IsString(v));return v->valuestring;}
static char* rpc(void* ctx,const char* method,const char* body){(void)ctx;++calls;cJSON* b=cJSON_Parse(body);CHECK(b);CHECK(!strcmp(string(b,"knowledge_base_id"),"kb-test"));
    if(!strcmp(method,"check_repeated_names")){cJSON_Delete(b);if(check_error)return answer("{\"code\":110030,\"msg\":\"denied\"}");char response[512];snprintf(response,sizeof(response),"{\"code\":0,\"data\":{\"results\":[{\"name\":\"%s\",\"is_repeated\":%s}]}}",current->name,duplicate?"true":"false");return answer(response);}
    if(!strcmp(method,"create_media")){CHECK(!strcmp(string(b,"file_name"),current->name));CHECK(!strcmp(string(b,"content_type"),"audio/wav"));cJSON_Delete(b);char* result=answer(invalid_credential?"{\"code\":0,\"data\":{\"media_id\":\"m1\",\"cos_credential\":{\"bucket_name\":\"evil.example/\",\"region\":\"ap-test\"}}}":"{\"code\":0,\"data\":{\"media_id\":\"m1\",\"cos_credential\":{\"bucket_name\":\"ima-123\",\"region\":\"ap-shanghai\",\"cos_key\":\"folder/test.wav\",\"secret_id\":\"testid\",\"secret_key\":\"testsecret\",\"token\":\"temp-token\",\"start_time\":1700000000,\"expired_time\":1700003600}}}");if(credential_case){cJSON* root=cJSON_Parse(result);free(result);cJSON* credential=cJSON_GetObjectItem(cJSON_GetObjectItem(root,"data"),"cos_credential");
        if(credential_case==1||credential_case==2)cJSON_ReplaceItemInObject(credential,"start_time",cJSON_CreateString("1700000000"));
        if(credential_case==1)cJSON_ReplaceItemInObject(credential,"expired_time",cJSON_CreateString("1700003600"));
        if(credential_case==3)cJSON_ReplaceItemInObject(credential,"start_time",cJSON_CreateNumber(1700000000.5));
        if(credential_case==4)cJSON_ReplaceItemInObject(credential,"start_time",cJSON_CreateString("1700000000junk"));
        if(credential_case==5)cJSON_ReplaceItemInObject(credential,"expired_time",cJSON_CreateString("18446744073709551616"));
        if(credential_case==6)cJSON_ReplaceItemInObject(credential,"expired_time",cJSON_CreateString("1699999999"));
        if(credential_case==7)cJSON_DeleteItemFromObject(credential,"token");
        result=cJSON_PrintUnformatted(root);cJSON_Delete(root);}return result;}
    if(!strcmp(method,"add_knowledge")){++adds;CHECK(put_calls==1);CHECK(!strcmp(string(b,"title"),current->name));CHECK(!strcmp(string(cJSON_GetObjectItem(b,"file_info"),"file_name"),current->name));CHECK(cJSON_GetObjectItem(b,"media_type")->valueint==15);cJSON_Delete(b);return add_lost?NULL:answer("{\"code\":0,\"data\":{\"media_id\":\"m1\"}}");}
    CHECK(!strcmp(method,"get_knowledge_list"));CHECK(cJSON_GetObjectItem(b,"limit")->valueint==50);++searches;cJSON_Delete(b);return answer(search_empty?"{\"code\":0,\"data\":{\"knowledge_list\":[],\"is_end\":true}}":"{\"code\":0,\"data\":{\"knowledge_list\":[{\"media_id\":\"m1\"}],\"is_end\":true}}");
}
static bool put(void* c,const char* host,const char* raw,const char* escaped,const char* auth,const char* token,uint64_t n){(void)c;++put_calls;CHECK(!strcmp(host,"ima-123.cos.ap-shanghai.myqcloud.com"));CHECK(!strcmp(raw,"/folder/test.wav")&&!strcmp(escaped,raw));CHECK(!strcmp(token,"temp-token"));CHECK(n==3244&&strstr(auth,"q-signature="));return !put_fail;}
static bool cancelled(void* c){(void)c;return cancel;}
static bool changed(void* c,const thingsos_ima_job_t* j){(void)c;return !(durable_fail&&j->phase==IMA_ADDING);}
static thingsos_ima_job_t job(void){thingsos_ima_job_t j={0};strcpy(j.path,"/sdcard/audio/REC-test.wav");strcpy(j.name,"REC-test.wav");strcpy(j.kb,"kb-test");j.bytes=3244;return j;}
static void reset(void){duplicate=put_fail=add_lost=search_empty=invalid_credential=cancel=durable_fail=check_error=false;calls=put_calls=adds=searches=credential_case=0;}
int main(void){
    thingsos_ima_ops_t o={.rpc=rpc,.put=put,.sha1=sha,.hmac=mac,.cancelled=cancelled,.changed=changed};
    CHECK(thingsos_ima_recording_path("/sdcard/audio/REC-1.wav"));CHECK(!thingsos_ima_recording_path("/sdcard/audio/../x.wav"));CHECK(!thingsos_ima_recording_path("/sdcard/music/x.wav"));CHECK(!thingsos_ima_recording_path("/sdcard/audio/x.mp3"));
    CHECK(thingsos_ima_preflight(3244,100));CHECK(!thingsos_ima_preflight(44,100));CHECK(!thingsos_ima_preflight(THINGSOS_IMA_MAX_BYTES+1ull,100));CHECK(!thingsos_ima_preflight(3244,7200001));
    char escaped[128];CHECK(thingsos_ima_escape("/a b/录.wav",escaped,sizeof(escaped),true));CHECK(!strcmp(escaped,"/a%20b/%E5%BD%95.wav"));CHECK(!thingsos_ima_escape("abc",escaped,3,false));
    char auth[768];CHECK(thingsos_ima_sign(&o,"sid","secret",1700000000,1700003600,"ima-123.cos.ap-shanghai.myqcloud.com","/folder/test.wav",3244,auth,sizeof(auth)));put_calls=0;
    CHECK(strstr(auth,"q-signature=")&&strlen(strstr(auth,"q-signature=")+12)==40);
    CHECK(!strcmp(strstr(auth,"q-signature=")+12,"721d17d5003195d63641b738d0e51729aa8ad544")); /* Independent Node crypto vector. */
    thingsos_ima_job_t j;
    reset();j=job();current=&j;thingsos_ima_execute(&j,&o);CHECK(j.phase==IMA_UPLOADED&&adds==1&&put_calls==1&&searches==1);
    for(unsigned kind=1;kind<=7;++kind){reset();credential_case=kind;j=job();thingsos_ima_execute(&j,&o);
        if(kind<=2)CHECK(j.phase==IMA_UPLOADED&&put_calls==1&&adds==1);
        else {CHECK(j.phase==IMA_FAILED&&put_calls==0&&adds==0);CHECK(strstr(j.message,kind==7?"令牌":"有效期"));}}
    reset();duplicate=true;j=job();thingsos_ima_execute(&j,&o);CHECK(j.phase==IMA_DUPLICATE&&calls==1&&put_calls==0&&adds==0);
    reset();check_error=true;j=job();thingsos_ima_execute(&j,&o);CHECK(j.phase==IMA_FAILED&&calls==1&&adds==0);
    reset();invalid_credential=true;j=job();thingsos_ima_execute(&j,&o);CHECK(j.phase==IMA_FAILED&&put_calls==0&&adds==0);
    reset();put_fail=true;j=job();thingsos_ima_execute(&j,&o);CHECK(j.phase==IMA_FAILED&&put_calls==1&&adds==0);
    reset();durable_fail=true;j=job();thingsos_ima_execute(&j,&o);CHECK(j.phase==IMA_FAILED&&put_calls==1&&adds==0);
    reset();add_lost=true;j=job();thingsos_ima_execute(&j,&o);CHECK(j.phase==IMA_UNCERTAIN&&adds==1);search_empty=true;thingsos_ima_execute(&j,&o);CHECK(j.phase==IMA_UNCERTAIN&&adds==1&&searches==1);search_empty=false;thingsos_ima_reconcile(&j,&o);CHECK(j.phase==IMA_UPLOADED&&adds==1);
    reset();cancel=true;j=job();thingsos_ima_execute(&j,&o);CHECK(j.phase==IMA_CANCELLED&&calls==0&&adds==0);
    printf("IMA contract, signing, bounded upload and uncertain-result tests passed\n");return 0;
}
