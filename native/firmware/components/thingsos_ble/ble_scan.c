/* SPDX-License-Identifier: Apache-2.0 */
/* Independent NimBLE central. Radio/session state belongs to the NimBLE host;
   UI commands and decoded key presses cross bounded queues. No canvas access. */
#include "thingsos/ble_discovery.h"
#include "thingsos/bluetooth_protocol.h"
#ifdef THINGSOS_BLE_HOST_TEST
#include "ble_service_host.h"
#else
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_sm.h"
#include "host/ble_store.h"
#include "host/util/util.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "esp_heap_caps.h"
#include "esp_rom_crc.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
extern SemaphoreHandle_t thingsos_io_lock;
#endif
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef enum {CMD_SCAN,CMD_STOP,CMD_CONNECT,CMD_DISCONNECT,CMD_FORGET,CMD_AUTO,CMD_PAIR} command_kind_t;
typedef struct {command_kind_t kind;bool value;uint32_t passkey,generation;thingsos_ble_device_t device;} command_t;
typedef struct {uint32_t version,automatic,count;thingsos_ble_device_t saved[THINGSOS_BLE_SAVED],last;uint32_t crc;} stored_t;
static thingsos_ble_status_t* mutable_state;
static thingsos_ble_status_t* published_state;
#define state (*mutable_state)
#define visible (*published_state)
static portMUX_TYPE guard=portMUX_INITIALIZER_UNLOCKED;
static QueueHandle_t commands,inputs;
static atomic_bool initialized,synced;
static struct ble_npl_callout timer;
static uint16_t connection=BLE_HS_CONN_HANDLE_NONE;
static uint32_t generation;
static uint32_t scan_generation;
static uint64_t deadline,hard_deadline,reconnect_at;
static unsigned reconnect_attempts;
static bool automatic_attempt,manual_disconnect,forget_pending;
static thingsos_ble_device_t forget_target;
static ble_store_read_fn* store_read;
static ble_store_write_fn* store_write;
static ble_store_delete_fn* store_delete;
static uint64_t clock_ms(void){return esp_timer_get_time()/1000;}
static bool io_take(bool* taken){
 *taken=false;if(!thingsos_io_lock||xSemaphoreGetMutexHolder(thingsos_io_lock)==xTaskGetCurrentTaskHandle())return true;
 if(xSemaphoreTake(thingsos_io_lock,pdMS_TO_TICKS(3000))!=pdTRUE)return false;*taken=true;return true;
}
static void io_give(bool taken){if(taken)xSemaphoreGive(thingsos_io_lock);}
static void publish(const char* text){
 if(text)snprintf(state.message,sizeof(state.message),"%s",text);
 state.generation=generation;++state.serial;portENTER_CRITICAL(&guard);visible=state;portEXIT_CRITICAL(&guard);
 ESP_LOGI("ThingsOS_ble","phase=%u scanning=%d connected=%d hid=%d saved=%u found=%u",(unsigned)state.phase,state.scanning,state.connected,state.hid_ready,state.saved_count,state.count);
}
void thingsos_ble_status(thingsos_ble_status_t* out){if(out){portENTER_CRITICAL(&guard);if(published_state)*out=visible;else{memset(out,0,sizeof(*out));out->automatic=true;}portEXIT_CRITICAL(&guard);}}
static bool persist(void){
 stored_t record={.version=1,.automatic=state.automatic,.count=state.saved_count};
 memcpy(record.saved,state.saved,sizeof(record.saved));record.last=state.last;
 record.crc=esp_rom_crc32_le(0,(const uint8_t*)&record,offsetof(stored_t,crc));bool taken;
 if(!io_take(&taken))return false;nvs_handle_t h;esp_err_t rc=nvs_open("thingsos-ble",NVS_READWRITE,&h);
 if(rc==ESP_OK){rc=nvs_set_blob(h,"devices",&record,sizeof(record));if(rc==ESP_OK)rc=nvs_commit(h);nvs_close(h);}io_give(taken);return rc==ESP_OK;
}
static bool valid_device(const thingsos_ble_device_t* d){return d&&thingsos_bt_address(d->device.address)&&d->address_type<=3&&memchr(d->device.name,0,96);}
static void load_records(void){
 stored_t record={0};size_t size=sizeof(record);bool taken;if(!io_take(&taken))return;
 nvs_handle_t h;esp_err_t rc=nvs_open("thingsos-ble",NVS_READONLY,&h);if(rc==ESP_OK){rc=nvs_get_blob(h,"devices",&record,&size);nvs_close(h);}io_give(taken);
 if(rc!=ESP_OK||size!=sizeof(record)||record.version!=1||record.automatic>1||record.count>THINGSOS_BLE_SAVED||record.crc!=esp_rom_crc32_le(0,(const uint8_t*)&record,offsetof(stored_t,crc)))return;
 bool last=!record.last.device.address[0];
 for(unsigned i=0;i<record.count;++i){if(!valid_device(record.saved+i))return;for(unsigned j=0;j<i;++j)if(thingsos_ble_same(record.saved+i,record.saved+j))return;if(thingsos_ble_same(record.saved+i,&record.last))last=true;}
 if(!last)return;state.automatic=record.automatic;state.saved_count=record.count;memcpy(state.saved,record.saved,sizeof(state.saved));state.last=record.last;
}
static int locked_read(int type,const union ble_store_key* key,union ble_store_value* value){bool taken;if(!io_take(&taken))return BLE_HS_ESTORE_FAIL;int rc=store_read(type,key,value);io_give(taken);return rc;}
static int locked_write(int type,const union ble_store_value* value){bool taken;if(!io_take(&taken))return BLE_HS_ESTORE_FAIL;int rc=store_write(type,value);io_give(taken);return rc;}
static int locked_delete(int type,const union ble_store_key* key){bool taken;if(!io_take(&taken))return BLE_HS_ESTORE_FAIL;int rc=store_delete(type,key);io_give(taken);return rc;}
static int store_full(struct ble_store_status_event* event,void* arg){(void)event;(void)arg;return BLE_HS_ENOMEM;}
static bool busy(void){return state.phase==THINGSOS_BLE_CONNECTING||state.phase==THINGSOS_BLE_PAIRING||state.phase==THINGSOS_BLE_DISCOVERING||state.phase==THINGSOS_BLE_DISCONNECTING;}
static void* token(void){return (void*)(uintptr_t)generation;}
static bool current(uint16_t conn,void* arg){return conn==connection&&(uint32_t)(uintptr_t)arg==generation;}
static void schedule_reconnect(void){
 if(!manual_disconnect&&state.automatic&&valid_device(&state.last)&&reconnect_attempts<3){static const unsigned wait[]={2000,5000,10000};reconnect_at=clock_ms()+wait[reconnect_attempts];}
 if(reconnect_at)ble_npl_callout_reset(&timer,ble_npl_time_ms_to_ticks32(100));
}
static void close_link(const char* text){
 manual_disconnect=true;reconnect_at=0;state.pairing=THINGSOS_BLE_PAIR_NONE;
 state.hid_ready=false;xQueueReset(inputs);
 if(connection!=BLE_HS_CONN_HANDLE_NONE){state.phase=THINGSOS_BLE_DISCONNECTING;deadline=clock_ms()+5000;ble_gap_terminate(connection,BLE_ERR_REM_USER_CONN_TERM);}
 else{ble_gap_conn_cancel();state.phase=THINGSOS_BLE_IDLE;state.connected=state.hid_ready=state.bonded=false;++generation;xQueueReset(inputs);}
 publish(text);
}
static void save_connected(void){
 thingsos_ble_device_t old[THINGSOS_BLE_SAVED],last=state.last;unsigned count=state.saved_count;memcpy(old,state.saved,sizeof(old));
 if(thingsos_ble_remember(&state,&state.identity)&&persist())return;
 memcpy(state.saved,old,sizeof(old));state.last=last;state.saved_count=count;
 snprintf(state.message,sizeof(state.message),"已连接，但设备记录保存失败");
}
static void emit_key(uint16_t page,uint16_t usage,uint8_t modifiers,bool caps,void* arg){
 (void)arg;thingsos_hid_key_t key={page,usage,modifiers,caps,generation,clock_ms()};xQueueSend(inputs,&key,0);
}
#include "ble_link.inc"
static int gap_event(struct ble_gap_event* event,void* arg){
 if(event->type==BLE_GAP_EVENT_DISC && state.scanning && (uint32_t)(uintptr_t)arg==scan_generation){
  const struct ble_gap_disc_desc* d=&event->disc;char address[13];snprintf(address,sizeof(address),"%02X%02X%02X%02X%02X%02X",d->addr.val[5],d->addr.val[4],d->addr.val[3],d->addr.val[2],d->addr.val[1],d->addr.val[0]);
  bool connectable=d->event_type==BLE_HCI_ADV_RPT_EVTYPE_ADV_IND||d->event_type==BLE_HCI_ADV_RPT_EVTYPE_DIR_IND;
  if(thingsos_ble_observe(&state,address,d->addr.type,d->rssi,connectable,d->data,d->length_data))publish(NULL);
 }else if(event->type==BLE_GAP_EVENT_DISC_COMPLETE && state.scanning && (uint32_t)(uintptr_t)arg==scan_generation){
  state.scanning=false;publish(state.full?"列表已满，请靠近目标后重搜":"低功耗设备搜索完成");
 }else if(event->type==BLE_GAP_EVENT_CONNECT){
  if((uint32_t)(uintptr_t)arg!=generation){if(!event->connect.status)ble_gap_terminate(event->connect.conn_handle,BLE_ERR_REM_USER_CONN_TERM);return 0;}
  if(event->connect.status){state.phase=THINGSOS_BLE_ERROR;state.connected=false;publish("连接失败，请检查配对模式");if(automatic_attempt)schedule_reconnect();return 0;}
  connection=event->connect.conn_handle;state.connected=true;state.phase=THINGSOS_BLE_PAIRING;deadline=clock_ms()+60000;hard_deadline=clock_ms()+120000;
  publish("正在配对并加密连接…");int rc=ble_gap_security_initiate(connection);
  if(rc && rc!=BLE_HS_EALREADY && rc!=BLE_HS_EBUSY)close_link("无法建立加密连接，请重试");
 }else if(event->type==BLE_GAP_EVENT_DISCONNECT && current(event->disconnect.conn.conn_handle,arg)){
  connection=BLE_HS_CONN_HANDLE_NONE;++generation;state.connected=state.bonded=state.hid_ready=false;state.pairing=THINGSOS_BLE_PAIR_NONE;state.phase=THINGSOS_BLE_IDLE;xQueueReset(inputs);hid_reset();
  publish(manual_disconnect?"低功耗设备已断开":"低功耗设备连接断开");if(forget_pending)forget_device(&forget_target);else schedule_reconnect();
 }else if(event->type==BLE_GAP_EVENT_ENC_CHANGE && current(event->enc_change.conn_handle,arg)){
  struct ble_gap_conn_desc desc;
  if(event->enc_change.status || ble_gap_conn_find(connection,&desc) || !desc.sec_state.encrypted){close_link("配对失败，请忘记旧配对后重试");return 0;}
  state.pairing=THINGSOS_BLE_PAIR_NONE;state.bonded=desc.sec_state.bonded;state.identity=state.current;
  const ble_addr_t* identity=&desc.peer_id_addr;
  snprintf(state.identity.device.address,13,"%02X%02X%02X%02X%02X%02X",identity->val[5],identity->val[4],identity->val[3],identity->val[2],identity->val[1],identity->val[0]);state.identity.address_type=identity->type&1;
  hid_discover();
 }else if(event->type==BLE_GAP_EVENT_PASSKEY_ACTION && current(event->passkey.conn_handle,arg)){
  unsigned action=event->passkey.params.action;state.phase=THINGSOS_BLE_PAIRING;deadline=clock_ms()+60000;
  if(action==BLE_SM_IOACT_NONE){state.pairing=THINGSOS_BLE_PAIR_NONE;publish("正在完成配对…");}
  else if(action==BLE_SM_IOACT_DISP){struct ble_sm_io io={0};io.action=action;io.passkey=esp_random()%1000000;state.passkey=io.passkey;state.pairing=THINGSOS_BLE_PAIR_DISPLAY;
   if(ble_sm_inject_io(connection,&io))close_link("配对验证码设置失败");else publish("请在外设输入屏幕验证码并按回车");
  }else if(action==BLE_SM_IOACT_NUMCMP){state.passkey=event->passkey.params.numcmp;state.pairing=THINGSOS_BLE_PAIR_CONFIRM;publish("请核对两端验证码后确认");}
  else if(action==BLE_SM_IOACT_INPUT){state.passkey=0;state.pairing=THINGSOS_BLE_PAIR_INPUT;publish("请输入外设显示的六位验证码");}
  else close_link("此设备的配对方式暂不支持");
 }else if(event->type==BLE_GAP_EVENT_REPEAT_PAIRING){if(connection!=BLE_HS_CONN_HANDLE_NONE && (uint32_t)(uintptr_t)arg==generation)close_link("配对记录不一致，请忘记后重新配对");return BLE_GAP_REPEAT_PAIRING_IGNORE;
 }else if(event->type==BLE_GAP_EVENT_NOTIFY_RX && current(event->notify_rx.conn_handle,arg) && state.hid_ready){
  struct ble_gap_conn_desc desc;if(!ble_gap_conn_find(connection,&desc)&&desc.sec_state.encrypted)hid_notify(event->notify_rx.attr_handle,event->notify_rx.om);
 }
 return 0;
}
static bool start_scan(void){
 if(busy()){publish("请先完成连接或配对");return false;}
 reconnect_at=0;if(state.scanning)ble_gap_disc_cancel();state.count=0;state.full=false;memset(state.devices,0,sizeof(state.devices));
 uint8_t own_type;int rc=ble_hs_id_infer_auto(0,&own_type);struct ble_gap_disc_params p={0};p.itvl=0x80;p.window=0x60;
 ++scan_generation;if(!rc)rc=ble_gap_disc(own_type,30000,&p,gap_event,(void*)(uintptr_t)scan_generation);
 state.scanning=rc==0;publish(rc?"低功耗搜索启动失败，请重试":"正在搜索低功耗设备（30 秒）");return rc==0;
}
static void begin_connect(const thingsos_ble_device_t* d,bool automatic){
 if(busy()||state.connected||connection!=BLE_HS_CONN_HANDLE_NONE){publish("请先断开当前设备");return;}if(!valid_device(d)){publish("设备地址无效，请重新搜索");return;}
 bool saved=false;for(unsigned i=0;i<state.saved_count;++i)if(thingsos_ble_same(d,state.saved+i))saved=true;
 if(!saved&&state.saved_count==THINGSOS_BLE_SAVED){publish("已保存设备已满，请先忘记一个设备");return;}
 if(state.scanning){state.scanning=false;++scan_generation;ble_gap_disc_cancel();}reconnect_at=0;manual_disconnect=false;automatic_attempt=automatic;if(!automatic)reconnect_attempts=0;
 ++generation;hid_reset();state.current=*d;state.identity=*d;state.connected=state.bonded=state.hid_ready=false;state.pairing=THINGSOS_BLE_PAIR_NONE;state.phase=THINGSOS_BLE_CONNECTING;
 uint8_t own_type;ble_addr_t peer={0};peer.type=d->address_type;
 for(unsigned i=0;i<6;++i){unsigned v=0;sscanf(d->device.address+2*i,"%2x",&v);peer.val[5-i]=(uint8_t)v;}
 deadline=clock_ms()+30000;hard_deadline=clock_ms()+120000;publish(automatic?"正在重连低功耗设备…":"正在连接低功耗设备…");
 int rc=ble_hs_id_infer_auto(0,&own_type);if(!rc)rc=ble_gap_connect(own_type,&peer,30000,NULL,gap_event,token());
 if(rc){state.phase=THINGSOS_BLE_ERROR;publish("连接启动失败，请重试");if(automatic)schedule_reconnect();}
}
static void forget_device(const thingsos_ble_device_t* d){
 forget_pending=false;thingsos_ble_device_t target=*d;
 if(connection!=BLE_HS_CONN_HANDLE_NONE && (thingsos_ble_same(d,&state.current)||thingsos_ble_same(d,&state.identity))){forget_target=state.identity;forget_pending=true;close_link("正在断开并忘记此设备…");return;}
 ble_addr_t address={0};address.type=target.address_type&1;for(unsigned i=0;i<6;++i){unsigned v=0;sscanf(target.device.address+2*i,"%2x",&v);address.val[5-i]=(uint8_t)v;}
 if(ble_store_util_delete_peer(&address)){publish("配对密钥移除失败，请重试");return;}
 thingsos_ble_device_t old[THINGSOS_BLE_SAVED],last=state.last;unsigned count=state.saved_count;memcpy(old,state.saved,sizeof(old));
 thingsos_ble_forget_record(&state,&target);if(!persist()){memcpy(state.saved,old,sizeof(old));state.last=last;state.saved_count=count;publish("密钥已移除，设备记录未保存，请重试");return;}
 reconnect_at=0;publish("已忘记此低功耗设备");
}
static void process_commands(void){
 command_t cmd;while(xQueueReceive(commands,&cmd,0)==pdTRUE){
  if(cmd.kind==CMD_SCAN)start_scan();
  else if(cmd.kind==CMD_STOP){state.scanning=false;++scan_generation;ble_gap_disc_cancel();publish("已停止低功耗搜索");}
  else if(cmd.kind==CMD_CONNECT)begin_connect(&cmd.device,cmd.value);
  else if(cmd.kind==CMD_DISCONNECT){forget_pending=false;close_link("正在断开低功耗设备…");}
  else if(cmd.kind==CMD_FORGET && valid_device(&cmd.device))forget_device(&cmd.device);
  else if(cmd.kind==CMD_AUTO){bool old=state.automatic;state.automatic=cmd.value;if(!persist()){state.automatic=old;publish("重连设置保存失败");}else{if(!cmd.value)reconnect_at=0;publish("低功耗重连设置已保存");}}
  else if(cmd.kind==CMD_PAIR && cmd.generation==generation && state.pairing!=THINGSOS_BLE_PAIR_NONE){
   if(!cmd.value){close_link("配对已取消");continue;}
   struct ble_sm_io io={0};io.action=state.pairing==THINGSOS_BLE_PAIR_CONFIRM?BLE_SM_IOACT_NUMCMP:BLE_SM_IOACT_INPUT;
   if(io.action==BLE_SM_IOACT_NUMCMP)io.numcmp_accept=cmd.value;else io.passkey=cmd.passkey;
   if(state.pairing==THINGSOS_BLE_PAIR_DISPLAY)continue;
   if(ble_sm_inject_io(connection,&io))close_link("配对确认失败，请重试");else{state.pairing=THINGSOS_BLE_PAIR_NONE;publish("正在完成配对…");}
  }
 }
}
static void tick(struct ble_npl_event* ev){
 (void)ev;if(atomic_load(&synced)){
  process_commands();uint64_t time=clock_ms();
  if(busy() && (time>=deadline||time>=hard_deadline)){
   if(state.phase==THINGSOS_BLE_DISCONNECTING){state.phase=THINGSOS_BLE_ERROR;state.hid_ready=false;xQueueReset(inputs);publish("断开未确认，请重试断开");}
   else {bool retry=automatic_attempt&&state.phase==THINGSOS_BLE_CONNECTING;close_link("连接或配对超时，可重试");if(retry){manual_disconnect=false;schedule_reconnect();}}
  }
  if(reconnect_at&&time>=reconnect_at&&!busy()&&!state.connected){reconnect_at=0;++reconnect_attempts;begin_connect(&state.last,true);}
 }
 if(busy()||reconnect_at)ble_npl_callout_reset(&timer,ble_npl_time_ms_to_ticks32(100));
}
static void sync_host(void){int rc=ble_hs_util_ensure_addr(0);atomic_store(&synced,rc==0);if(rc){state.phase=THINGSOS_BLE_ERROR;publish("低功耗蓝牙地址初始化失败");}else{process_commands();if(busy()||reconnect_at)ble_npl_callout_reset(&timer,ble_npl_time_ms_to_ticks32(100));}}
static void reset_host(int reason){
 (void)reason;atomic_store(&synced,false);connection=BLE_HS_CONN_HANDLE_NONE;++generation;state.scanning=state.connected=state.hid_ready=state.bonded=false;state.pairing=THINGSOS_BLE_PAIR_NONE;state.phase=THINGSOS_BLE_ERROR;xQueueReset(inputs);hid_reset();publish("低功耗蓝牙已重置，请重新连接");
}
static void host_task(void* p){(void)p;nimble_port_run();vTaskDelete(NULL);}
void ble_store_config_init(void);
static void initialize(void* p){
 (void)p;int rc;bool taken;
 /* Controller/PHY setup can touch Flash as well as the bond store. Serialize
    the whole first initialization with panel scans and application writes. */
 bool low_memory=false;
 if(!io_take(&taken))rc=BLE_HS_ESTORE_FAIL;
 else{
  /* S31 controller pools require INTERNAL|DMA, unlike the host's PSRAM
     buffers. SDK 6.1 aborts while cleaning up an early pool allocation failure.
     Check the DMA heap under the same I/O lock before entering that path. */
  unsigned caps=MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA|MALLOC_CAP_8BIT;
  size_t available=heap_caps_get_free_size(caps),largest=heap_caps_get_largest_free_block(caps);
  ESP_LOGI("ThingsOS_ble","Controller init budget: dma_free=%u dma_largest=%u internal_free=%u",(unsigned)available,(unsigned)largest,(unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
  low_memory=available<64u*1024u||largest<24u*1024u;
  rc=low_memory?BLE_HS_ENOMEM:nimble_port_init();if(!rc)ble_store_config_init();io_give(taken);
 }
 if(rc){xQueueReset(commands);atomic_store(&initialized,false);state.scanning=false;state.phase=THINGSOS_BLE_ERROR;publish(low_memory?"低功耗 DMA 不足，音频搜索仍可用":"低功耗蓝牙启动失败，请重试");}
 else{
  ble_hs_cfg.sync_cb=sync_host;ble_hs_cfg.reset_cb=reset_host;ble_hs_cfg.sm_io_cap=BLE_HS_IO_DISPLAY_YESNO;ble_hs_cfg.sm_bonding=1;ble_hs_cfg.sm_sc=1;ble_hs_cfg.sm_mitm=0;
  ble_hs_cfg.sm_our_key_dist=BLE_SM_PAIR_KEY_DIST_ENC|BLE_SM_PAIR_KEY_DIST_ID;ble_hs_cfg.sm_their_key_dist=BLE_SM_PAIR_KEY_DIST_ENC|BLE_SM_PAIR_KEY_DIST_ID;
  store_read=ble_hs_cfg.store_read_cb;store_write=ble_hs_cfg.store_write_cb;store_delete=ble_hs_cfg.store_delete_cb;
  ble_hs_cfg.store_read_cb=locked_read;ble_hs_cfg.store_write_cb=locked_write;ble_hs_cfg.store_delete_cb=locked_delete;ble_hs_cfg.store_status_cb=store_full;
  ble_npl_callout_init(&timer,nimble_port_get_dflt_eventq(),tick,NULL);
  /* The SDK void wrapper silently ignores xTaskCreate failure. Own/check this
     task so a missing host cannot strand commands or masquerade as scanning. */
  if(xTaskCreate(host_task,"nimble_host",CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE,NULL,configMAX_PRIORITIES-4,NULL)!=pdPASS){
   if(io_take(&taken)){ble_npl_callout_deinit(&timer);nimble_port_deinit();io_give(taken);}
   xQueueReset(commands);atomic_store(&initialized,false);state.phase=THINGSOS_BLE_ERROR;publish("低功耗任务内存不足，请稍后重试");
  }else ESP_LOGI("ThingsOS_ble","Controller/host started: dma_free=%u dma_largest=%u internal_free=%u",(unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA|MALLOC_CAP_8BIT),(unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA|MALLOC_CAP_8BIT),(unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
 }
 vTaskDelete(NULL);
}
void thingsos_ble_init(void){
 if(!mutable_state||!published_state){
  if(!mutable_state){mutable_state=heap_caps_calloc(1,sizeof(*mutable_state),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(!mutable_state)mutable_state=calloc(1,sizeof(*mutable_state));if(mutable_state)state.automatic=true;}
  if(!published_state){published_state=heap_caps_calloc(1,sizeof(*published_state),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(!published_state)published_state=calloc(1,sizeof(*published_state));if(published_state)visible.automatic=true;}
  if(!mutable_state||!published_state)return;
 }
 if(commands&&inputs)return;if(!commands)commands=xQueueCreate(4,sizeof(command_t));if(!inputs)inputs=xQueueCreate(32,sizeof(thingsos_hid_key_t));if(!commands||!inputs)return;
 load_records();publish(NULL);
 if(state.automatic&&valid_device(&state.last)){command_t cmd={.kind=CMD_CONNECT,.value=true,.device=state.last};xQueueSend(commands,&cmd,0);
  bool expected=false;if(atomic_compare_exchange_strong(&initialized,&expected,true)&&xTaskCreate(initialize,"ble_init",6144,NULL,3,NULL)!=pdPASS)atomic_store(&initialized,false);
 }
}
static bool submit(command_t* cmd){
 thingsos_ble_init();if(!commands||!inputs)return false;
 if(xQueueSend(commands,cmd,0)!=pdPASS)return false;bool expected=false;
 if(atomic_compare_exchange_strong(&initialized,&expected,true)){
  if(xTaskCreate(initialize,"ble_init",6144,NULL,3,NULL)!=pdPASS){atomic_store(&initialized,false);xQueueReset(commands);return false;}
 }
 if(atomic_load(&synced))ble_npl_callout_reset(&timer,ble_npl_time_ms_to_ticks32(1));
 return true;
}
bool thingsos_ble_scan(bool start){if(!start&&!atomic_load(&initialized))return true;command_t cmd={.kind=start?CMD_SCAN:CMD_STOP};return submit(&cmd);}
bool thingsos_ble_connect(const thingsos_ble_device_t* d){if(!valid_device(d))return false;command_t cmd={.kind=CMD_CONNECT,.device=*d};return submit(&cmd);}
bool thingsos_ble_disconnect(void){command_t cmd={.kind=CMD_DISCONNECT};return submit(&cmd);}
bool thingsos_ble_forget(const thingsos_ble_device_t* d){if(!valid_device(d))return false;command_t cmd={.kind=CMD_FORGET,.device=*d};return submit(&cmd);}
bool thingsos_ble_automatic(bool on){command_t cmd={.kind=CMD_AUTO,.value=on};return submit(&cmd);}
bool thingsos_ble_pair_reply(bool accept,uint32_t passkey){if(passkey>999999||!published_state)return false;command_t cmd={.kind=CMD_PAIR,.value=accept,.passkey=passkey};portENTER_CRITICAL(&guard);cmd.generation=visible.generation;portEXIT_CRITICAL(&guard);return submit(&cmd);}
bool thingsos_ble_input(thingsos_hid_key_t* out){
 if(!out||!inputs)return false;while(xQueueReceive(inputs,out,0)==pdTRUE){portENTER_CRITICAL(&guard);bool valid=visible.connected&&visible.hid_ready&&visible.generation==out->generation;portEXIT_CRITICAL(&guard);if(valid&&clock_ms()-out->time_ms<=5000)return true;}return false;
}
