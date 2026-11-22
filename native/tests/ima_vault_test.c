// SPDX-License-Identifier: Apache-2.0
#define THINGSOS_IMA_VAULT_HOST 1
#include "../firmware/components/thingsos_ima/secure.c"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"IMA credentials line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
int main(void){
    ima_config_t config={0},result;strcpy(config.client,"test-client");strcpy(config.key,"TEST_API_KEY");strcpy(config.kb,"kb-test");strcpy(config.name,"Knowledge");
    CHECK(!ima_config_load(&result));
    CHECK(ima_config_save(&config));CHECK(ima_config_load(&result));CHECK(!memcmp(&config,&result,sizeof(config)));
    /* Reopening into fresh RAM restores the key and default library with no password. */
    memset(&result,0,sizeof(result));CHECK(ima_config_load(&result));CHECK(!strcmp(result.key,"TEST_API_KEY")&&!strcmp(result.kb,"kb-test"));
    uint8_t previous[4096];size_t previous_bytes=nvs_bytes;memcpy(previous,nvs_blob,nvs_bytes);
    strcpy(config.kb,"other-kb");host_nvs_fail_write=true;CHECK(!ima_config_save(&config));host_nvs_fail_write=false;
    CHECK(nvs_bytes==previous_bytes&&!memcmp(previous,nvs_blob,nvs_bytes));CHECK(ima_config_load(&result)&&!strcmp(result.kb,"kb-test"));
    CHECK(ima_config_save(&config));CHECK(ima_config_load(&result)&&!strcmp(result.kb,"other-kb"));
    memcpy(previous,nvs_blob,nvs_bytes);nvs_blob[nvs_bytes-1]^=1;memset(&result,0x77,sizeof(result));CHECK(!ima_config_load(&result));
    for(unsigned i=0;i<sizeof(result);++i)CHECK(((uint8_t*)&result)[i]==0);
    memcpy(nvs_blob,previous,nvs_bytes);((connection_t*)nvs_blob)->magic=0x494d4132;CHECK(!ima_config_load(&result)); /* Obsolete encrypted record cannot be silently imported. */
    memcpy(nvs_blob,previous,nvs_bytes);--nvs_bytes;CHECK(!ima_config_load(&result));++nvs_bytes;
    CHECK(ima_config_clear());CHECK(!ima_config_load(&result));
    config.client[0]=0;CHECK(!ima_config_save(&config));
    puts("Production credential persistence: restore, library update, failed save preservation, corrupt/old record rejection and disconnect passed");return 0;
}
