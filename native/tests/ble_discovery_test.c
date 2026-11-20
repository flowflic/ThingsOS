/* SPDX-License-Identifier: Apache-2.0 */
#include "thingsos/ble_discovery.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"BLE:%d: %s\n",__LINE__,#x);exit(1);}}while(0)
int main(void){
 thingsos_ble_status_t s={0};const uint8_t flags[]={2,1,6};
 CHECK(thingsos_ble_observe(&s,"001122aabbff",0,-60,true,flags,sizeof(flags)));
 CHECK(s.count==1 && !strcmp(s.devices[0].device.name,"001122AABBFF"));
 const uint8_t name[]={9,9,'K','e','y','b','o','a','r','d'};
 CHECK(thingsos_ble_observe(&s,"001122AABBFF",0,-52,false,name,sizeof(name)));
 CHECK(s.count==1 && !strcmp(s.devices[0].device.name,"Keyboard") && s.devices[0].complete_name && s.devices[0].connectable);
 const uint8_t short_name[]={4,8,'K','e','y'};
 CHECK(!thingsos_ble_observe(&s,"001122AABBFF",0,-52,false,short_name,sizeof(short_name)));
 CHECK(!strcmp(s.devices[0].device.name,"Keyboard"));
 CHECK(thingsos_ble_observe(&s,"001122AABBFF",1,-80,false,flags,sizeof(flags)) && s.count==2);
 const uint8_t malformed[]={8,9,'x'};
 CHECK(!thingsos_ble_observe(&s,"102233445566",0,-55,true,malformed,sizeof(malformed)) && s.count==2);
 CHECK(!thingsos_ble_observe(&s,"invalid",0,-55,true,name,sizeof(name)));
 CHECK(!thingsos_ble_observe(&s,"102233445566",0,-55,true,NULL,1));
 for(unsigned i=2;i<THINGSOS_BLE_FOUND;++i){char address[13];snprintf(address,sizeof(address),"00223344%04X",i);CHECK(thingsos_ble_observe(&s,address,0,-40,false,flags,sizeof(flags)));}
 CHECK(s.count==THINGSOS_BLE_FOUND && !s.full);
 CHECK(thingsos_ble_observe(&s,"ABCDEF123456",0,-50,false,flags,sizeof(flags)) && s.full);
 CHECK(!thingsos_ble_observe(&s,"ABCDEF123456",0,-50,false,flags,sizeof(flags)));
 CHECK(!thingsos_ble_observe(&s,"001122AABBFF",0,-42,true,flags,sizeof(flags)));
 CHECK(s.count==THINGSOS_BLE_FOUND && !strcmp(s.devices[0].device.name,"Keyboard") && s.devices[0].rssi==-42);
 CHECK(thingsos_ble_named(s.devices) && !thingsos_ble_named(s.devices+1));
 /* A full list of anonymous broadcasters yields to a named device. */
 const uint8_t remote[]={7,9,'R','e','m','o','t','e'};
 CHECK(thingsos_ble_observe(&s,"112233445566",0,-50,true,remote,sizeof(remote)) && s.count==THINGSOS_BLE_FOUND);
 unsigned at=0;while(at<s.count&&strcmp(s.devices[at].device.address,"112233445566"))++at;
 CHECK(at==1 && !strcmp(s.devices[at].device.name,"Remote") && s.devices[at].connectable);
 CHECK(!strcmp(s.devices[0].device.name,"Keyboard"));
 /* Nameless HID appearance or service gets a readable kind label, which a
    later scan-response name replaces; plain anonymous adverts stay unnamed. */
 thingsos_ble_status_t k={0};
 const uint8_t keyboard[]={2,1,6,3,0x19,0xC1,0x03};
 CHECK(thingsos_ble_observe(&k,"A1B2C3D4E5F6",1,-50,true,keyboard,sizeof(keyboard)));
 CHECK(thingsos_ble_named(k.devices) && !strcmp(k.devices[0].device.name,"\xe6\x9c\xaa\xe5\x91\xbd\xe5\x90\x8d\xe9\x94\xae\xe7\x9b\x98 \xc2\xb7 E5F6"));
 const uint8_t hid_service[]={3,3,0x12,0x18};
 CHECK(thingsos_ble_observe(&k,"0A0B0C0D0E0F",1,-50,true,hid_service,sizeof(hid_service)));
 CHECK(!strcmp(k.devices[1].device.name,"\xe6\x9c\xaa\xe5\x91\xbd\xe5\x90\x8d\xe8\xbe\x93\xe5\x85\xa5\xe8\xae\xbe\xe5\xa4\x87 \xc2\xb7 0E0F"));
 const uint8_t pager[]={7,9,'P','a','g','e','r','1'};
 CHECK(thingsos_ble_observe(&k,"A1B2C3D4E5F6",1,-50,false,pager,sizeof(pager)));
 CHECK(!strcmp(k.devices[0].device.name,"Pager1") && k.devices[0].connectable);
 CHECK(!thingsos_ble_observe(&k,"A1B2C3D4E5F6",1,-50,true,keyboard,sizeof(keyboard)) && !strcmp(k.devices[0].device.name,"Pager1"));
 const uint8_t blank[]={3,9,' ',' '};
 CHECK(!thingsos_ble_observe(&k,"A1B2C3D4E5F6",1,-50,false,blank,sizeof(blank)) && !strcmp(k.devices[0].device.name,"Pager1"));
 CHECK(thingsos_ble_observe(&k,"FFEEDDCCBBAA",1,-50,false,flags,sizeof(flags)) && !thingsos_ble_named(k.devices+2));
 puts("PASS: BLE active-scan name enrichment, canonical/type-aware deduplication, malformed AD rejection, bounded results and explicit saturation");return 0;
}
