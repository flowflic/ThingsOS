// SPDX-License-Identifier: Apache-2.0
#include "thingsos/release.h"
#include "cJSON.h"
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static const char valid[]="{\"schema\":2,\"project\":\"ThingsOS_native\",\"board\":\"Metalio-E-Ink4-Plus\",\"layout\":\"thingsos-metalio-single-v1\",\"version\":\"0.3.0\",\"ota\":{\"path\":\"firmware/ThingsOS-0.3.0-ota.bin\",\"size\":3946512,\"sha256\":\"8fa3067b9b2925cbeac28015e8d0a295d9cc9c1491ba7b5928478b764aa730b8\"},\"changes\":[\"Book search\",\"USB direct close\"]}";
static bool parse(cJSON* object) {
    char* json=cJSON_PrintUnformatted(object);thingsos_release_t release;
    bool ok=thingsos_release_parse(json,strlen(json)+1,&release);free(json);return ok;
}
int main(int argc,char** argv) {
    thingsos_release_t release;
    assert(thingsos_release_parse(valid,sizeof(valid),&release));
    assert(!strcmp(release.version,"0.3.0") && release.bytes==3946512);
    assert(!strcmp(release.notes,"Book search\nUSB direct close") && release.sha256[0]==0x8f);
    assert(thingsos_release_newer("0.3.0","0.2.0"));
    assert(thingsos_release_newer("0.10.0","0.9.9"));
    assert(!thingsos_release_newer("0.2.0","0.2.0") && !thingsos_release_newer("0.1.9","0.2.0"));
    assert(!thingsos_release_newer("0.3","0.2.0") && !thingsos_release_newer("0.3.0evil","0.2.0"));
    assert(!thingsos_release_parse(valid,sizeof(valid)-1,&release));
    cJSON* larger=cJSON_Parse(valid);
    cJSON_ReplaceItemInObjectCaseSensitive(cJSON_GetObjectItem(larger,"ota"),"size",cJSON_CreateNumber(11403264));assert(parse(larger));
    cJSON_ReplaceItemInObjectCaseSensitive(larger,"layout",cJSON_CreateString("thingsos-native-dual-4m-v1"));assert(!parse(larger));cJSON_Delete(larger);
    const char* keys[]={"schema","project","board","layout","version","ota"};
    for(unsigned i=0;i<6;++i){cJSON* root=cJSON_Parse(valid);cJSON_DeleteItemFromObjectCaseSensitive(root,keys[i]);assert(!parse(root));cJSON_Delete(root);}
    const char* paths[]={"https://evil.org/a.bin","firmware/../x.bin","firmware/x.bin?q=1","firmware/%2e.bin","firmware/usb.exe"};
    for(unsigned i=0;i<5;++i){cJSON* root=cJSON_Parse(valid);cJSON* ota=cJSON_GetObjectItemCaseSensitive(root,"ota");cJSON_ReplaceItemInObjectCaseSensitive(ota,"path",cJSON_CreateString(paths[i]));assert(!parse(root));cJSON_Delete(root);}
    for(unsigned i=0;i<4;++i){double sizes[]={-1,200,11403265,300.5};cJSON* root=cJSON_Parse(valid);cJSON* ota=cJSON_GetObjectItemCaseSensitive(root,"ota");cJSON_ReplaceItemInObjectCaseSensitive(ota,"size",cJSON_CreateNumber(sizes[i]));assert(!parse(root));cJSON_Delete(root);}
    {cJSON* other=cJSON_Parse(valid);cJSON_ReplaceItemInObjectCaseSensitive(other,"board",cJSON_CreateString("RDP-G01-W"));assert(!parse(other));cJSON_Delete(other);} /* Read Pico package. */
    cJSON* root=cJSON_Parse(valid);cJSON_ReplaceItemInObjectCaseSensitive(cJSON_GetObjectItem(root,"ota"),"sha256",cJSON_CreateString("invalid"));assert(!parse(root));cJSON_Delete(root);
    if(argc>1){FILE* f=fopen(argv[1],"rb");assert(f);char json[16385];size_t n=fread(json,1,16384,f);fclose(f);json[n]=0;assert(thingsos_release_parse(json,n+1,&release));assert(!thingsos_release_newer(release.version,"999.0.0"));}
    puts("Release manifest: native compatibility, ordered versions, hashes, bounds and trusted artifact paths passed");
    return 0;
}
