// SPDX-License-Identifier: Apache-2.0
/* Independent BH1098G transmitter service. One task owns UART reads while
   enabled. UI/device ownership and all rendering stay on the main task. */
#include "thingsos/bluetooth_protocol.h"
#include "thingsos/audio.h"
#ifdef THINGSOS_BT_HOST_TEST
#include "bluetooth_service_host.h"
#else
#include "metalio_bsp.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_rom_crc.h"
#include "esp_attr.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdio.h>
#include <stdatomic.h>
extern SemaphoreHandle_t thingsos_io_lock;
#endif
#include <stdatomic.h>
typedef enum {POWER_ON,POWER_OFF,SCAN,SCAN_STOP,CONNECT,DISCONNECT,FORGET,AUTOMATIC} kind_t;
typedef struct {kind_t kind;bool value;thingsos_bt_device_t device;} command_t;
typedef struct {uint32_t version,flags,count;thingsos_bt_device_t devices[THINGSOS_BT_SAVED];char last[13];uint8_t reserved[3];uint32_t crc;} stored_t;
#ifdef THINGSOS_BT_HOST_TEST
static thingsos_bt_status_t state;
#else
/* UART worker-owned device/name arrays are ordinary CPU data. Keep the
   visible audio-route snapshot internal; free DMA-capable RAM for BLE. */
static EXT_RAM_BSS_ATTR thingsos_bt_status_t state;
#endif
static thingsos_bt_status_t visible={.automatic=true};
static portMUX_TYPE guard=portMUX_INITIALIZER_UNLOCKED;
static SemaphoreHandle_t lifecycle;
static QueueHandle_t commands;
static TaskHandle_t task;
static thingsos_bt_framer_t framer;
static thingsos_bt_device_t pending;
static bool transmitter_mode;
static uint64_t deadline;
static unsigned scan_round;
static bool scan_next;
static bool scan_wait_start;
static bool scan_suppressed;
static uint64_t scan_next_at;
static bool initialized;
static atomic_uint route_requests;
static atomic_bool link_lost;
static uint64_t clock_ms(void){return esp_timer_get_time()/1000;}
static void publish(thingsos_bt_phase_t phase,const char* message){
 state.phase=phase;state.busy=phase==THINGSOS_BT_STARTING||phase==THINGSOS_BT_SCANNING||phase==THINGSOS_BT_CONNECTING;
 if(message)snprintf(state.message,sizeof(state.message),"%s",message);
 ++state.serial;portENTER_CRITICAL(&guard);visible=state;portEXIT_CRITICAL(&guard);
 ESP_LOGI("ThingsOS_bt","phase=%u connected=%u saved=%u found=%u",(unsigned)phase,(unsigned)state.connected,state.saved_count,state.found_count);
}
void thingsos_bluetooth_status(thingsos_bt_status_t* out){if(out){portENTER_CRITICAL(&guard);*out=visible;portEXIT_CRITICAL(&guard);}}
bool thingsos_bluetooth_enabled(void){portENTER_CRITICAL(&guard);bool result=visible.enabled;portEXIT_CRITICAL(&guard);return result||atomic_load(&route_requests)!=0;}
bool thingsos_bluetooth_connected(void){portENTER_CRITICAL(&guard);bool result=visible.enabled&&visible.connected;portEXIT_CRITICAL(&guard);return result;}
bool thingsos_bluetooth_audio_ready(bool microphone){
 portENTER_CRITICAL(&guard);
 /* Recording and local playback only need an idle enabled service. Playback
    uses the local speaker until a link is confirmed, then routes wirelessly;
    a scan/connect transition and the shared-route guard still block both. */
 bool ok=!visible.enabled;
 if(!ok)ok=microphone?(visible.phase==THINGSOS_BT_READY&&!visible.connected&&!visible.busy)
                     :(!visible.busy&&(visible.connected||visible.phase==THINGSOS_BT_READY));
 portEXIT_CRITICAL(&guard);
 return ok&&!atomic_load(&route_requests);
}
static bool persist(void){
 stored_t data={.version=1,.flags=(state.enabled?1:0)|(state.automatic?2:0),.count=state.saved_count};
 memcpy(data.devices,state.saved,sizeof(data.devices));memcpy(data.last,state.last,sizeof(data.last));
 data.crc=esp_rom_crc32_le(0,(const uint8_t*)&data,offsetof(stored_t,crc));
 if(thingsos_io_lock&&xSemaphoreTake(thingsos_io_lock,pdMS_TO_TICKS(3000))!=pdTRUE)return false;
 nvs_handle_t handle;esp_err_t e=nvs_open("thingsos-bt",NVS_READWRITE,&handle);
 if(e==ESP_OK){e=nvs_set_blob(handle,"devices",&data,sizeof(data));if(e==ESP_OK)e=nvs_commit(handle);nvs_close(handle);}
 if(thingsos_io_lock)xSemaphoreGive(thingsos_io_lock);return e==ESP_OK;
}
static void load(void){
 state.automatic=true;stored_t data={0};size_t size=sizeof(data);nvs_handle_t handle;
 if(nvs_open("thingsos-bt",NVS_READONLY,&handle)!=ESP_OK)return;
 esp_err_t e=nvs_get_blob(handle,"devices",&data,&size);nvs_close(handle);
 if(e!=ESP_OK||size!=sizeof(data)||data.version!=1||data.count>THINGSOS_BT_SAVED||data.flags>3||data.last[12]||
    data.crc!=esp_rom_crc32_le(0,(const uint8_t*)&data,offsetof(stored_t,crc)))return;
 for(unsigned i=0;i<data.count;++i){if(data.devices[i].address[12]||!thingsos_bt_address(data.devices[i].address)||!memchr(data.devices[i].name,0,96))return;
  for(unsigned j=0;j<i;++j)if(!strcmp(data.devices[i].address,data.devices[j].address))return;}
 bool last_ok=!data.last[0];for(unsigned i=0;i<data.count;++i)if(!strcmp(data.last,data.devices[i].address))last_ok=true;if(!last_ok)return;
 memcpy(state.saved,data.devices,sizeof(state.saved));memcpy(state.last,data.last,sizeof(state.last));state.saved_count=data.count;state.automatic=(data.flags&2)!=0;state.enabled=(data.flags&1)!=0;
}
static void found(const thingsos_bt_device_t* d){
 unsigned i=0;while(i<state.found_count&&strcmp(state.found[i].address,d->address))++i;
 if(i==state.found_count){if(i==THINGSOS_BT_FOUND)return;++state.found_count;}
 if(i<state.found_count && !memcmp(state.found+i,d,sizeof(*d)))return;
 state.found[i]=*d;publish(state.phase,NULL);
}
static bool scan_request(void){
 ESP_LOGD("ThingsOS_bt","UART TX: AT+INQUIRING round=%u",scan_round+1);
 if(metalio_bh_send("AT+INQUIRING\r\n")!=ESP_OK){publish(THINGSOS_BT_ERROR,"搜索命令发送失败");return false;}
 ++scan_round;scan_next=false;scan_wait_start=false;deadline=clock_ms()+25000;
 char message[100];snprintf(message,sizeof(message),"正在搜索耳机和音箱（第 %u / 3 轮）…",scan_round);
 publish(THINGSOS_BT_SCANNING,message);return true;
}
static void scan_complete(void){
 if(scan_round<3){scan_next=true;scan_next_at=clock_ms()+300;return;}
 char message[100];snprintf(message,sizeof(message),"搜索完成，发现 %u 个音频设备",state.found_count);
 publish(THINGSOS_BT_READY,message);
}
static bool scan_begin(void);
static void scan_poll(void);
static bool remember_durable(const thingsos_bt_device_t* device){
 thingsos_bt_device_t old[THINGSOS_BT_SAVED];char last[13];unsigned count=state.saved_count;
 memcpy(old,state.saved,sizeof(old));memcpy(last,state.last,sizeof(last));
 if(thingsos_bt_remember(&state,device)&&persist())return true;
 state.saved_count=count;memcpy(state.saved,old,sizeof(old));memcpy(state.last,last,sizeof(last));return false;
}
static bool connection_request(void){
 char command[40];
 snprintf(command,sizeof(command),"AT+CONNECT=%s\r\n",pending.address);
 ESP_LOGD("ThingsOS_bt","UART TX: AT+CONNECT=%s",pending.address);
 if(metalio_bh_send(command)!=ESP_OK){pending.address[0]=0;publish(THINGSOS_BT_ERROR,"蓝牙连接命令发送失败");return false;}
 deadline=clock_ms()+20000;
 publish(THINGSOS_BT_CONNECTING,"正在配对并连接…");return true;
}
static void connection_failed(void){
 pending.address[0]=0;state.connected=false;metalio_audio_wireless(false);
 publish(THINGSOS_BT_READY,"连接失败，请检查耳机配对状态或是否被其他设备占用");
}
static void event(thingsos_bt_line_t type,const thingsos_bt_device_t* d){
 /* MODE 2 acknowledges before the module's two saved-device reconnects.
    An immediate INQUIRING overlaps those reconnects (captured TIMEOUTs during
    inquiry). Adopt its fallback inquiry first, without another mode reset. */
 if(type==BT_LINE_SCAN_BEGIN&&state.phase==THINGSOS_BT_SCANNING&&scan_wait_start){
  ++scan_round;scan_wait_start=false;deadline=clock_ms()+25000;
  publish(THINGSOS_BT_SCANNING,"正在搜索耳机和音箱…");return;
 }
 /* BH1098G may autonomously fall back from reconnect timeouts into inquiry.
    Adopt its real START while idle, otherwise all following device lines are
    discarded because the host is still READY. A user stop remains authoritative. */
 if(type==BT_LINE_SCAN_BEGIN && state.enabled && state.automatic && !scan_suppressed &&
    state.phase==THINGSOS_BT_READY && !state.connected){
  scan_round=3;scan_next=false;scan_wait_start=false;deadline=clock_ms()+25000;
  publish(THINGSOS_BT_SCANNING,"正在自动搜索耳机和音箱…");
 }else if(type==BT_LINE_DEVICE&&state.phase==THINGSOS_BT_SCANNING)found(d);
 else if(type==BT_LINE_SCAN_END&&state.phase==THINGSOS_BT_SCANNING&&!scan_wait_start)scan_complete();
 else if(type==BT_LINE_CONNECTED&&state.phase==THINGSOS_BT_CONNECTING&&thingsos_bt_address(pending.address)){
  state.connected=true;state.current=pending;metalio_audio_wireless(true);
  bool saved=remember_durable(&pending);
  publish(THINGSOS_BT_CONNECTED,saved?"已连接，音频输出切换到蓝牙设备":"已连接，但设备记录保存失败，请重试");pending.address[0]=0;
 }else if(type==BT_LINE_TIMEOUT&&state.phase==THINGSOS_BT_CONNECTING){connection_failed();}
 else if(type==BT_LINE_DISCONNECTED&&state.connected){state.connected=false;metalio_audio_wireless(false);publish(THINGSOS_BT_READY,"蓝牙连接断开，播放已暂停；设备记录保留");}
 else if(type==BT_LINE_CONNECTED)ESP_LOGW("ThingsOS_bt","Unrequested connection event ignored; output remains gated");
}
/* Waits only run on this worker. No main-task UART sleeps or canvas operations. */
static bool receive(thingsos_bt_line_t expected,unsigned timeout){
 uint64_t until=clock_ms()+timeout;char line[256];uint8_t bytes[512];unsigned batches=0;
 do{int n=metalio_bh_read(bytes,sizeof(bytes),batches?0:20);if(n<0)return false;
  bool matched=false;
  for(int i=0;i<n;++i)if(thingsos_bt_feed(&framer,bytes[i],line)){thingsos_bt_device_t d={0};thingsos_bt_line_t type=thingsos_bt_parse(line,&d);
   /* Keep discovery payloads in diagnostics too: a count alone cannot show
      whether a missing headset was reported, rejected, or named differently. */
   ESP_LOGD("ThingsOS_bt","UART RX type=%u phase=%u: %.255s",(unsigned)type,(unsigned)state.phase,line);
   if(expected!=BT_LINE_NONE&&type==expected)matched=true;else event(type,&d);}
  if(matched)return true;
  if(expected==BT_LINE_NONE && (!n || ++batches>=8))break;
 }while(expected==BT_LINE_NONE || clock_ms()<until);
 return expected==BT_LINE_NONE;
}
static bool mode(bool transmitter){
 transmitter_mode=false;
 scan_wait_start=false;scan_next=false;
 state.connected=false;pending.address[0]=0;metalio_audio_wireless(false);atomic_store(&link_lost,false);memset(&framer,0,sizeof(framer));
 if(metalio_bh_send(transmitter?"AT+TX=1\r\n":"AT+RX=2\r\n")!=ESP_OK)return false;
 vTaskDelay(pdMS_TO_TICKS(700));
 if(metalio_bh_send(transmitter?"AT+MODE=2\r\n":"AT+MODE=1\r\n")!=ESP_OK)return false;
 bool ok=receive(transmitter?BT_LINE_TX:BT_LINE_LOCAL,2500);
 transmitter_mode=ok&&transmitter;return ok;
}
static bool transmitter(void){
 publish(THINGSOS_BT_STARTING,"正在启动蓝牙…");
 /* Establish local mode first to discard any prior module-owned connection. */
 if(!mode(false)||!mode(true)){publish(THINGSOS_BT_ERROR,"蓝牙模块无响应，请关闭后重试");return false;}
 state.enabled=true;publish(THINGSOS_BT_READY,"");return true;
}
static bool scan_begin(void){
 scan_suppressed=true;
 bool reuse=transmitter_mode&&!state.connected&&state.phase!=THINGSOS_BT_CONNECTING&&state.phase!=THINGSOS_BT_ERROR;
 if(!reuse&&!transmitter())return false;
 state.found_count=0;memset(state.found,0,sizeof(state.found));scan_round=0;scan_next=false;
 if(reuse){pending.address[0]=0;return scan_request();}
 scan_wait_start=true;deadline=clock_ms()+20000;
 publish(THINGSOS_BT_SCANNING,"正在等待模块结束旧设备重连并搜索…");return true;
}
static void scan_poll(void){
 if(state.phase!=THINGSOS_BT_SCANNING)return;
 if(scan_next){if(clock_ms()>=scan_next_at)scan_request();return;}
 if(clock_ms()<deadline)return;
 if(scan_wait_start){scan_request();return;}
 /* Missing completion must not silently discard later results. Stop the old
    inquiry via the documented local/TX mode sequence before a bounded retry. */
 if(scan_round<3){if(transmitter()){
  scan_wait_start=true;scan_next=false;deadline=clock_ms()+20000;
  publish(THINGSOS_BT_SCANNING,"正在等待模块恢复搜索…");
 }}
 else {scan_round=3;scan_complete();}
}
static bool connect_device(const thingsos_bt_device_t* device){
 if(!thingsos_bt_address(device->address))return false;
 unsigned i=0;while(i<state.saved_count&&strcmp(state.saved[i].address,device->address))++i;
 if(i==state.saved_count&&state.saved_count==THINGSOS_BT_SAVED){publish(THINGSOS_BT_READY,"已保存设备已满，请先忘记一个设备");return false;}
 /* Official mode-2 UI sends CONNECT directly using the discovery address.
    Re-entering MODE 2 here restarts module-owned saved-device reconnects. */
 if(!transmitter_mode||state.connected){if(!transmitter())return false;}
 scan_wait_start=false;scan_next=false;
 pending=*device;return connection_request();
}
static void apply_link_lost(void){
 if(!atomic_exchange(&link_lost,false)||!state.connected)return;
 state.connected=false;metalio_audio_wireless(false);publish(THINGSOS_BT_READY,"蓝牙连接断开，播放已暂停；设备记录保留");
}
static void worker(void* unused){
 (void)unused;command_t command;
 for(;;){
  bool have=xQueueReceive(commands,&command,pdMS_TO_TICKS(20))==pdTRUE;
  if(have){
   if(command.kind==POWER_ON){scan_suppressed=false;state.enabled=true;if(transmitter()){
     if(!persist())publish(state.phase,"蓝牙设置保存失败");
     if(state.automatic&&state.last[0])for(unsigned i=0;i<state.saved_count;++i)if(!strcmp(state.last,state.saved[i].address)){connect_device(state.saved+i);break;}
   }}else if(command.kind==POWER_OFF){scan_suppressed=true;
    publish(THINGSOS_BT_STARTING,"正在关闭蓝牙…");if(mode(false)){state.enabled=false;state.connected=false;if(persist())publish(THINGSOS_BT_OFF,"");else publish(THINGSOS_BT_OFF,"蓝牙已关闭，但设置保存失败");}
    else publish(THINGSOS_BT_ERROR,"模块未确认关闭，请重试");
   }else if(command.kind==AUTOMATIC){bool prior=state.automatic;state.automatic=command.value;if(!persist()){state.automatic=prior;publish(state.phase,"自动重连设置保存失败");}else {scan_suppressed=!state.automatic;publish(state.phase,"");}}
   else if(command.kind==CONNECT||command.kind==SCAN){
    bool was_enabled=state.enabled;scan_suppressed=command.kind==SCAN;
    bool ok=command.kind==SCAN?scan_begin():connect_device(&command.device);
    /* Shared device search/connect can activate audio directly from local mode.
       Persist activation only after the module acknowledged TX mode. */
    if(ok&&!was_enabled&&!persist())publish(state.phase,"蓝牙已开启，但设置保存失败");
   }
   else if(command.kind==SCAN_STOP||command.kind==DISCONNECT){scan_suppressed=true;if(transmitter())publish(THINGSOS_BT_READY,command.kind==DISCONNECT?"已断开，播放暂停；设备记录保留":"已停止搜索");}
   else if(command.kind==FORGET){
    bool was_connected=state.connected&&!strcmp(state.current.address,command.device.address);
    if(was_connected&&!transmitter()){atomic_fetch_sub(&route_requests,1);continue;}
    /* Preserve state until the durable write succeeds, without a 3KiB stack copy. */
    thingsos_bt_device_t old[THINGSOS_BT_SAVED];char last[13];unsigned count=state.saved_count;
    memcpy(old,state.saved,sizeof(old));memcpy(last,state.last,sizeof(last));
    bool removed=thingsos_bt_forget_record(&state,command.device.address);
    if(!removed&&was_connected)publish(THINGSOS_BT_READY,"已断开；此设备没有已保存的连接记录");
    else if(removed&&persist())publish(state.connected?THINGSOS_BT_CONNECTED:THINGSOS_BT_READY,"已忘记此设备的本机连接记录");
    else{state.saved_count=count;memcpy(state.saved,old,sizeof(old));memcpy(state.last,last,sizeof(last));publish(state.phase,"忘记设备失败，记录已保留");}
   }
   if(command.kind!=AUTOMATIC)atomic_fetch_sub(&route_requests,1);
  }
  if(state.enabled){
   /* Set by the audio task when the module stopped clocking I2S, which is how
      a headset that powers off in its case looks to a slave transmitter. */
   apply_link_lost();
   receive(BT_LINE_NONE,0);
   scan_poll();
   if(state.phase==THINGSOS_BT_CONNECTING&&clock_ms()>=deadline)connection_failed();
  }
  xSemaphoreTake(lifecycle,portMAX_DELAY);
  if(!state.enabled&&!uxQueueMessagesWaiting(commands)){task=NULL;xSemaphoreGive(lifecycle);vTaskDelete(NULL);return;}
  xSemaphoreGive(lifecycle);
 }
}
static bool submit(kind_t kind,bool value,const thingsos_bt_device_t* device){
 if(!initialized)return false;
 if(kind!=AUTOMATIC&&!thingsos_audio_route_idle())return false;
 thingsos_bt_status_t snapshot;thingsos_bluetooth_status(&snapshot);
 if(snapshot.busy&&kind!=POWER_OFF&&kind!=SCAN_STOP&&!(kind==SCAN&&snapshot.phase==THINGSOS_BT_CONNECTING)&&!(kind==CONNECT&&snapshot.phase==THINGSOS_BT_SCANNING))return false;
 if(!snapshot.enabled&&kind!=POWER_ON&&kind!=AUTOMATIC&&kind!=FORGET&&kind!=SCAN&&kind!=CONNECT)return false;
 command_t command={.kind=kind,.value=value};if(device){if(!thingsos_bt_address(device->address))return false;command.device=*device;command.device.name[95]=0;}
 xSemaphoreTake(lifecycle,portMAX_DELAY);bool route=kind!=AUTOMATIC;if(route)atomic_fetch_add(&route_requests,1);
 bool ok=xQueueSend(commands,&command,0)==pdPASS;
 if(!ok&&route)atomic_fetch_sub(&route_requests,1);
 if(ok&&!task&&xTaskCreate(worker,"bt_audio",6144,NULL,3,&task)!=pdPASS){xQueueReset(commands);if(route)atomic_fetch_sub(&route_requests,1);ok=false;}
 xSemaphoreGive(lifecycle);return ok;
}
void thingsos_bluetooth_init(void){
 if(initialized)return;
 commands=xQueueCreate(2,sizeof(command_t));lifecycle=xSemaphoreCreateMutex();if(!commands||!lifecycle)return;
 load();bool enabled=state.enabled;state.enabled=false;publish(THINGSOS_BT_OFF,"");initialized=true;
 if(enabled&&!thingsos_bluetooth_power(true))publish(THINGSOS_BT_ERROR,"蓝牙自动恢复失败，请手动开启后重试");
}
bool thingsos_bluetooth_power(bool on){return submit(on?POWER_ON:POWER_OFF,false,NULL);}
bool thingsos_bluetooth_scan(bool start){return submit(start?SCAN:SCAN_STOP,false,NULL);}
bool thingsos_bluetooth_connect(const thingsos_bt_device_t* d){return d&&submit(CONNECT,false,d);}
bool thingsos_bluetooth_disconnect(void){return submit(DISCONNECT,false,NULL);}
bool thingsos_bluetooth_forget(const char* address){thingsos_bt_device_t d={0};if(!thingsos_bt_address(address))return false;memcpy(d.address,address,13);return submit(FORGET,false,&d);}
bool thingsos_bluetooth_automatic(bool on){return submit(AUTOMATIC,on,NULL);}
void thingsos_bluetooth_link_lost(void){atomic_store(&link_lost,true);}
