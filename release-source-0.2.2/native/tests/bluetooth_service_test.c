// SPDX-License-Identifier: Apache-2.0
#define THINGSOS_BT_HOST_TEST 1
#include "../firmware/components/thingsos_bluetooth/bluetooth.c"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"Bluetooth service line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
int main(void){
 thingsos_bluetooth_init();CHECK(initialized&&!state.enabled&&state.automatic);
 thingsos_bt_device_t device={"001122334455","测试耳机"};
 /* Unified search and saved-device connect activate audio from local mode;
    a discovered headset may be selected before inquiry finishes. */
 CHECK(thingsos_bluetooth_scan(true));command_t unified;CHECK(xQueueReceive(commands,&unified,0)&&unified.kind==SCAN);atomic_fetch_sub(&route_requests,1);
 CHECK(scan_begin()&&state.enabled&&state.phase==THINGSOS_BT_SCANNING);
 CHECK(thingsos_bluetooth_connect(&device));CHECK(xQueueReceive(commands,&unified,0)&&unified.kind==CONNECT);atomic_fetch_sub(&route_requests,1);
 state.enabled=false;publish(THINGSOS_BT_OFF,"");CHECK(thingsos_bluetooth_connect(&device));CHECK(xQueueReceive(commands,&unified,0)&&unified.kind==CONNECT);atomic_fetch_sub(&route_requests,1);
 host_bt_inquiries=0;
 event(BT_LINE_CONNECTED,&device);CHECK(!thingsos_bluetooth_connected()&&!host_bt_wireless);
 host_bt_ack=false;state.enabled=true;CHECK(!transmitter());CHECK(state.phase==THINGSOS_BT_ERROR&&!state.connected&&!host_bt_wireless);
 CHECK(!thingsos_bluetooth_audio_ready(false)&&!thingsos_bluetooth_audio_ready(true));
 host_bt_ack=true;CHECK(connect_device(&device));CHECK(state.phase==THINGSOS_BT_CONNECTING&&!state.connected&&!host_bt_wireless);
 CHECK(!strcmp(host_bt_last_command,"AT+CONNECT=001122334455\r\n"));
 CHECK(!state.saved_count);event(BT_LINE_CONNECTED,&device);
 CHECK(thingsos_bluetooth_connected()&&host_bt_wireless&&state.saved_count==1&&host_bt_store_bytes==sizeof(stored_t));
 CHECK(thingsos_bluetooth_audio_ready(false)&&!thingsos_bluetooth_audio_ready(true));
 event(BT_LINE_DISCONNECTED,&device);CHECK(!state.connected&&!host_bt_wireless&&thingsos_bluetooth_audio_ready(false)&&thingsos_bluetooth_audio_ready(true)&&state.saved_count==1);
 /* A headset that powers off never sends a disconnect line; the audio path sees
    the I2S master stop clocking and the service applies that on its own task. */
 CHECK(connect_device(&device));event(BT_LINE_CONNECTED,&device);CHECK(state.connected&&host_bt_wireless);
 thingsos_bluetooth_link_lost();CHECK(state.connected&&host_bt_wireless);
 apply_link_lost();CHECK(!state.connected&&!host_bt_wireless&&strstr(state.message,"连接断开")&&thingsos_bluetooth_audio_ready(false));
 thingsos_bluetooth_link_lost();apply_link_lost();CHECK(!state.connected&&!host_bt_wireless);
 memset(&state,0,sizeof(state));load();CHECK(state.saved_count==1&&state.enabled&&state.automatic&&!strcmp(state.last,device.address));
 host_bt_store[30]^=1;memset(&state,0,sizeof(state));load();CHECK(!state.saved_count&&!state.enabled);host_bt_store[30]^=1;
 memset(&state,0,sizeof(state));load();publish(THINGSOS_BT_READY,"");
 CHECK(thingsos_bluetooth_audio_ready(true)&&thingsos_bluetooth_audio_ready(false)); /* enabled-idle is local playback */
 thingsos_bt_device_t second={"AABBCCDDEEFF","另一副耳机"};CHECK(connect_device(&second));host_bt_nvs_fail=true;event(BT_LINE_CONNECTED,&second);
 CHECK(state.connected&&state.saved_count==1&&!strcmp(state.last,device.address));CHECK(strstr(state.message,"保存失败"));host_bt_nvs_fail=false;
 host_bt_audio_active=true;host_bt_audio_suspended=false;CHECK(!thingsos_bluetooth_power(false)&&!uxQueueMessagesWaiting(commands)&&!atomic_load(&route_requests));
 host_bt_audio_suspended=true;CHECK(thingsos_bluetooth_power(false));CHECK(atomic_load(&route_requests)==1&&!thingsos_bluetooth_audio_ready(false));
 worker(NULL);CHECK(!state.enabled&&!task&&!host_bt_wireless&&!atomic_load(&route_requests));CHECK(thingsos_bluetooth_audio_ready(false)&&thingsos_bluetooth_audio_ready(true));
 host_bt_audio_active=false;host_bt_audio_suspended=false;host_bt_task_fail=true;
 CHECK(!thingsos_bluetooth_power(true));CHECK(!atomic_load(&route_requests)&&!uxQueueMessagesWaiting(commands));host_bt_task_fail=false;
 state.enabled=false;publish(THINGSOS_BT_OFF,"");CHECK(thingsos_bluetooth_forget(device.address));worker(NULL);
 CHECK(!state.saved_count&&!state.last[0]);memset(&state,0,sizeof(state));load();CHECK(!state.saved_count&&!state.last[0]);
 /* A user scan can supersede a stale reconnect, and results in later inquiry
    rounds enrich the same list rather than being dropped after 18 seconds. */
 state.enabled=true;CHECK(connect_device(&device));CHECK(thingsos_bluetooth_scan(true));
 command_t command;CHECK(xQueueReceive(commands,&command,0) && command.kind==SCAN);atomic_fetch_sub(&route_requests,1);
 CHECK(scan_begin() && scan_round==0 && host_bt_inquiries==0 && scan_wait_start && !pending.address[0]);
 /* MODE acknowledgement is followed by module-owned reconnects. Do not send
    inquiry until fallback START, and do not reset modes on its completion. */
 event(BT_LINE_TIMEOUT,NULL);event(BT_LINE_TIMEOUT,NULL);
 CHECK(state.phase==THINGSOS_BT_SCANNING && host_bt_inquiries==0);
 event(BT_LINE_SCAN_END,NULL);CHECK(scan_wait_start&&!scan_next);
 event(BT_LINE_SCAN_BEGIN,NULL);CHECK(scan_round==1&&!scan_wait_start);
 for(unsigned i=0;i<24;++i){char line[150];int n=snprintf(line,sizeof(line),"AT+BT:00112233%04XNew headset number %u with a long advertised name\r\n",i,i);memcpy(host_bt_uart+host_bt_uart_used,line,n);host_bt_uart_used+=n;}
 memcpy(host_bt_uart+host_bt_uart_used,"INQ COMPLETE\r\n",14);host_bt_uart_used+=14;
 CHECK(receive(BT_LINE_NONE,0) && !host_bt_uart_used && state.found_count==24 && scan_next);
 CHECK(state.phase==THINGSOS_BT_SCANNING && !state.connected && !host_bt_wireless);
 host_bt_time=scan_next_at;scan_poll();CHECK(scan_round==2 && host_bt_inquiries==1 && state.found_count==24);
 thingsos_bt_device_t late={"AABBCCDDEE01","Late pairing headset"};event(BT_LINE_DEVICE,&late);CHECK(state.found_count==25);
 event(BT_LINE_SCAN_END,NULL);host_bt_time=scan_next_at;scan_poll();CHECK(scan_round==3 && host_bt_inquiries==2);
 event(BT_LINE_SCAN_END,NULL);CHECK(state.phase==THINGSOS_BT_READY && state.found_count==25);
 event(BT_LINE_SCAN_END,NULL);host_bt_time+=60000;scan_poll();CHECK(host_bt_inquiries==2);
 transmitter_mode=false;CHECK(scan_begin());event(BT_LINE_DEVICE,&late);host_bt_time=deadline;scan_poll();CHECK(scan_round==1 && !scan_wait_start && state.found_count==1);
 host_bt_time=deadline;scan_poll();CHECK(scan_wait_start&&scan_round==1&&state.found_count==1);
 event(BT_LINE_SCAN_BEGIN,NULL);CHECK(!scan_wait_start&&scan_round==2);
 const char* freeclip="AT+BT:AABBCCDDEE02HUAWEI FreeClip\r\n";
 memcpy(host_bt_uart+host_bt_uart_used,freeclip,strlen(freeclip));host_bt_uart_used+=strlen(freeclip);
 CHECK(receive(BT_LINE_NONE,0)&&state.found_count==2);
 CHECK(!strcmp(state.found[1].name,"HUAWEI FreeClip"));
 CHECK(transmitter());unsigned stopped=host_bt_inquiries;host_bt_time+=60000;scan_poll();CHECK(host_bt_inquiries==stopped);
 /* Reproduce the captured boot sequence: reconnect times out; the module
    autonomously emits START/DEVICE/COMPLETE while the host used to stay READY. */
 state.enabled=state.automatic=true;state.connected=false;state.found_count=0;scan_suppressed=false;
 publish(THINGSOS_BT_READY,"");event(BT_LINE_SCAN_BEGIN,NULL);
 CHECK(state.phase==THINGSOS_BT_SCANNING && scan_round==3);
 event(BT_LINE_DEVICE,&late);CHECK(state.found_count==1);
 event(BT_LINE_SCAN_END,NULL);CHECK(state.phase==THINGSOS_BT_READY && state.found_count==1 && host_bt_inquiries==stopped);
 scan_suppressed=true;event(BT_LINE_SCAN_BEGIN,NULL);CHECK(state.phase==THINGSOS_BT_READY);
 scan_suppressed=false;state.automatic=false;event(BT_LINE_SCAN_BEGIN,NULL);CHECK(state.phase==THINGSOS_BT_READY);
 state.automatic=true;publish(THINGSOS_BT_CONNECTING,"");event(BT_LINE_SCAN_BEGIN,NULL);CHECK(state.phase==THINGSOS_BT_CONNECTING);
 /* Official mode-2 connection uses the raw discovered address without mode
    reset, even during inquiry. A timeout must not try an invented address. */
 thingsos_bt_device_t clip={"10973779E5C0","HUAWEI FreeClip"};
 CHECK(transmitter());publish(THINGSOS_BT_SCANNING,"");
 unsigned modes=host_bt_mode_commands;
 CHECK(scan_begin()&&host_bt_mode_commands==modes&&!scan_wait_start&&scan_round==1);
 CHECK(connect_device(&clip)&&host_bt_mode_commands==modes);
 CHECK(!strcmp(host_bt_last_command,"AT+CONNECT=10973779E5C0\r\n"));
 event(BT_LINE_TIMEOUT,NULL);CHECK(state.phase==THINGSOS_BT_READY&&!pending.address[0]&&!state.connected);
 CHECK(host_bt_mode_commands==modes&&!strcmp(host_bt_last_command,"AT+CONNECT=10973779E5C0\r\n"));
 CHECK(connect_device(&clip));event(BT_LINE_CONNECTED,NULL);
 CHECK(state.connected&&host_bt_wireless&&!strcmp(state.current.address,clip.address));
 puts("PASS: production BH service: mode acknowledgements, confirmed links only, durable records/CRC, save rollback, audio route gating, link loss, official direct mode-2 connection, task failure and forget persistence");
 free(commands);return 0;
}
