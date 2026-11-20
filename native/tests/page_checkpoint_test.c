/* SPDX-License-Identifier: Apache-2.0 */
#include "thingsos/pagination.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"checkpoint line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
int main(void) {
    const char* path="page-checkpoint-test.dat";remove(path);
    thingsos_page_position_t p[]={{0,0},{0,120},{1,0},{1,200},{2,0}},*loaded=NULL;
    size_t count=0;unsigned done=0;
    CHECK(thingsos_page_checkpoint_save(path,55,175,p,4,2));
    CHECK(thingsos_page_checkpoint_load(path,55,175,&loaded,&count,&done));
    CHECK(count==4 && done==2 && !memcmp(p,loaded,sizeof(*p)*4));free(loaded);
    CHECK(!thingsos_page_cache_load(path,55,&loaded,&count));
    CHECK(!thingsos_page_checkpoint_load(path,56,175,&loaded,&count,&done));
    CHECK(!thingsos_page_checkpoint_load(path,55,176,&loaded,&count,&done));
    CHECK(!thingsos_page_checkpoint_save(path,55,175,p,5,2)); /* excludes unfinished chapter */
    CHECK(!thingsos_page_checkpoint_save(path,55,2,p,4,2)); /* not a full index */
    p[2].offset=3;CHECK(!thingsos_page_checkpoint_save(path,55,175,p,4,2));p[2].offset=0;
    FILE* f=fopen(path,"r+b");CHECK(f && !fseek(f,-1,SEEK_END) && fputc(0xff,f)!=EOF && !fclose(f));
    CHECK(!thingsos_page_checkpoint_load(path,55,175,&loaded,&count,&done));
    CHECK(thingsos_page_cache_save(path,55,p,5));
    CHECK(!thingsos_page_checkpoint_load(path,55,175,&loaded,&count,&done));
    CHECK(thingsos_page_cache_load(path,55,&loaded,&count));free(loaded);remove(path);
    puts("PASS: chapter checkpoint recovery, key/shape/CRC guards and full-index separation");return 0;
}
