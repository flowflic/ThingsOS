/* SPDX-License-Identifier: Apache-2.0 */
/* Independently written bounded HID short-item/report decoder. Usage numbers
   are standard HID values also identified in the pinned TinyUSB hid.h (MIT).
   No device- or legacy-driver implementation is copied. */
#include "thingsos/ble_hid.h"
#include <string.h>
typedef struct {uint32_t page,size,count,id;int32_t logical_min;} globals_t;
static uint32_t number(const uint8_t* p,unsigned n){uint32_t v=0;for(unsigned i=0;i<n;++i)v|=(uint32_t)p[i]<<(8*i);return v;}
static int report_index(thingsos_hid_layout_t* l,uint8_t id){
 for(unsigned i=0;i<l->reports;++i)if(l->report[i].id==id)return (int)i;
 if(l->reports==THINGSOS_HID_REPORTS)return -1;
 unsigned i=l->reports++;l->report[i]=(thingsos_hid_report_t){id,0};return (int)i;
}
bool thingsos_hid_parse(thingsos_hid_layout_t* l,const uint8_t* data,size_t bytes){
 if(!l || !data || !bytes || bytes>2048)return false;
 memset(l,0,sizeof(*l));globals_t g={0},stack[8];unsigned depth=0,used=0;uint32_t usages[32],minimum=0,maximum=0;bool range=false;
 for(size_t at=0;at<bytes;){uint8_t prefix=data[at++];
  if(prefix==0xfe){if(bytes-at<2)return false;unsigned len=data[at];at+=2;if(len>bytes-at)return false;at+=len;continue;}
  unsigned n=prefix&3;if(n==3)n=4;if(n>bytes-at)return false;
  uint32_t v=number(data+at,n);at+=n;unsigned type=(prefix>>2)&3,tag=prefix>>4;
  if(type==1){
   switch(tag){case 0:g.page=v;break;case 1:g.logical_min=n==1?(int8_t)v:n==2?(int16_t)v:(int32_t)v;break;
    case 7:g.size=v;break;case 8:if(!v||v>255)return false;g.id=v;break;case 9:g.count=v;break;
    case 10:if(depth==8)return false;stack[depth++]=g;break;case 11:if(!depth)return false;g=stack[--depth];break;default:break;}
  }else if(type==2){
   if(tag==0){if(used==32)return false;usages[used++]=n==4?v:(g.page<<16)|v;}
   else if(tag==1){minimum=n==4?v:(g.page<<16)|v;range=true;}
   else if(tag==2)maximum=n==4?v:(g.page<<16)|v;
  }else if(type==0){
   if(tag==8){
    if(!g.size||g.size>32||!g.count||g.count>256||g.page>65535||g.logical_min<-32768||g.logical_min>32767)return false;
    int r=report_index(l,(uint8_t)g.id);if(r<0)return false;
    uint32_t offset=l->report[r].bits,total=g.size*g.count;if(total>2048-offset)return false;
    if(!(v&1)){
     bool variable=(v&2)!=0;
     for(unsigned i=0;i<(variable?g.count:1);++i){
      uint32_t usage=used?usages[i<used?i:used-1]:range?minimum+i:(g.page<<16);
      if(range && (!maximum || (minimum>>16)!=(maximum>>16) || maximum<minimum))return false;
      if(range && variable && usage>maximum)usage=maximum;
      uint16_t page=(uint16_t)(usage>>16);
      if(page!=7 && page!=12)continue;
      if(l->fields==THINGSOS_HID_FIELDS)return false;
      l->field[l->fields++]=(thingsos_hid_field_t){(uint16_t)(offset+i*g.size),page,(uint16_t)usage,(uint16_t)(variable?1:g.count),(int16_t)g.logical_min,(uint8_t)g.size,(uint8_t)r,variable};
     }
    }
    l->report[r].bits=(uint16_t)(offset+total);
   }
   used=0;minimum=maximum=0;range=false;
  }
 }
 return !depth && l->fields>0;
}
static uint32_t bits(const uint8_t* p,unsigned offset,unsigned size){uint32_t v=0;for(unsigned i=0;i<size;++i)v|=(uint32_t)((p[(offset+i)/8]>>((offset+i)%8))&1)<<i;return v;}
static bool contains(const thingsos_hid_pressed_t* p,uint32_t code){for(unsigned i=0;i<p->count;++i)if(p->pressed[i]==code)return true;return false;}
bool thingsos_hid_decode(const thingsos_hid_layout_t* l,thingsos_hid_decoder_t* decoder,uint8_t id,const uint8_t* data,size_t bytes,thingsos_hid_emit_t emit,void* arg){
 if(!l||!decoder||!data||!emit||bytes>257||l->reports>THINGSOS_HID_REPORTS||l->fields>THINGSOS_HID_FIELDS)return false;
 unsigned r=0;while(r<l->reports&&l->report[r].id!=id)++r;if(r==l->reports)return false;
 unsigned required=(l->report[r].bits+7)/8;
 if(id && bytes==required+1 && data[0]==id){++data;--bytes;}
 if(bytes!=required||decoder->report[r].count>THINGSOS_HID_PRESSED)return false;
 thingsos_hid_pressed_t current={0};uint8_t modifiers=0;bool has_modifiers=false;
 for(unsigned i=0;i<l->fields;++i){const thingsos_hid_field_t* f=l->field+i;if(f->report!=r)continue;
  if(!f->size||f->size>32||!f->count||(uint32_t)f->offset+(uint32_t)f->size*f->count>l->report[r].bits)return false;
  if(f->page==7 && f->variable && f->usage>=0xe0 && f->usage<=0xe7)has_modifiers=true;
  for(unsigned j=0;j<f->count;++j){uint32_t value=bits(data,f->offset+j*f->size,f->size);if(!value)continue;
   int64_t usage=f->variable?f->usage:(int64_t)value+f->usage-f->logical_min;
   if(usage<0||usage>65535)return false;
   if(f->page==7 && usage>=1 && usage<=3)return false; /* rollover/error report */
   if(f->page==7 && usage>=0xe0 && usage<=0xe7){modifiers|=1u<<(usage-0xe0);continue;}
   uint32_t code=((uint32_t)f->page<<16)|(uint16_t)usage;
   if(!contains(&current,code)){if(current.count==THINGSOS_HID_PRESSED)return false;current.pressed[current.count++]=code;}
  }
 }
 if(has_modifiers)decoder->modifiers=modifiers;
 for(unsigned i=0;i<current.count;++i){uint32_t code=current.pressed[i];if(contains(decoder->report+r,code))continue;
  if(code==0x70039)decoder->caps=!decoder->caps;
  emit((uint16_t)(code>>16),(uint16_t)code,decoder->modifiers,decoder->caps,arg);
 }
 decoder->report[r]=current;return true;
}
char thingsos_hid_ascii(const thingsos_hid_key_t* k){
 if(!k||k->page!=7 || (k->modifiers&0xdd))return 0; /* Ctrl/Alt/GUI shortcuts */
 bool shift=(k->modifiers&0x22)!=0;
 if(k->usage>=4 && k->usage<=29)return (char)(((shift!=k->caps)?'A':'a')+k->usage-4);
 if(k->usage>=30 && k->usage<=39)return (shift?"!@#$%^&*()":"1234567890")[k->usage-30];
 if(k->usage==0x2c)return ' ';
 if(k->usage>=0x59&&k->usage<=0x62)return "1234567890"[k->usage-0x59];
 if(k->usage>=0x54&&k->usage<=0x57)return "/*-+"[k->usage-0x54];
 if(k->usage==0x63)return '.';
 if(k->usage>=0x2d && k->usage<=0x38)return (shift?"_+{}|~:\"~<>?":"-=[]\\#;'`,./")[k->usage-0x2d];
 return 0;
}
thingsos_key_action_t thingsos_hid_navigation(const thingsos_hid_key_t* k){
 if(!k || (k->modifiers&0xdd))return THINGSOS_KEY_NONE;
 if(k->page==7){switch(k->usage){case 0x4b:case 0x50:case 0x52:return THINGSOS_KEY_PREVIOUS;
  case 0x28:case 0x2c:case 0x4e:case 0x4f:case 0x51:return THINGSOS_KEY_NEXT;
  case 0x29:case 0x65:return THINGSOS_KEY_MENU;case 0x4a:return THINGSOS_KEY_HOME;default:break;}}
 if(k->page==12){switch(k->usage){case 0xea:case 0xb6:case 0x224:return THINGSOS_KEY_PREVIOUS;
  case 0xe9:case 0xb5:case 0x225:return THINGSOS_KEY_NEXT;case 0xcd:return THINGSOS_KEY_MENU;default:break;}}
 return THINGSOS_KEY_NONE;
}
