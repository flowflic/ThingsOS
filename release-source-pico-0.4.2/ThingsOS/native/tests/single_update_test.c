// SPDX-License-Identifier: Apache-2.0
#undef NDEBUG
#define THINGSOS_SINGLE_TEST 1
#define THINGSOS_UPDATE_FILE "single-update-owned-test.bin"
#include "../firmware/components/thingsos_net/single_update.c"
static uint8_t image[20000],digest[32];
static void fixture(void) {
    for(size_t i=0;i<sizeof(image);++i)image[i]=(uint8_t)(i*7);
    esp_image_header_t h={.magic=0xe9,.chip_id=9};memcpy(image,&h,sizeof(h));
    esp_app_desc_t app={.magic_word=ESP_APP_DESC_MAGIC_WORD};strcpy(app.version,"0.3.2");strcpy(app.project_name,"ThingsOS_native");
    memcpy(image+sizeof(h)+sizeof(esp_image_segment_header_t),&app,sizeof(app));
    psa_hash_operation_t sha;psa_hash_setup(&sha,1);psa_hash_update(&sha,image,sizeof(image));size_t n;psa_hash_finish(&sha,digest,32,&n);
    FILE* f=fopen(THINGSOS_UPDATE_FILE,"wb");assert(f);assert(fwrite(image,1,sizeof(image),f)==sizeof(image));assert(!fclose(f));
}
int main(void) {
    fixture();assert(thingsos_single_install()==ESP_ERR_NOT_FOUND&&erases==0);
    uint8_t bad[32];memcpy(bad,digest,32);bad[0]^=1;
    assert(thingsos_single_stage(sizeof(image),bad,"0.3.2")!=0&&erases==0&&selected_partition==&app_partition);
    assert(thingsos_single_stage(sizeof(image)-1,digest,"0.3.2")!=0&&erases==0);
    assert(thingsos_single_stage(sizeof(image),digest,"wrong")!=0&&erases==0);
    assert(!thingsos_single_stage(sizeof(image),digest,"0.3.2")&&erases==0&&persisted_size&&selected_partition==&rescue_partition);
    assert(thingsos_single_install()==ESP_ERR_INVALID_STATE&&erases==0); // still running main
    running_partition=&rescue_partition;
    FILE* f=fopen(THINGSOS_UPDATE_FILE,"r+b");assert(f);fputc(0,f);fclose(f);
    assert(thingsos_single_install()!=0&&erases==0&&selected_partition==&rescue_partition);fixture();
    fail_write=1;assert(thingsos_single_install()!=0&&erases==1&&persisted_size&&selected_partition==&rescue_partition);fail_write=0;
    fail_end=1;assert(thingsos_single_install()!=0&&persisted_size&&selected_partition==&rescue_partition);fail_end=0;
    corrupt_read=1;assert(thingsos_single_install()!=0&&persisted_size&&selected_partition==&rescue_partition);corrupt_read=0;
    assert(!thingsos_single_install()&&selected_partition==&app_partition&&!persisted_size&&written==sizeof(image));
    assert(!memcmp(image,flash_data,sizeof(image)));
    assert(!remove(THINGSOS_UPDATE_FILE));
    puts("Single-slot installer: pre-erase verification, main protection, media tamper, write/end/readback failures, retry and activation passed");
    return 0;
}
