// SPDX-License-Identifier: Apache-2.0
/* Independent implementation of IMA's documented file flow and COS v5 signing.
   Reference: ima-skill 1.1.7; cloud.tencent.com/document/product/436/7778. */
#include "thingsos/ima_protocol.h"
#include "thingsos/mem.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
static void copy(char* out,size_t n,const char* in){snprintf(out,n,"%s",in?in:"");}
static const char* str(cJSON* o,const char* k){cJSON* v=cJSON_GetObjectItemCaseSensitive(o,k);return cJSON_IsString(v)?v->valuestring:NULL;}
static bool safe(const char* s,size_t max){if(!s||!*s||strlen(s)>=max)return false;for(;*s;++s)if((unsigned char)*s<32||*s==127)return false;return true;}
static bool atom(const char* s,size_t max){if(!safe(s,max))return false;for(;*s;++s)if(!((*s>='a'&&*s<='z')||(*s>='A'&&*s<='Z')||(*s>='0'&&*s<='9')||*s=='-'))return false;return true;}
/* JSON int64 may be serialized as a decimal string. Reject fractions,
   signs, overflow and out-of-range dates rather than converting loosely. */
static bool credential_time(const cJSON* value,uint64_t* out){
    if(cJSON_IsNumber(value)){
        double n=value->valuedouble;if(!isfinite(n)||n<=0||n>=4102444800.0||floor(n)!=n)return false;
        *out=(uint64_t)n;return true;
    }
    if(!cJSON_IsString(value)||!value->valuestring[0])return false;
    uint64_t n=0;for(const unsigned char* p=(const unsigned char*)value->valuestring;*p;++p){
        if(*p<'0'||*p>'9'||n>4102444800ull/10)return false;n=n*10+(*p-'0');if(n>=4102444800ull)return false;
    }
    if(!n)return false;*out=n;return true;
}
bool thingsos_ima_recording_path(const char* p){
    if(!p||strncmp(p,"/sdcard/audio/",14)||!p[14]||strchr(p+14,'/')||strchr(p+14,'\\')||strstr(p+14,".."))return false;
    size_t n=strlen(p);return n<512&&n>18&&!strcmp(p+n-4,".wav")&&safe(p,512);
}
bool thingsos_ima_preflight(uint64_t n,uint64_t ms){return n>44&&n<=THINGSOS_IMA_MAX_BYTES&&ms>0&&ms<=7200000;}
bool thingsos_ima_escape(const char* s,char* out,size_t n,bool slash){
    const char hex[]="0123456789ABCDEF";size_t at=0;
    if(!s||!out||!n)return false;
    for(;*s;++s){unsigned c=(unsigned char)*s;bool plain=(c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'||c=='~'||(slash&&c=='/');
        if(at+(plain?1:3)>=n)return false;
        if(plain)out[at++]=(char)c;else{out[at++]='%';out[at++]=hex[c>>4];out[at++]=hex[c&15];}}
    out[at]=0;return true;
}
static void hex20(const unsigned char* h,char* out){for(unsigned i=0;i<20;++i)sprintf(out+i*2,"%02x",h[i]);}
bool thingsos_ima_sign(const thingsos_ima_ops_t* o,const char* sid,const char* secret,uint64_t start,uint64_t end,const char* host,const char* path,uint64_t bytes,char* out,size_t cap){
    if(!o||!o->sha1||!o->hmac||!atom(sid,256)||!safe(secret,256)||!safe(host,256)||!safe(path,1024)||start>=end)return false;
    char times[64],sign_key[41],digest[41],sig[41];unsigned char hash[20];
    snprintf(times,sizeof(times),"%llu;%llu",(unsigned long long)start,(unsigned long long)end);
    if(!o->hmac(secret,strlen(secret),times,strlen(times),hash))return false;hex20(hash,sign_key);
    char* canonical=thingsos_psram_calloc(1,1600);if(!canonical)return false;
    int n=snprintf(canonical,1600,"put\n%s\n\ncontent-length=%llu&host=%s\n",path,(unsigned long long)bytes,host);
    bool ok=n>0&&n<1600&&o->sha1(canonical,(size_t)n,hash);free(canonical);if(!ok)return false;hex20(hash,digest);
    char to_sign[160];snprintf(to_sign,sizeof(to_sign),"sha1\n%s\n%s\n",times,digest);
    if(!o->hmac(sign_key,40,to_sign,strlen(to_sign),hash))return false;hex20(hash,sig);memset(sign_key,0,sizeof(sign_key));
    n=snprintf(out,cap,"q-sign-algorithm=sha1&q-ak=%s&q-sign-time=%s&q-key-time=%s&q-header-list=content-length;host&q-url-param-list=&q-signature=%s",sid,times,times,sig);
    return n>0&&(size_t)n<cap;
}
static bool state(thingsos_ima_job_t* j,const thingsos_ima_ops_t* o,thingsos_ima_phase_t p,const char* message){j->phase=p;copy(j->message,sizeof(j->message),message);return !o->changed||o->changed(o->context,j);}
static bool cancelled(thingsos_ima_job_t* j,const thingsos_ima_ops_t* o){if(o->cancelled&&o->cancelled(o->context)){state(j,o,IMA_CANCELLED,"已取消，本地录音保留");return true;}return false;}
static cJSON* call(thingsos_ima_job_t* j,const thingsos_ima_ops_t* o,const char* method,cJSON* body,bool uncertain){
    char* request=cJSON_PrintUnformatted(body);cJSON_Delete(body);
    if(!request){state(j,o,IMA_FAILED,"请求内存不足");return NULL;}
    char* response=o->rpc(o->context,method,request);free(request);
    cJSON* root=response?cJSON_Parse(response):NULL;free(response);
    cJSON* code=cJSON_GetObjectItemCaseSensitive(root,"code"),*data=cJSON_GetObjectItemCaseSensitive(root,"data");
    if(!cJSON_IsNumber(code)||code->valuedouble!=0||!cJSON_IsObject(data)){
        const char* msg=str(root,"msg");
        state(j,o,uncertain?IMA_UNCERTAIN:IMA_FAILED,msg?msg:uncertain?"结果待确认，请检查结果，勿重复上传":"ima 请求失败，请检查网络或连接配置");cJSON_Delete(root);return NULL;}
    cJSON* result=cJSON_DetachItemFromObject(root,"data");cJSON_Delete(root);return result;
}
static cJSON* base(const thingsos_ima_job_t* j){cJSON* b=cJSON_CreateObject();cJSON_AddStringToObject(b,"knowledge_base_id",j->kb);return b;}
static bool valid_job(const thingsos_ima_job_t* j){size_t n=strlen(j->name);return safe(j->kb,sizeof(j->kb))&&safe(j->name,sizeof(j->name))&&n>4&&!strcmp(j->name+n-4,".wav")&&!strchr(j->name,'/')&&!strchr(j->name,'\\')&&j->bytes>44&&j->bytes<=THINGSOS_IMA_MAX_BYTES;}
void thingsos_ima_execute(thingsos_ima_job_t* j,const thingsos_ima_ops_t* o){
    if(!j||!o||!o->rpc||!o->put)return;
    if(!valid_job(j)){state(j,o,IMA_FAILED,"录音文件或知识库无效");return;}
    if(j->phase==IMA_UNCERTAIN||j->phase==IMA_ADDING){thingsos_ima_reconcile(j,o);return;}
    j->media[0]=0;j->percent=0;state(j,o,IMA_CHECKING,"正在检查录音…");if(cancelled(j,o))return;
    cJSON* b=base(j),*params=cJSON_AddArrayToObject(b,"params"),*p=cJSON_CreateObject();cJSON_AddItemToArray(params,p);cJSON_AddStringToObject(p,"name",j->name);cJSON_AddNumberToObject(p,"media_type",15);
    cJSON* d=call(j,o,"check_repeated_names",b,false);if(!d)return;
    cJSON* results=cJSON_GetObjectItemCaseSensitive(d,"results"),*r=cJSON_GetArrayItem(results,0),*repeat=cJSON_GetObjectItemCaseSensitive(r,"is_repeated");
    bool valid=cJSON_IsArray(results)&&cJSON_GetArraySize(results)==1&&str(r,"name")&&!strcmp(str(r,"name"),j->name)&&cJSON_IsBool(repeat);
    bool duplicate=cJSON_IsTrue(repeat);cJSON_Delete(d);
    if(!valid){state(j,o,IMA_FAILED,"ima 重名检查响应无效");return;}
    if(duplicate){state(j,o,IMA_DUPLICATE,"知识库已有同名录音，可取消或保留两份");return;}
    if(cancelled(j,o))return;
    b=base(j);cJSON_AddStringToObject(b,"file_name",j->name);cJSON_AddNumberToObject(b,"file_size",(double)j->bytes);cJSON_AddStringToObject(b,"content_type","audio/wav");cJSON_AddStringToObject(b,"file_ext","wav");
    d=call(j,o,"create_media",b,false);if(!d)return;
    cJSON* c=cJSON_GetObjectItemCaseSensitive(d,"cos_credential");
    const char* id=str(d,"media_id"),*bucket=str(c,"bucket_name"),*region=str(c,"region"),*key=str(c,"cos_key"),*sid=str(c,"secret_id"),*secret=str(c,"secret_key"),*token=str(c,"token");
    uint64_t start=0,end=0;const char* invalid=NULL;
    if(!safe(id,sizeof(j->media)))invalid="ima 返回的媒体标识无效";
    else if(!atom(bucket,128)||!atom(region,64))invalid="ima 临时凭证的存储桶或区域无效";
    else if(!safe(key,1000)||key[0]=='/'||strstr(key,".."))invalid="ima 临时凭证的对象路径无效";
    else if(!safe(token,4096)||!atom(sid,256)||!safe(secret,256))invalid="ima 临时凭证缺少有效密钥或令牌";
    else if(!credential_time(cJSON_GetObjectItemCaseSensitive(c,"start_time"),&start)||
            !credential_time(cJSON_GetObjectItemCaseSensitive(c,"expired_time"),&end)||end<=start)invalid="ima 临时凭证的有效期无效";
    if(invalid){cJSON_Delete(d);state(j,o,IMA_FAILED,invalid);return;}
    struct transfer_scratch {char host[256],path[1024],escaped[3072],auth[768];};
    struct transfer_scratch* scratch=thingsos_psram_calloc(1,sizeof(*scratch));
    if(!scratch){cJSON_Delete(d);state(j,o,IMA_FAILED,"ima 上传工作区内存不足");return;}
    snprintf(scratch->host,sizeof(scratch->host),"%s.cos.%s.myqcloud.com",bucket,region);snprintf(scratch->path,sizeof(scratch->path),"/%s",key);
    valid=thingsos_ima_escape(scratch->path,scratch->escaped,sizeof(scratch->escaped),true)&&thingsos_ima_sign(o,sid,secret,start,end,scratch->host,scratch->path,j->bytes,scratch->auth,sizeof(scratch->auth));
    if(!valid){memset(scratch,0,sizeof(*scratch));free(scratch);cJSON_Delete(d);state(j,o,IMA_FAILED,"ima 上传签名生成失败");return;}
    copy(j->media,sizeof(j->media),id);
    state(j,o,IMA_TRANSFERRING,"正在上传…");
    bool uploaded=!cancelled(j,o)&&o->put(o->context,scratch->host,scratch->path,scratch->escaped,scratch->auth,token,j->bytes);
    memset(scratch,0,sizeof(*scratch));free(scratch);
    if(!uploaded){cJSON_Delete(d);if(!cancelled(j,o))state(j,o,IMA_FAILED,"录音上传未完成，本地文件保留");return;}
    b=base(j);cJSON_AddNumberToObject(b,"media_type",15);cJSON_AddStringToObject(b,"media_id",j->media);cJSON_AddStringToObject(b,"title",j->name);
    cJSON* f=cJSON_AddObjectToObject(b,"file_info");cJSON_AddStringToObject(f,"cos_key",key);cJSON_AddStringToObject(f,"file_name",j->name);cJSON_AddNumberToObject(f,"file_size",(double)j->bytes);cJSON_Delete(d);
    if(cancelled(j,o)){cJSON_Delete(b);return;}
    j->percent=95;
    if(!state(j,o,IMA_ADDING,"正在加入知识库…")){cJSON_Delete(b);state(j,o,IMA_FAILED,"无法保存上传状态，未提交到知识库");return;}
    d=call(j,o,"add_knowledge",b,true);if(!d)return;
    id=str(d,"media_id");valid=id&&!strcmp(id,j->media);cJSON_Delete(d);
    if(!valid){state(j,o,IMA_UNCERTAIN,"云端结果待确认，请检查结果");return;}
    thingsos_ima_reconcile(j,o);
}
void thingsos_ima_reconcile(thingsos_ima_job_t* j,const thingsos_ima_ops_t* o){
    if(!valid_job(j)||!safe(j->media,sizeof(j->media))){state(j,o,IMA_FAILED,"没有可确认的上传记录");return;}
    /* List the root directly: audio indexing can lag while IMA processes it. */
    cJSON* b=base(j);cJSON_AddNumberToObject(b,"limit",50);cJSON_AddStringToObject(b,"cursor","");
    char cursor[512]="";bool found=false;
    for(unsigned page=0;page<64;++page){
        cJSON_ReplaceItemInObject(b,"cursor",cJSON_CreateString(cursor));
        cJSON* request=cJSON_Duplicate(b,1),*d=call(j,o,"get_knowledge_list",request,true);if(!d){cJSON_Delete(b);return;}
        cJSON* list=cJSON_GetObjectItemCaseSensitive(d,"knowledge_list"),*item;
        cJSON_ArrayForEach(item,list){const char* id=str(item,"media_id");if(id&&!strcmp(id,j->media))found=true;}
        bool end=cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(d,"is_end"));const char* next=str(d,"next_cursor");
        bool more=!found&&!end&&safe(next,sizeof(cursor))&&strcmp(next,cursor);
        if(more)copy(cursor,sizeof(cursor),next);cJSON_Delete(d);if(!more)break;
    }
    cJSON_Delete(b);j->percent=found?100:j->percent;state(j,o,found?IMA_UPLOADED:IMA_UNCERTAIN,found?"已加入知识库，内容处理中":"云端结果待确认，请稍后检查结果，勿重复上传");
}
