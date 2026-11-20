/* SPDX-License-Identifier: Apache-2.0 */
#include "thingsos/ble_hid.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"HID:%d: %s\n",__LINE__,#x);exit(1);}}while(0)
static thingsos_hid_layout_t layout;static thingsos_hid_decoder_t decoder;
static thingsos_hid_key_t keys[128];static unsigned count;
static void emit(uint16_t page,uint16_t usage,uint8_t modifiers,bool caps,void* arg){(void)arg;CHECK(count<128);keys[count++]=(thingsos_hid_key_t){.page=page,.usage=usage,.modifiers=modifiers,.caps=caps};}
static const uint8_t keyboard[]={0x05,7,0x19,0xe0,0x29,0xe7,0x15,0,0x25,1,0x75,1,0x95,8,0x81,2,0x75,8,0x95,1,0x81,1,0x19,0,0x29,0x65,0x15,0,0x25,0x65,0x75,8,0x95,6,0x81,0};
int main(void){
 CHECK(thingsos_hid_parse(&layout,keyboard,sizeof(keyboard)));CHECK(layout.reports==1&&layout.report[0].bits==64);
 uint8_t packet[9]={2,0,4,0,0,0,0,0};
 CHECK(thingsos_hid_decode(&layout,&decoder,0,packet,8,emit,NULL));CHECK(count==1&&thingsos_hid_ascii(keys)=='A');
 CHECK(thingsos_hid_decode(&layout,&decoder,0,packet,8,emit,NULL)&&count==1); /* held/repeated notification */
 packet[2]=1;CHECK(!thingsos_hid_decode(&layout,&decoder,0,packet,8,emit,NULL)); /* rollover cannot create keys */
 packet[2]=4;CHECK(thingsos_hid_decode(&layout,&decoder,0,packet,8,emit,NULL)&&count==1);
 memset(packet,0,8);CHECK(thingsos_hid_decode(&layout,&decoder,0,packet,8,emit,NULL));
 packet[2]=0x39;CHECK(thingsos_hid_decode(&layout,&decoder,0,packet,8,emit,NULL));CHECK(decoder.caps);
 packet[2]=4;packet[3]=5;CHECK(thingsos_hid_decode(&layout,&decoder,0,packet,8,emit,NULL));CHECK(count==4&&thingsos_hid_ascii(keys+2)=='A'&&thingsos_hid_ascii(keys+3)=='B');
 packet[2]=5;packet[3]=4;CHECK(thingsos_hid_decode(&layout,&decoder,0,packet,8,emit,NULL)&&count==4);
 CHECK(!thingsos_hid_decode(&layout,&decoder,0,packet,7,emit,NULL));CHECK(!thingsos_hid_decode(&layout,&decoder,3,packet,8,emit,NULL));
 const uint8_t consumer[]={0x05,12,0x85,2,0x15,0,0x26,0xff,3,0x19,0,0x2a,0xff,3,0x75,16,0x95,1,0x81,0};
 memset(&decoder,0,sizeof(decoder));count=0;CHECK(thingsos_hid_parse(&layout,consumer,sizeof(consumer)));
 uint8_t volume[]={0xe9,0};CHECK(thingsos_hid_decode(&layout,&decoder,2,volume,2,emit,NULL));CHECK(count==1&&thingsos_hid_navigation(keys)==THINGSOS_KEY_NEXT);
 CHECK(thingsos_hid_decode(&layout,&decoder,2,volume,2,emit,NULL)&&count==1);
 volume[0]=0;CHECK(thingsos_hid_decode(&layout,&decoder,2,volume,2,emit,NULL));
 uint8_t compatibility[]={2,0xea,0};CHECK(thingsos_hid_decode(&layout,&decoder,2,compatibility,3,emit,NULL));CHECK(count==2&&thingsos_hid_navigation(keys+1)==THINGSOS_KEY_PREVIOUS);
 const uint8_t bitfield[]={0x05,12,0x85,7,0x15,0,0x25,1,0x19,0xe9,0x29,0xea,0x75,1,0x95,2,0x81,2,0x75,6,0x95,1,0x81,1};
 memset(&decoder,0,sizeof(decoder));count=0;CHECK(thingsos_hid_parse(&layout,bitfield,sizeof(bitfield)));uint8_t bit=1;
 CHECK(thingsos_hid_decode(&layout,&decoder,7,&bit,1,emit,NULL)&&count==1&&keys[0].usage==0xe9);bit=2;
 CHECK(thingsos_hid_decode(&layout,&decoder,7,&bit,1,emit,NULL)&&count==2&&keys[1].usage==0xea);
 uint8_t nkro[sizeof(keyboard)];memcpy(nkro,keyboard,sizeof(nkro));
 /* Independent bitmap report: modifier byte then four key bits. */
 const uint8_t bitmap[]={0x05,7,0x19,0xe0,0x29,0xe7,0x15,0,0x25,1,0x75,1,0x95,8,0x81,2,0x19,4,0x29,7,0x75,1,0x95,4,0x81,2,0x75,4,0x95,1,0x81,1};
 memset(&decoder,0,sizeof(decoder));count=0;CHECK(thingsos_hid_parse(&layout,bitmap,sizeof(bitmap)));uint8_t bitmap_packet[]={2,5};
 CHECK(thingsos_hid_decode(&layout,&decoder,0,bitmap_packet,2,emit,NULL)&&count==2&&thingsos_hid_ascii(keys)=='A'&&thingsos_hid_ascii(keys+1)=='C');
 thingsos_hid_key_t punctuation={.page=7,.usage=0x34};CHECK(thingsos_hid_ascii(&punctuation)=='\'');punctuation.modifiers=2;CHECK(thingsos_hid_ascii(&punctuation)=='"');punctuation.modifiers=1;CHECK(!thingsos_hid_ascii(&punctuation));
 punctuation.modifiers=0;punctuation.usage=0x62;CHECK(thingsos_hid_ascii(&punctuation)=='0');punctuation.usage=0x4e;CHECK(thingsos_hid_navigation(&punctuation)==THINGSOS_KEY_NEXT);
 const uint8_t badpop[]={0xb4},badsize[]={0x75,33,0x95,1,0x81,2},badlong[]={0xfe,5,0,1},badid[]={0x85,0};
 CHECK(!thingsos_hid_parse(&layout,badpop,sizeof(badpop)));CHECK(!thingsos_hid_parse(&layout,badsize,sizeof(badsize)));CHECK(!thingsos_hid_parse(&layout,badlong,sizeof(badlong)));CHECK(!thingsos_hid_parse(&layout,badid,sizeof(badid)));
 for(size_t n=1;n<sizeof(keyboard);++n){bool valid=thingsos_hid_parse(&layout,keyboard,n);if(valid)CHECK(layout.fields<=THINGSOS_HID_FIELDS&&layout.reports<=THINGSOS_HID_REPORTS);}
 uint32_t random=0x213671;uint8_t fuzz[128];for(unsigned test=0;test<2000;++test){for(unsigned i=0;i<sizeof(fuzz);++i){random^=random<<13;random^=random>>17;random^=random<<5;fuzz[i]=(uint8_t)random;}thingsos_hid_parse(&layout,fuzz,1+test%sizeof(fuzz));}
 puts("PASS: keyboard arrays/bitmap, modifiers/caps, consumer arrays/bitfields, report IDs, held-key suppression, malformed/truncated maps and bounded fuzz");return 0;
}
