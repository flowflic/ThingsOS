/* SPDX-License-Identifier: Apache-2.0 */
#include "thingsos/ime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"IME words line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void type(thingsos_input_t* i,const char* text) {for(;*text;++text)CHECK(thingsos_ime_letter(i,*text));}
static size_t find(thingsos_input_t* i,const char* word) {
    char text[THINGSOS_IME_CANDIDATE_BYTES];size_t count=thingsos_ime_count(i);
    for(size_t n=0;n<count;++n)if(thingsos_ime_candidate(i,n,text) && !strcmp(text,word))return n;
    fprintf(stderr,"Missing candidate: %s composition=%s count=%zu\n",word,i->composition,count);exit(1);
}
int main(void) {
    thingsos_input_t i;
    const char* cases[][2]={{"nihao","你好"},{"xihuan","喜欢"},{"yuedu","阅读"},{"chongqing","重庆"},{"yinhang","银行"},{"zhonghuarenmingongheguo","中华人民共和国"},{"xi'an","西安"}};
    for(size_t n=0;n<sizeof(cases)/sizeof(*cases);++n) {
        CHECK(thingsos_input_open(&i,"",1024,false));i.chinese=true;type(&i,cases[n][0]);
        CHECK(thingsos_ime_commit(&i,find(&i,cases[n][1])) && !strcmp(i.text,cases[n][1]) && !i.composition[0]);
    }
    CHECK(thingsos_input_open(&i,".txt",96,false));thingsos_input_cursor(&i,0);type(&i,"zhongg");
    CHECK(thingsos_ime_commit(&i,find(&i,"中国")) && !strcmp(i.text,"中国.txt"));
    CHECK(thingsos_input_open(&i,"",96,false));i.chinese=true;type(&i,"woaizhongguo");
    CHECK(thingsos_ime_commit(&i,find(&i,"我")) && !strcmp(i.composition,"aizhongguo"));
    CHECK(thingsos_ime_commit(&i,find(&i,"爱")) && !strcmp(i.composition,"zhongguo"));
    CHECK(thingsos_ime_commit(&i,find(&i,"中国")) && !strcmp(i.text,"我爱中国"));
    CHECK(thingsos_input_open(&i,"中.txt",96,false));i.chinese=true;thingsos_input_cursor(&i,1);
    CHECK(thingsos_ime_commit(&i,find(&i,"中国")) && !strcmp(i.text,"中国.txt"));
    CHECK(thingsos_input_open(&i,"",5,false));type(&i,"nihao");
    CHECK(!thingsos_ime_commit(&i,find(&i,"你好")) && !i.length && !strcmp(i.composition,"nihao"));
    thingsos_ime_backspace(&i);CHECK(!strcmp(i.composition,"niha"));
    thingsos_input_clear(&i);CHECK(!i.length && !i.composition[0]);
    CHECK(!thingsos_ime_letter(&i,'\''));type(&i,"xi'");CHECK(!thingsos_ime_letter(&i,'\''));
    thingsos_input_clear(&i);
    for(unsigned n=0;n<THINGSOS_IME_COMPOSITION_LIMIT;++n)CHECK(thingsos_ime_letter(&i,'a'));
    CHECK(!thingsos_ime_letter(&i,'a') && strlen(i.composition)==THINGSOS_IME_COMPOSITION_LIMIT);
    CHECK(!thingsos_ime_count(NULL));
    puts("offline word conversion, disambiguation, continuation, cursor and byte limits passed");return 0;
}
