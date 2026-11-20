/* SPDX-License-Identifier: Apache-2.0 */
#include "thingsos/ble_discovery.h"
#include "thingsos/bluetooth_protocol.h"
#include <string.h>
#include <stdio.h>
bool thingsos_ble_same(const thingsos_ble_device_t* a,const thingsos_ble_device_t* b){
 return a&&b&&thingsos_bt_address(a->device.address)&&thingsos_bt_address(b->device.address)&&
        (a->address_type&1)==(b->address_type&1)&&!strcmp(a->device.address,b->device.address);
}
bool thingsos_ble_remember(thingsos_ble_status_t* s,const thingsos_ble_device_t* d){
 if(!s||!d||!thingsos_bt_address(d->device.address)||d->address_type>3||s->saved_count>THINGSOS_BLE_SAVED)return false;
 unsigned i=0;while(i<s->saved_count&&!thingsos_ble_same(s->saved+i,d))++i;
 if(i==s->saved_count){if(i==THINGSOS_BLE_SAVED)return false;++s->saved_count;}
 s->saved[i]=*d;s->saved[i].device.name[95]=0;s->saved[i].address_type&=1;s->last=s->saved[i];return true;
}
bool thingsos_ble_forget_record(thingsos_ble_status_t* s,const thingsos_ble_device_t* d){
 if(!s||!d||s->saved_count>THINGSOS_BLE_SAVED)return false;
 for(unsigned i=0;i<s->saved_count;++i)if(thingsos_ble_same(s->saved+i,d)){
  memmove(s->saved+i,s->saved+i+1,(s->saved_count-i-1)*sizeof(*s->saved));memset(s->saved+--s->saved_count,0,sizeof(*s->saved));
  if(thingsos_ble_same(&s->last,d))memset(&s->last,0,sizeof(s->last));return true;
 }return false;
}
bool thingsos_ble_named(const thingsos_ble_device_t* d){
 return d&&d->device.name[0]&&strcmp(d->device.name,d->device.address)!=0;
}
/* Bluetooth Assigned Numbers appearance categories / HID service 0x1812.
   UTF-8 escapes: 键盘, 鼠标, 手柄, 遥控器, 输入设备; label "未命名<kind> · <last 4 hex>". */
static const char* ble_kind(uint16_t appearance,bool hid){
 if(appearance==0x03C1)return "\xe9\x94\xae\xe7\x9b\x98";
 if(appearance==0x03C2)return "\xe9\xbc\xa0\xe6\xa0\x87";
 if(appearance==0x03C3||appearance==0x03C4)return "\xe6\x89\x8b\xe6\x9f\x84";
 if(appearance>>6==0x0180>>6)return "\xe9\x81\xa5\xe6\x8e\xa7\xe5\x99\xa8";
 if(appearance>>6==0x03C0>>6||hid)return "\xe8\xbe\x93\xe5\x85\xa5\xe8\xae\xbe\xe5\xa4\x87";
 return NULL;
}
bool thingsos_ble_observe(thingsos_ble_status_t* s,const char* address,uint8_t type,int8_t rssi,bool connectable,const uint8_t* data,size_t bytes){
 if(!s || !thingsos_bt_address(address) || (!data&&bytes) || s->count>THINGSOS_BLE_FOUND)return false;
 const uint8_t* name=NULL;size_t name_size=0;bool complete=false,hid=false;uint16_t appearance=0;
 for(size_t at=0;at<bytes;){size_t len=data[at++];if(!len)break;if(len>bytes-at)return false;
  if((data[at]==9 || (data[at]==8&&!complete)) && len>1){name=data+at+1;name_size=len-1;complete=data[at]==9;}
  else if(data[at]==0x19 && len>=3)appearance=(uint16_t)(data[at+1]|data[at+2]<<8);
  else if(data[at]==2 || data[at]==3)for(size_t k=1;k+1<len;k+=2)hid|=(data[at+k]|data[at+k+1]<<8)==0x1812;
  at+=len;
 }
 char line[114];snprintf(line,sizeof(line),"AT+BT:%s",address);
 if(name){if(name_size>95)name_size=95;memcpy(line+18,name,name_size);line[18+name_size]=0;}
 thingsos_bt_device_t d;
 if(thingsos_bt_parse(line,&d)!=BT_LINE_DEVICE)return false;
 /* Nameless input peripherals still identify their kind; other nameless
    advertisers keep the address as name and are hidden by list consumers. */
 const char* kind=name&&d.name[0]&&strcmp(d.name,d.address)?NULL:ble_kind(appearance,hid);
 if(kind)snprintf(d.name,sizeof(d.name),"\xe6\x9c\xaa\xe5\x91\xbd\xe5\x90\x8d%s \xc2\xb7 %.4s",kind,d.address+8);
 bool named=strcmp(d.name,d.address)!=0;
 unsigned i=0;while(i<s->count&&(s->devices[i].address_type!=type || strcmp(s->devices[i].device.address,d.address)))++i;
 if(i==s->count){
  if(i==THINGSOS_BLE_FOUND){
   /* Anonymous broadcasters must not crowd out a named device. */
   unsigned j=0;while(named&&j<s->count&&thingsos_ble_named(s->devices+j))++j;
   if(!named||j==s->count){bool changed=!s->full;s->full=true;return changed;}
   i=j;
  }else ++s->count;
  memset(s->devices+i,0,sizeof(s->devices[i]));
 }
 thingsos_ble_device_t old=s->devices[i],*found=s->devices+i;
 if(!old.device.address[0] || (name&&named&&(!old.complete_name||complete)) || (kind&&!thingsos_ble_named(&old)))found->device=d;
 found->address_type=type;found->rssi=rssi;found->connectable|=connectable;found->complete_name|=complete;
 /* Signal samples are not list changes. E-paper consumers must not repaint
    unchanged names/addresses for every received advertisement. */
 return memcmp(&old.device,&found->device,sizeof(old.device))!=0 || old.address_type!=found->address_type || old.connectable!=found->connectable || old.complete_name!=found->complete_name;
}
