// SPDX-License-Identifier: Apache-2.0
#include "thingsos/release.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>
static bool version(const char* s,unsigned out[3]) {
    if(!s || !s[0])return false;
    for(unsigned i=0;i<3;++i) {
        unsigned n=0,digits=0;
        while(*s>='0' && *s<='9') {if(++digits>5)return false;n=n*10+(unsigned)(*s++-'0');}
        if(!digits)return false;
        out[i]=n;
        if(i<2){if(*s++!='.')return false;}else if(*s)return false;
    }
    return true;
}
bool thingsos_release_newer(const char* candidate,const char* installed) {
    unsigned a[3],b[3];if(!version(candidate,a) || !version(installed,b))return false;
    for(unsigned i=0;i<3;++i)if(a[i]!=b[i])return a[i]>b[i];
    return false;
}
static const char* text(cJSON* object,const char* key) {
    cJSON* item=cJSON_GetObjectItemCaseSensitive(object,key);
    return cJSON_IsString(item)?item->valuestring:NULL;
}
static bool same(cJSON* o,const char* k,const char* value) {
    const char* s=text(o,k);return s && !strcmp(s,value);
}
static int digit(char c) {return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;}
bool thingsos_release_parse(const char* json,size_t length,thingsos_release_t* out) {
    if(!json || !out || !length || length>16385 || json[length-1])return false;
    cJSON* root=cJSON_ParseWithLengthOpts(json,length,NULL,true);
    if(!root)return false;
    cJSON* schema=cJSON_GetObjectItemCaseSensitive(root,"schema");
    cJSON* ota=cJSON_GetObjectItemCaseSensitive(root,"ota");
    cJSON* size=cJSON_GetObjectItemCaseSensitive(ota,"size");
    const char *v=text(root,"version"),*path=text(ota,"path"),*hash=text(ota,"sha256");
    unsigned parts[3];bool ok=cJSON_IsNumber(schema) && schema->valuedouble==2 &&
        same(root,"project","ThingsOS_native") && same(root,"board",THINGSOS_RELEASE_BOARD) &&
        same(root,"layout","thingsos-metalio-single-v1") && v && strlen(v)<sizeof(out->version) && version(v,parts) &&
        path && strlen(path)<sizeof(out->path) && !strncmp(path,"firmware/",9) && !strstr(path,"..") &&
        hash && strlen(hash)==64 && cJSON_IsNumber(size) && size->valuedouble>=288 && size->valuedouble<=11403264;
    if(ok)for(const char* p=path;*p;++p)
        if(!((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||*p=='-'||*p=='_'||*p=='.'||*p=='/'))ok=false;
    if(ok)ok=strlen(path)>4 && !strcmp(path+strlen(path)-4,".bin") && (size_t)size->valuedouble==size->valuedouble;
    if(ok)for(unsigned i=0;i<64;++i)if(digit(hash[i])<0)ok=false;
    if(ok) {
        memset(out,0,sizeof(*out));strcpy(out->version,v);strcpy(out->path,path);out->bytes=(size_t)size->valuedouble;
        for(unsigned i=0;i<32;++i)out->sha256[i]=(uint8_t)((digit(hash[2*i])<<4)|digit(hash[2*i+1]));
        cJSON* changes=cJSON_GetObjectItemCaseSensitive(root,"changes");cJSON* item;
        cJSON_ArrayForEach(item,changes) {
            if(!cJSON_IsString(item) || !item->valuestring[0])continue;
            size_t used=strlen(out->notes),n=strlen(item->valuestring);
            if(used+n+2>=sizeof(out->notes))break;
            if(used)out->notes[used++]='\n';
            memcpy(out->notes+used,item->valuestring,n+1);
        }
        if(!out->notes[0])strcpy(out->notes,"此版本未提供更新说明");
    }
    cJSON_Delete(root);return ok;
}
