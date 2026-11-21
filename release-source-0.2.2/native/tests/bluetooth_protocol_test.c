// SPDX-License-Identifier: Apache-2.0
#include "thingsos/bluetooth_protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"Bluetooth protocol line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
int main(void){
 CHECK(thingsos_bt_address("001122AaBBff"));CHECK(!thingsos_bt_address("00:11:22:33:44:55"));CHECK(!thingsos_bt_address("00112233445\r\nAT+MODE=3"));
 thingsos_bt_device_t d={0};
 CHECK(thingsos_bt_parse("AT+BT:10973779E5C0",&d)==BT_LINE_DEVICE);
 char display[18];thingsos_bt_display_address(&d,display);
 CHECK(!strcmp(display,"C0:E5:79:37:97:10"));
 CHECK(!strcmp(d.address,"10973779E5C0")&&!strcmp(thingsos_bt_display_name(&d),"未命名音频设备"));
 CHECK(thingsos_bt_parse("AT+BT:001122aabbff耳机 CONNECT SUCCESS\r",&d)==BT_LINE_DEVICE);
 CHECK(!strcmp(d.address,"001122AABBFF")&&!strcmp(d.name,"耳机 CONNECT SUCCESS"));
 CHECK(thingsos_bt_parse("AT+BT:GG1122AABBFFbad",&d)==BT_LINE_NONE);
 CHECK(thingsos_bt_parse("CONNECT SUCCESS trailing",&d)==BT_LINE_NONE);
 CHECK(thingsos_bt_parse(" CONNECT SUCCESS\r\n",&d)==BT_LINE_CONNECTED);
 CHECK(thingsos_bt_parse("SET MODE 1",&d)==BT_LINE_LOCAL);CHECK(thingsos_bt_parse("SET MODE 2",&d)==BT_LINE_TX);
 CHECK(thingsos_bt_parse("INQUIRING START",&d)==BT_LINE_SCAN_BEGIN);CHECK(thingsos_bt_parse("INQ COMPLETE",&d)==BT_LINE_SCAN_END);
 CHECK(thingsos_bt_parse("CONNECT TIMEOUT",&d)==BT_LINE_TIMEOUT);CHECK(thingsos_bt_parse("DISCONNECTED",&d)==BT_LINE_DISCONNECTED);
 thingsos_bt_framer_t f={0};char out[256];const char* lines="SET MODE 2\r\nCONNECT SUCCESS\r\n";unsigned count=0;
 for(const unsigned char* p=(const unsigned char*)lines;*p;++p)if(thingsos_bt_feed(&f,*p,out))++count;
 CHECK(count==2&&!strcmp(out,"CONNECT SUCCESS\r"));
 for(unsigned i=0;i<400;++i)CHECK(!thingsos_bt_feed(&f,'x',out));CHECK(!thingsos_bt_feed(&f,'\n',out));
 CHECK(!thingsos_bt_feed(&f,0,out));CHECK(!thingsos_bt_feed(&f,'\n',out));
 for(const unsigned char* p=(const unsigned char*)"INQ COMPLETE\n";*p;++p)if(thingsos_bt_feed(&f,*p,out))CHECK(!strcmp(out,"INQ COMPLETE"));
 thingsos_bt_status_t s={0};
 for(unsigned i=0;i<THINGSOS_BT_SAVED;++i){memset(&d,0,sizeof(d));snprintf(d.address,sizeof(d.address),"0011223344%02u",i);snprintf(d.name,sizeof(d.name),"耳机 %u",i);CHECK(thingsos_bt_remember(&s,&d));}
 CHECK(s.saved_count==THINGSOS_BT_SAVED);
 strcpy(d.address,"001122334499");CHECK(!thingsos_bt_remember(&s,&d));CHECK(!strcmp(s.last,"001122334403"));
 strcpy(d.address,"001122334401");strcpy(d.name,"更新名称");CHECK(thingsos_bt_remember(&s,&d));CHECK(s.saved_count==THINGSOS_BT_SAVED&&!strcmp(s.saved[1].name,"更新名称"));
 CHECK(thingsos_bt_forget_record(&s,d.address));CHECK(!s.last[0]&&s.saved_count==3&&!strcmp(s.saved[1].address,"001122334402"));CHECK(!thingsos_bt_forget_record(&s,d.address));
 puts("PASS: bounded BH UART lines, strict status classification, UTF-8 names, canonical addresses, saved-device bounds/deduplication/forget");return 0;
}
