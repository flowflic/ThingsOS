// SPDX-License-Identifier: Apache-2.0
#include "thingsos/ime.h"
#include <string.h>
#include "ime_table.inc"
#include "ime_words.inc"
#define WORD_CHOICES 24
#define WORD_COUNT (sizeof(ime_words)/sizeof(*ime_words))
typedef struct {
    unsigned short words[WORD_CHOICES];size_t count,consume,context_bytes;
    const char* chars;bool chars_first;
} choice_plan_t;
static const char* syllable(const char* key) {
    size_t lo=0,hi=sizeof(ime_table)/sizeof(*ime_table);
    while(lo<hi){size_t mid=lo+(hi-lo)/2;int c=strcmp(key,ime_table[mid].syllable);
        if(!c)return ime_table[mid].characters;
        if(c<0)hi=mid;else lo=mid+1;}
    return "";
}
static void ranked(choice_plan_t* p,unsigned index) {
    size_t at=0;
    while(at<p->count && ime_words[p->words[at]].frequency>=ime_words[index].frequency)++at;
    if(at==WORD_CHOICES)return;
    if(p->count<WORD_CHOICES)++p->count;
    for(size_t n=p->count-1;n>at;--n)p->words[n]=p->words[n-1];
    p->words[at]=(unsigned short)index;
}
static size_t previous(const char* text,size_t at) {
    if(at){--at;while(at && ((unsigned char)text[at]&192)==128)--at;}return at;
}
static void context_plan(const thingsos_input_t* i,choice_plan_t* p) {
    if(!i->chinese || !i->cursor)return;
    size_t last=previous(i->text,i->cursor),first=previous(i->text,last);
    for(unsigned pass=0;pass<2;++pass) {
        size_t start=pass?last:first,bytes=i->cursor-start;
        if(bytes<3 || bytes>6)continue;
        char prefix[7];memcpy(prefix,i->text+start,bytes);prefix[bytes]=0;
        size_t lo=0,hi=WORD_COUNT;
        while(lo<hi){size_t mid=lo+(hi-lo)/2;
            if(strcmp(ime_words[ime_context_index[mid]].word,prefix)<0)lo=mid+1;else hi=mid;}
        unsigned scanned=0;
        for(size_t n=lo;n<WORD_COUNT && scanned<384;++n,++scanned) {
            unsigned index=ime_context_index[n];const char* word=ime_words[index].word;
            if(strncmp(word,prefix,bytes))break;
            if(strlen(word)>bytes)ranked(p,index);
        }
        if(p->count){p->context_bytes=bytes;return;}
        if(first==last)break;
    }
}
static void plan(const thingsos_input_t* i,choice_plan_t* p) {
    memset(p,0,sizeof(*p));p->chars="";if(!i)return;
    size_t raw=strlen(i->composition);
    if(!raw){context_plan(i,p);return;}
    char key[THINGSOS_IME_COMPOSITION_LIMIT+1];size_t bytes=0;
    for(size_t n=0;n<raw;++n)if(i->composition[n]!='\'')key[bytes++]=i->composition[n];
    key[bytes]=0;
    size_t n=0;while(n<raw && n<6 && i->composition[n]!='\'')++n;
    for(;n;--n) {
        char head[7];memcpy(head,i->composition,n);head[n]=0;
        const char* found=syllable(head);
        if(found[0]){p->chars=found;p->consume=n+(i->composition[n]=='\'');break;}
    }
    p->chars_first=p->consume==raw && !strchr(i->composition, '\'');
    size_t lo=0,hi=WORD_COUNT;
    while(lo<hi){size_t mid=lo+(hi-lo)/2;if(strcmp(ime_words[mid].key,key)<0)lo=mid+1;else hi=mid;}
    for(size_t at=lo;at<WORD_COUNT && !strcmp(ime_words[at].key,key) && p->count<WORD_CHOICES;++at)ranked(p,(unsigned)at);
    if(!p->count && !p->chars_first && bytes>=3) {
        unsigned scanned=0;
        for(size_t at=lo;at<WORD_COUNT && scanned<384;++at,++scanned) {
            if(strncmp(ime_words[at].key,key,bytes))break;
            ranked(p,(unsigned)at);
        }
    }
}
static bool selected(const thingsos_input_t* i,size_t index,char out[THINGSOS_IME_CANDIDATE_BYTES],size_t* consume,size_t* skip) {
    choice_plan_t p;plan(i,&p);size_t chars=strlen(p.chars)/3;
    if(index>=p.count+chars)return false;
    bool character=p.chars_first?index<chars:index>=p.count;
    *skip=0;
    if(character){size_t at=p.chars_first?index:index-p.count;memcpy(out,p.chars+at*3,3);out[3]=0;*consume=p.consume;}
    else {size_t at=p.chars_first?index-chars:index;strcpy(out,ime_words[p.words[at]].word);*consume=strlen(i->composition);*skip=p.context_bytes;}
    return true;
}
void thingsos_input_clear(thingsos_input_t* i) {
    if(i){i->text[0]=i->composition[0]=0;i->length=i->cursor=i->candidate_page=0;}
}
void thingsos_input_cursor(thingsos_input_t* i,size_t character) {
    if(!i)return;
    i->cursor=0;i->candidate_page=0;
    while(character-- && i->cursor<i->length)thingsos_input_move(i,1);
}
bool thingsos_ime_letter(thingsos_input_t* i,char c) {
    if(!i || ((c<'a'||c>'z') && c!='\''))return false;
    size_t n=strlen(i->composition);
    if(n>=THINGSOS_IME_COMPOSITION_LIMIT || (c=='\'' && (!n || i->composition[n-1]=='\'')))return false;
    i->composition[n]=c;i->composition[n+1]=0;i->candidate_page=0;return true;
}
void thingsos_ime_backspace(thingsos_input_t* i) {
    if(!i)return;
    size_t n=strlen(i->composition);
    if(n)i->composition[n-1]=0;else thingsos_input_backspace(i);
    i->candidate_page=0;
}
size_t thingsos_ime_count(const thingsos_input_t* i) {
    choice_plan_t p;plan(i,&p);return p.count+strlen(p.chars)/3;
}
bool thingsos_ime_candidate(const thingsos_input_t* i,size_t index,char out[THINGSOS_IME_CANDIDATE_BYTES]) {
    size_t consume,skip;return out && selected(i,index,out,&consume,&skip);
}
bool thingsos_ime_commit(thingsos_input_t* i,size_t index) {
    char out[THINGSOS_IME_CANDIDATE_BYTES];size_t consume,skip;
    if(!i || !selected(i,index,out,&consume,&skip) || !thingsos_input_insert(i,out+skip))return false;
    memmove(i->composition,i->composition+consume,strlen(i->composition+consume)+1);i->candidate_page=0;return true;
}
void thingsos_ime_page(thingsos_input_t* i,int direction) {
    if(!i)return;
    size_t count=thingsos_ime_count(i);
    if(direction<0 && i->candidate_page)--i->candidate_page;
    if(direction>0 && (i->candidate_page+1)*THINGSOS_IME_PAGE_SIZE<count)++i->candidate_page;
}
