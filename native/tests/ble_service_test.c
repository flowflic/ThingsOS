/* SPDX-License-Identifier: Apache-2.0 */
#define THINGSOS_BLE_HOST_TEST 1
#include "../firmware/components/thingsos_ble/ble_scan.c"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"BLE service:%d: %s\n",__LINE__,#x);exit(1);}}while(0)
static const struct ble_gatt_error ok={0},done={BLE_HS_EDONE};
static const uint8_t keyboard[]={0x05,7,0x19,0xe0,0x29,0xe7,0x15,0,0x25,1,0x75,1,0x95,8,0x81,2,0x75,8,0x95,1,0x81,1,0x19,0,0x29,0x65,0x15,0,0x25,0x65,0x75,8,0x95,6,0x81,0};
static void connect_success(void){struct ble_gap_event e={.type=BLE_GAP_EVENT_CONNECT};e.connect.conn_handle=host_ble_conn;gap_event(&e,host_ble_connect_arg);CHECK(state.phase==THINGSOS_BLE_PAIRING&&state.connected&&!state.hid_ready);}
static void encrypted(void){host_ble_peer.sec_state.encrypted=host_ble_peer.sec_state.bonded=true;struct ble_gap_event e={.type=BLE_GAP_EVENT_ENC_CHANGE};e.enc_change.conn_handle=host_ble_conn;gap_event(&e,host_ble_connect_arg);CHECK(state.phase==THINGSOS_BLE_DISCOVERING);}
static void disconnect_event(void){struct ble_gap_event e={.type=BLE_GAP_EVENT_DISCONNECT};e.disconnect.conn.conn_handle=host_ble_conn;gap_event(&e,host_ble_connect_arg);}
static void input_packet(uint8_t key){struct os_mbuf om={.len=8};om.data[2]=key;struct ble_gap_event e={.type=BLE_GAP_EVENT_NOTIFY_RX};e.notify_rx.conn_handle=host_ble_conn;e.notify_rx.attr_handle=7;e.notify_rx.om=&om;gap_event(&e,host_ble_connect_arg);}
static void discover_keyboard(void){
 struct ble_gatt_svc svc={.start_handle=1,.end_handle=50};CHECK(host_ble_service_cb(host_ble_conn,&ok,&svc,host_ble_gatt_arg)==0);
 host_ble_service_cb(host_ble_conn,&done,NULL,host_ble_gatt_arg);CHECK(hid->operation==OP_CHARACTERISTIC);
 uint16_t uuids[]={0x2a4b,0x2a4e,0x2a4d,0x2a4a};uint16_t defs[]={2,4,6,11};uint8_t props[]={2,4,0x12,2};
 for(unsigned i=0;i<4;++i){struct ble_gatt_chr chr={.def_handle=defs[i],.val_handle=defs[i]+1,.properties=props[i]};chr.uuid.u16=(ble_uuid16_t)BLE_UUID16_INIT(uuids[i]);host_ble_chr_cb(host_ble_conn,&ok,&chr,host_ble_gatt_arg);}
 host_ble_chr_cb(host_ble_conn,&done,NULL,host_ble_gatt_arg);CHECK(hid->operation==OP_MAP&&host_ble_attribute==3);
 /* Default ATT MTU 23 fragments this report map into 22-byte read responses. */
 struct os_mbuf map={.len=22};memcpy(map.data,keyboard,22);struct ble_gatt_attr attr={.handle=3,.om=&map};
 host_ble_attr_cb(host_ble_conn,&ok,&attr,host_ble_gatt_arg);CHECK(hid->raw_size==22);
 map.len=sizeof(keyboard)-22;memcpy(map.data,keyboard+22,map.len);attr.offset=22;
 host_ble_attr_cb(host_ble_conn,&ok,&attr,host_ble_gatt_arg);host_ble_attr_cb(host_ble_conn,&done,NULL,host_ble_gatt_arg);
 CHECK(hid->operation==OP_DESCRIPTOR&&host_ble_attribute==7&&host_ble_end==10);
 struct ble_gatt_dsc dsc={.handle=8};dsc.uuid.u16=(ble_uuid16_t)BLE_UUID16_INIT(0x2902);host_ble_dsc_cb(host_ble_conn,&ok,7,&dsc,host_ble_gatt_arg);
 dsc.handle=9;dsc.uuid.u16=(ble_uuid16_t)BLE_UUID16_INIT(0x2908);host_ble_dsc_cb(host_ble_conn,&ok,7,&dsc,host_ble_gatt_arg);
 host_ble_dsc_cb(host_ble_conn,&done,7,NULL,host_ble_gatt_arg);CHECK(hid->operation==OP_REFERENCE&&host_ble_attribute==9);
 struct os_mbuf reference={.len=2};reference.data[1]=1;attr=(struct ble_gatt_attr){.handle=9,.om=&reference};host_ble_attr_cb(host_ble_conn,&ok,&attr,host_ble_gatt_arg);
 CHECK(hid->operation==OP_SUBSCRIBE&&host_ble_attribute==8&&host_ble_write_len==2&&host_ble_write[0]==1);
 host_ble_attr_cb(host_ble_conn,&ok,NULL,host_ble_gatt_arg);CHECK(state.phase==THINGSOS_BLE_CONNECTED&&state.hid_ready&&state.saved_count==1);
}
int main(void){
 thingsos_ble_init();CHECK(commands&&inputs&&!atomic_load(&initialized)&&!state.saved_count);
 /* A large RTC heap does not satisfy controller DMA allocations. Reject both
    low total DMA memory and fragmentation without entering abort-prone SDK init. */
 host_ble_dma_free=32u*1024u;CHECK(thingsos_ble_scan(true));host_ble_initializer(NULL);
 CHECK(!host_ble_port_inits&&!atomic_load(&initialized)&&!commands->count&&state.phase==THINGSOS_BLE_ERROR&&!state.scanning);
 host_ble_dma_free=96u*1024u;host_ble_dma_largest=20u*1024u;CHECK(thingsos_ble_scan(true));host_ble_initializer(NULL);
 CHECK(!host_ble_port_inits&&!atomic_load(&initialized)&&!commands->count);
 CHECK(!host_ble_peak_windows); /* refused before init: no peak window */
 /* 2026-11-22 field case just under the old 64 KiB total stays refused only
    below the measured 56 KiB budget; 35 KiB contiguous is too fragmented. */
 host_ble_dma_free=55u*1024u;host_ble_dma_largest=48u*1024u;CHECK(thingsos_ble_scan(true));host_ble_initializer(NULL);
 CHECK(!host_ble_port_inits&&!atomic_load(&initialized)&&!commands->count);
 host_ble_dma_free=96u*1024u;host_ble_dma_largest=35u*1024u;CHECK(thingsos_ble_scan(true));host_ble_initializer(NULL);
 CHECK(!host_ble_port_inits&&!atomic_load(&initialized)&&!commands->count&&!host_ble_peak_windows);
 /* The measured field case 62,135 / 38,900 B now starts the controller. */
 host_ble_dma_free=62135u;host_ble_dma_largest=38900u;host_ble_host_task_fail=true;CHECK(thingsos_ble_scan(true));host_ble_initializer(NULL);
 /* A failed host task disables the controller but keeps its DMA pools. */
 CHECK(host_ble_port_inits==1&&!host_ble_port_deinits&&host_ble_disables==1&&host_ble_host_deinits==1&&atomic_load(&controller_ready));
 CHECK(host_ble_peak_windows==1); /* the real init is measured once */
 CHECK(!atomic_load(&initialized)&&!commands->count&&state.phase==THINGSOS_BLE_ERROR);
 host_ble_host_task_fail=false;
 /* Restart reuses the allocated controller even when DMA is now short. */
 host_ble_dma_free=16u*1024u;
 CHECK(thingsos_ble_scan(true)&&host_ble_initializer);host_ble_initializer(NULL);
 CHECK(host_ble_port_inits==1&&host_ble_enables==1&&host_ble_host_inits==1&&atomic_load(&running));
 host_ble_dma_free=96u*1024u;CHECK(ble_hs_cfg.sm_bonding&&ble_hs_cfg.sm_sc&&ble_hs_cfg.sm_io_cap==BLE_HS_IO_DISPLAY_YESNO);
 ble_hs_cfg.sync_cb();CHECK(state.scanning);void* old_scan=host_ble_scan_arg;
 CHECK(thingsos_ble_scan(false)&&thingsos_ble_scan(true));tick(NULL);CHECK(state.scanning&&host_ble_scan_arg!=old_scan);
 struct ble_gap_event event={.type=BLE_GAP_EVENT_DISC_COMPLETE};gap_event(&event,old_scan);CHECK(state.scanning);
 uint8_t adv[]={4,9,'K','E','Y'};event=(struct ble_gap_event){.type=BLE_GAP_EVENT_DISC};event.disc.data=adv;event.disc.length_data=sizeof(adv);event.disc.event_type=0;event.disc.rssi=-40;event.disc.addr.val[0]=0xaa;
 gap_event(&event,host_ble_scan_arg);CHECK(state.count==1&&state.devices[0].connectable);
 unsigned signal_serial=state.serial;event.disc.rssi=-47;gap_event(&event,host_ble_scan_arg);
 CHECK(state.serial==signal_serial&&state.devices[0].rssi==-47); /* Signal changes are not UI events. */
 thingsos_ble_device_t device=state.devices[0];
 CHECK(thingsos_ble_connect(&device));tick(NULL);CHECK(state.phase==THINGSOS_BLE_CONNECTING&&!state.hid_ready&&host_ble_connects==1);void* first_token=host_ble_connect_arg;
 connect_success();event=(struct ble_gap_event){.type=BLE_GAP_EVENT_PASSKEY_ACTION};event.passkey.conn_handle=host_ble_conn;event.passkey.params.action=BLE_SM_IOACT_DISP;gap_event(&event,first_token);
 CHECK(state.pairing==THINGSOS_BLE_PAIR_DISPLAY&&state.passkey==537201&&host_ble_sm_io.passkey==537201);
 unsigned injections=host_ble_injections;event.passkey.params.action=BLE_SM_IOACT_NUMCMP;event.passkey.params.numcmp=654321;gap_event(&event,first_token);
 CHECK(state.pairing==THINGSOS_BLE_PAIR_CONFIRM&&host_ble_injections==injections);
 CHECK(thingsos_ble_pair_reply(true,0));tick(NULL);CHECK(host_ble_sm_io.action==BLE_SM_IOACT_NUMCMP&&host_ble_sm_io.numcmp_accept==1);
 event.passkey.params.action=BLE_SM_IOACT_INPUT;gap_event(&event,first_token);CHECK(thingsos_ble_pair_reply(true,765432));tick(NULL);CHECK(host_ble_sm_io.action==BLE_SM_IOACT_INPUT&&host_ble_sm_io.passkey==765432);
 encrypted();discover_keyboard();CHECK(host_ble_blob_size==sizeof(stored_t));
 union ble_store_value value={0};CHECK(ble_hs_cfg.store_write_cb(0,&value)==0&&host_ble_writes==1&&!host_ble_io_held);
 input_packet(0x4e);input_packet(0x4e);thingsos_hid_key_t key;CHECK(thingsos_ble_input(&key)&&thingsos_hid_navigation(&key)==THINGSOS_KEY_NEXT);CHECK(!thingsos_ble_input(&key));
 input_packet(0);host_ble_peer.sec_state.encrypted=false;input_packet(0x4e);CHECK(!thingsos_ble_input(&key));host_ble_peer.sec_state.encrypted=true;input_packet(0x4e);CHECK(thingsos_ble_input(&key));
 input_packet(0);input_packet(0x4b);host_ble_time+=5001;CHECK(!thingsos_ble_input(&key));
 CHECK(thingsos_ble_disconnect());tick(NULL);CHECK(!state.hid_ready&&state.phase==THINGSOS_BLE_DISCONNECTING);input_packet(0x4e);CHECK(!thingsos_ble_input(&key));disconnect_event();CHECK(!state.connected&&state.saved_count==1&&!reconnect_at);
 CHECK(thingsos_ble_connect(&device));tick(NULL);void* second_token=host_ble_connect_arg;CHECK(second_token!=first_token);
 event=(struct ble_gap_event){.type=BLE_GAP_EVENT_CONNECT};event.connect.conn_handle=42;gap_event(&event,first_token);CHECK(host_ble_last_terminated==42&&connection==BLE_HS_CONN_HANDLE_NONE);
 connect_success();encrypted();struct ble_gatt_svc fake={.start_handle=1,.end_handle=50};service(host_ble_conn,&ok,&fake,first_token);CHECK(!hid->services); /* late completion cannot mutate new link */
 host_ble_time=deadline+1;tick(NULL);CHECK(state.phase==THINGSOS_BLE_DISCONNECTING&&!state.hid_ready);disconnect_event();
 CHECK(thingsos_ble_connect(&device));tick(NULL);connect_success();encrypted();host_ble_service_cb(host_ble_conn,&done,NULL,host_ble_gatt_arg);CHECK(state.connected&&!state.hid_ready); /* generic BLE is not claimed to be a keyboard */
 disconnect_event();CHECK(reconnect_at);for(unsigned retry=0;retry<3;++retry){host_ble_time=reconnect_at;tick(NULL);CHECK(reconnect_attempts==retry+1);event=(struct ble_gap_event){.type=BLE_GAP_EVENT_CONNECT};event.connect.status=1;gap_event(&event,host_ble_connect_arg);}CHECK(!reconnect_at);
 CHECK(thingsos_ble_disconnect());tick(NULL);CHECK(!reconnect_at);
 CHECK(thingsos_ble_forget(&device));tick(NULL);CHECK(state.saved_count==0&&!state.last.device.address[0]&&host_ble_deletes==1);
 state.identity=device;host_ble_nvs_fail=true;save_connected();CHECK(!state.saved_count&&strstr(state.message,"保存失败"));host_ble_nvs_fail=false;
 CHECK(thingsos_ble_remember(&state,&device)&&persist());state.saved_count=0;memset(&state.last,0,sizeof(state.last));load_records();CHECK(state.saved_count==1);
 host_ble_blob[24]^=1;state.saved_count=0;memset(&state.last,0,sizeof(state.last));load_records();CHECK(!state.saved_count);host_ble_blob[24]^=1;
 host_ble_io=false;CHECK(locked_write(0,&value)==BLE_HS_ESTORE_FAIL);host_ble_io=true;
 state.phase=THINGSOS_BLE_IDLE;state.connected=false;reconnect_at=0;unsigned timer_resets=host_ble_timer_resets;tick(NULL);CHECK(host_ble_timer_resets==timer_resets); /* no idle 10 Hz polling */
 /* Light sleep: host and controller stop before suspend; wake reconnects the last device. */
 state.automatic=true;state.last=device;CHECK(valid_device(&state.last)&&atomic_load(&running)&&atomic_load(&initialized));
 unsigned disables=host_ble_disables;state.connected=true;connection=host_ble_conn;
 CHECK(thingsos_ble_suspend());
 CHECK(host_ble_port_stops==1&&host_ble_disables==disables+1&&!host_ble_port_deinits&&!atomic_load(&initialized)&&!atomic_load(&running)&&atomic_load(&suspended));
 CHECK(!state.connected&&connection==BLE_HS_CONN_HANDLE_NONE&&state.phase==THINGSOS_BLE_IDLE&&resume_automatic);
 schedule_reconnect();CHECK(!reconnect_at); /* nothing re-arms the radio while suspended */
 host_ble_initializer=NULL;thingsos_ble_resume();CHECK(!atomic_load(&suspended)&&host_ble_initializer&&commands->count==1);
 unsigned inits=host_ble_port_inits,enables=host_ble_enables,connects=host_ble_connects;host_ble_initializer(NULL);
 CHECK(host_ble_port_inits==inits&&host_ble_enables==enables+1&&atomic_load(&running)); /* re-enable, no new DMA pools */
 ble_hs_cfg.sync_cb();CHECK(host_ble_connects==connects+1&&state.phase==THINGSOS_BLE_CONNECTING&&thingsos_ble_same(&state.current,&state.last));
 /* Without automatic reconnect, wake leaves BLE off; a stopped stack suspends as a no-op. */
 state.automatic=false;state.phase=THINGSOS_BLE_IDLE;CHECK(thingsos_ble_suspend()&&host_ble_port_stops==2&&!resume_automatic);
 host_ble_initializer=NULL;thingsos_ble_resume();CHECK(!host_ble_initializer&&!atomic_load(&initialized));
 CHECK(thingsos_ble_suspend()&&host_ble_port_stops==2);thingsos_ble_resume();CHECK(!host_ble_initializer);
 puts("PASS: production BLE scan generations, dynamic/confirmed passkeys, encrypted GATT/CCC readiness, key edges, stale callbacks, deadlines, reconnect bounds, bond deletion and durable records");
 free(hid);free(commands->data);free(commands);free(inputs->data);free(inputs);free(mutable_state);free(published_state);return 0;
}
