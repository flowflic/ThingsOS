// SPDX-License-Identifier: Apache-2.0
/* Independently written BH UART parser. Protocol strings identified from the
   MIT Metalio board reference; no UI/driver implementation is copied. */
#include "thingsos/bluetooth_protocol.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>
bool thingsos_bt_address(const char* text){
 if(!text||strlen(text)!=12)return false;
 for(unsigned i=0;i<12;++i)if(!isxdigit((unsigned char)text[i]))return false;
 return true;
}
void thingsos_bt_display_address(const thingsos_bt_device_t* d,char out[18]){
 if(!out)return;out[0]=0;if(!d||!thingsos_bt_address(d->address))return;
 snprintf(out,18,"%.2s:%.2s:%.2s:%.2s:%.2s:%.2s",d->address+10,d->address+8,d->address+6,d->address+4,d->address+2,d->address);
}
const char* thingsos_bt_display_name(const thingsos_bt_device_t* d){
 return !d||!d->name[0]||!strcmp(d->name,d->address)?"未命名音频设备":d->name;
}
static void bt_name(char out[96],const char* in){
 size_t used=0;
 while(*in&&used<95){unsigned char c=(unsigned char)*in;size_t n=c<128?1:c>=0xc2&&c<=0xdf?2:c>=0xe0&&c<=0xef?3:c>=0xf0&&c<=0xf4?4:0;
  if(!n||used+n>95)break;
  bool valid=true;for(size_t j=1;j<n;++j)if(!in[j]||((unsigned char)in[j]&0xc0)!=0x80){valid=false;break;}
  if(!valid)break;
  if(n>=3&&((c==0xe0&&(unsigned char)in[1]<0xa0)||(c==0xed&&(unsigned char)in[1]>=0xa0)||(c==0xf0&&(unsigned char)in[1]<0x90)||(c==0xf4&&(unsigned char)in[1]>=0x90)))break;
  if(n==1&&(c<32||c==127)){++in;continue;}
  memcpy(out+used,in,n);used+=n;in+=n;
 }
 while(used&&out[used-1]==' ')--used;out[used]=0;
}
thingsos_bt_line_t thingsos_bt_parse(const char* raw,thingsos_bt_device_t* device){
 if(!raw)return BT_LINE_NONE;while(*raw==' ')++raw;
 char line[256];size_t n=strlen(raw);if(n>=sizeof(line))return BT_LINE_NONE;
 memcpy(line,raw,n+1);while(n&&(line[n-1]=='\r'||line[n-1]=='\n'||line[n-1]==' '))line[--n]=0;
 if(!strncmp(line,"AT+BT:",6)){
  if(n<18||!device)return BT_LINE_NONE;
  memset(device,0,sizeof(*device));memcpy(device->address,line+6,12);
  if(!thingsos_bt_address(device->address))return BT_LINE_NONE;
  for(unsigned i=0;i<12;++i)device->address[i]=(char)toupper((unsigned char)device->address[i]);
  bt_name(device->name,line+18);if(!device->name[0])snprintf(device->name,sizeof(device->name),"%s",device->address);
  return BT_LINE_DEVICE;
 }
 if(!strcmp(line,"SET MODE 1"))return BT_LINE_LOCAL;
 if(!strcmp(line,"SET MODE 2"))return BT_LINE_TX;
 if(!strcmp(line,"INQUIRING START"))return BT_LINE_SCAN_BEGIN;
 if(!strcmp(line,"INQ COMPLETE"))return BT_LINE_SCAN_END;
 if(!strcmp(line,"CONNECT SUCCESS"))return BT_LINE_CONNECTED;
 if(!strcmp(line,"CONNECT TIMEOUT")||!strcmp(line,"CONNECT FAIL")||!strcmp(line,"CONNECT FAILED"))return BT_LINE_TIMEOUT;
 if(!strcmp(line,"DISCONNECT")||!strcmp(line,"DISCONNECTED")||!strcmp(line,"DISCONNECT SUCCESS")||!strcmp(line,"NO CARRIER"))return BT_LINE_DISCONNECTED;
 return BT_LINE_NONE;
}
bool thingsos_bt_feed(thingsos_bt_framer_t* f,unsigned char c,char out[256]){
 if(!f||!out)return false;
 if(c=='\n'){bool ready=!f->overflow&&f->used;f->bytes[f->used]=0;if(ready)memcpy(out,f->bytes,f->used+1);f->used=0;f->overflow=false;return ready;}
 if(!c){f->overflow=true;return false;}
 if(!f->overflow){if(f->used+1<sizeof(f->bytes))f->bytes[f->used++]=(char)c;else f->overflow=true;}
 return false;
}
bool thingsos_bt_remember(thingsos_bt_status_t* s,const thingsos_bt_device_t* d){
 if(!s||!d||!thingsos_bt_address(d->address)||s->saved_count>THINGSOS_BT_SAVED)return false;
 unsigned at=0;while(at<s->saved_count&&strcmp(s->saved[at].address,d->address))++at;
 if(at==s->saved_count){if(at==THINGSOS_BT_SAVED)return false;++s->saved_count;}
 s->saved[at]=*d;s->saved[at].name[95]=0;memcpy(s->last,d->address,13);return true;
}
bool thingsos_bt_forget_record(thingsos_bt_status_t* s,const char* address){
 if(!s||!thingsos_bt_address(address)||s->saved_count>THINGSOS_BT_SAVED)return false;
 for(unsigned i=0;i<s->saved_count;++i)if(!strcmp(s->saved[i].address,address)){
  memmove(s->saved+i,s->saved+i+1,(s->saved_count-i-1)*sizeof(*s->saved));memset(s->saved+--s->saved_count,0,sizeof(*s->saved));
  if(!strcmp(s->last,address))s->last[0]=0;return true;
 }return false;
}
